# Module 06 — Symbols & Type System

## Why Symbols Matter

Symbols turn raw addresses into meaningful names. Without symbols, `0x00007ff8a1234567` is just a number. With symbols, it becomes `ntdll!RtlUserThreadStart+0x21`. The type system goes further — it lets you interpret raw memory as structured data.

## Symbol Resolution

### Address to Symbol

```javascript
// Using dx-style access
function resolveAddress(addrStr) {
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
        "ln " + addrStr
    );
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }
}
```

### Symbol to Address

```javascript
function resolveSymbol(symbolName) {
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
        "x " + symbolName
    );
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }
}
```

### Using `host.getModuleSymbolAddress()`

```javascript
// Get the address of a specific symbol
let addr = host.getModuleSymbolAddress("ntdll", "RtlUserThreadStart");
host.diagnostics.debugLog("RtlUserThreadStart @ 0x" + addr.toString(16) + "\n");
```

### Enumerating Module Symbols

```javascript
function listExports(moduleName) {
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
        "x " + moduleName + "!*"
    );

    let count = 0;
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
        count++;
        if (count > 100) {
            host.diagnostics.debugLog("... (truncated, use a filter)\n");
            break;
        }
    }
}

// More targeted: find specific functions
function findFunctions(moduleName, pattern) {
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
        "x " + moduleName + "!" + pattern
    );
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }
}
```

Usage:
```
dx @$scriptContents.findFunctions("kernel32", "*CreateFile*")
dx @$scriptContents.findFunctions("ntdll", "Rtl*String*")
```

## The Type System

The type system lets you overlay C/C++ structure definitions on raw memory. This is the most powerful feature for reverse engineering and analysis.

### `host.createTypedObject()`

Create a typed view of memory at an address:

```javascript
host.createTypedObject(address, moduleName, typeName)
```

```javascript
// View the PEB (Process Environment Block)
function showPEB() {
    // Get PEB address from the TEB
    let pebAddr = host.evaluateExpression("@$peb");

    let peb = host.createTypedObject(pebAddr, "ntdll", "_PEB");

    host.diagnostics.debugLog("=== Process Environment Block ===\n");
    host.diagnostics.debugLog("ImageBaseAddress:    0x" + peb.ImageBaseAddress.toString(16) + "\n");
    host.diagnostics.debugLog("BeingDebugged:       " + peb.BeingDebugged + "\n");
    host.diagnostics.debugLog("ProcessHeap:         0x" + peb.ProcessHeap.toString(16) + "\n");
    host.diagnostics.debugLog("NumberOfProcessors:  " + peb.NumberOfProcessors + "\n");
    host.diagnostics.debugLog("OSMajorVersion:      " + peb.OSMajorVersion + "\n");
    host.diagnostics.debugLog("OSMinorVersion:      " + peb.OSMinorVersion + "\n");
    host.diagnostics.debugLog("OSBuildNumber:       " + peb.OSBuildNumber + "\n");
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(showPEB, "showpeb")
    ];
}
```

### `host.createPointerObject()`

Create a typed pointer:

```javascript
// Create a pointer to a structure
let ptr = host.createPointerObject(address, "ntdll", "_PEB *");
host.diagnostics.debugLog("Dereferenced: " + ptr.dereference().ImageBaseAddress + "\n");
```

## Navigating Structures

### Accessing Fields

```javascript
let peb = host.createTypedObject(pebAddr, "ntdll", "_PEB");

// Direct field access
let imageBase = peb.ImageBaseAddress;

// Nested structures — just chain dots
let ldr = peb.Ldr;  // _PEB_LDR_DATA*
let inLoadOrder = ldr.InLoadOrderModuleList;  // _LIST_ENTRY
```

### Walking Linked Lists

Windows kernel structures heavily use `LIST_ENTRY` doubly-linked lists. Here's how to walk them:

```javascript
"use strict";

function walkLoadedModules() {
    let pebAddr = host.evaluateExpression("@$peb");
    let peb = host.createTypedObject(pebAddr, "ntdll", "_PEB");

    // PEB -> Ldr -> InLoadOrderModuleList
    let ldr = peb.Ldr;
    let head = ldr.address.add(
        host.getModuleType("ntdll", "_PEB_LDR_DATA").fields.InLoadOrderModuleList.offset
    );

    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
        "dt ntdll!_PEB_LDR_DATA InLoadOrderModuleList. @$peb->Ldr"
    );
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }

    // Alternatively, use the data model which handles lists natively:
    host.diagnostics.debugLog("\n=== Via Data Model ===\n");
    let proc = host.currentProcess;
    for (let mod of proc.Modules) {
        host.diagnostics.debugLog(
            mod.Name.padEnd(30) + " @ 0x" + mod.BaseAddress.toString(16) + "\n"
        );
    }
}
```

### Examining Type Layout

To understand a type's layout:

```javascript
function showTypeLayout(moduleName, typeName) {
    // Use the dt command for full layout
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
        "dt " + moduleName + "!" + typeName
    );
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }
}
```

Usage:
```
dx @$scriptContents.showTypeLayout("ntdll", "_PEB")
dx @$scriptContents.showTypeLayout("ntdll", "_TEB")
dx @$scriptContents.showTypeLayout("ntdll", "_LDR_DATA_TABLE_ENTRY")
```

## `host.getModuleType()`

Get type information without creating an instance:

