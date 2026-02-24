#include "dbg_engine.h"

#include <sstream>
#include <iomanip>
#include <algorithm>

DbgEngine::~DbgEngine()
{
    Shutdown();
}

HRESULT DbgEngine::Initialize(IDebugClient* existingClient)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_client)
        return S_OK; // Already initialized

    // Create a new client that shares the existing debugging session.
    // This is the correct way to get a thread-safe client in a WinDbg extension.
    HRESULT hr = existingClient->CreateClient(&m_client);
    if (FAILED(hr))
        return hr;

    hr = m_client->QueryInterface(__uuidof(IDebugControl), reinterpret_cast<void**>(&m_control));
    if (FAILED(hr))
        goto fail;

    hr = m_client->QueryInterface(__uuidof(IDebugSymbols3), reinterpret_cast<void**>(&m_symbols));
    if (FAILED(hr))
        goto fail;

    hr = m_client->QueryInterface(__uuidof(IDebugRegisters), reinterpret_cast<void**>(&m_registers));
    if (FAILED(hr))
        goto fail;

    hr = m_client->QueryInterface(__uuidof(IDebugDataSpaces4), reinterpret_cast<void**>(&m_dataspaces));
    if (FAILED(hr))
        goto fail;

    hr = m_client->QueryInterface(__uuidof(IDebugSystemObjects), reinterpret_cast<void**>(&m_sysobj));
    if (FAILED(hr))
        goto fail;

    // Set our output callbacks on this client so Execute() output goes to our buffer
    hr = m_client->SetOutputCallbacks(&m_outputCb);
    if (FAILED(hr))
        goto fail;

    return S_OK;

fail:
    Shutdown();
    return hr;
}

void DbgEngine::Shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_sysobj)   { m_sysobj->Release();   m_sysobj = nullptr; }
    if (m_dataspaces){ m_dataspaces->Release();m_dataspaces = nullptr; }
    if (m_registers) { m_registers->Release(); m_registers = nullptr; }
    if (m_symbols)   { m_symbols->Release();   m_symbols = nullptr; }
    if (m_control)   { m_control->Release();   m_control = nullptr; }
    if (m_client)    { m_client->Release();    m_client = nullptr; }
}

std::string DbgEngine::ExecuteCommandLocked(const std::string& cmd)
{
    if (!m_control)
        return "[error] Debug engine not initialized";

    m_outputCb.Clear();

    HRESULT hr = m_control->Execute(
        DEBUG_OUTCTL_THIS_CLIENT,
        cmd.c_str(),
        DEBUG_EXECUTE_NO_REPEAT
    );

    std::string output = m_outputCb.GetBufferAndClear();

    if (FAILED(hr))
    {
        if (output.empty())
        {
            std::ostringstream oss;
            oss << "[error] Command failed with HRESULT 0x"
                << std::hex << std::setfill('0') << std::setw(8) << hr;
            return oss.str();
        }
    }

    return output;
}

std::string DbgEngine::ExecuteCommand(const std::string& cmd)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return ExecuteCommandLocked(cmd);
}

std::string DbgEngine::ReadMemory(uint64_t address, uint32_t size)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_dataspaces)
        return "[error] Debug engine not initialized";

    // Cap size to prevent huge reads
    if (size > 0x10000)
        size = 0x10000;

    std::vector<uint8_t> buffer(size);
    ULONG bytesRead = 0;

    HRESULT hr = m_dataspaces->ReadVirtual(address, buffer.data(), size, &bytesRead);
    if (FAILED(hr))
    {
        std::ostringstream oss;
        oss << "[error] ReadVirtual failed at 0x"
            << std::hex << address << " HRESULT 0x"
            << std::setfill('0') << std::setw(8) << hr;
        return oss.str();
    }

    // Format as hex dump with ASCII
    std::ostringstream oss;
    for (uint32_t i = 0; i < bytesRead; i += 16)
    {
        // Address
        oss << std::hex << std::setfill('0') << std::setw(16) << (address + i) << "  ";

        // Hex bytes
        for (uint32_t j = 0; j < 16; j++)
        {
            if (i + j < bytesRead)
                oss << std::setw(2) << static_cast<unsigned>(buffer[i + j]) << " ";
            else
                oss << "   ";

            if (j == 7) oss << " ";
        }

        oss << " ";

        // ASCII
        for (uint32_t j = 0; j < 16 && (i + j) < bytesRead; j++)
        {
            char c = static_cast<char>(buffer[i + j]);
            oss << (c >= 32 && c < 127 ? c : '.');
        }

        oss << "\n";
    }

    return oss.str();
}

