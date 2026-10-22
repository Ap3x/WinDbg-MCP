# Module 08 — Iterators & LINQ

## The Data Model is Built on Iterables

Nearly every collection in the Debugger Data Model is iterable — threads, modules, stack frames, breakpoints. WinDbg extends these iterables with LINQ-style query methods, giving you SQL-like querying power over debugger data.

## LINQ Methods Available

These methods work on any iterable collection in the data model:

| Method | Description |
|--------|-------------|
| `.Select(f)` | Transform each element (map) |
| `.Where(f)` | Filter elements (filter) |
| `.OrderBy(f)` | Sort ascending by key |
| `.OrderByDescending(f)` | Sort descending by key |
| `.First()` | Get the first element |
| `.Last()` | Get the last element |
| `.Count()` | Count elements |
| `.Any(f)` | Check if any element matches |
| `.All(f)` | Check if all elements match |
| `.Contains(v)` | Check if collection contains value |
| `.SelectMany(f)` | Flatten nested collections |
| `.Distinct()` | Remove duplicates |
| `.GroupBy(f)` | Group elements by key |
| `.Sum(f)` | Sum a numeric property |
| `.Min(f)` | Find minimum |
| `.Max(f)` | Find maximum |
| `.Take(n)` | Take first N elements |
| `.Skip(n)` | Skip first N elements |
| `.Flatten()` | Flatten nested iterables |

## Using LINQ in `dx` (Interactive)

Before scripting LINQ, practice with `dx`:

```
dx @$curprocess.Threads.Count()
dx @$curprocess.Modules.Count()
dx @$curprocess.Modules.Select(m => m.Name)
dx @$curprocess.Modules.Where(m => m.Size > 0x100000)
dx @$curprocess.Modules.OrderByDescending(m => m.Size).Select(m => new { Name = m.Name, Size = m.Size })
dx @$curprocess.Modules.Where(m => m.Name.Contains("ntdll"))
dx @$curprocess.Threads.Where(t => t.Id == 1234)
dx @$curprocess.Modules.Sum(m => m.Size)
```

## Using LINQ in JavaScript

In JavaScript, you access the same LINQ methods on data model collections:

```javascript
"use strict";

function linqDemo() {
    let proc = host.currentProcess;

    // Count modules
    let modCount = proc.Modules.Count();
    host.diagnostics.debugLog("Module count: " + modCount + "\n");

    // Find largest module
    let largest = proc.Modules.OrderByDescending(function(m) {
        return m.Size;
    }).First();
    host.diagnostics.debugLog("Largest module: " + largest.Name +
        " (" + largest.Size + " bytes)\n");

    // Filter modules > 1MB
    let bigModules = proc.Modules.Where(function(m) {
        return m.Size > 1048576;
    });
    host.diagnostics.debugLog("\nModules > 1MB:\n");
    for (let mod of bigModules) {
        host.diagnostics.debugLog("  " + mod.Name +
            " (" + (mod.Size / 1048576).toFixed(2) + " MB)\n");
    }

    // Total memory footprint
    let totalSize = proc.Modules.Sum(function(m) { return m.Size; });
    host.diagnostics.debugLog("\nTotal module footprint: " +
        (totalSize / 1048576).toFixed(2) + " MB\n");
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(linqDemo, "linqdemo")
    ];
}
```

### Arrow Functions vs Regular Functions

WinDbg JS supports ES6, so arrow functions work:

```javascript
// These are equivalent:
proc.Modules.Where(function(m) { return m.Size > 0x100000; });
proc.Modules.Where(m => m.Size > 0x100000);

// Chaining with arrows is cleaner:
proc.Modules
    .Where(m => m.Size > 0x100000)
    .OrderByDescending(m => m.Size)
    .Select(m => m.Name);
```

## Select — Transforming Data

`Select` creates a new collection by transforming each element:

```javascript
// Get just the names
let names = proc.Modules.Select(m => m.Name);
for (let name of names) {
    host.diagnostics.debugLog(name + "\n");
}

// Create custom objects with selected fields
let summary = proc.Modules.Select(m => ({
    name: m.Name,
    base: m.BaseAddress.toString(16),
    sizeKB: (m.Size / 1024).toFixed(0)
}));

for (let item of summary) {
    host.diagnostics.debugLog(
        item.name.padEnd(30) +
        "0x" + item.base.padStart(16, "0") +
        item.sizeKB.padStart(10) + " KB\n"
    );
}
```

## Where — Filtering

```javascript
// Modules from Microsoft (name starts with common Microsoft DLL prefixes)
let msModules = proc.Modules.Where(m => {
    let name = m.Name.toLowerCase();
    return name.indexOf("microsoft") !== -1 ||
           name.indexOf("windows") !== -1 ||
           name.startsWith("ms");
});

// Threads with a deep stack (more than 20 frames)
let deepThreads = proc.Threads.Where(t => {
    try {
        let count = 0;
        for (let f of t.Stack.Frames) {
            count++;
            if (count > 20) return true;
        }
        return false;
    } catch (e) {
        return false;
    }
});
```

## OrderBy — Sorting

```javascript
// Modules sorted by base address
let byAddress = proc.Modules.OrderBy(m => m.BaseAddress);

// Modules sorted by size (descending)
let bySize = proc.Modules.OrderByDescending(m => m.Size);

// Top 5 largest modules
let top5 = proc.Modules
    .OrderByDescending(m => m.Size)
    .Take(5);

host.diagnostics.debugLog("Top 5 largest modules:\n");
for (let mod of top5) {
    host.diagnostics.debugLog(
        "  " + mod.Name.padEnd(30) + (mod.Size / 1024).toFixed(0) + " KB\n"
    );
}
```

## GroupBy — Grouping

