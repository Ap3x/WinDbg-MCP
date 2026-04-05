# Module 04 — Working with Memory

## Overview

Reading and writing memory is the bread and butter of debugging. WinDbg JavaScript gives you direct access to process memory through the `host.memory` API and through typed object access.

## Reading Memory with `host.memory.readMemoryValues()`

The primary function for raw memory reads:

```javascript
host.memory.readMemoryValues(address, count, elementSize)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `address` | 64-bit int | Starting virtual address |
| `count` | number | Number of elements to read |
| `elementSize` | number | Size of each element: 1, 2, 4, or 8 bytes |

### Reading Bytes

```javascript
"use strict";

function readBytes(addrStr, count) {
    let addr = host.parseInt64(addrStr);
    let bytes = host.memory.readMemoryValues(addr, count, 1);

    host.diagnostics.debugLog("Memory at 0x" + addr.toString(16) + ":\n");

    let hex = "";
    let ascii = "";
    let offset = 0;

    for (let b of bytes) {
        if (offset > 0 && offset % 16 === 0) {
            host.diagnostics.debugLog(
                "  " + hex.padEnd(50) + "| " + ascii + "\n"
            );
            hex = "";
            ascii = "";
        }

        hex += b.toString(16).padStart(2, "0") + " ";
        ascii += (b >= 0x20 && b <= 0x7e) ? String.fromCharCode(b) : ".";
        offset++;
    }

    if (hex.length > 0) {
        host.diagnostics.debugLog(
            "  " + hex.padEnd(50) + "| " + ascii + "\n"
        );
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(readBytes, "hexdump")
    ];
}
```

Usage:
```
!hexdump 0x00007ff6`12340000 128
```

### Reading DWORDs and QWORDs

```javascript
// Read 10 DWORDs (32-bit values)
let dwords = host.memory.readMemoryValues(addr, 10, 4);
for (let d of dwords) {
    host.diagnostics.debugLog("0x" + d.toString(16) + "\n");
}

// Read 5 QWORDs (64-bit values)
let qwords = host.memory.readMemoryValues(addr, 5, 8);
for (let q of qwords) {
    host.diagnostics.debugLog("0x" + q.toString(16) + "\n");
}
```

### Reading WORDs (16-bit)

```javascript
// Read 20 WORDs (16-bit values)
let words = host.memory.readMemoryValues(addr, 20, 2);
```

## Reading Strings from Memory

WinDbg JS doesn't have a built-in "read string" function, but you can build one:

### ASCII Strings

```javascript
function readAsciiString(addr, maxLen) {
    maxLen = maxLen || 256;
    let bytes = host.memory.readMemoryValues(addr, maxLen, 1);
    let str = "";
    for (let b of bytes) {
        if (b === 0) break;  // null terminator
        str += String.fromCharCode(b);
    }
    return str;
}
```

### Unicode (UTF-16) Strings

Windows uses UTF-16 extensively. `UNICODE_STRING` and `WCHAR*` are everywhere:

```javascript
function readUnicodeString(addr, maxChars) {
    maxChars = maxChars || 256;
    let words = host.memory.readMemoryValues(addr, maxChars, 2);
    let str = "";
    for (let w of words) {
        if (w === 0) break;
        str += String.fromCharCode(w);
    }
    return str;
}
```

### Reading a UNICODE_STRING Structure

The Windows `UNICODE_STRING` struct is:
```c
typedef struct _UNICODE_STRING {
    USHORT Length;        // length in bytes (not chars)
    USHORT MaximumLength; // buffer size in bytes
    PWSTR  Buffer;        // pointer to wide-char string
} UNICODE_STRING;
```

```javascript
function readUnicodeStruct(addr) {
    // Read the 3 fields
    let length = host.memory.readMemoryValues(addr, 1, 2)[0];          // USHORT
    let maxLength = host.memory.readMemoryValues(addr.add(2), 1, 2)[0]; // USHORT
    let bufferPtr = host.memory.readMemoryValues(addr.add(8), 1, 8)[0]; // PWSTR (64-bit pointer)

    let charCount = length / 2;  // length is in bytes, each char is 2 bytes
    return readUnicodeString(bufferPtr, charCount);
}
```

## Writing Memory

```javascript
host.memory.writeMemoryValues(address, elementSize, values)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `address` | 64-bit int | Target address |
| `elementSize` | number | Size per element: 1, 2, 4, or 8 |
| `values` | array | Array of values to write |

```javascript
// Write bytes
let addr = host.parseInt64("0x00000001`00001000");
host.memory.writeMemoryValues(addr, 1, [0x90, 0x90, 0x90, 0x90]); // NOP sled

// Write a DWORD
host.memory.writeMemoryValues(addr, 4, [0xDEADBEEF]);

