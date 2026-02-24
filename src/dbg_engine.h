#pragma once

#define _WINSOCKAPI_
#include <windows.h>
#include <dbgeng.h>
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

#include "output_callbacks.h"

// Wrapper around dbgeng COM interfaces for use from the MCP server thread.
// Creates its own IDebugClient (separate from WinDbg's UI client) and
// serializes all calls via an internal mutex.
class DbgEngine
{
public:
    DbgEngine() = default;
    ~DbgEngine();

    // Initialize from the WinDbg extension's client.
    // Uses CreateClient() to get a new client that shares the session.
    // Must be called from the WinDbg extension thread (during !mcp command).
    HRESULT Initialize(IDebugClient* existingClient);

    // Shutdown: release all COM interfaces.
    void Shutdown();

    bool IsInitialized() const { return m_client != nullptr; }

    // Execute an arbitrary WinDbg command and return its text output.
    std::string ExecuteCommand(const std::string& cmd);

    // Read memory at the given virtual address. Returns hex dump string.
    std::string ReadMemory(uint64_t address, uint32_t size);

    // Write bytes to the given virtual address. Returns true on success.
    bool WriteMemory(uint64_t address, const std::vector<uint8_t>& data);

    // Get all register values as text.
    std::string GetRegisters();

    // Set a register by name. Value is a hex string (e.g. "0x1234").
    bool SetRegister(const std::string& name, uint64_t value);

    // Get call stack. Returns text output of 'k' command.
    std::string GetCallStack(uint32_t maxFrames);

    // Disassemble at address. Returns text output.
    std::string Disassemble(uint64_t address, uint32_t instructionCount);

    // List loaded modules.
    std::string ListModules();

    // List threads.
    std::string ListThreads();

    // List breakpoints.
    std::string ListBreakpoints();

    // Set a breakpoint. Expression can be an address or symbol.
    std::string SetBreakpoint(const std::string& expression, const std::string& condition);

    // Remove a breakpoint by ID.
    std::string RemoveBreakpoint(uint32_t id);

    // Evaluate a debugger expression (e.g. "poi(esp)").
    std::string EvaluateExpression(const std::string& expression);

    // Search memory for a byte pattern.
    std::string SearchMemory(uint64_t startAddress, uint64_t length, const std::vector<uint8_t>& pattern);

    // Resolve symbol to address or address to symbol.
    std::string GetSymbol(const std::string& expression);

private:
    // Execute a command string, capturing output via our callbacks.
    // Caller must hold m_mutex.
    std::string ExecuteCommandLocked(const std::string& cmd);

    IDebugClient*        m_client      = nullptr;
    IDebugControl*       m_control     = nullptr;
    IDebugSymbols3*      m_symbols     = nullptr;
    IDebugRegisters*     m_registers   = nullptr;
    IDebugDataSpaces4*   m_dataspaces  = nullptr;
    IDebugSystemObjects* m_sysobj      = nullptr;
    OutputCallbacks      m_outputCb;
    std::mutex           m_mutex;
};
