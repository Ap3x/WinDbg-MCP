# Module 12 — Async Scripting & Time Travel Debugging

## Async in WinDbg JavaScript

WinDbg's JavaScript provider supports ES6 generators and async patterns. This is particularly important when working with Time Travel Debugging (TTD), where you can run queries that traverse huge execution traces.

## Generators and Yield

Generators let you produce sequences lazily — essential when dealing with large datasets:

```javascript
"use strict";

// A generator that yields memory values one at a time
function* readMemoryGenerator(addr, count) {
    let values = host.memory.readMemoryValues(addr, count, 8);
    for (let val of values) {
        yield val;
    }
}

// A generator that walks a linked list
function* walkLinkedList(headAddr, nextOffset, maxItems) {
    maxItems = maxItems || 1000;
    let current = host.memory.readMemoryValues(headAddr, 1, 8)[0];
    let count = 0;

    while (current.compareTo(headAddr) !== 0 && count < maxItems) {
        yield current;
        current = host.memory.readMemoryValues(current.add(nextOffset), 1, 8)[0];
        count++;
    }
}

function invokeScript() {
    let rsp = host.currentThread.Registers.User.rsp;

    // Use the generator — only reads what we need
    let gen = readMemoryGenerator(rsp, 100);
    let first5 = [];
    for (let val of gen) {
        first5.push(val);
        if (first5.length >= 5) break;
    }

    host.diagnostics.debugLog("First 5 values on stack:\n");
    for (let v of first5) {
        host.diagnostics.debugLog("  0x" + v.toString(16) + "\n");
    }
}
```

## Time Travel Debugging (TTD)

TTD records a program's execution and lets you replay it — including stepping **backwards**. WinDbg JavaScript can query TTD traces with powerful APIs.

### TTD Prerequisites

- WinDbg Preview (TTD is not in classic WinDbg)
- Record a trace: Debug > Time Travel Debugging > Launch/Attach
- Or open a `.run` trace file

### TTD Data Model

TTD extends the data model with:

```
dx @$curprocess.TTD
├── .Lifetime                  → start/end positions
│   ├── .MinPosition           → trace start
│   └── .MaxPosition           → trace end
├── .Threads[]                 → all threads that existed
├── .Events[]                  → debug events
│   ├── .Type                  → "ModuleLoaded", "Exception", etc.
│   └── .Position              → trace position
├── .Calls("module!func")      → query all calls to a function
└── .Memory(addr, size, "rw")  → query memory accesses
```

### Querying Function Calls

The killer TTD feature — find every call to a function across the entire trace:

```javascript
"use strict";

function findCalls(functionName) {
    host.diagnostics.debugLog("Searching for calls to: " + functionName + "\n\n");

    let calls = host.currentProcess.TTD.Calls(functionName);

    let count = 0;
    for (let call of calls) {
        host.diagnostics.debugLog(
            "[" + count + "] Position: " + call.TimeStart +
            "  Thread: " + call.ThreadId + "\n"
        );

        // Show the return value
        host.diagnostics.debugLog(
            "    Return: 0x" + call.ReturnValue.toString(16) + "\n"
        );

        // Show arguments (first 4 in x64 calling convention)
        if (call.Parameters) {
            let paramIdx = 0;
            for (let param of call.Parameters) {
                host.diagnostics.debugLog(
                    "    Param[" + paramIdx + "]: 0x" + param.toString(16) + "\n"
                );
                paramIdx++;
                if (paramIdx >= 4) break;
            }
        }

        host.diagnostics.debugLog("\n");
        count++;
        if (count >= 50) {
            host.diagnostics.debugLog("... (showing first 50 results)\n");
            break;
        }
    }

    host.diagnostics.debugLog("Total calls found: " + count + "\n");
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(findCalls, "ttdcalls")
    ];
}
```

Usage:
```
!ttdcalls kernel32!CreateFileW
!ttdcalls ntdll!NtAllocateVirtualMemory
```

### Querying Memory Accesses

Find all reads/writes to a specific memory address:

