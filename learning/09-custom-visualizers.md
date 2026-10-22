# Module 09 — Custom Visualizers

## What Are Visualizers?

When you type `dx myObject` in WinDbg, the debugger displays the object's properties. A **visualizer** lets you customize this display — adding computed properties, custom string representations, and child objects to any type.

This is WinDbg JS's answer to NatVis, but with full programming power.

## How Visualizers Work

You define a JavaScript class and register it as a "parent model" for a specific type. When WinDbg displays an object of that type, it includes your class's properties alongside the native ones.

```
Type: MyStruct              Your Visualizer Class
┌─────────────────┐        ┌──────────────────────┐
│ field1: 42      │        │ [Summary]: "MyStruct" │
│ field2: 0xff    │  +     │ ComputedProp: 66      │
│ field3: "hello" │        │ HexDump: "ff 00 ..."  │
└─────────────────┘        └──────────────────────┘
                  ═══
         Combined dx output
```

## Basic Visualizer

Let's add a visualizer for `_LARGE_INTEGER`, a common Windows type:

```javascript
"use strict";

class LargeIntegerVisualizer {
    // The toString() method controls the one-line summary display
    toString() {
        let low = this.LowPart;
        let high = this.HighPart;
        // Combine into a 64-bit value display
        let val = this.QuadPart;
        return "0x" + val.toString(16) + " (" + val.toString() + ")";
    }

    // Any getter becomes a visible property in dx output
    get Decimal() {
        return this.QuadPart.toString();
    }

    get Hex() {
        return "0x" + this.QuadPart.toString(16);
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.typeSignatureRegistration(
            LargeIntegerVisualizer,
            "_LARGE_INTEGER"
        )
    ];
}
```

After loading:
```
dx -r1 someObject.SomeLargeInteger
```
Now shows your custom `Decimal` and `Hex` properties.

## `host.typeSignatureRegistration`

```javascript
new host.typeSignatureRegistration(VisualizerClass, typeName, moduleName?)
```

| Parameter | Description |
|-----------|-------------|
| `VisualizerClass` | Your JavaScript class |
| `typeName` | The C/C++ type name to attach to |
| `moduleName` | Optional module filter (e.g., "ntdll") |

```javascript
// Attach to _EPROCESS in the nt module (kernel)
new host.typeSignatureRegistration(EProcessVis, "_EPROCESS", "nt")

// Attach to any _LIST_ENTRY regardless of module
new host.typeSignatureRegistration(ListEntryVis, "_LIST_ENTRY")

// Attach to a specific class
new host.typeSignatureRegistration(MyClassVis, "MyNamespace::MyClass", "myapp")
```

## The `this` Context

Inside your visualizer class, `this` refers to the native typed object. You can access all its original fields:

```javascript
class ProcessVisualizer {
    get Summary() {
        // 'this' is the _EPROCESS object
        return this.ImageFileName + " (PID: " + this.UniqueProcessId + ")";
    }

    toString() {
        return this.Summary;
    }
}
```

## Adding Computed Properties

```javascript
"use strict";

class ModuleVisualizer {
    // Show size in human-readable format
    get SizeFormatted() {
        let size = this.Size;
        if (size > 1048576) {
            return (size / 1048576).toFixed(2) + " MB";
        } else if (size > 1024) {
            return (size / 1024).toFixed(1) + " KB";
        }
        return size + " bytes";
    }

    // Show the end address
    get EndAddress() {
        return this.BaseAddress.add(this.Size);
    }

    // Show address range as a string
    get AddressRange() {
        return "0x" + this.BaseAddress.toString(16) +
               " - 0x" + this.EndAddress.toString(16);
    }

    toString() {
        return this.Name + " [" + this.SizeFormatted + "]";
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        // Note: Module visualization requires the correct internal type name
        // This varies by WinDbg version
    ];
}
```

## Extending the Debugger Namespace

You can add entirely new properties to existing data model objects using `host.namedModelParent`:

```javascript
"use strict";

// Add custom properties to the Process object
class ProcessExtensions {
    get ModuleCount() {
        let count = 0;
        for (let m of this.Modules) count++;
        return count;
    }

    get TotalModuleSize() {
        let total = 0;
        for (let m of this.Modules) total += m.Size;
        return total;
    }

    get TotalModuleSizeMB() {
        return (this.TotalModuleSize / 1048576).toFixed(2) + " MB";
    }

    get ThreadCount() {
        let count = 0;
        for (let t of this.Threads) count++;
        return count;
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.namedModelParent(
            ProcessExtensions,
            "Debugger.Models.Process"
        )
    ];
}
```

