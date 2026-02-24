#pragma once

#define _WINSOCKAPI_
#include <windows.h>
#include <dbgeng.h>
#include <string>
#include <mutex>

// IDebugOutputCallbacks implementation that captures command output into a string buffer.
// Used by DbgEngine to intercept output from IDebugControl::Execute().
class OutputCallbacks : public IDebugOutputCallbacks
{
public:
    // IUnknown
    STDMETHOD(QueryInterface)(REFIID InterfaceId, PVOID* Interface) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    // IDebugOutputCallbacks
    STDMETHOD(Output)(ULONG Mask, PCSTR Text) override;

    // Retrieve accumulated output and clear the buffer
    std::string GetBufferAndClear();

    // Clear the buffer without retrieving
    void Clear();

private:
    std::string m_buffer;
    std::mutex  m_bufferMutex;
};
