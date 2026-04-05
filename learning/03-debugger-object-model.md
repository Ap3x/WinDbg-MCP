# Module 03 — The Debugger Object Model

## The `host` Object

The `host` object is the root of everything in WinDbg JavaScript. It's the bridge between your script and the Debugger Data Model (DDM).

```
host
├── .currentProcess          → current process object
├── .currentThread           → current thread object
├── .currentSession          → current debug session
├── .diagnostics             → logging utilities
│   └── .debugLog()          → print to command window
├── .memory                  → memory access
├── .namespace               → the Debugger namespace (root of dx Debugger)
├── .typeSystem              → type creation and casting
├── .parseInt64()            → parse 64-bit integers
├── .Int64()                 → create 64-bit integer
├── .apiVersionSupport()     → declare API version
├── .functionAlias()         → register a command
├── .namedModelParent()      → register a visualizer
├── .namedModelRegistration()→ register a model extension
└── .evaluateExpression()    → evaluate a WinDbg expression string
```

## Exploring with `dx`

Before writing scripts, learn to explore the model interactively with `dx`:

```
dx Debugger
dx Debugger.Sessions
dx Debugger.Sessions[0]
dx Debugger.Sessions[0].Processes
dx Debugger.Sessions[0].Processes[0]
dx Debugger.Sessions[0].Processes[0].Threads
```

Every object you see in `dx` output is accessible from JavaScript via `host.namespace`.

**Shortcut aliases:**
```
dx @$curprocess            → host.currentProcess
dx @$curthread             → host.currentThread
dx @$cursession            → host.currentSession
dx @$curprocess.Threads    → host.currentProcess.Threads
```

## Sessions, Processes, Threads

The debugger model is hierarchical:

```
Debugger
└── Sessions[]                    (debug sessions — usually one)
    └── Processes[]               (debugged processes)
        ├── .Name                 (executable name)
        ├── .Id                   (PID)
        ├── .Threads[]            (threads in the process)
        │   ├── .Id               (thread ID)
        │   ├── .Stack            (call stack)
        │   │   └── .Frames[]     (stack frames)
        │   └── .Registers        (register sets)
        │       └── .User         (general-purpose registers)
        ├── .Modules[]            (loaded modules/DLLs)
        │   ├── .Name
        │   ├── .BaseAddress
        │   ├── .Size
        │   └── .Symbols          (module symbols)
        └── .Io                   (handles, etc.)
```

### Walking the Hierarchy in JavaScript

```javascript
"use strict";

function invokeScript() {
    // Access via host shortcuts
    let session = host.currentSession;
    let process = host.currentProcess;
    let thread  = host.currentThread;

    host.diagnostics.debugLog("Session: " + session.Id + "\n");
    host.diagnostics.debugLog("Process: " + process.Name + " (PID " + process.Id + ")\n");
    host.diagnostics.debugLog("Thread:  " + thread.Id + "\n");

    // Or walk the full hierarchy
    for (let sess of host.namespace.Debugger.Sessions) {
        host.diagnostics.debugLog("\nSession " + sess.Id + ":\n");
        for (let proc of sess.Processes) {
            host.diagnostics.debugLog("  Process " + proc.Id + ": " + proc.Name + "\n");
            let threadCount = 0;
            for (let t of proc.Threads) {
                threadCount++;
            }
            host.diagnostics.debugLog("    Threads: " + threadCount + "\n");
        }
    }
}
```

## Modules In Depth

Modules represent loaded DLLs and the main executable:

```javascript
"use strict";

function dumpModules() {
    let proc = host.currentProcess;

    for (let mod of proc.Modules) {
        host.diagnostics.debugLog("Module: " + mod.Name + "\n");
        host.diagnostics.debugLog("  Base Address:  0x" + mod.BaseAddress.toString(16) + "\n");
        host.diagnostics.debugLog("  Size:          0x" + mod.Size.toString(16) + "\n");

        // Version info (if available)
        try {
            let ver = mod.Contents.Headers.FileHeader;
            host.diagnostics.debugLog("  Machine:       0x" + ver.Machine.toString(16) + "\n");
        } catch (e) {
            // Version info not always available
        }

        host.diagnostics.debugLog("\n");
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(dumpModules, "dumpmod")
    ];
}
```

### Finding a Specific Module

```javascript
function findModule(name) {
    let proc = host.currentProcess;
    for (let mod of proc.Modules) {
        if (mod.Name.toLowerCase().indexOf(name.toLowerCase()) !== -1) {
            return mod;
        }
    }
    return undefined;
}
```

## Threads In Depth

