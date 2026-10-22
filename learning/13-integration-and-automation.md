# Module 13 — Integration & Automation

## Overview

This final module brings everything together — combining JavaScript scripts with WinDbg extensions, MCP servers (like the WinDbg-MCP project this course lives in), and automated debugging workflows.

## Running WinDbg Commands from Scripts

We've used `ExecuteCommand` throughout the course. Here's a robust wrapper:

```javascript
"use strict";

function exec(command) {
    let lines = [];
    try {
        let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(command);
        for (let line of output) {
            lines.push(line);
        }
    } catch (e) {
        lines.push("ERROR: " + e.message);
    }
    return lines;
}

function execAndPrint(command) {
    let lines = exec(command);
    for (let line of lines) {
        host.diagnostics.debugLog(line + "\n");
    }
    return lines;
}

// Parse key-value output from WinDbg commands
function parseKeyValue(lines) {
    let result = {};
    for (let line of lines) {
        let colonIdx = line.indexOf(":");
        if (colonIdx !== -1) {
            let key = line.substring(0, colonIdx).trim();
            let val = line.substring(colonIdx + 1).trim();
            result[key] = val;
        }
    }
    return result;
}
```

## Building a Debugging Toolkit Script

Combine multiple analysis functions into a single toolkit:

```javascript
"use strict";

// ===== Utility Functions =====

function exec(cmd) {
    let lines = [];
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(cmd);
    for (let line of output) lines.push(line);
    return lines;
}

function log(msg) {
    host.diagnostics.debugLog(msg + "\n");
}

function readWideString(addr, maxChars) {
    maxChars = maxChars || 260;
    try {
        let words = host.memory.readMemoryValues(addr, maxChars, 2);
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

// ===== Analysis Commands =====

function quickAnalysis() {
    log("=== Quick Analysis Report ===\n");

    // Process info
    let proc = host.currentProcess;
    log("Process: " + proc.Name + " (PID " + proc.Id + ")");

    // Module count
    let modCount = 0;
    for (let m of proc.Modules) modCount++;
    log("Modules: " + modCount);

    // Thread count
    let threadCount = 0;
    let waitingCount = 0;
    for (let t of proc.Threads) {
        threadCount++;
        try {
            for (let f of t.Stack.Frames) {
                let name = f.toString().toLowerCase();
                if (name.indexOf("wait") !== -1 || name.indexOf("sleep") !== -1) {
                    waitingCount++;
                }
                break;
            }
        } catch (e) {}
    }
    log("Threads: " + threadCount + " (" + waitingCount + " waiting)");

    // Current thread context
    log("\nCurrent Thread: " + host.currentThread.Id);
    let frames = [];
    try {
        for (let f of host.currentThread.Stack.Frames) {
            frames.push(f.toString());
            if (frames.length >= 5) break;
        }
    } catch (e) {}

    log("Top frames:");
    for (let f of frames) {
        log("  " + f);
    }

    // Check for exceptions
    log("\nException check:");
    let exrLines = exec(".exr -1");
    for (let line of exrLines) {
        if (line.indexOf("not found") === -1) {
            log("  " + line);
        }
    }
}

function fullReport() {
    quickAnalysis();
    log("\n" + "=".repeat(60));

    // Detailed module list
    log("\n=== Module List ===\n");
    let proc = host.currentProcess;
    for (let mod of proc.Modules) {
        log("  " + mod.Name.padEnd(30) +
            "0x" + mod.BaseAddress.toString(16).padStart(16, "0") +
            "  " + (mod.Size / 1024).toFixed(0).padStart(8) + " KB");
    }

    // All thread stacks
    log("\n=== Thread Stacks ===\n");
    for (let thread of proc.Threads) {
        log("Thread " + thread.Id + ":");
        try {
            let depth = 0;
            for (let frame of thread.Stack.Frames) {
                log("  #" + depth + " " + frame);
                depth++;
                if (depth >= 8) {
                    log("  ...");
                    break;
                }
            }
        } catch (e) {
            log("  (stack unavailable)");
        }
        log("");
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(quickAnalysis, "qa"),
        new host.functionAlias(fullReport, "fullreport")
    ];
}
```