// Write a QWORD
host.memory.writeMemoryValues(addr, 8, [host.parseInt64("0x00007ff6`12345678")]);
```

**Warning:** Writing to invalid or protected memory will throw an exception. Always wrap writes in try/catch:

```javascript
function safeWrite(addr, size, values) {
    try {
        host.memory.writeMemoryValues(addr, size, values);
        host.diagnostics.debugLog("Write succeeded at 0x" + addr.toString(16) + "\n");
        return true;
    } catch (e) {
        host.diagnostics.debugLog("Write failed at 0x" + addr.toString(16) +
            ": " + e.message + "\n");
        return false;
    }
}
```

## Searching Memory

You can search memory by combining reads with JavaScript logic:

```javascript
function searchMemory(startAddr, length, pattern) {
    // pattern is an array of bytes
    let patLen = pattern.length;
    let bytes = host.memory.readMemoryValues(startAddr, length, 1);

    let results = [];
    let byteArray = [];
    for (let b of bytes) {
        byteArray.push(b);
    }

    for (let i = 0; i <= byteArray.length - patLen; i++) {
        let match = true;
        for (let j = 0; j < patLen; j++) {
            if (pattern[j] !== null && byteArray[i + j] !== pattern[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            results.push(startAddr.add(i));
        }
    }

    return results;
}

// Search for "MZ" header (0x4D, 0x5A) in a region
function findMZHeaders() {
    let proc = host.currentProcess;
    for (let mod of proc.Modules) {
        let bytes = host.memory.readMemoryValues(mod.BaseAddress, 2, 1);
        let arr = [];
        for (let b of bytes) arr.push(b);
        if (arr[0] === 0x4D && arr[1] === 0x5A) {
            host.diagnostics.debugLog(
                mod.Name + " starts with MZ at 0x" +
                mod.BaseAddress.toString(16) + "\n"
            );
        }
    }
}
```

### Using WinDbg's Built-in Search via ExecuteCommand

For large searches, leverage WinDbg's native `s` command:

```javascript
function searchString(searchStr) {
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
        's -a 0 L?0x7fffffffffffffff "' + searchStr + '"'
    );
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }
}
```

## Reading Memory Through Typed Objects

Instead of raw reads, you can cast memory to types (covered more in Module 06):

```javascript
// If you have symbols, you can create typed views of memory
let peb = host.createTypedObject(pebAddr, "ntdll", "_PEB");
host.diagnostics.debugLog("Image base: 0x" + peb.ImageBaseAddress.toString(16) + "\n");
host.diagnostics.debugLog("Being debugged: " + peb.BeingDebugged + "\n");
```

This is far more convenient than manually reading offsets — the type system handles field offsets, sizes, and nested structures automatically.

## Practical Example: Stack Dump

```javascript
"use strict";

function stackDump(frameCount) {
    frameCount = frameCount || 32;
    let thread = host.currentThread;
    let rsp;

    try {
        rsp = thread.Registers.User.rsp;
    } catch (e) {
        rsp = thread.Registers.User.esp;
    }

    host.diagnostics.debugLog("Stack dump from RSP = 0x" + rsp.toString(16) + ":\n\n");

    let qwords = host.memory.readMemoryValues(rsp, frameCount, 8);
    let offset = 0;

    for (let val of qwords) {
        let addrStr = rsp.add(offset * 8).toString(16).padStart(16, "0");
        let valStr = val.toString(16).padStart(16, "0");

        // Try to resolve as a symbol
        let sym = "";
        try {
            let symResult = host.namespace.Debugger.Utility.Control.ExecuteCommand(
                "ln 0x" + valStr
            );
            for (let line of symResult) {
                if (line.indexOf("(") !== -1) {
                    sym = "  ; " + line.trim();
                    break;
                }
            }
        } catch (e) {
            // Not a valid symbol
        }

        host.diagnostics.debugLog(
            "0x" + addrStr + ": 0x" + valStr + sym + "\n"
        );
        offset++;
    }
}

function initializeScript() {
    return [
        new host.apiVersionSupport(1, 7),
        new host.functionAlias(stackDump, "stackdump")
    ];
}
```

## Memory Protection and Regions

You can query memory region information using WinDbg commands:

```javascript
function memoryInfo(addrStr) {
    let output = host.namespace.Debugger.Utility.Control.ExecuteCommand(
        "!address " + addrStr
    );
    for (let line of output) {
        host.diagnostics.debugLog(line + "\n");
    }
}
```

## Exercises

1. Write a `!hexdump` command that takes an address and byte count, printing a formatted hex dump with ASCII sidebar.
2. Write a function that reads a null-terminated ASCII string from an address and returns it.
3. Write a `!patchbyte` command that writes a single byte to a given address (takes address and byte value as arguments).
4. Write a memory search function that finds all occurrences of a DWORD value within a module's address range.
5. Write a `!stackstrings` command that walks the stack looking for pointers to readable ASCII strings.

## Next

[Module 05 — Registers & Threads](05-registers-and-threads.md) — Accessing CPU registers, switching threads, and inspecting thread state.
