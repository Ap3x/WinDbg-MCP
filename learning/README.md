# WinDbg JavaScript Scripting Course

A comprehensive, hands-on course for learning the WinDbg JavaScript Scripting Provider — from first principles to building production-quality debugging scripts.

## Prerequisites

- Windows 10/11 with WinDbg (WinDbg Preview or classic WinDbg)
- Basic familiarity with debugging concepts (breakpoints, call stacks, memory)
- Some JavaScript experience (ES6+ knowledge helpful but not required)
- A target process or crash dump to debug against

## Course Outline

| Module | Title | Description |
|--------|-------|-------------|
| 01 | [Introduction & Setup](01-introduction-and-setup.md) | What is the JS Provider, why use it, and how to set up your environment |
| 02 | [First Script](02-first-script.md) | Writing, loading, and running your first WinDbg JS script |
| 03 | [The Debugger Object Model](03-debugger-object-model.md) | Understanding `host`, sessions, processes, threads, and the data model |
| 04 | [Working with Memory](04-working-with-memory.md) | Reading, writing, and searching memory from JavaScript |
| 05 | [Registers & Threads](05-registers-and-threads.md) | Accessing CPU registers, switching threads, and inspecting thread state |
| 06 | [Symbols & Type System](06-symbols-and-type-system.md) | Navigating symbols, casting memory to types, and traversing structures |
| 07 | [Breakpoints & Events](07-breakpoints-and-events.md) | Setting conditional breakpoints with JS callbacks and handling debug events |
| 08 | [Iterators & LINQ](08-iterators-and-linq.md) | Using the LINQ-style query methods built into the debugger data model |
| 09 | [Custom Visualizers](09-custom-visualizers.md) | Extending `dx` output with custom object visualizations |
| 10 | [Practical Scripts](10-practical-scripts.md) | Real-world scripts: heap analysis, lock inspection, string scanning, and more |
| 11 | [Kernel Debugging with JS](11-kernel-debugging-with-js.md) | Enumerating processes, drivers, and kernel structures via JavaScript |
| 12 | [Async Scripting & Time Travel](12-async-and-time-travel.md) | Async patterns, promises, and integrating with Time Travel Debugging (TTD) |
| 13 | [Integration & Automation](13-integration-and-automation.md) | Combining JS scripts with WinDbg extensions, MCP servers, and CI pipelines |

## How to Use This Course

1. **Read each module in order** — concepts build on one another.
2. **Type the examples yourself** — copy-pasting skips the muscle memory.
3. **Experiment** — after each module, modify the examples and break things on purpose.
4. **Use the exercises** — each module ends with practice problems.

## Quick Reference

```
.scriptproviders              — List available script providers
.scriptload <path>            — Load a JS script
.scriptunload <path>          — Unload a JS script
.scriptrun <path>             — Load and run initializeScript + invokeScript
.scriptlist                   — List loaded scripts
dx @$scriptContents           — Browse script-exported objects
dx @$curprocess               — Current process
dx @$curthread                — Current thread
dx @$cursession               — Current debug session
dx Debugger                   — Root debugger object
```

## Companion Project

This course lives inside the [WinDbg-MCP](https://github.com/ap3x/windbg-mcp) repository — a native WinDbg extension that exposes debugging tools via the Model Context Protocol (MCP). Module 13 shows how to combine JS scripting with MCP-based automation.