```javascript
let pebType = host.getModuleType("ntdll", "_PEB");

// Access field information
host.diagnostics.debugLog("PEB type info:\n");
host.diagnostics.debugLog("  Size: " + pebType.size + " bytes\n");

// List fields
for (let field of pebType.fields) {
    host.diagnostics.debugLog(
        "  +" + field.offset.toString(16).padStart(4, "0") +
        " " + field.name.padEnd(30) +
        " " + field.type.name + "\n"
    );
}
```

## Casting and Type Conversion

### Casting a Raw Address to a Typed Object

```javascript
// You found an address you know points to a specific structure
function castToType(addrStr, moduleName, typeName) {
    let addr = host.parseInt64(addrStr);
    let obj = host.createTypedObject(addr, moduleName, typeName);
    return obj;
}

// Example: cast an address to _EPROCESS (kernel debugging)
// let eproc = castToType("0xfffff806`12345000", "nt", "_EPROCESS");
```

### Reading Arrays of Structures

```javascript
function readStructArray(baseAddr, moduleName, typeName, count) {
    let typeInfo = host.getModuleType(moduleName, typeName);
    let structSize = typeInfo.size;

    host.diagnostics.debugLog(
        "Reading " + count + " x " + typeName +
        " (" + structSize + " bytes each):\n\n"
    );

    for (let i = 0; i < count; i++) {
        let addr = baseAddr.add(i * structSize);
        let obj = host.createTypedObject(addr, moduleName, typeName);
        host.diagnostics.debugLog("[" + i + "] @ 0x" + addr.toString(16) + ":\n");

        // Print first few fields
        try {
            let fieldCount = 0;
            for (let field of typeInfo.fields) {
                host.diagnostics.debugLog("  " + field.name + " = " + obj[field.name] + "\n");
                fieldCount++;
                if (fieldCount >= 5) break;
            }
        } catch (e) {
            host.diagnostics.debugLog("  (error reading fields)\n");
        }
        host.diagnostics.debugLog("\n");
    }
}
```

## Practical Example: TEB/PEB Explorer

```javascript
"use strict";

function tebExplorer() {
    // TEB is at gs:[0x30] on x64, fs:[0x18] on x86
    let tebAddr = host.evaluateExpression("@$teb");
    let teb = host.createTypedObject(tebAddr, "ntdll", "_TEB");

    host.diagnostics.debugLog("=== Thread Environment Block ===\n");
    host.diagnostics.debugLog("TEB Address:        0x" + tebAddr.toString(16) + "\n");
    host.diagnostics.debugLog("Process ID:         " + teb.ClientId.UniqueProcess + "\n");
    host.diagnostics.debugLog("Thread ID:          " + teb.ClientId.UniqueThread + "\n");
    host.diagnostics.debugLog("Stack Base:         0x" +
        teb.NtTib.StackBase.toString(16) + "\n");
    host.diagnostics.debugLog("Stack Limit:        0x" +
        teb.NtTib.StackLimit.toString(16) + "\n");
    host.diagnostics.debugLog("PEB Address:        0x" +
        teb.ProcessEnvironmentBlock.toString(16) + "\n");
    host.diagnostics.debugLog("Last Error:         " + teb.LastErrorValue + "\n");
    host.diagnostics.debugLog("Last Status:        0x" +
        teb.LastStatusValue.toString(16) + "\n");

    host.diagnostics.debugLog("\n=== Process Environment Block ===\n");
    let peb = host.createTypedObject(teb.ProcessEnvironmentBlock, "ntdll", "_PEB");
    host.diagnostics.debugLog("Image Base:         0x" +
        peb.ImageBaseAddress.toString(16) + "\n");
    host.diagnostics.debugLog("Being Debugged:     " + peb.BeingDebugged + "\n");
    host.diagnostics.debugLog("Heap:               0x" +
        peb.ProcessHeap.toString(16) + "\n");
    host.diagnostics.debugLog("OS Version:         " +
        peb.OSMajorVersion + "." + peb.OSMinorVersion +
        " (Build " + peb.OSBuildNumber + ")\n");
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(tebExplorer, "tebinfo")
    ];
}
```

## Working with Enums and Bitfields

```javascript
// Enums are returned as integer values — you need to decode them yourself
function decodeNtStatus(statusCode) {
    let known = {
        0x00000000: "STATUS_SUCCESS",
        0xC0000005: "STATUS_ACCESS_VIOLATION",
        0xC000000D: "STATUS_INVALID_PARAMETER",
        0xC0000022: "STATUS_ACCESS_DENIED",
        0xC0000034: "STATUS_OBJECT_NAME_NOT_FOUND",
        0xC0000135: "STATUS_DLL_NOT_FOUND",
        0xC0000139: "STATUS_ENTRYPOINT_NOT_FOUND",
        0xC0000142: "STATUS_DLL_INIT_FAILED",
    };

    let code = parseInt(statusCode);
    let name = known[code] || "(unknown)";
    host.diagnostics.debugLog(
        "NTSTATUS 0x" + code.toString(16) + " = " + name + "\n"
    );
}
```

## Exercises

1. Write a script that resolves a symbol name to an address and an address back to a symbol.
2. Use `host.createTypedObject()` to read the PEB and print the `BeingDebugged` flag, image base, and OS version.
3. Write a `!typeinfo` command that takes a module and type name and prints all field names, offsets, and types.
4. Write a function that walks the loaded module list using `_LDR_DATA_TABLE_ENTRY` structures.
5. Create a `!ntstatus` command that decodes common NTSTATUS values.

## Next

[Module 07 — Breakpoints & Events](07-breakpoints-and-events.md) — Set conditional breakpoints with JS callbacks and handle debug events.
