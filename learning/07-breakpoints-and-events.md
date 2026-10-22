# Module 07 — Breakpoints & Events

## Why JavaScript Breakpoints?

WinDbg has always supported conditional breakpoints, but the syntax is limited:

```
bp kernel32!CreateFileW ".if (poi(@rcx) == 0x43) { .echo 'Found C' } .else { gc }"
```

With JavaScript, you get full programming power in your breakpoint handlers — parse arguments, maintain state, filter conditions, and log structured data.

## Setting Breakpoints from JavaScript

### Method 1: Using `ExecuteCommand`

The simplest way — just run the WinDbg breakpoint command:

```javascript
function setBP(symbol) {
    host.namespace.Debugger.Utility.Control.ExecuteCommand("bp " + symbol);
    host.diagnostics.debugLog("Breakpoint set on " + symbol + "\n");
}
```

### Method 2: Breakpoints with JavaScript Handlers

This is the powerful approach. You define a JavaScript function that WinDbg calls when the breakpoint hits:

```javascript
"use strict";

// The breakpoint handler function
// Return true  = break into debugger (stop execution)
// Return false = continue execution (like 'gc')
function onCreateFile(/* no arguments for bp handler */) {
    let regs = host.currentThread.Registers.User;

    // On x64 Windows, first arg is in rcx
    // CreateFileW(LPCWSTR lpFileName, ...)
    let fileNamePtr = regs.rcx;

    // Read the wide string
    let words = host.memory.readMemoryValues(fileNamePtr, 260, 2);
    let fileName = "";
    for (let w of words) {
        if (w === 0) break;
        fileName += String.fromCharCode(w);
    }

    host.diagnostics.debugLog("[CreateFileW] " + fileName + "\n");

    // Only break if the filename contains ".exe"
    if (fileName.toLowerCase().indexOf(".exe") !== -1) {
        host.diagnostics.debugLog("  >>> Breaking on EXE file access!\n");
        return true;  // break into debugger
    }

    return false;  // continue execution
}

function invokeScript() {
    // Set the breakpoint with our JS handler
    host.namespace.Debugger.Utility.Control.ExecuteCommand(
        'bp /w "@$scriptContents.onCreateFile()" kernel32!CreateFileW'
    );
    host.diagnostics.debugLog("Breakpoint set on CreateFileW with JS handler.\n");
    host.diagnostics.debugLog("Run 'g' to continue. Will break on .exe file access.\n");
}
```

### The `/w` Flag

The `/w` flag on `bp` specifies a WinDbg data model expression to evaluate. If the expression returns a truthy value, the debugger breaks; if falsy, it continues.

```
bp /w "@$scriptContents.myHandler()" module!function
```

This is the bridge between WinDbg breakpoints and your JavaScript functions.

## Conditional Breakpoints

### Filtering by Argument Values

```javascript
"use strict";

// Track CreateFileW calls, only break on write access
function onCreateFileFiltered() {
    let regs = host.currentThread.Registers.User;

    // CreateFileW(lpFileName, dwDesiredAccess, ...)
    let fileNamePtr = regs.rcx;
    let desiredAccess = regs.rdx;

    let GENERIC_WRITE = host.parseInt64("0x40000000");

    // Check if GENERIC_WRITE is in the access mask
    let accessVal = parseInt(desiredAccess.toString());
    if ((accessVal & 0x40000000) !== 0) {
        let fileName = readWideString(fileNamePtr);
        host.diagnostics.debugLog("[WRITE] " + fileName + "\n");
        return true;  // break on write access
    }

    return false;  // continue on read-only access
}

function readWideString(addr) {
    let words = host.memory.readMemoryValues(addr, 260, 2);
    let str = "";
    for (let w of words) {
        if (w === 0) break;
        str += String.fromCharCode(w);
    }
    return str;
}
```

### Counting Hits

Maintain state across breakpoint hits:

