# Module 05 — Registers & Threads

## Accessing Registers

Every thread has a set of registers. In WinDbg JS, registers are accessed through the thread object:

```javascript
let thread = host.currentThread;
let regs = thread.Registers.User;  // User-mode general-purpose registers
```

### x64 Registers

```javascript
"use strict";

function showRegisters() {
    let regs = host.currentThread.Registers.User;

    // General purpose registers
    host.diagnostics.debugLog("=== General Purpose Registers (x64) ===\n");
    host.diagnostics.debugLog("rax = 0x" + regs.rax.toString(16) + "\n");
    host.diagnostics.debugLog("rbx = 0x" + regs.rbx.toString(16) + "\n");
    host.diagnostics.debugLog("rcx = 0x" + regs.rcx.toString(16) + "\n");
    host.diagnostics.debugLog("rdx = 0x" + regs.rdx.toString(16) + "\n");
    host.diagnostics.debugLog("rsi = 0x" + regs.rsi.toString(16) + "\n");
    host.diagnostics.debugLog("rdi = 0x" + regs.rdi.toString(16) + "\n");
    host.diagnostics.debugLog("rsp = 0x" + regs.rsp.toString(16) + "\n");
    host.diagnostics.debugLog("rbp = 0x" + regs.rbp.toString(16) + "\n");
    host.diagnostics.debugLog("r8  = 0x" + regs.r8.toString(16) + "\n");
    host.diagnostics.debugLog("r9  = 0x" + regs.r9.toString(16) + "\n");
    host.diagnostics.debugLog("r10 = 0x" + regs.r10.toString(16) + "\n");
    host.diagnostics.debugLog("r11 = 0x" + regs.r11.toString(16) + "\n");
    host.diagnostics.debugLog("r12 = 0x" + regs.r12.toString(16) + "\n");
    host.diagnostics.debugLog("r13 = 0x" + regs.r13.toString(16) + "\n");
    host.diagnostics.debugLog("r14 = 0x" + regs.r14.toString(16) + "\n");
    host.diagnostics.debugLog("r15 = 0x" + regs.r15.toString(16) + "\n");

    // Instruction pointer and flags
    host.diagnostics.debugLog("\n=== Control Registers ===\n");
    host.diagnostics.debugLog("rip = 0x" + regs.rip.toString(16) + "\n");
    host.diagnostics.debugLog("efl = 0x" + regs.efl.toString(16) + "\n");

    // Segment registers
    host.diagnostics.debugLog("\n=== Segment Registers ===\n");
    host.diagnostics.debugLog("cs = 0x" + regs.cs.toString(16) + "\n");
    host.diagnostics.debugLog("ds = 0x" + regs.ds.toString(16) + "\n");
    host.diagnostics.debugLog("es = 0x" + regs.es.toString(16) + "\n");
    host.diagnostics.debugLog("fs = 0x" + regs.fs.toString(16) + "\n");
    host.diagnostics.debugLog("gs = 0x" + regs.gs.toString(16) + "\n");
    host.diagnostics.debugLog("ss = 0x" + regs.ss.toString(16) + "\n");
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(showRegisters, "regs64")
    ];
}
```

### x86 Registers

For 32-bit targets, use the 32-bit register names:

```javascript
let regs = host.currentThread.Registers.User;
let eax = regs.eax;
let ebx = regs.ebx;
let ecx = regs.ecx;
let edx = regs.edx;
let esi = regs.esi;
let edi = regs.edi;
let esp = regs.esp;
let ebp = regs.ebp;
let eip = regs.eip;
```

### Portable Register Access

Write scripts that work on both x86 and x64:

```javascript
function getInstructionPointer(thread) {
    let regs = thread.Registers.User;
    try {
        return regs.rip;  // Try 64-bit first
    } catch (e) {
        return regs.eip;  // Fall back to 32-bit
    }
}

function getStackPointer(thread) {
    let regs = thread.Registers.User;
    try {
        return regs.rsp;
    } catch (e) {
        return regs.esp;
    }
}

function getFramePointer(thread) {
    let regs = thread.Registers.User;
    try {
        return regs.rbp;
    } catch (e) {
        return regs.ebp;
    }
}
```

