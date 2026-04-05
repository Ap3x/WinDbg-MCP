# Module 01 — Introduction & Setup

## What is the WinDbg JavaScript Provider?

The WinDbg JavaScript Scripting Provider (JsProvider) is a built-in scripting engine that lets you write JavaScript to automate and extend WinDbg. It was introduced in WinDbg Preview and is now part of the modern WinDbg experience.

**Why JavaScript in WinDbg?**

Before the JS Provider, your automation options in WinDbg were:
- **WinDbg command scripts** (`.foreach`, `$$`, aliasing) — cryptic, fragile, hard to maintain
- **`dx` expressions** — powerful but limited to single expressions
- **C/C++ extensions** — full power, but heavy to write and deploy
- **Python via PyKD** — third-party, requires separate installation

The JavaScript provider gives you a **modern, familiar language** with direct access to the Debugger Data Model — the same model that powers `dx` commands. You get:

- Full ES6 JavaScript (classes, arrow functions, template literals, destructuring)
- Direct access to every debugger object (processes, threads, modules, memory, registers)
- LINQ-style queries over collections
- The ability to define custom commands, visualizers, and breakpoint callbacks
- No external dependencies — it ships with WinDbg

## When to Use JS vs. Other Approaches

| Task | Best Tool |
|------|-----------|
| Quick one-off inspection | `dx` expression or WinDbg command |
| Repeated multi-step analysis | **JavaScript script** |
| Conditional breakpoint with logic | **JavaScript breakpoint callback** |
| Custom type visualization | **JavaScript visualizer** (or NatVis for simple cases) |
| High-performance extension with UI | C/C++ extension (dbgeng) |
| Cross-debugger automation | MCP + JS scripts together |

## Setting Up Your Environment

### Step 1: Install WinDbg

**Option A — WinDbg (modern, recommended):**
Install from the Microsoft Store or via `winget`:
```
winget install Microsoft.WinDbg
```

**Option B — WinDbg Classic (from Windows SDK):**
Install the "Debugging Tools for Windows" component from the Windows SDK.

### Step 2: Verify the JavaScript Provider

Open WinDbg and run:
```
.scriptproviders
```

You should see output like:
```
Available Script Providers:
    NatVis (extension '.NatVis')
    JavaScript (extension '.js')
```

If `JavaScript` is listed, you're ready. If not, update WinDbg — the JS provider ships with all modern versions.

### Step 3: Set Up a Script Directory

Create a directory for your scripts:
```
mkdir C:\WinDbgScripts
```

Tell WinDbg where to find scripts (optional, for convenience):
```
.scriptpath C:\WinDbgScripts
```

You can add this to your WinDbg workspace or startup commands so it persists across sessions.

### Step 4: Get a Debug Target

You need something to debug. Options:

**A. Attach to a running process:**
File > Attach to a Process, or:
```
.attach <pid>
```

**B. Open a crash dump:**
File > Open Dump File, or:
```
.opendump C:\path\to\dump.dmp
```

**C. Launch a process under the debugger:**
File > Launch Executable, or:
```
.create notepad.exe
g
```

**D. Kernel debugging (advanced):**
Set up a VM with kernel debugging enabled (covered in Module 11).

### Step 5: Editor Setup

You can write scripts in any text editor. Recommendations:

- **VS Code** with a generic JavaScript extension — syntax highlighting, linting
- **Notepad++** — lightweight, quick edits
- **WinDbg's built-in script window** — View > Script (in modern WinDbg)

There's no IntelliSense for WinDbg JS objects (yet), but the `dx` command is your interactive explorer — use it liberally to discover object shapes.

## Architecture: How JS Scripts Fit In

```
┌──────────────────────────────────────────┐
│              WinDbg Process               │
│                                           │
│  ┌─────────┐    ┌──────────────────────┐ │
│  │  WinDbg  │    │  JavaScript Provider │ │
│  │   UI /   │◄──►│  (JsProvider.dll)    │ │
│  │ Commands │    │                      │ │
│  └────┬─────┘    │  ┌────────────────┐  │ │
│       │          │  │  Your Script   │  │ │
│       │          │  │  (.js files)   │  │ │
│       │          │  └───────┬────────┘  │ │
│       │          └──────────┼───────────┘ │
│       │                     │             │
│       ▼                     ▼             │
│  ┌──────────────────────────────────────┐ │
│  │     Debugger Data Model (DDM)        │ │
│  │  (processes, threads, modules, etc.) │ │
│  └──────────────────┬───────────────────┘ │
│                     │                     │
│                     ▼                     │
│  ┌──────────────────────────────────────┐ │
│  │     Debug Engine (dbgeng.dll)         │ │
│  │  (breakpoints, memory, symbols)      │ │
│  └──────────────────────────────────────┘ │
└──────────────────────────────────────────┘
```

Your JavaScript runs inside the JsProvider, which bridges to the Debugger Data Model (DDM). The DDM is the unified object system that exposes everything — processes, threads, modules, type information, memory — as navigable objects. This is the same model `dx` uses.

## Key Terminology

| Term | Meaning |
|------|---------|
| **JsProvider** | The JavaScript scripting engine built into WinDbg |
| **Debugger Data Model (DDM)** | The object model that represents debugger state |
| **`host`** | The global object in WinDbg JS that gives access to DDM |
| **`dx`** | WinDbg command to evaluate DDM expressions |
| **Data Model Object** | Any object from the DDM (process, thread, module, etc.) |
| **Script Command** | A function you export from JS that becomes a WinDbg command |
| **Visualizer** | A JS class that customizes how a type appears in `dx` output |

## Exercises

1. Install WinDbg and verify `.scriptproviders` shows the JavaScript provider.
2. Create a `C:\WinDbgScripts` directory and set it with `.scriptpath`.
3. Launch `notepad.exe` under the debugger, break in, and run `dx @$curprocess` to see the process object.
4. Explore the output of `dx Debugger` — this is the root object you'll be scripting against.

## Next

[Module 02 — First Script](02-first-script.md) — Write, load, and execute your first WinDbg JavaScript.
