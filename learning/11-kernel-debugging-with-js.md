# Module 11 — Kernel Debugging with JS

## Overview

Kernel debugging opens up the entire operating system to inspection. WinDbg JavaScript works equally well in kernel mode — you get access to kernel structures, driver objects, process lists, and more.

## Prerequisites

Kernel debugging requires a different setup:

1. **Two machines** (or a VM): a debugger host and a debug target
2. **Target configured for kernel debugging**: `bcdedit /debug on`
3. **Connection**: serial, network (KDNET), or USB
4. **Symbols**: Microsoft symbol server configured (`.symfix; .reload`)

Alternatively, use a **kernel dump file** — many of the scripts below work on crash dumps too.

## The Kernel Data Model

In kernel mode, the DDM provides different root objects:

```
dx Debugger.Sessions[0].Processes     → kernel processes (via _EPROCESS)
dx @$cursession                       → kernel session
dx @$curprocess                       → the current process context
```

## Enumerating Processes

### Using the Data Model

```javascript
"use strict";

function listProcesses() {
    let session = host.currentSession;

    host.diagnostics.debugLog("=== Kernel Process List ===\n\n");
    host.diagnostics.debugLog(
        "PID".padStart(8) +
        "  EPROCESS".padEnd(20) +
        "  Name\n"
    );
    host.diagnostics.debugLog("─".repeat(50) + "\n");

    for (let proc of session.Processes) {
        host.diagnostics.debugLog(
            proc.Id.toString().padStart(8) +
            "  " + proc.Name + "\n"
        );
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(listProcesses, "pslist")
    ];
}
```

### Walking the _EPROCESS List Manually

For deeper inspection, walk the kernel's process list directly:

```javascript
"use strict";

function walkEprocessList() {
    // PsActiveProcessHead is the list head
    let listHead = host.getModuleSymbolAddress("nt", "PsActiveProcessHead");
    host.diagnostics.debugLog("PsActiveProcessHead: 0x" + listHead.toString(16) + "\n\n");

    // Read the first Flink
    let flink = host.memory.readMemoryValues(listHead, 1, 8)[0];

    // Get the offset of ActiveProcessLinks in _EPROCESS
    let eprocType = host.getModuleType("nt", "_EPROCESS");
    let linkOffset = 0;
    for (let field of eprocType.fields) {
        if (field.name === "ActiveProcessLinks") {
            linkOffset = field.offset;
            break;
        }
    }

    host.diagnostics.debugLog(
        "ActiveProcessLinks offset: 0x" + linkOffset.toString(16) + "\n\n"
    );

    host.diagnostics.debugLog(
        "EPROCESS".padEnd(20) +
        "PID".padStart(8) +
        "  ImageFileName\n"
    );
    host.diagnostics.debugLog("─".repeat(55) + "\n");

    let current = flink;
    let count = 0;

    while (current.compareTo(listHead) !== 0 && count < 500) {
        // Subtract the offset to get the _EPROCESS base
        let eprocAddr = current.subtract(linkOffset);

        let eproc = host.createTypedObject(eprocAddr, "nt", "_EPROCESS");

        // Read ImageFileName (char[15] field)
        let name = "";
        try {
            name = eproc.ImageFileName;
        } catch (e) {
            name = "(unknown)";
        }

        let pid = 0;
        try {
            pid = eproc.UniqueProcessId;
        } catch (e) {}

        host.diagnostics.debugLog(
            ("0x" + eprocAddr.toString(16)).padEnd(20) +
            pid.toString().padStart(8) +
            "  " + name + "\n"
        );

        // Move to next entry
        current = host.memory.readMemoryValues(current, 1, 8)[0];
        count++;
    }

    host.diagnostics.debugLog("\nTotal: " + count + " processes\n");
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(walkEprocessList, "eprocesswalk")
    ];
}
```

## Enumerating Loaded Drivers

