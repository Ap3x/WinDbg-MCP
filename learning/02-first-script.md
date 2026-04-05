# Module 02 — First Script

## The Anatomy of a WinDbg JavaScript Script

Every WinDbg JS script can define special functions that the provider calls at specific times:

```javascript
"use strict";

// Called when the script is loaded (.scriptload)
// Return an array of registration objects (commands, visualizers, etc.)
function initializeScript() {
    return [
        // registrations go here
    ];
}

// Called when the script is run (.scriptrun)
function invokeScript() {
    host.diagnostics.debugLog("Hello from WinDbg JavaScript!\n");
}

// Called when the script is unloaded (.scriptunload)
function uninitializeScript() {
    host.diagnostics.debugLog("Script unloaded.\n");
}
```

| Function | When Called | Required? |
|----------|-----------|-----------|
| `initializeScript()` | `.scriptload` or `.scriptrun` | No |
| `invokeScript()` | `.scriptrun` only | No |
| `uninitializeScript()` | `.scriptunload` | No |

## Hello World

Create a file `C:\WinDbgScripts\hello.js`:

```javascript
"use strict";

function invokeScript() {
    host.diagnostics.debugLog("Hello, WinDbg JavaScript World!\n");
}
```

In WinDbg (with any debug target loaded):
```
.scriptrun C:\WinDbgScripts\hello.js
```

Output:
```
JavaScript script successfully loaded from 'C:\WinDbgScripts\hello.js'
Hello, WinDbg JavaScript World!
```

### `.scriptload` vs `.scriptrun`

- **`.scriptload`** — loads the script and calls `initializeScript()` but NOT `invokeScript()`
- **`.scriptrun`** — loads the script, calls `initializeScript()`, then calls `invokeScript()`

Use `.scriptload` when you're registering commands/visualizers that will be called later.  
Use `.scriptrun` when you want immediate execution.

## Printing Output

There are two ways to output text:

```javascript
// Method 1: host.diagnostics.debugLog — prints to the command window
host.diagnostics.debugLog("This appears in the WinDbg command output\n");

// Method 2: Print multiple values
host.diagnostics.debugLog("Value: ", someVar, ", Type: ", typeof someVar, "\n");
```

Note: `console.log()` does NOT work in WinDbg JavaScript. Always use `host.diagnostics.debugLog()`.

**Tip:** Create a shorthand helper at the top of your scripts:

```javascript
function log(msg) {
    host.diagnostics.debugLog(msg + "\n");
}
```

## Accessing Debugger State

The `host` object is your gateway to everything. Here's a taste:

```javascript
"use strict";

function invokeScript() {
    // Current process
    let proc = host.currentProcess;
    host.diagnostics.debugLog("Process: " + proc.Name + "\n");
    host.diagnostics.debugLog("PID: " + proc.Id + "\n");

    // Current thread
    let thread = host.currentThread;
    host.diagnostics.debugLog("Thread ID: " + thread.Id + "\n");

    // List all modules
    host.diagnostics.debugLog("\nLoaded Modules:\n");
    for (let mod of proc.Modules) {
        host.diagnostics.debugLog("  " + mod.Name + " @ 0x" +
            mod.BaseAddress.toString(16) + "\n");
    }
}
```

Save this as `inspect.js` and run it:
```
.scriptrun C:\WinDbgScripts\inspect.js
```

## Creating Custom Commands

The most practical use of scripts is defining custom commands. These become available as WinDbg commands you can call anytime.

```javascript
"use strict";

// Define a function — this becomes the command implementation
function listModuleSizes() {
    let proc = host.currentProcess;
    host.diagnostics.debugLog("Module sizes for: " + proc.Name + "\n");
    host.diagnostics.debugLog("─".repeat(60) + "\n");

    for (let mod of proc.Modules) {
        let size = mod.Size;
        let sizeKB = (size / 1024).toFixed(1);
        let name = mod.Name;
        host.diagnostics.debugLog(
            name.padEnd(40) + sizeKB.padStart(10) + " KB\n"
        );
    }
}

// Register the command
function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(listModuleSizes, "modsizes")
    ];
}
```

Load it:
```
.scriptload C:\WinDbgScripts\modsizes.js
```

Now you can call it anytime:
```
!modsizes
```

### Command with Arguments

Commands can accept arguments:

```javascript
"use strict";

function searchModule(moduleName) {
    let proc = host.currentProcess;
    for (let mod of proc.Modules) {
        if (mod.Name.toLowerCase().indexOf(moduleName.toLowerCase()) !== -1) {
            host.diagnostics.debugLog("Found: " + mod.Name + "\n");
            host.diagnostics.debugLog("  Base: 0x" + mod.BaseAddress.toString(16) + "\n");
            host.diagnostics.debugLog("  Size: " + mod.Size + " bytes\n");
            return;
        }
    }
    host.diagnostics.debugLog("Module '" + moduleName + "' not found.\n");
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(searchModule, "findmod")
    ];
}
```

Usage:
```
!findmod ntdll
!findmod kernel32
```

## Working with `dx` and Script Contents

After loading a script, you can access its exported functions via `dx`:

```
dx @$scriptContents.listModuleSizes()
dx @$scriptContents.searchModule("ntdll")
```

This is powerful for interactive exploration — you can call any function your script exports and see the results formatted by the DDM.

## Error Handling

Errors in WinDbg JS print a stack trace to the command window:

```javascript
function invokeScript() {
    try {
        let proc = host.currentProcess;
        // This might fail if there's no target
        let name = proc.Name;
        host.diagnostics.debugLog("Process: " + name + "\n");
    } catch (e) {
        host.diagnostics.debugLog("Error: " + e.message + "\n");
        host.diagnostics.debugLog("Make sure a debug target is loaded.\n");
    }
}
```

Common errors:
- **"No process is currently being debugged"** — no target loaded
- **"Object reference not set"** — accessing a property on `undefined`/`null`
- **"SyntaxError"** — JavaScript syntax issue; check your script

## Script Lifecycle Summary

```
.scriptrun myScript.js
    │
    ├─► initializeScript()    ← register commands, visualizers
    │       returns [registrations]
    │
    └─► invokeScript()        ← immediate execution code
            runs once

.scriptload myScript.js
    │
    └─► initializeScript()    ← register commands, visualizers only
            returns [registrations]

!myCommand                    ← calls registered function any time
    │
    └─► myCommandFunction()

.scriptunload myScript.js
    │
    └─► uninitializeScript()  ← cleanup
```

## Exercises

1. Create a `hello.js` that prints "Hello World" and run it with `.scriptrun`.
2. Modify `hello.js` to print the current process name and PID.
3. Create a script that defines a `!threadcount` command that prints the number of threads in the current process.
4. Create a script with both `initializeScript` (registering a command) and `invokeScript` (printing a message). Observe the difference between `.scriptload` and `.scriptrun`.
5. Experiment with `dx @$scriptContents` after loading a script — browse the exported functions.

## Next

[Module 03 — The Debugger Object Model](03-debugger-object-model.md) — Deep dive into `host`, sessions, processes, threads, and how the data model is structured.