```javascript
"use strict";

var hitCount = 0;
var callLog = [];

function onApiCall() {
    hitCount++;
    let regs = host.currentThread.Registers.User;
    let rip = regs.rip;

    callLog.push({
        count: hitCount,
        ip: rip.toString(16),
        thread: host.currentThread.Id
    });

    host.diagnostics.debugLog(
        "[Hit #" + hitCount + "] Thread " +
        host.currentThread.Id + " at 0x" + rip.toString(16) + "\n"
    );

    // Break every 10th hit
    if (hitCount % 10 === 0) {
        host.diagnostics.debugLog("  Breaking at hit #" + hitCount + "\n");
        return true;
    }

    return false;
}

function showLog() {
    host.diagnostics.debugLog("Total hits: " + hitCount + "\n");
    for (let entry of callLog) {
        host.diagnostics.debugLog(
            "  #" + entry.count +
            " Thread " + entry.thread +
            " @ 0x" + entry.ip + "\n"
        );
    }
}

function resetLog() {
    hitCount = 0;
    callLog = [];
    host.diagnostics.debugLog("Log cleared.\n");
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(showLog, "apilog"),
        new host.functionAlias(resetLog, "apireset")
    ];
}
```

## Hardware Breakpoints

Hardware breakpoints (using debug registers) can be set from JS via commands:

```javascript
// Hardware breakpoint on memory access
function setHwBreakpoint(addrStr, accessType) {
    // accessType: "r" = read, "w" = write, "e" = execute
    let cmd = "ba " + accessType + "1 " + addrStr;
    host.namespace.Debugger.Utility.Control.ExecuteCommand(cmd);
    host.diagnostics.debugLog("Hardware breakpoint set: " + cmd + "\n");
}

// Watch a memory address for writes
// setHwBreakpoint("0x00000001`00001000", "w")
```

## Data Breakpoints (Break on Memory Write)

```javascript
"use strict";

function watchVariable(expression) {
    // Evaluate the expression to get the address
    let addr = host.evaluateExpression(expression);
    host.diagnostics.debugLog(
        "Watching address 0x" + addr.toString(16) + " for writes...\n"
    );

    // Set a write-access hardware breakpoint (4 bytes)
    host.namespace.Debugger.Utility.Control.ExecuteCommand(
        "ba w4 0x" + addr.toString(16)
    );
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(watchVariable, "watchvar")
    ];
}
```

## Breakpoint Management

### Listing Breakpoints

```javascript
function listBreakpoints() {
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand("bl");
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }
}
```

### Enabling/Disabling/Removing

```javascript
function enableBP(id)  {
    host.namespace.Debugger.Utility.Control.ExecuteCommand("be " + id);
}

function disableBP(id) {
    host.namespace.Debugger.Utility.Control.ExecuteCommand("bd " + id);
}

function removeBP(id)  {
    host.namespace.Debugger.Utility.Control.ExecuteCommand("bc " + id);
}

function clearAllBPs()  {
    host.namespace.Debugger.Utility.Control.ExecuteCommand("bc *");
    host.diagnostics.debugLog("All breakpoints cleared.\n");
}
```

## Practical Example: API Tracer

A comprehensive API tracer that logs function calls with arguments:

```javascript
"use strict";

var traceLog = [];

function traceApi(symbolName) {
    // Set the breakpoint
    host.namespace.Debugger.Utility.Control.ExecuteCommand(
        'bp /w "@$scriptContents.onTrace()" ' + symbolName
    );
    host.diagnostics.debugLog("Tracing: " + symbolName + "\n");
}