After loading:
```
dx @$curprocess.ModuleCount
dx @$curprocess.TotalModuleSizeMB
dx @$curprocess.ThreadCount
```

These properties now appear on EVERY process object.

### Common Named Models

| Named Model | Applies To |
|-------------|-----------|
| `Debugger.Models.Process` | Process objects |
| `Debugger.Models.Thread` | Thread objects |
| `Debugger.Models.Module` | Module objects |
| `Debugger.Models.Stack` | Stack objects |
| `Debugger.Models.StackFrame` | Stack frame objects |
| `Debugger.Models.Session` | Debug session objects |

## Extending Stack Frames

```javascript
"use strict";

class StackFrameExtensions {
    // Show the module name for this frame
    get ModuleName() {
        let frameStr = this.toString();
        let bangIdx = frameStr.indexOf("!");
        if (bangIdx !== -1) {
            return frameStr.substring(0, bangIdx);
        }
        return "(unknown)";
    }

    // Show if this frame is in user code vs system code
    get IsSystemFrame() {
        let mod = this.ModuleName.toLowerCase();
        return mod === "ntdll" || mod === "kernel32" ||
               mod === "kernelbase" || mod === "win32u" ||
               mod === "user32" || mod === "gdi32";
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.namedModelParent(
            StackFrameExtensions,
            "Debugger.Models.StackFrame"
        )
    ];
}
```

After loading:
```
dx @$curthread.Stack.Frames.Select(f => new { Frame = f, Module = f.ModuleName, System = f.IsSystemFrame })
```

## Iterable Visualizers

If your type represents a collection, implement `[Symbol.iterator]` to make it iterable in `dx`:

```javascript
class LinkedListVisualizer {
    *[Symbol.iterator]() {
        // Walk a singly-linked list
        let current = this.Head;
        let count = 0;
        let maxItems = 1000;  // safety limit

        while (current !== null && current.address !== 0 && count < maxItems) {
            yield current;
            try {
                current = current.Next;
            } catch (e) {
                break;
            }
            count++;
        }
    }

    get Length() {
        let count = 0;
        for (let item of this) {
            count++;
        }
        return count;
    }

    toString() {
        return "LinkedList [" + this.Length + " items]";
    }
}
```

## Practical Example: Enhanced Thread Visualizer

```javascript
"use strict";

class ThreadExtensions {
    get StackDepth() {
        let count = 0;
        try {
            for (let f of this.Stack.Frames) count++;
        } catch (e) {}
        return count;
    }

    get TopFunction() {
        try {
            for (let f of this.Stack.Frames) {
                return f.toString();
            }
        } catch (e) {}
        return "(unavailable)";
    }

    get IsWaiting() {
        let top = this.TopFunction.toLowerCase();
        return top.indexOf("wait") !== -1 ||
               top.indexOf("sleep") !== -1 ||
               top.indexOf("delay") !== -1;
    }

    get Status() {
        if (this.IsWaiting) return "WAITING";
        return "ACTIVE";
    }

    get InstructionPointer() {
        try {
            return "0x" + this.Registers.User.rip.toString(16);
        } catch (e) {
            try {
                return "0x" + this.Registers.User.eip.toString(16);
            } catch (e2) {
                return "(unknown)";
            }
        }
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.namedModelParent(
            ThreadExtensions,
            "Debugger.Models.Thread"
        )
    ];
}
```

After loading:
```
dx @$curprocess.Threads.Select(t => new { Id = t.Id, Status = t.Status, Top = t.TopFunction, Depth = t.StackDepth })
```

## Visualizer Debugging Tips

1. **Test with `dx` first** — use `dx` expressions to verify your properties work before packaging as a visualizer
2. **Handle errors gracefully** — wrap property accessors in try/catch; a crashing visualizer makes the whole type unreadable
3. **Avoid expensive operations in `toString()`** — it gets called for every display; keep it fast
4. **Use `.scriptunload` to iterate** — unload, edit, reload during development
5. **Check `.scriptlist`** — make sure your script loaded without errors

## Exercises

1. Create a visualizer for `_UNICODE_STRING` that shows the actual string content in `toString()`.
2. Add a `SizeFormatted` property to `Debugger.Models.Module` that shows size in KB/MB.
3. Create a thread extension that adds a `CallStackSummary` property showing the top 3 functions.
4. Write a visualizer for `_LIST_ENTRY` that implements `[Symbol.iterator]` to walk the list.
5. Add a `HasDebugger` property to `Debugger.Models.Process` that checks the PEB's `BeingDebugged` flag.

## Next

[Module 10 — Practical Scripts](10-practical-scripts.md) — Real-world scripts for heap analysis, lock inspection, string scanning, and more.