```javascript
"use strict";

function memoryAccesses(addrStr, size, accessType) {
    size = size || 4;
    accessType = accessType || "w";  // "r" = read, "w" = write, "rw" = both

    let addr = host.parseInt64(addrStr);

    host.diagnostics.debugLog(
        "Searching for " + accessType + " accesses to 0x" +
        addr.toString(16) + " (size=" + size + ")...\n\n"
    );

    let accesses = host.currentProcess.TTD.Memory(addr, addr.add(size), accessType);

    let count = 0;
    for (let access of accesses) {
        host.diagnostics.debugLog(
            "[" + count + "] Position: " + access.TimeStart +
            "  Thread: " + access.ThreadId +
            "  IP: 0x" + access.IP.toString(16) +
            "  Type: " + access.AccessType +
            "  Value: 0x" + access.Value.toString(16) + "\n"
        );

        count++;
        if (count >= 100) {
            host.diagnostics.debugLog("... (showing first 100)\n");
            break;
        }
    }

    host.diagnostics.debugLog("\nTotal accesses: " + count + "\n");
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(memoryAccesses, "ttdmem")
    ];
}
```

Usage:
```
!ttdmem 0x00000001`00001234 4 w
!ttdmem 0x00000001`00001234 8 rw
```

### TTD Events

Query debug events across the trace:

```javascript
"use strict";

function ttdEvents() {
    let events = host.currentProcess.TTD.Events;

    host.diagnostics.debugLog("=== TTD Trace Events ===\n\n");

    let typeCounts = {};

    for (let event of events) {
        let type = event.Type;
        typeCounts[type] = (typeCounts[type] || 0) + 1;

        host.diagnostics.debugLog(
            "Position: " + event.Position +
            "  Type: " + type
        );

        // Show extra info for module events
        if (type === "ModuleLoaded" || type === "ModuleUnloaded") {
            try {
                host.diagnostics.debugLog("  Module: " + event.Module.Name);
            } catch (e) {}
        }

        // Show extra info for exceptions
        if (type === "Exception") {
            try {
                host.diagnostics.debugLog(
                    "  Code: 0x" + event.Exception.ExceptionCode.toString(16)
                );
            } catch (e) {}
        }

        host.diagnostics.debugLog("\n");
    }

    host.diagnostics.debugLog("\n=== Event Summary ===\n");
    for (let type in typeCounts) {
        host.diagnostics.debugLog(
            "  " + type.padEnd(25) + typeCounts[type] + "\n"
        );
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(ttdEvents, "ttdevents")
    ];
}
```

### Navigating to Positions

TTD positions let you jump to any point in the trace:

```javascript
function gotoPosition(positionStr) {
    // Position format: "AA:BB" where AA is sequence number, BB is step
    host.namespace.Debugger.Utility.Control.ExecuteCommand(
        "!tt " + positionStr
    );
    host.diagnostics.debugLog("Jumped to position " + positionStr + "\n");

    // Show context at this position
    let regs = host.currentThread.Registers.User;
    host.diagnostics.debugLog("RIP: 0x" + regs.rip.toString(16) + "\n");

    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand("k 5");
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }
}
```

## Practical TTD Scripts

### Find the Root Cause of an Access Violation

```javascript
"use strict";

function findAccessViolation() {
    host.diagnostics.debugLog("=== Finding Access Violation Root Cause ===\n\n");

    // Find exception events
    let events = host.currentProcess.TTD.Events;
    let avEvent = null;

    for (let event of events) {
        if (event.Type === "Exception") {
            try {
                if (event.Exception.ExceptionCode === 0xC0000005) {
                    avEvent = event;
                    break;
                }
            } catch (e) {}
        }
    }

    if (!avEvent) {
        host.diagnostics.debugLog("No access violation found in trace.\n");
        return;
    }

    host.diagnostics.debugLog("Access violation at position: " + avEvent.Position + "\n\n");

    // Jump to the exception
    host.namespace.Debugger.Utility.Control.ExecuteCommand(
        "!tt " + avEvent.Position
    );

    // Show the faulting instruction
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand("u @rip L1");
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }

    // Show the stack at crash time
    host.diagnostics.debugLog("\nStack at crash:\n");
    output = host.namespace.Debugger.Utility.Control.ExecuteCommand("k");
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }

    // Show registers
    host.diagnostics.debugLog("\nRegisters:\n");
    output = host.namespace.Debugger.Utility.Control.ExecuteCommand("r");
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(findAccessViolation, "findav")
    ];
}
```