### Flags Register Decoding

The EFLAGS/RFLAGS register encodes CPU status flags:

```javascript
function decodeFlags() {
    let efl = host.currentThread.Registers.User.efl;
    let flags = parseInt(efl.toString());

    host.diagnostics.debugLog("EFLAGS: 0x" + efl.toString(16) + "\n\n");

    let flagDefs = [
        { bit: 0,  name: "CF",  desc: "Carry Flag" },
        { bit: 2,  name: "PF",  desc: "Parity Flag" },
        { bit: 4,  name: "AF",  desc: "Auxiliary Carry" },
        { bit: 6,  name: "ZF",  desc: "Zero Flag" },
        { bit: 7,  name: "SF",  desc: "Sign Flag" },
        { bit: 8,  name: "TF",  desc: "Trap Flag" },
        { bit: 9,  name: "IF",  desc: "Interrupt Enable" },
        { bit: 10, name: "DF",  desc: "Direction Flag" },
        { bit: 11, name: "OF",  desc: "Overflow Flag" },
    ];

    for (let f of flagDefs) {
        let set = (flags >> f.bit) & 1;
        host.diagnostics.debugLog(
            "  " + f.name + " = " + set + "  (" + f.desc + ")\n"
        );
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(decodeFlags, "flags")
    ];
}
```

## Working with Threads

### Iterating All Threads

```javascript
"use strict";

function listThreads() {
    let proc = host.currentProcess;
    let current = host.currentThread;

    host.diagnostics.debugLog("Threads in " + proc.Name + ":\n");
    host.diagnostics.debugLog("─".repeat(70) + "\n");

    for (let thread of proc.Threads) {
        let marker = (thread.Id === current.Id) ? " <<<" : "";
        let ip = getInstructionPointer(thread);

        host.diagnostics.debugLog(
            "  Thread " + thread.Id.toString().padStart(5) +
            "  IP=0x" + ip.toString(16).padStart(16, "0") +
            marker + "\n"
        );
    }
}

function getInstructionPointer(thread) {
    let regs = thread.Registers.User;
    try { return regs.rip; } catch (e) { return regs.eip; }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(listThreads, "threads")
    ];
}
```

### Thread Call Stacks

```javascript
function threadStacks() {
    let proc = host.currentProcess;

    for (let thread of proc.Threads) {
        host.diagnostics.debugLog("\n=== Thread " + thread.Id + " ===\n");

        try {
            let frameIdx = 0;
            for (let frame of thread.Stack.Frames) {
                let addr = frame.InstructionOffset;
                let funcName = frame.toString();

                host.diagnostics.debugLog(
                    "  #" + frameIdx.toString().padStart(2) +
                    "  0x" + addr.toString(16).padStart(16, "0") +
                    "  " + funcName + "\n"
                );

                frameIdx++;
                if (frameIdx >= 10) {
                    host.diagnostics.debugLog("  ... (truncated)\n");
                    break;
                }
            }
        } catch (e) {
            host.diagnostics.debugLog("  (stack unavailable: " + e.message + ")\n");
        }
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(threadStacks, "allstacks")
    ];
}
```

### Finding Threads by Criteria

```javascript
// Find threads that are in a specific module
function threadsInModule(moduleName) {
    let proc = host.currentProcess;
    let targetMod = undefined;

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
    let end = base.add(targetMod.Size);

    host.diagnostics.debugLog(
        "Threads with IP in " + targetMod.Name +
        " (0x" + base.toString(16) + " - 0x" + end.toString(16) + "):\n\n"
    );

    for (let thread of proc.Threads) {
        let ip = getInstructionPointer(thread);
        if (ip.compareTo(base) >= 0 && ip.compareTo(end) < 0) {
            host.diagnostics.debugLog("  Thread " + thread.Id +
                " at 0x" + ip.toString(16) + "\n");
        }
    }
}

function getInstructionPointer(thread) {
    try { return thread.Registers.User.rip; }
    catch (e) { return thread.Registers.User.eip; }
}
```

### Switching Threads

