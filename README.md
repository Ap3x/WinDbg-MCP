# WinDbg MCP Server

A WinDbg extension that embeds an [MCP (Model Context Protocol)](https://modelcontextprotocol.io/) server, allowing AI assistants like Claude to interact with live debugging sessions.

Built with [fastmcpp](https://github.com/0xeb/fastmcpp) and the Windows Debugger Engine (dbgeng).

**Session sharing** — Because WinDbg-MCP loads into your existing WinDbg session, you can set breakpoints in the UI, hit one, and then ask Claude to analyze the state. With mcp-windbg, CDB runs in its own isolated session — you can't interact with it through a GUI, and it can't share state with an existing debugging session.

**Kernel debugging** — If WinDbg is attached to a kernel target, the extension just works since it shares the same dbgeng session. Getting CDB to attach to kernel targets is more involved.

## How It Works

```
┌──────────────────────────────────────────────────┐
│                 WinDbg Process                   │
│                                                  │
│  WinDbg UI ◄──► windbg-mcp.dll (extension)       │
│                  │                               │
│                  ├─ HTTP POST /mcp (bg thread)   │
│                  │   JSON-RPC 2.0 endpoint       │
│                  │                               │
│                  └─ DbgEngine (mutex-protected)  │
│                      Own IDebugClient sharing    │
│                      the same debug session      │
│                                                  │
└──────────────────────┬───────────────────────────┘
                       │ HTTP localhost:9742
                       │
┌──────────────────────┴───────────────────────────┐
│  windbg-mcp-proxy.exe                            │
│  STDIO-to-HTTP bridge                            │
│  Reads JSON-RPC from stdin, POSTs to /mcp,       │
│  writes response to stdout                       │
│  Launched by Claude Desktop / Claude Code        │
└──────────────────────────────────────────────────┘
```

1. Load the extension in WinDbg and run `!mcp` to start the HTTP server on port 9742
2. The MCP client (Claude Desktop or Claude Code) launches the STDIO proxy as a subprocess
3. The proxy bridges JSON-RPC messages between stdin/stdout and the HTTP endpoint
4. Tool calls are routed through the proxy to the WinDbg extension, which executes debugger commands via the dbgeng COM interfaces and returns the results

## Building

**Requirements**: Visual Studio 2022 with C++ workload, CMake 3.20+

```bash
git clone https://github.com/0xeb/fastmcpp.git
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 ^
  -DFASTMCPP_BUILD_TESTS=OFF ^
  -DFASTMCPP_BUILD_EXAMPLES=OFF
cmake --build build --config Release
```

Output:
- `build/Release/windbg-mcp.dll` — WinDbg extension
- `build/Release/windbg-mcp-proxy.exe` — STDIO proxy for Claude Desktop

## Usage

### Install the Extension

Copy the built DLL to the WinDbg engine extensions directory:

```
copy build\Release\windbg-mcp.dll %LOCALAPPDATA%\Dbg\EngineExtensions\
```

### In WinDbg

```
.load windbg-mcp

!mcp              # Start MCP server on default port 9742
!mcp 8080         # Start on custom port
!mcp_status       # Check server status
!mcp_stop         # Stop the server
```

### Claude Desktop Configuration

Add to `%APPDATA%\Claude\claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "windbg-mcp": {
      "command": "C:\\path\\to\\windbg-mcp-proxy.exe",
      "args": []
    }
  }
}
```

**Important**: Start `!mcp` in WinDbg **before** launching Claude Desktop, since the proxy needs the HTTP server to be running.

### Claude Code Configuration

Add to your MCP settings (`.claude/settings.json`):

```json
{
  "mcpServers": {
    "windbg-mcp": {
      "command": "C:\\path\\to\\windbg-mcp-proxy.exe",
      "args": []
    }
  }
}
```

To use a custom port: `"args": ["--port", "8080"]`

## Available Tools

| Tool | Description |
|------|-------------|
| `execute_command` | Run any WinDbg command and return its text output |
| `read_memory` | Read memory at a virtual address (hex dump with ASCII) |
| `write_memory` | Write bytes to a virtual address |
| `get_registers` | Get all CPU register values |
| `set_register` | Set a CPU register to a specific value |
| `set_breakpoint` | Set a breakpoint at an address, symbol, or source location |
| `remove_breakpoint` | Remove a breakpoint by ID |
| `list_breakpoints` | List all breakpoints with status |
| `get_callstack` | Get the call stack of the current thread |
| `disassemble` | Disassemble instructions at an address |
| `list_modules` | List all loaded modules with base address and size |
| `list_threads` | List all threads in the target process |
| `evaluate_expression` | Evaluate a debugger expression (e.g. `poi(esp)`) |
| `search_memory` | Search memory for a byte pattern |
| `get_symbol` | Resolve a symbol name to address or vice versa |

## Architecture

- **extension.cpp** — WinDbg extension entry points (`!mcp`, `!mcp_stop`, `!mcp_status`)
- **mcp_server.cpp** — HTTP server with JSON-RPC endpoint and tool registration using fastmcpp
- **dbg_engine.cpp** — Thread-safe wrapper around dbgeng COM interfaces (`IDebugClient`, `IDebugControl`, `IDebugSymbols3`, `IDebugRegisters`, `IDebugDataSpaces4`)
- **output_callbacks.cpp** — `IDebugOutputCallbacks` implementation for capturing command output
- **stdio_proxy.cpp** — Standalone STDIO-to-HTTP bridge (launched by Claude Desktop)

The extension creates a separate `IDebugClient` via `CreateClient()` on the existing WinDbg client, sharing the same debug session without interfering with the UI. All dbgeng calls from MCP tool handlers are serialized with a mutex to ensure thread safety. The HTTP server runs on a background thread, and the STDIO proxy handles MCP protocol details like filtering JSON-RPC notifications.

## Resources

- [IDebugClient (dbgeng.h)](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dbgeng/nn-dbgeng-idebugclient) — Windows Debugger Engine COM interface documentation
- [fastmcpp](https://github.com/0xeb/fastmcpp) — C++ MCP server library used for tool registration and JSON-RPC handling
- [ret-sync](https://github.com/bootleg/ret-sync) — Reference implementation for WinDbg extension patterns and dbgeng COM usage

## License

GPL-3.0 — see [LICENSE](LICENSE) for details.
