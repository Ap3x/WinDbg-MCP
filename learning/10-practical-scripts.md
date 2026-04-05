# Module 10 — Practical Scripts

## Overview

This module contains complete, production-quality scripts for common debugging scenarios. Each script demonstrates patterns from previous modules combined into useful tools.

## Script 1: Process Environment Dumper

Dump environment variables, command line, and current directory:

```javascript
"use strict";

function processEnvironment() {
    let proc = host.currentProcess;
    host.diagnostics.debugLog("=== Process Environment for " + proc.Name +
        " (PID " + proc.Id + ") ===\n\n");

    // Get the PEB
    let pebAddr = host.evaluateExpression("@$peb");
    let peb = host.createTypedObject(pebAddr, "ntdll", "_PEB");

    // Process Parameters contain the good stuff
    let params = peb.ProcessParameters;

    // Command Line
    host.diagnostics.debugLog("Command Line:\n");
    try {
        let cmdLine = readUnicodeString(params.CommandLine);
        host.diagnostics.debugLog("  " + cmdLine + "\n\n");
    } catch (e) {
        host.diagnostics.debugLog("  (unavailable)\n\n");
    }

    // Current Directory
    host.diagnostics.debugLog("Current Directory:\n");
    try {
        let curDir = readUnicodeString(params.CurrentDirectory.DosPath);
        host.diagnostics.debugLog("  " + curDir + "\n\n");
    } catch (e) {
        host.diagnostics.debugLog("  (unavailable)\n\n");
    }

    // Image Path
    host.diagnostics.debugLog("Image Path:\n");
    try {
        let imgPath = readUnicodeString(params.ImagePathName);
        host.diagnostics.debugLog("  " + imgPath + "\n\n");
    } catch (e) {
        host.diagnostics.debugLog("  (unavailable)\n\n");
    }

    // DLL search path
    host.diagnostics.debugLog("DLL Path:\n");
    try {
        let dllPath = readUnicodeString(params.DllPath);
        host.diagnostics.debugLog("  " + dllPath + "\n\n");
    } catch (e) {
        host.diagnostics.debugLog("  (unavailable)\n\n");
    }
}

function readUnicodeString(unicodeStr) {
    let length = unicodeStr.Length;
    let buffer = unicodeStr.Buffer;
    let charCount = length / 2;
    let words = host.memory.readMemoryValues(buffer, charCount, 2);
    let str = "";
    for (let w of words) {
        str += String.fromCharCode(w);
    }
    return str;
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(processEnvironment, "procenv")
    ];
}
```

## Script 2: Heap Summary

Analyze the process heap and show allocation statistics:

```javascript
"use strict";

function heapSummary() {
    host.diagnostics.debugLog("=== Heap Summary ===\n\n");

    // Use the !heap command for heap enumeration
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand("!heap -s");
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }

    host.diagnostics.debugLog("\n=== Heap Statistics ===\n\n");

    // Get heap count from PEB
    try {
        let pebAddr = host.evaluateExpression("@$peb");
        let peb = host.createTypedObject(pebAddr, "ntdll", "_PEB");

        host.diagnostics.debugLog("Process Heap:    0x" +
            peb.ProcessHeap.toString(16) + "\n");
        host.diagnostics.debugLog("Number of Heaps: " +
            peb.NumberOfHeaps + "\n");
    } catch (e) {
        host.diagnostics.debugLog("(PEB access error: " + e.message + ")\n");
    }
}

function heapAllocations(heapAddr) {
    // Detailed view of a specific heap
    let cmd = heapAddr ? "!heap -a " + heapAddr : "!heap -a";
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(cmd);

    let lineCount = 0;
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
        lineCount++;
        if (lineCount > 200) {
            host.diagnostics.debugLog("... (truncated at 200 lines)\n");
            break;
        }
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(heapSummary, "heapsum"),
        new host.functionAlias(heapAllocations, "heapalloc")
    ];
}
```

## Script 3: String Finder

Scan memory for ASCII and Unicode strings:

```javascript
"use strict";

function findStrings(moduleName, minLength) {
    minLength = minLength || 4;

    let proc = host.currentProcess;
    let targetMod = null;

    for (let mod of proc.Modules) {
        if (mod.Name.toLowerCase().indexOf(moduleName.toLowerCase()) !== -1) {
            targetMod = mod;
            break;
        }
    }

    if (!targetMod) {
        host.diagnostics.debugLog("Module '" + moduleName + "' not found.\n");
        return;
    }

    let base = targetMod.BaseAddress;
    let size = targetMod.Size;

    host.diagnostics.debugLog("Scanning " + targetMod.Name +
        " (0x" + base.toString(16) + ", " + size + " bytes) " +
        "for strings >= " + minLength + " chars...\n\n");

    // Read the module memory in chunks
    let chunkSize = Math.min(size, 0x10000); // 64KB chunks
    let offset = 0;
    let stringCount = 0;

    while (offset < size && stringCount < 500) {
        let readSize = Math.min(chunkSize, size - offset);
        let addr = base.add(offset);

        try {
            let bytes = host.memory.readMemoryValues(addr, readSize, 1);
            let byteArr = [];
            for (let b of bytes) byteArr.push(b);

            // Find ASCII strings
            let current = "";
            for (let i = 0; i < byteArr.length; i++) {
                let b = byteArr[i];
                if (b >= 0x20 && b <= 0x7e) {
                    current += String.fromCharCode(b);
                } else {
                    if (current.length >= minLength) {
                        let strAddr = addr.add(i - current.length);
                        host.diagnostics.debugLog(
                            "0x" + strAddr.toString(16).padStart(16, "0") +
                            "  ASCII  " + current + "\n"
                        );
                        stringCount++;
                    }
                    current = "";
                }
            }
        } catch (e) {
            // Unreadable region, skip
        }

        offset += chunkSize;
    }

    host.diagnostics.debugLog("\nFound " + stringCount + " strings.\n");
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(findStrings, "strings")
    ];
}
```

Usage:
```
!strings ntdll 8
!strings myapp 5
```

## Script 4: Handle Inspector

List open handles with type information:

```javascript
"use strict";

function handleInspector() {
    host.diagnostics.debugLog("=== Open Handles ===\n\n");

    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
        "!handle 0 f"
    );

    let handleCount = 0;
    let typeCounts = {};

    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");

        // Parse handle types for summary
        if (line.indexOf("Type") !== -1) {
            let parts = line.trim().split(/\s+/);
            if (parts.length >= 2) {
                let type = parts[parts.length - 1];
                typeCounts[type] = (typeCounts[type] || 0) + 1;
            }
        }
        if (line.indexOf("Handle") !== -1 && line.indexOf("Object") !== -1) {
            handleCount++;
        }
    }

    host.diagnostics.debugLog("\n=== Handle Type Summary ===\n");
    let sortedTypes = Object.keys(typeCounts).sort(
        (a, b) => typeCounts[b] - typeCounts[a]
    );
    for (let type of sortedTypes) {
        host.diagnostics.debugLog(
            "  " + type.padEnd(20) + typeCounts[type] + "\n"
        );
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(handleInspector, "handles")
    ];
}
```

## Script 5: Critical Section Finder

Find and analyze critical sections (locks):

```javascript
"use strict";

function criticalSections() {
    host.diagnostics.debugLog("=== Critical Section Analysis ===\n\n");

    // Use !locks to find active critical sections
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand("!locks");

    let activeLocks = [];

    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }

    host.diagnostics.debugLog("\n=== Detailed Lock Analysis ===\n\n");

    // Also check for potential deadlocks by examining thread wait chains
    let proc = host.currentProcess;
    let waitingThreads = [];

    for (let thread of proc.Threads) {
        try {
            let topFrame = "";
            for (let frame of thread.Stack.Frames) {
                topFrame = frame.toString();
                break;
            }

            if (topFrame.toLowerCase().indexOf("waitforsingle") !== -1 ||
                topFrame.toLowerCase().indexOf("entercriticalsection") !== -1 ||
                topFrame.toLowerCase().indexOf("rtlacquiresrwlock") !== -1) {
                waitingThreads.push({
                    id: thread.Id,
                    waitFunction: topFrame
                });
            }
        } catch (e) {}
    }

    if (waitingThreads.length > 0) {
        host.diagnostics.debugLog("Threads waiting on locks:\n");
        for (let t of waitingThreads) {
            host.diagnostics.debugLog(
                "  Thread " + t.id + ": " + t.waitFunction + "\n"
            );
        }
    } else {
        host.diagnostics.debugLog("No threads currently waiting on locks.\n");
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(criticalSections, "critsec")
    ];
}
```