You can't directly "switch" to a thread from JS the way `~Ns` does, but you can use `ExecuteCommand`:

```javascript
function switchToThread(threadId) {
    host.namespace.Debugger.Utility.Control.ExecuteCommand("~" + threadId + "s");
    host.diagnostics.debugLog("Switched to thread " + threadId + "\n");
}
```

## Reading Registers from Any Thread

You don't need to switch threads to read their registers — just access the thread object directly:

```javascript
function compareThreadRegisters() {
    let proc = host.currentProcess;
    let threads = [];

    for (let t of proc.Threads) {
        threads.push(t);
    }

    if (threads.length < 2) {
        host.diagnostics.debugLog("Need at least 2 threads to compare.\n");
        return;
    }

    let t1 = threads[0];
    let t2 = threads[1];

    host.diagnostics.debugLog("Comparing Thread " + t1.Id + " vs Thread " + t2.Id + ":\n\n");

    let regNames = ["rax", "rbx", "rcx", "rdx", "rsp", "rbp", "rip"];
    for (let name of regNames) {
        try {
            let v1 = t1.Registers.User[name];
            let v2 = t2.Registers.User[name];
            let same = v1.compareTo(v2) === 0 ? "  (same)" : "  (DIFF)";
            host.diagnostics.debugLog(
                "  " + name.padEnd(5) +
                "T" + t1.Id + "=0x" + v1.toString(16).padStart(16, "0") +
                "  T" + t2.Id + "=0x" + v2.toString(16).padStart(16, "0") +
                same + "\n"
            );
        } catch (e) {
            // 32-bit target, skip 64-bit regs
        }
    }
}
```

## Practical Example: Thread Monitor

A script that summarizes thread states by categorizing what each thread is doing:

```javascript
"use strict";

function threadMonitor() {
    let proc = host.currentProcess;
    let categories = {
        "waiting": [],
        "running": [],
        "system": [],
        "other": []
    };

    for (let thread of proc.Threads) {
        let topFrame = "";
        try {
            for (let frame of thread.Stack.Frames) {
                topFrame = frame.toString();
                break;
            }
        } catch (e) {
            topFrame = "(unknown)";
        }

        let lower = topFrame.toLowerCase();
        if (lower.indexOf("wait") !== -1 || lower.indexOf("sleep") !== -1 ||
            lower.indexOf("delay") !== -1) {
            categories.waiting.push({ id: thread.Id, frame: topFrame });
        } else if (lower.indexOf("ntdll") !== -1 || lower.indexOf("kernel") !== -1) {
            categories.system.push({ id: thread.Id, frame: topFrame });
        } else {
            categories.other.push({ id: thread.Id, frame: topFrame });
        }
    }

    host.diagnostics.debugLog("=== Thread Monitor for " + proc.Name + " ===\n\n");

    host.diagnostics.debugLog("Waiting/Sleeping (" + categories.waiting.length + "):\n");
    for (let t of categories.waiting) {
        host.diagnostics.debugLog("  Thread " + t.id + ": " + t.frame + "\n");
    }

    host.diagnostics.debugLog("\nIn System Code (" + categories.system.length + "):\n");
    for (let t of categories.system) {
        host.diagnostics.debugLog("  Thread " + t.id + ": " + t.frame + "\n");
    }

    host.diagnostics.debugLog("\nOther (" + categories.other.length + "):\n");
    for (let t of categories.other) {
        host.diagnostics.debugLog("  Thread " + t.id + ": " + t.frame + "\n");
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(threadMonitor, "tmon")
    ];
}
```

## Exercises

1. Write a `!regs` command that prints all general-purpose registers in a nicely formatted table (works on both x86 and x64).
2. Write a `!flags` command that decodes the EFLAGS register and shows which flags are set.
3. Write a `!findthread` command that takes a module name and lists all threads currently executing in that module.
4. Write a script that finds the thread with the deepest call stack.
5. Write a `!regdiff` command that saves register state on first call and shows differences on subsequent calls.

## Next

[Module 06 — Symbols & Type System](06-symbols-and-type-system.md) — Navigate symbols, cast memory to types, and traverse structures.
