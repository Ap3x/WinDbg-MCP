#include "dbg_engine.h"
#include "mcp_server.h"

#define _WINSOCKAPI_
#include <windows.h>

#define KDEXT_64BIT
#include <wdbgexts.h>
#include <dbgeng.h>

#include <memory>
#include <string>
#include <cstdlib>

// Global state
static std::shared_ptr<DbgEngine> g_DbgEngine;
static std::unique_ptr<McpServer> g_McpServer;

static constexpr int DEFAULT_PORT = 9742;

// Helper to print to the WinDbg console
static void PrintOutput(PDEBUG_CLIENT4 Client, const char* fmt, ...)
{
    PDEBUG_CONTROL control = nullptr;
    if (SUCCEEDED(Client->QueryInterface(__uuidof(IDebugControl), reinterpret_cast<void**>(&control))))
    {
        va_list args;
        va_start(args, fmt);

        char buf[2048];
        vsnprintf(buf, sizeof(buf), fmt, args);
        control->Output(DEBUG_OUTPUT_NORMAL, "%s", buf);

        va_end(args);
        control->Release();
    }
}

// ============================================================================
// WinDbg Extension Entry Points
// ============================================================================

extern "C" __declspec(dllexport) HRESULT CALLBACK
DebugExtensionInitialize(PULONG Version, PULONG Flags)
{
    *Version = DEBUG_EXTENSION_VERSION(1, 0);
    *Flags = 0;
    return S_OK;
}

extern "C" __declspec(dllexport) void CALLBACK
DebugExtensionUninitialize(void)
{
    try
    {
        if (g_McpServer)
        {
            g_McpServer->Stop();
            g_McpServer.reset();
        }
        g_DbgEngine.reset();
    }
    catch (...) {}
}

extern "C" __declspec(dllexport) void CALLBACK
DebugExtensionNotify(ULONG Notify, ULONG64 Argument)
{
    UNREFERENCED_PARAMETER(Notify);
    UNREFERENCED_PARAMETER(Argument);
}

// ============================================================================
// !mcp [port] - Start the MCP server (inner implementation)
// ============================================================================
static HRESULT McpStartImpl(PDEBUG_CLIENT4 Client, PCSTR Args)
{
    // Check if already running
    if (g_McpServer && g_McpServer->IsRunning())
    {
        PrintOutput(Client, "[windbg-mcp] MCP server is already running on port %d\n",
                    g_McpServer->Port());
        return S_OK;
    }

    // Parse optional port argument
    int port = DEFAULT_PORT;
    if (Args && *Args)
    {
        while (*Args == ' ') Args++;
        if (*Args)
        {
            port = atoi(Args);
            if (port <= 0 || port > 65535)
                port = DEFAULT_PORT;
        }
    }

    PrintOutput(Client, "[windbg-mcp] Initializing debug engine...\n");

    // Get base IDebugClient from the passed-in client
    IDebugClient* baseClient = nullptr;
    HRESULT hr = Client->QueryInterface(__uuidof(IDebugClient), reinterpret_cast<void**>(&baseClient));
    if (FAILED(hr))
    {
        PrintOutput(Client, "[windbg-mcp] Failed to get IDebugClient (0x%08x)\n", hr);
        return hr;
    }

    // Initialize our debug engine with a new client that shares the session
    g_DbgEngine = std::make_shared<DbgEngine>();
    hr = g_DbgEngine->Initialize(baseClient);
    baseClient->Release();

    if (FAILED(hr))
    {
        PrintOutput(Client, "[windbg-mcp] Failed to initialize debug engine (0x%08x)\n", hr);
        g_DbgEngine.reset();
        return hr;
    }

    PrintOutput(Client, "[windbg-mcp] Debug engine initialized. Starting HTTP server on port %d...\n", port);

    // Start the MCP server
    g_McpServer = std::make_unique<McpServer>();
    if (!g_McpServer->Start(g_DbgEngine, port))
    {
        PrintOutput(Client, "[windbg-mcp] Failed to start MCP server on port %d\n", port);
        g_McpServer.reset();
        g_DbgEngine.reset();
        return E_FAIL;
    }

    PrintOutput(Client, "[windbg-mcp] MCP server started successfully\n");
    PrintOutput(Client, "[windbg-mcp] HTTP endpoint: http://127.0.0.1:%d/mcp\n", port);
    PrintOutput(Client, "[windbg-mcp] Use !mcp_stop to stop the server\n");

    return S_OK;
}

// SEH wrapper for !mcp
extern "C" __declspec(dllexport) HRESULT CALLBACK
mcp(PDEBUG_CLIENT4 Client, PCSTR Args)
{
    HRESULT hr = E_FAIL;
    __try
    {
        hr = McpStartImpl(Client, Args);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        // If we get here, something crashed - don't take down WinDbg
        hr = E_FAIL;
    }
    return hr;
}

// ============================================================================
// !mcp_stop - Stop the MCP server
// ============================================================================
extern "C" __declspec(dllexport) HRESULT CALLBACK
mcp_stop(PDEBUG_CLIENT4 Client, PCSTR Args)
{
    UNREFERENCED_PARAMETER(Args);

    if (!g_McpServer || !g_McpServer->IsRunning())
    {
        PrintOutput(Client, "[windbg-mcp] MCP server is not running\n");
        return S_OK;
    }

    try
    {
        g_McpServer->Stop();
        g_McpServer.reset();
        g_DbgEngine.reset();
        PrintOutput(Client, "[windbg-mcp] MCP server stopped\n");
    }
    catch (...)
    {
        PrintOutput(Client, "[windbg-mcp] Error stopping server\n");
    }

    return S_OK;
}

// ============================================================================
// !mcp_status - Show MCP server status
// ============================================================================
extern "C" __declspec(dllexport) HRESULT CALLBACK
mcp_status(PDEBUG_CLIENT4 Client, PCSTR Args)
{
    UNREFERENCED_PARAMETER(Args);

    if (g_McpServer && g_McpServer->IsRunning())
    {
        PrintOutput(Client, "[windbg-mcp] Status: RUNNING\n");
        PrintOutput(Client, "[windbg-mcp] HTTP endpoint: http://127.0.0.1:%d/mcp\n",
                    g_McpServer->Port());
        PrintOutput(Client, "[windbg-mcp] Debug engine: %s\n",
                    (g_DbgEngine && g_DbgEngine->IsInitialized()) ? "initialized" : "not initialized");
    }
    else
    {
        PrintOutput(Client, "[windbg-mcp] Status: STOPPED\n");
        PrintOutput(Client, "[windbg-mcp] Use !mcp [port] to start (default port: %d)\n", DEFAULT_PORT);
    }

    return S_OK;
}