```javascript
"use strict";

function listDrivers() {
    host.diagnostics.debugLog("=== Loaded Kernel Drivers ===\n\n");

    // Use lm command for driver modules
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand("lm k");
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }

    host.diagnostics.debugLog("\n=== Driver Objects ===\n\n");

    // Enumerate driver objects through the object directory
    output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
        '!object \\Driver'
    );
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }
}

function driverDetails(driverName) {
    host.diagnostics.debugLog("=== Driver: " + driverName + " ===\n\n");

    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
        "!drvobj \\Driver\\" + driverName + " 7"
    );
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(listDrivers, "drivers"),
        new host.functionAlias(driverDetails, "drvinfo")
    ];
}
```

## Inspecting Kernel Objects

### _EPROCESS Deep Dive

```javascript
"use strict";

function processDetails(pidStr) {
    let pid = parseInt(pidStr);

    // Find the process by PID
    for (let proc of host.currentSession.Processes) {
        if (proc.Id === pid) {
            host.diagnostics.debugLog("=== Process Details: " + proc.Name + " ===\n\n");

            // Get _EPROCESS via dt
            let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
                "!process " + pid.toString(16) + " 0"
            );
            for (let line of output) {
                host.diagnostics.debugLog(line + "\n");
            }

            // Show loaded DLLs
            host.diagnostics.debugLog("\n=== Loaded Modules ===\n");
            for (let mod of proc.Modules) {
                host.diagnostics.debugLog(
                    "  " + mod.Name.padEnd(30) +
                    "0x" + mod.BaseAddress.toString(16) + "\n"
                );
            }

            // Show threads
            host.diagnostics.debugLog("\n=== Threads ===\n");
            for (let thread of proc.Threads) {
                host.diagnostics.debugLog("  Thread " + thread.Id + "\n");
            }

            return;
        }
    }

    host.diagnostics.debugLog("Process with PID " + pid + " not found.\n");
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(processDetails, "procdetail")
    ];
}
```

### System Service Table (SSDT)

```javascript
"use strict";

function dumpSSDT(count) {
    count = count || 20;
    host.diagnostics.debugLog("=== System Service Descriptor Table ===\n\n");

    // Get KeServiceDescriptorTable
    let ssdtAddr = host.getModuleSymbolAddress("nt", "KeServiceDescriptorTable");
    host.diagnostics.debugLog("SSDT at: 0x" + ssdtAddr.toString(16) + "\n\n");

    // The first QWORD is the base of the service table
    let serviceTableBase = host.memory.readMemoryValues(ssdtAddr, 1, 8)[0];

    // Read the count (at offset 0x10 in the structure)
    let serviceCount = host.memory.readMemoryValues(ssdtAddr.add(0x10), 1, 8)[0];

    host.diagnostics.debugLog("Service Table Base: 0x" + serviceTableBase.toString(16) + "\n");
    host.diagnostics.debugLog("Number of Services: " + serviceCount + "\n\n");

    // On x64, the SSDT entries are 4-byte offsets (shifted)
    let entries = host.memory.readMemoryValues(serviceTableBase, count, 4);

    let idx = 0;
    for (let entry of entries) {
        if (idx >= count) break;

        // The offset is entry >> 4, relative to the table base
        let offset = parseInt(entry.toString()) >> 4;
        let funcAddr = serviceTableBase.add(offset);

        // Try to resolve to a symbol
        let symbol = "0x" + funcAddr.toString(16);
        try {
            let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
                "ln 0x" + funcAddr.toString(16)
            );
            for (let line of output) {
                if (line.trim().length > 0) {
                    symbol = line.trim();
                    break;
                }
            }
        } catch (e) {}

        host.diagnostics.debugLog(
            "[" + idx.toString().padStart(4) + "] " + symbol + "\n"
        );
        idx++;
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(dumpSSDT, "ssdt")
    ];
}
```

## Interrupt Descriptor Table (IDT)

```javascript
"use strict";

function dumpIDT(count) {
    count = count || 32;
    host.diagnostics.debugLog("=== Interrupt Descriptor Table ===\n\n");

    // Use the !idt command for clean output
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
        "!idt -a"
    );

    let lineCount = 0;
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
        lineCount++;
        if (lineCount >= count * 2) {
            host.diagnostics.debugLog("... (use !idt -a for full output)\n");
            break;
        }
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(dumpIDT, "idtdump")
    ];
}
```