### Track a Variable's History

```javascript
"use strict";

function variableHistory(addrStr, size) {
    size = size || 8;
    let addr = host.parseInt64(addrStr);

    host.diagnostics.debugLog(
        "=== Write History for 0x" + addr.toString(16) + " ===\n\n"
    );

    let writes = host.currentProcess.TTD.Memory(addr, addr.add(size), "w");

    let history = [];
    for (let write of writes) {
        history.push({
            position: write.TimeStart.toString(),
            thread: write.ThreadId,
            ip: write.IP.toString(16),
            value: write.Value.toString(16)
        });

        if (history.length >= 200) break;
    }

    host.diagnostics.debugLog(
        "Position".padEnd(15) +
        "Thread".padEnd(10) +
        "IP".padEnd(20) +
        "New Value\n"
    );
    host.diagnostics.debugLog("─".repeat(60) + "\n");

    for (let entry of history) {
        host.diagnostics.debugLog(
            entry.position.padEnd(15) +
            entry.thread.toString().padEnd(10) +
            ("0x" + entry.ip).padEnd(20) +
            "0x" + entry.value + "\n"
        );
    }

    host.diagnostics.debugLog("\nTotal writes: " + history.length + "\n");
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(variableHistory, "varhistory")
    ];
}
```

### API Call Timeline

```javascript
"use strict";

function apiTimeline(func1, func2) {
    host.diagnostics.debugLog("=== API Call Timeline ===\n");
    host.diagnostics.debugLog(func1 + " vs " + func2 + "\n\n");

    let calls1 = [];
    for (let c of host.currentProcess.TTD.Calls(func1)) {
        calls1.push({ func: func1, time: c.TimeStart, ret: c.ReturnValue });
        if (calls1.length >= 100) break;
    }

    let calls2 = [];
    for (let c of host.currentProcess.TTD.Calls(func2)) {
        calls2.push({ func: func2, time: c.TimeStart, ret: c.ReturnValue });
        if (calls2.length >= 100) break;
    }

    // Merge and sort by time
    let all = calls1.concat(calls2);
    all.sort((a, b) => {
        let ta = a.time.toString();
        let tb = b.time.toString();
        return ta < tb ? -1 : ta > tb ? 1 : 0;
    });

    for (let entry of all) {
        let marker = entry.func === func1 ? ">>>" : "   <<<";
        host.diagnostics.debugLog(
            entry.time.toString().padEnd(15) +
            marker + " " + entry.func +
            " => 0x" + entry.ret.toString(16) + "\n"
        );
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(apiTimeline, "timeline")
    ];
}
```

Usage:
```
!timeline kernel32!CreateFileW kernel32!CloseHandle
```

## Performance Considerations

TTD queries can be slow on large traces. Tips:

1. **Limit result counts** — always use a maximum iteration count
2. **Narrow your time range** — if you know approximately when something happened, filter by position
3. **Use specific function names** — `kernel32!CreateFileW` is faster than `kernel32!Create*`
4. **Cache results** — store query results in variables for reuse
5. **Use `dx` to prototype** — test queries interactively before scripting

## Exercises

1. Write a TTD script that finds all `VirtualAlloc` calls and shows what protection was requested.
2. Create a "data flow tracker" that traces a value from when it was first written to when it was read.
3. Write a script that finds all exceptions in a trace and categorizes them by type.
4. Create a timeline showing `CreateFile`/`ReadFile`/`WriteFile`/`CloseHandle` calls for file I/O analysis.
5. Write a TTD script that detects use-after-free: find memory that was freed and then accessed again.

## Next

[Module 13 — Integration & Automation](13-integration-and-automation.md) — Combine JS scripts with WinDbg extensions, MCP servers, and CI pipelines.