## Integration with WinDbg-MCP

The [WinDbg-MCP](https://github.com/ap3x/windbg-mcp) project in this repository exposes WinDbg functionality via the Model Context Protocol (MCP). This lets AI assistants like Claude interact with debugging sessions.

### How JS Scripts Complement MCP

```
┌────────────────────────────────┐
│         AI Assistant           │
│  (Claude via MCP protocol)     │
└──────────────┬─────────────────┘
               │ MCP Tool Calls
               ▼
┌────────────────────────────────┐
│      WinDbg-MCP Extension      │
│  execute_command("...")         │
│  read_memory(addr, size)       │
│  get_callstack()               │
└──────────────┬─────────────────┘
               │ dbgeng COM
               ▼
┌────────────────────────────────┐
│          WinDbg Engine          │
│  ┌──────────────────────────┐  │
│  │  Your JS Scripts         │  │
│  │  (loaded via .scriptload)│  │
│  └──────────────────────────┘  │
└────────────────────────────────┘
```

The MCP `execute_command` tool can call your custom JS commands:

```json
{
    "tool": "execute_command",
    "arguments": {
        "command": "!qa"
    }
}
```

This means you can:
1. **Write JS scripts** that perform complex analysis
2. **Register them as commands** (`!mycommand`)
3. **Call them via MCP** from an AI assistant
4. **Get structured results** back

### Designing Scripts for MCP Consumption

When writing scripts intended to be called through MCP, format output for easy parsing:

```javascript
"use strict";

// Output structured data that's easy for AI to parse
function processSnapshot() {
    let proc = host.currentProcess;

    // Use a consistent, parseable format
    log("PROCESS_NAME: " + proc.Name);
    log("PROCESS_PID: " + proc.Id);

    log("MODULE_COUNT: " + proc.Modules.Count());

    log("---MODULES_START---");
    for (let mod of proc.Modules) {
        // CSV-like format: name,base,size
        log(mod.Name + "," +
            "0x" + mod.BaseAddress.toString(16) + "," +
            mod.Size);
    }
    log("---MODULES_END---");

    log("---THREADS_START---");
    for (let thread of proc.Threads) {
        let topFrame = "(unknown)";
        try {
            for (let f of thread.Stack.Frames) {
                topFrame = f.toString();
                break;
            }
        } catch (e) {}
        log(thread.Id + "," + topFrame);
    }
    log("---THREADS_END---");
}

function log(msg) {
    host.diagnostics.debugLog(msg + "\n");
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(processSnapshot, "snapshot")
    ];
}
```

## Automated Crash Dump Analysis

Create a script that performs automated triage on crash dumps:

```javascript
"use strict";

function log(msg) { host.diagnostics.debugLog(msg + "\n"); }

function exec(cmd) {
    let lines = [];
    try {
        let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(cmd);
        for (let line of output) lines.push(line);
    } catch (e) {
        lines.push("ERROR: " + e.message);
    }
    return lines;
}

function triageDump() {
    log("╔══════════════════════════════════════════╗");
    log("║      Automated Crash Dump Triage         ║");
    log("╚══════════════════════════════════════════╝\n");

    // Step 1: Target info
    log("=== Step 1: Target Information ===\n");
    let vtLines = exec("vertarget");
    for (let line of vtLines) log("  " + line);

    // Step 2: Exception record
    log("\n=== Step 2: Exception Analysis ===\n");
    let exrLines = exec(".exr -1");
    for (let line of exrLines) log("  " + line);

    // Step 3: Faulting thread stack
    log("\n=== Step 3: Faulting Thread Stack ===\n");
    exec(".ecxr");  // set exception context
    let kLines = exec("k");
    for (let line of kLines) log("  " + line);

    // Step 4: Registers at exception
    log("\n=== Step 4: Registers ===\n");
    let rLines = exec("r");
    for (let line of rLines) log("  " + line);

    // Step 5: Faulting instruction
    log("\n=== Step 5: Faulting Instruction ===\n");
    let uLines = exec("u @rip L3");
    for (let line of uLines) log("  " + line);

    // Step 6: Module at fault
    log("\n=== Step 6: Faulting Module ===\n");
    let lmLines = exec("lm a @rip");
    for (let line of lmLines) log("  " + line);

    // Step 7: Quick check for common issues
    log("\n=== Step 7: Common Issue Check ===\n");

    // Check for null pointer dereference
    for (let line of exrLines) {
        if (line.indexOf("c0000005") !== -1 || line.indexOf("C0000005") !== -1) {
            log("  [!] Access Violation detected");

            // Check if it's a null deref
            for (let eline of exrLines) {
                if (eline.indexOf("Read") !== -1 || eline.indexOf("Write") !== -1) {
                    if (eline.indexOf("00000000") !== -1) {
                        log("  [!] Likely NULL pointer dereference");
                    }
                }
            }
        }
        if (line.indexOf("c00000fd") !== -1 || line.indexOf("C00000FD") !== -1) {
            log("  [!] Stack Overflow detected");
        }
        if (line.indexOf("80000003") !== -1) {
            log("  [i] Breakpoint (int 3) — may be intentional");
        }
    }

    // Step 8: !analyze summary
    log("\n=== Step 8: Automated Analysis Summary ===\n");
    let analyzeLines = exec("!analyze -v");
    let printNext = false;
    for (let line of analyzeLines) {
        if (line.indexOf("BUCKET_ID") !== -1 ||
            line.indexOf("FAILURE_BUCKET_ID") !== -1 ||
            line.indexOf("FOLLOWUP_IP") !== -1 ||
            line.indexOf("FAULTING_SOURCE_CODE") !== -1 ||
            line.indexOf("MODULE_NAME") !== -1 ||
            line.indexOf("IMAGE_NAME") !== -1 ||
            line.indexOf("SYMBOL_NAME") !== -1) {
            log("  " + line);
        }
    }

    log("\n=== Triage Complete ===");
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(triageDump, "triage")
    ];
}
```

## Batch Processing Multiple Dumps

Use WinDbg's command-line mode (CDB) with JavaScript for batch processing:

```batch
@echo off
REM batch_analyze.bat - Analyze all dump files in a directory

for %%f in (C:\Dumps\*.dmp) do (
    echo Analyzing %%f...
    cdb -z "%%f" -c ".scriptrun C:\WinDbgScripts\triage.js; !triage; q" > "%%f.analysis.txt" 2>&1
)
echo Done.
```

## Creating Reusable Script Libraries

Organize your scripts into a reusable library structure:

```
C:\WinDbgScripts\
├── lib\
│   ├── utils.js         — shared utility functions
│   ├── memory.js        — memory reading helpers
│   └── strings.js       — string reading helpers
├── analysis\
│   ├── triage.js        — crash dump triage
│   ├── security.js      — security feature checks
│   └── performance.js   — performance analysis
├── tracing\
│   ├── apitracer.js     — API call tracer
│   └── memtracer.js     — memory access tracer
└── visualizers\
    ├── threads.js       — enhanced thread display
    └── modules.js       — enhanced module display
```

### Loading Dependencies

WinDbg JS doesn't have `require()` or `import`. Load multiple scripts:

```
.scriptload C:\WinDbgScripts\lib\utils.js
.scriptload C:\WinDbgScripts\analysis\triage.js
```

Or create a loader script:

```javascript
"use strict";

function invokeScript() {
    let scripts = [
        "C:\\WinDbgScripts\\lib\\utils.js",
        "C:\\WinDbgScripts\\visualizers\\threads.js",
        "C:\\WinDbgScripts\\visualizers\\modules.js",
        "C:\\WinDbgScripts\\analysis\\triage.js"
    ];

    for (let script of scripts) {
        try {
            host.namespace.Debugger.Utility.Control.ExecuteCommand(
                '.scriptload "' + script + '"'
            );
            host.diagnostics.debugLog("Loaded: " + script + "\n");
        } catch (e) {
            host.diagnostics.debugLog("FAILED: " + script + " - " + e.message + "\n");
        }
    }

    host.diagnostics.debugLog("\nAll scripts loaded. Available commands:\n");
    host.diagnostics.debugLog("  !qa         - Quick analysis\n");
    host.diagnostics.debugLog("  !triage     - Full triage\n");
    host.diagnostics.debugLog("  !threads    - Enhanced thread view\n");
    host.diagnostics.debugLog("  !seccheck   - Security feature check\n");
}
```

## Startup Automation

Configure WinDbg to load your scripts automatically:

### Via Workspace

1. Load your scripts manually
2. Save the workspace (File > Save Workspace)
3. Scripts load automatically when you open that workspace

### Via Command File

Create a startup command file `init.txt`:
```
.scriptpath C:\WinDbgScripts
.scriptload C:\WinDbgScripts\loader.js
.scriptrun C:\WinDbgScripts\loader.js
```

Launch WinDbg with:
```
windbg -cf init.txt -z mydump.dmp
```

### Via Registry (Global)

For WinDbg classic, you can set default extensions in the registry. For modern WinDbg, use workspace settings.

## Combining JS with MCP for AI-Powered Debugging

The ultimate workflow — your JS scripts provide domain-specific analysis, and the MCP server makes them accessible to AI:

```javascript
"use strict";

// A comprehensive analysis command designed for AI consumption
function aiAnalysis() {
    let result = {
        sections: []
    };

    // Structured output that AI can parse
    log("=== AI ANALYSIS START ===");

    // Process context
    log("SECTION: PROCESS");
    let proc = host.currentProcess;
    log("name=" + proc.Name);
    log("pid=" + proc.Id);

    // Exception info
    log("SECTION: EXCEPTION");
    let exr = exec(".exr -1");
    for (let line of exr) log("exr=" + line.trim());

    // Stack
    log("SECTION: CALLSTACK");
    let k = exec("k 20");
    for (let line of k) log("frame=" + line.trim());

    // Modules near fault
    log("SECTION: CONTEXT");
    let regs = exec("r");
    for (let line of regs) log("reg=" + line.trim());

    // Disassembly around fault
    log("SECTION: DISASM");
    let disasm = exec("u @rip-0x10 L10");
    for (let line of disasm) log("insn=" + line.trim());

    log("=== AI ANALYSIS END ===");
}

function log(msg) { host.diagnostics.debugLog(msg + "\n"); }

function exec(cmd) {
    let lines = [];
    try {
        let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(cmd);
        for (let line of output) lines.push(line);
    } catch (e) { lines.push("ERROR: " + e.message); }
    return lines;
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(aiAnalysis, "aianalyze")
    ];
}
```

Then through WinDbg-MCP, Claude can call:
```json
{ "tool": "execute_command", "arguments": { "command": "!aianalyze" } }
```

And receive structured debugging data it can reason about.

## Course Wrap-Up

You've now learned:

1. **Fundamentals** — script structure, loading, the `host` object
2. **Data Model** — sessions, processes, threads, modules
3. **Memory** — reading, writing, searching
4. **Registers & Threads** — CPU state, thread enumeration
5. **Symbols & Types** — type system, structure navigation
6. **Breakpoints** — conditional breakpoints with JS handlers
7. **LINQ** — querying collections with Select, Where, OrderBy
8. **Visualizers** — custom type display
9. **Practical Scripts** — real-world analysis tools
10. **Kernel Debugging** — kernel structures and drivers
11. **TTD** — time travel queries and async patterns
12. **Integration** — MCP, automation, batch processing

### Where to Go From Here

- **Read the WinDbg docs**: [Microsoft Debugger Documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/)
- **Study the WinDbg-MCP source**: See how the MCP extension wraps dbgeng APIs
- **Build your own toolkit**: Start with the scripts in this course and customize them
- **Contribute**: Add your scripts to the WinDbg-MCP project's learning folder
- **Practice**: The best way to learn debugging is to debug real problems

## Final Exercises

1. Build a complete "crash dump triage" script that produces a single-page report suitable for a bug report.
2. Create a script library with at least 5 reusable commands, a loader script, and a README.
3. Write a JS script that generates output designed for MCP/AI consumption, then use WinDbg-MCP to have Claude analyze a crash dump.
4. Create an automated test harness: launch a test program, set breakpoints with JS handlers, verify behavior, and report pass/fail.
5. Contribute a useful script back to the WinDbg-MCP project.