```javascript
// Group modules by their file extension
function groupModulesByExtension() {
    let proc = host.currentProcess;

    // GroupBy returns groups with a .Key and iterable values
    // Note: GroupBy in the data model may behave differently than C# LINQ
    // Using manual grouping for reliability:

    let groups = {};
    for (let mod of proc.Modules) {
        let name = mod.Name;
        let ext = name.substring(name.lastIndexOf(".") + 1).toLowerCase();
        if (!groups[ext]) groups[ext] = [];
        groups[ext].push(mod.Name);
    }

    for (let ext in groups) {
        host.diagnostics.debugLog("\n." + ext + " (" + groups[ext].length + "):\n");
        for (let name of groups[ext]) {
            host.diagnostics.debugLog("  " + name + "\n");
        }
    }
}
```

## Chaining — Composing Queries

The real power of LINQ is chaining multiple operations:

```javascript
"use strict";

function moduleReport() {
    let proc = host.currentProcess;

    host.diagnostics.debugLog("=== Module Report for " + proc.Name + " ===\n\n");

    // Chain: filter → sort → transform → iterate
    let report = proc.Modules
        .Where(m => m.Size > 0)
        .OrderByDescending(m => m.Size)
        .Select(m => ({
            name: m.Name,
            base: "0x" + m.BaseAddress.toString(16),
            sizeMB: (m.Size / 1048576).toFixed(2)
        }));

    let rank = 1;
    for (let item of report) {
        host.diagnostics.debugLog(
            "#" + rank.toString().padStart(2) + "  " +
            item.name.padEnd(35) +
            item.base.padStart(18) +
            item.sizeMB.padStart(10) + " MB\n"
        );
        rank++;
    }

    // Summary statistics
    let total = proc.Modules.Sum(m => m.Size);
    let count = proc.Modules.Count();
    let avg = total / count;

    host.diagnostics.debugLog("\n");
    host.diagnostics.debugLog("Total modules: " + count + "\n");
    host.diagnostics.debugLog("Total size:    " + (total / 1048576).toFixed(2) + " MB\n");
    host.diagnostics.debugLog("Average size:  " + (avg / 1024).toFixed(1) + " KB\n");
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(moduleReport, "modreport")
    ];
}
```

## Any, All, Contains — Boolean Queries

```javascript
// Is ntdll loaded?
let hasNtdll = proc.Modules.Any(m => m.Name.toLowerCase() === "ntdll.dll");
host.diagnostics.debugLog("ntdll loaded: " + hasNtdll + "\n");

// Are all modules > 4KB?
let allBigEnough = proc.Modules.All(m => m.Size > 4096);
host.diagnostics.debugLog("All modules > 4KB: " + allBigEnough + "\n");
```

## SelectMany — Flattening

`SelectMany` flattens nested collections. Great for collecting stack frames across all threads:

```javascript
// Get all stack frames across all threads (dx expression)
// dx @$curprocess.Threads.SelectMany(t => t.Stack.Frames)

// In JavaScript:
function allFrames() {
    let proc = host.currentProcess;
    let frameCount = 0;
    let functionCounts = {};

    for (let thread of proc.Threads) {
        try {
            for (let frame of thread.Stack.Frames) {
                let name = frame.toString();
                frameCount++;
                functionCounts[name] = (functionCounts[name] || 0) + 1;
            }
        } catch (e) {
            // Skip threads with unavailable stacks
        }
    }

    host.diagnostics.debugLog("Total frames across all threads: " + frameCount + "\n\n");
    host.diagnostics.debugLog("Most common functions on stack:\n");

    // Sort by count
    let sorted = Object.keys(functionCounts).sort(
        (a, b) => functionCounts[b] - functionCounts[a]
    );

    for (let i = 0; i < Math.min(20, sorted.length); i++) {
        host.diagnostics.debugLog(
            "  " + functionCounts[sorted[i]].toString().padStart(4) +
            "x  " + sorted[i] + "\n"
        );
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(allFrames, "allframes")
    ];
}
```

## Custom Iterators

You can make your own JavaScript objects iterable, so they work with LINQ and `for...of`:

```javascript
// A custom iterable that yields memory regions
class MemoryRegionIterator {
    constructor(startAddr, count, regionSize) {
        this.start = startAddr;
        this.count = count;
        this.regionSize = regionSize;
    }

    *[Symbol.iterator]() {
        for (let i = 0; i < this.count; i++) {
            yield {
                index: i,
                address: this.start.add(i * this.regionSize),
                size: this.regionSize
            };
        }
    }
}
```

## Performance Tips

1. **Use `Take(n)` early** — don't sort a million items just to get the top 10
2. **`Where` before `Select`** — filter first to reduce work
3. **Avoid repeated `Count()`** — it iterates the whole collection each time; cache the result
4. **Break out of loops** — when searching, `break` once found instead of iterating everything
5. **Use `dx` to prototype** — test LINQ queries interactively before putting them in scripts

```javascript
// Efficient: filter then take
let top5 = proc.Modules
    .Where(m => m.Size > 0x10000)
    .OrderByDescending(m => m.Size)
    .Take(5);

// Less efficient: transform everything then filter
// (don't do this for large collections)
```

## Exercises

1. Use LINQ to find the module with the smallest size in the current process.
2. Write a query that lists all threads sorted by thread ID.
3. Use `Select` to create a formatted string for each module showing "name @ base (size KB)".
4. Write a script that uses LINQ to find which function appears most often across all thread stacks.
5. Chain `Where`, `OrderBy`, and `Take` to find the top 3 largest DLLs (filtering out .exe files).

## Next

[Module 09 — Custom Visualizers](09-custom-visualizers.md) — Extend `dx` output with custom object visualizations.