## Script 6: Exception Record Analyzer

Parse and display exception information after a crash:

```javascript
"use strict";

function exceptionAnalyzer() {
    host.diagnostics.debugLog("=== Exception Analysis ===\n\n");

    // Get basic exception info
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(".exr -1");
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }

    host.diagnostics.debugLog("\n=== Faulting Context ===\n\n");

    // Show the context at the exception
    output = host.namespace.Debugger.Utility.Control.ExecuteCommand(".ecxr");
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }

    host.diagnostics.debugLog("\n=== Faulting Stack ===\n\n");

    // Show the stack from the exception context
    output = host.namespace.Debugger.Utility.Control.ExecuteCommand("kb");
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }

    host.diagnostics.debugLog("\n=== Automated Analysis ===\n\n");

    // Run !analyze
    output = host.namespace.Debugger.Utility.Control.ExecuteCommand("!analyze -v");
    let inBucketSection = false;
    for (let line of output) {
        // Only print the key sections
        if (line.indexOf("EXCEPTION_RECORD") !== -1 ||
            line.indexOf("FAULTING_IP") !== -1 ||
            line.indexOf("EXCEPTION_CODE") !== -1 ||
            line.indexOf("DEFAULT_BUCKET_ID") !== -1 ||
            line.indexOf("BUCKET_ID") !== -1 ||
            line.indexOf("FAILURE_BUCKET_ID") !== -1 ||
            line.indexOf("MODULE_NAME") !== -1 ||
            line.indexOf("IMAGE_NAME") !== -1 ||
            line.indexOf("STACK_TEXT") !== -1) {
            inBucketSection = true;
        }
        if (inBucketSection) {
            host.diagnostics.debugLog(line + "\n");
            if (line.trim() === "") inBucketSection = false;
        }
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(exceptionAnalyzer, "crashinfo")
    ];
}
```

## Script 7: Module Security Check

Check security features (ASLR, DEP, CFG) of loaded modules:

```javascript
"use strict";

function securityCheck() {
    let proc = host.currentProcess;
    host.diagnostics.debugLog("=== Module Security Features ===\n\n");
    host.diagnostics.debugLog(
        "Module".padEnd(30) +
        "ASLR".padEnd(8) +
        "DEP".padEnd(8) +
        "CFG".padEnd(8) +
        "SafeSEH".padEnd(10) + "\n"
    );
    host.diagnostics.debugLog("─".repeat(64) + "\n");

    for (let mod of proc.Modules) {
        try {
            let base = mod.BaseAddress;

            // Read the PE headers
            let dosHeader = host.memory.readMemoryValues(base, 2, 1);
            let arr = [];
            for (let b of dosHeader) arr.push(b);

            if (arr[0] !== 0x4D || arr[1] !== 0x5A) continue; // Not a PE

            // e_lfanew at offset 0x3C
            let eLfaNew = host.memory.readMemoryValues(base.add(0x3C), 1, 4)[0];
            let peAddr = base.add(eLfaNew);

            // Check PE signature
            let peSig = host.memory.readMemoryValues(peAddr, 1, 4)[0];
            if (peSig !== 0x00004550) continue; // "PE\0\0"

            // DllCharacteristics at PE + 0x18 (optional header) + offset
            // For PE32+: at optional header + 0x46
            // For PE32:  at optional header + 0x42
            let optHeaderAddr = peAddr.add(0x18);
            let magic = host.memory.readMemoryValues(optHeaderAddr, 1, 2)[0];
            let dllCharOffset = (magic === 0x20B) ? 0x46 : 0x42; // PE32+ vs PE32

            let dllChar = host.memory.readMemoryValues(
                optHeaderAddr.add(dllCharOffset), 1, 2
            )[0];

            let aslr = (dllChar & 0x0040) ? "Yes" : "No";    // IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE
            let dep  = (dllChar & 0x0100) ? "Yes" : "No";     // IMAGE_DLLCHARACTERISTICS_NX_COMPAT
            let cfg  = (dllChar & 0x4000) ? "Yes" : "No";     // IMAGE_DLLCHARACTERISTICS_GUARD_CF
            let seh  = (dllChar & 0x0400) ? "No"  : "Yes";    // IMAGE_DLLCHARACTERISTICS_NO_SEH (inverted)

            host.diagnostics.debugLog(
                mod.Name.padEnd(30) +
                aslr.padEnd(8) +
                dep.padEnd(8) +
                cfg.padEnd(8) +
                seh.padEnd(10) + "\n"
            );
        } catch (e) {
            host.diagnostics.debugLog(
                mod.Name.padEnd(30) + "(error reading headers)\n"
            );
        }
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(securityCheck, "seccheck")
    ];
}
```