## Pool Memory Analysis

```javascript
"use strict";

function poolStats() {
    host.diagnostics.debugLog("=== Kernel Pool Statistics ===\n\n");

    // Non-paged pool
    host.diagnostics.debugLog("--- Non-Paged Pool ---\n");
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand("!poolused 2");
    let lineCount = 0;
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
        lineCount++;
        if (lineCount > 30) {
            host.diagnostics.debugLog("... (truncated)\n");
            break;
        }
    }

    host.diagnostics.debugLog("\n--- Paged Pool ---\n");
    output = host.namespace.Debugger.Utility.Control.ExecuteCommand("!poolused 4");
    lineCount = 0;
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
        lineCount++;
        if (lineCount > 30) {
            host.diagnostics.debugLog("... (truncated)\n");
            break;
        }
    }
}

function poolTag(tag) {
    host.diagnostics.debugLog("=== Pool Allocations for Tag '" + tag + "' ===\n\n");
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
        "!poolfind " + tag
    );
    let lineCount = 0;
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
        lineCount++;
        if (lineCount > 100) {
            host.diagnostics.debugLog("... (truncated)\n");
            break;
        }
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(poolStats, "poolstats"),
        new host.functionAlias(poolTag, "pooltag")
    ];
}
```

## IRP (I/O Request Packet) Tracing

```javascript
"use strict";

function irpTrace(deviceName) {
    host.diagnostics.debugLog("=== IRP Analysis ===\n\n");

    if (deviceName) {
        // Trace IRPs for a specific device
        let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
            "!devobj " + deviceName
        );
        for (let line of output) {
            host.diagnostics.debugLog(line + "\n");
        }
    }

    // Show pending IRPs
    host.diagnostics.debugLog("\n=== Pending IRPs ===\n\n");
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand("!irpfind");
    let lineCount = 0;
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
        lineCount++;
        if (lineCount > 50) {
            host.diagnostics.debugLog("... (truncated)\n");
            break;
        }
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(irpTrace, "irps")
    ];
}
```

## Kernel Process Security Tokens

```javascript
"use strict";

function processTokens() {
    host.diagnostics.debugLog("=== Process Token Information ===\n\n");

    for (let proc of host.currentSession.Processes) {
        host.diagnostics.debugLog("Process: " + proc.Name +
            " (PID " + proc.Id + ")\n");

        try {
            let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
                "!token -n @$proc(" + proc.Id.toString(16) + ")->Token"
            );
            for (let line of output) {
                if (line.indexOf("User") !== -1 ||
                    line.indexOf("Group") !== -1 ||
                    line.indexOf("Privilege") !== -1) {
                    host.diagnostics.debugLog("  " + line + "\n");
                }
            }
        } catch (e) {
            host.diagnostics.debugLog("  (token unavailable)\n");
        }

        host.diagnostics.debugLog("\n");
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(processTokens, "tokens")
    ];
}
```

## Tips for Kernel Debugging with JS

1. **Always check for symbols** — kernel scripts are useless without `ntkrnlmp.pdb`. Run `.symfix; .reload` first.
2. **Be careful with memory reads** — kernel addresses can be paged out; wrap reads in try/catch.
3. **Use `!process 0 0`** before walking lists — it populates the debugger's process cache.
4. **Structure offsets change between Windows versions** — use typed objects (`host.createTypedObject`) instead of hardcoding offsets.
5. **Performance** — kernel process lists can be large; use `Take()` or limit iterations.

## Exercises

1. Write a script that lists all kernel processes with their token integrity levels (System, High, Medium, Low).
2. Create a driver enumerator that shows each driver's dispatch routines (MajorFunction table).
3. Write a script that finds all processes with a specific DLL loaded.
4. Create a `!callback` command that enumerates kernel notification callbacks (process, thread, image load).
5. Write a kernel module security checker that verifies driver code integrity signatures.

## Next

[Module 12 — Async & Time Travel](12-async-and-time-travel.md) — Async patterns, promises, and integrating with Time Travel Debugging (TTD).