```javascript
"use strict";

function threadInfo() {
    let proc = host.currentProcess;
    host.diagnostics.debugLog("Threads in " + proc.Name + ":\n\n");

    for (let thread of proc.Threads) {
        host.diagnostics.debugLog("Thread " + thread.Id + ":\n");

        // Get the top stack frame
        try {
            let frames = thread.Stack.Frames;
            let topFrame = undefined;
            for (let f of frames) {
                topFrame = f;
                break; // just get the first one
            }
            if (topFrame) {
                host.diagnostics.debugLog("  Top Frame: " + topFrame + "\n");
            }
        } catch (e) {
            host.diagnostics.debugLog("  (stack unavailable)\n");
        }

        // Get instruction pointer
        try {
            let rip = thread.Registers.User.rip;
            if (rip === undefined) {
                rip = thread.Registers.User.eip; // 32-bit
            }
            host.diagnostics.debugLog("  IP: 0x" + rip.toString(16) + "\n");
        } catch (e) {
            // Registers might not be accessible
        }

        host.diagnostics.debugLog("\n");
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(threadInfo, "threadinfo")
    ];
}
```

## The `host.namespace.Debugger` Tree

The `Debugger` namespace in `dx` maps to `host.namespace.Debugger` in JavaScript. This gives you access to everything:

```javascript
// These are equivalent:
//   dx Debugger.Sessions[0].Processes[0]
let proc = host.namespace.Debugger.Sessions[0].Processes[0];

// But it's easier to use shortcuts:
let proc2 = host.currentProcess;

// Access registered model extensions:
//   dx Debugger.Utility
let utility = host.namespace.Debugger.Utility;
```

### Useful Debugger.Utility Paths

```
dx Debugger.Utility.Control            — execution control
dx Debugger.Utility.Control.ExecuteCommand("vertarget")
dx Debugger.Utility.Collections        — collection helpers
dx Debugger.Utility.FileSystem         — file I/O (limited)
```

In JavaScript:
```javascript
// Execute a WinDbg command and get its text output
let output = host.namespace.Debugger.Utility.Control.ExecuteCommand("vertarget");
for (let line of output) {
    host.diagnostics.debugLog(line + "\n");
}
```

This is incredibly powerful — you can run any WinDbg command and parse its output.

## `host.evaluateExpression()`

Evaluate WinDbg/MASM or C++ expressions from JavaScript:

```javascript
// MASM expression
let val = host.evaluateExpression("poi(@rsp)");
host.diagnostics.debugLog("Value at RSP: 0x" + val.toString(16) + "\n");

// Debugger data model expression (same as dx)
let ntdll = host.evaluateExpression("@$curprocess.Modules[\"ntdll\"]");
host.diagnostics.debugLog("ntdll base: 0x" + ntdll.BaseAddress.toString(16) + "\n");
```

## 64-bit Integers

JavaScript numbers are IEEE 754 doubles, which lose precision above 2^53. WinDbg addresses are 64-bit. The provider handles this via special 64-bit integer objects:

```javascript
// Parse a 64-bit hex value
let addr = host.parseInt64("0x00007ff`6c3a10000");

// Create a 64-bit value
let val = host.Int64(0x1000);

// Arithmetic
let nextPage = addr.add(0x1000);
let diff = addr.subtract(host.parseInt64("0x00007ff600000000"));

// Comparison
let isEqual = addr.compareTo(nextPage) === 0;
let isLess  = addr.compareTo(nextPage) < 0;

// Convert to string
host.diagnostics.debugLog("Address: 0x" + addr.toString(16) + "\n");
```

**Important:** Always use `host.parseInt64()` for addresses — regular `parseInt()` will lose bits on 64-bit values.

## Object Properties and Iteration

DDM objects have both native properties and model-added properties. You can explore them:

```javascript
function exploreObject(obj) {
    // Iterate over an object's keys
    for (let key of Object.keys(obj)) {
        try {
            let val = obj[key];
            host.diagnostics.debugLog("  " + key + ": " + val + "\n");
        } catch (e) {
            host.diagnostics.debugLog("  " + key + ": (error reading)\n");
        }
    }
}

function invokeScript() {
    host.diagnostics.debugLog("Current Process properties:\n");
    exploreObject(host.currentProcess);
}
```

## Exercises

1. Write a script that prints the full session > process > thread hierarchy for your debug target.
2. Use `host.namespace.Debugger.Utility.Control.ExecuteCommand()` to run `lm` and print its output.
3. Write a function that finds the largest module loaded in the current process.
4. Experiment with `host.parseInt64()` — parse an address, add an offset, and print the result.
5. Use `host.evaluateExpression()` to evaluate `@$curprocess.Threads.Count()` and print the result.

## Next

[Module 04 — Working with Memory](04-working-with-memory.md) — Read, write, and search process memory from JavaScript.