bool DbgEngine::WriteMemory(uint64_t address, const std::vector<uint8_t>& data)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_dataspaces)
        return false;

    ULONG bytesWritten = 0;
    HRESULT hr = m_dataspaces->WriteVirtual(
        address,
        const_cast<uint8_t*>(data.data()),
        static_cast<ULONG>(data.size()),
        &bytesWritten
    );

    return SUCCEEDED(hr) && bytesWritten == data.size();
}

std::string DbgEngine::GetRegisters()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return ExecuteCommandLocked("r");
}

bool DbgEngine::SetRegister(const std::string& name, uint64_t value)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::ostringstream cmd;
    cmd << "r " << name << "=0x" << std::hex << value;
    std::string output = ExecuteCommandLocked(cmd.str());

    // If the output contains "error" or "Bad register", it failed
    return output.find("error") == std::string::npos &&
           output.find("Bad register") == std::string::npos;
}

std::string DbgEngine::GetCallStack(uint32_t maxFrames)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::ostringstream cmd;
    cmd << "k " << std::dec << maxFrames;
    return ExecuteCommandLocked(cmd.str());
}

std::string DbgEngine::Disassemble(uint64_t address, uint32_t instructionCount)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::ostringstream cmd;
    cmd << "u 0x" << std::hex << address << " L" << std::dec << instructionCount;
    return ExecuteCommandLocked(cmd.str());
}

std::string DbgEngine::ListModules()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return ExecuteCommandLocked("lm");
}

std::string DbgEngine::ListThreads()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return ExecuteCommandLocked("~");
}

std::string DbgEngine::ListBreakpoints()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return ExecuteCommandLocked("bl");
}

std::string DbgEngine::SetBreakpoint(const std::string& expression, const std::string& condition)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string cmd = "bp " + expression;
    std::string result = ExecuteCommandLocked(cmd);

    if (!condition.empty())
    {
        // Get the ID of the breakpoint we just set (last one in the list)
        std::string bpList = ExecuteCommandLocked("bl");
        result += "\n" + bpList;

        // Apply condition - user should specify the bp ID in the condition command
        // For simplicity, we'll try to apply it if we can parse the ID
    }

    // Return breakpoint list so caller can see the new breakpoint
    if (result.empty())
        result = ExecuteCommandLocked("bl");

    return result;
}

std::string DbgEngine::RemoveBreakpoint(uint32_t id)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::ostringstream cmd;
    cmd << "bc " << std::dec << id;
    std::string result = ExecuteCommandLocked(cmd.str());

    // Return updated breakpoint list
    std::string bpList = ExecuteCommandLocked("bl");
    return result + bpList;
}

std::string DbgEngine::EvaluateExpression(const std::string& expression)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string cmd = "? " + expression;
    return ExecuteCommandLocked(cmd);
}

std::string DbgEngine::SearchMemory(uint64_t startAddress, uint64_t length,
                                     const std::vector<uint8_t>& pattern)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Build the 's' command: s -b <start> L<length> <byte1> <byte2> ...
    std::ostringstream cmd;
    cmd << "s -b 0x" << std::hex << startAddress
        << " L0x" << std::hex << length;

    for (auto b : pattern)
        cmd << " " << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned>(b);

    return ExecuteCommandLocked(cmd.str());
}

std::string DbgEngine::GetSymbol(const std::string& expression)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Try 'ln' (list nearest symbols) for address resolution
    std::string cmd = "ln " + expression;
    return ExecuteCommandLocked(cmd);
}