## Script 8: Stack Variables Viewer

Display local variables for each stack frame:

```javascript
"use strict";

function stackLocals(maxFrames) {
    maxFrames = maxFrames || 5;
    let thread = host.currentThread;

    host.diagnostics.debugLog("=== Stack Locals (Thread " + thread.Id + ") ===\n\n");

    let frameIdx = 0;
    for (let frame of thread.Stack.Frames) {
        if (frameIdx >= maxFrames) break;

        host.diagnostics.debugLog("Frame #" + frameIdx + ": " + frame + "\n");

        // Try to get locals using the dv command for this frame
        try {
            let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
                ".frame " + frameIdx + "; dv /t"
            );
            for (let line of output) {
                if (line.trim().length > 0) {
                    host.diagnostics.debugLog("  " + line + "\n");
                }
            }
        } catch (e) {
            host.diagnostics.debugLog("  (locals unavailable)\n");
        }

        host.diagnostics.debugLog("\n");
        frameIdx++;
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(stackLocals, "locals")
    ];
}
```

## Script 9: Quick Diff Tool

Compare two memory regions:

```javascript
"use strict";

function memDiff(addr1Str, addr2Str, length) {
    length = length || 256;
    let addr1 = host.parseInt64(addr1Str);
    let addr2 = host.parseInt64(addr2Str);

    let bytes1 = host.memory.readMemoryValues(addr1, length, 1);
    let bytes2 = host.memory.readMemoryValues(addr2, length, 1);

    let arr1 = []; for (let b of bytes1) arr1.push(b);
    let arr2 = []; for (let b of bytes2) arr2.push(b);

    let diffCount = 0;

    host.diagnostics.debugLog("Comparing " + length + " bytes:\n");
    host.diagnostics.debugLog("  Region A: 0x" + addr1.toString(16) + "\n");
    host.diagnostics.debugLog("  Region B: 0x" + addr2.toString(16) + "\n\n");

    host.diagnostics.debugLog(
        "Offset".padEnd(10) + "Region A".padEnd(12) + "Region B".padEnd(12) + "\n"
    );
    host.diagnostics.debugLog("─".repeat(34) + "\n");

    for (let i = 0; i < length; i++) {
        if (arr1[i] !== arr2[i]) {
            host.diagnostics.debugLog(
                ("+0x" + i.toString(16)).padEnd(10) +
                ("0x" + arr1[i].toString(16).padStart(2, "0")).padEnd(12) +
                ("0x" + arr2[i].toString(16).padStart(2, "0")).padEnd(12) +
                " <-- DIFF\n"
            );
            diffCount++;
        }
    }

    host.diagnostics.debugLog("\n" + diffCount + " byte(s) differ out of " + length + ".\n");
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(memDiff, "memdiff")
    ];
}
```

## Exercises

1. Modify the string finder to also detect Unicode (UTF-16) strings.
2. Extend the security check to also parse the import table and count imported DLLs.
3. Write a script that dumps all environment variables by parsing the environment block from the PEB.
4. Create a "process snapshot" script that saves current registers, loaded modules, and thread states to a text format.
5. Combine the handle inspector and critical section finder into a single "deadlock detective" script.

## Next

[Module 11 — Kernel Debugging with JS](11-kernel-debugging-with-js.md) — Enumerate processes, drivers, and kernel structures via JavaScript.