function onTrace() {
    let regs = host.currentThread.Registers.User;

    // x64 calling convention: rcx, rdx, r8, r9, then stack
    let entry = {
        thread: host.currentThread.Id,
        rip: regs.rip.toString(16),
        arg1: regs.rcx.toString(16),
        arg2: regs.rdx.toString(16),
        arg3: regs.r8.toString(16),
        arg4: regs.r9.toString(16),
        returnAddr: host.memory.readMemoryValues(regs.rsp, 1, 8)[0].toString(16)
    };

    // Try to resolve the function name
    try {
        let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
            "ln 0x" + entry.rip
        );
        for (let line of output) {
            if (line.trim().length > 0) {
                entry.symbol = line.trim();
                break;
            }
        }
    } catch (e) {
        entry.symbol = "(unknown)";
    }

    traceLog.push(entry);

    host.diagnostics.debugLog(
        "[TRACE] " + entry.symbol +
        " rcx=0x" + entry.arg1 +
        " rdx=0x" + entry.arg2 + "\n"
    );

    return false;  // don't break, continue tracing
}

function showTrace() {
    host.diagnostics.debugLog("=== API Trace Log (" + traceLog.length + " entries) ===\n\n");
    for (let i = 0; i < traceLog.length; i++) {
        let e = traceLog[i];
        host.diagnostics.debugLog(
            "[" + i + "] T" + e.thread +
            " " + (e.symbol || "0x" + e.rip) +
            " (0x" + e.arg1 + ", 0x" + e.arg2 +
            ", 0x" + e.arg3 + ", 0x" + e.arg4 + ")" +
            " ret=0x" + e.returnAddr + "\n"
        );
    }
}

function clearTrace() {
    traceLog = [];
    host.diagnostics.debugLog("Trace log cleared.\n");
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(traceApi, "trace"),
        new host.functionAlias(showTrace, "showtrace"),
        new host.functionAlias(clearTrace, "cleartrace")
    ];
}
```

Usage:
```
.scriptload C:\WinDbgScripts\tracer.js
!trace kernel32!CreateFileW
!trace kernel32!ReadFile
!trace kernel32!CloseHandle
g
                            <-- let it run, then break with Ctrl+Break
!showtrace
```

## Practical Example: Function Parameter Logger

Log specific API calls with decoded parameters:

```javascript
"use strict";

function onRegOpenKey() {
    let regs = host.currentThread.Registers.User;

    // RegOpenKeyExW(HKEY hKey, LPCWSTR lpSubKey, ...)
    let hKey = regs.rcx;
    let subKeyPtr = regs.rdx;

    // Decode well-known HKEY values
    let hKeyName = "0x" + hKey.toString(16);
    let hKeyVal = parseInt(hKey.toString());
    if (hKeyVal === 0x80000000) hKeyName = "HKEY_CLASSES_ROOT";
    else if (hKeyVal === 0x80000001) hKeyName = "HKEY_CURRENT_USER";
    else if (hKeyVal === 0x80000002) hKeyName = "HKEY_LOCAL_MACHINE";

    // Read the subkey name
    let subKey = readWideString(subKeyPtr);

    host.diagnostics.debugLog(
        "[RegOpenKeyExW] " + hKeyName + "\\" + subKey + "\n"
    );

    return false;
}

function readWideString(addr) {
    try {
        let words = host.memory.readMemoryValues(addr, 260, 2);
        let str = "";
        for (let w of words) {
            if (w === 0) break;
            str += String.fromCharCode(w);
        }
        return str;
    } catch (e) {
        return "(unreadable)";
    }
}

function invokeScript() {
    host.namespace.Debugger.Utility.Control.ExecuteCommand(
        'bp /w "@$scriptContents.onRegOpenKey()" advapi32!RegOpenKeyExW'
    );
    host.diagnostics.debugLog("Logging RegOpenKeyExW calls. Run 'g' to continue.\n");
}
```

## Exercises

1. Write a breakpoint handler that logs all `CreateFileW` calls with the filename argument decoded.
2. Write a hit counter that breaks on the Nth call to a function (configurable N).
3. Write a breakpoint handler that only breaks when called from a specific module (check the return address).
4. Create an API tracer for `VirtualAlloc` that logs the requested size and protection flags.
5. Write a script that sets a data breakpoint on a global variable and logs what writes to it.

## Next

[Module 08 — Iterators & LINQ](08-iterators-and-linq.md) — Use the LINQ-style query methods built into the debugger data model.
