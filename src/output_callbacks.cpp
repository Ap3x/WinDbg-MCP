#include "output_callbacks.h"

STDMETHODIMP
OutputCallbacks::QueryInterface(REFIID InterfaceId, PVOID* Interface)
{
    *Interface = nullptr;

    if (IsEqualIID(InterfaceId, __uuidof(IUnknown)) ||
        IsEqualIID(InterfaceId, __uuidof(IDebugOutputCallbacks)))
    {
        *Interface = static_cast<IDebugOutputCallbacks*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
OutputCallbacks::AddRef()
{
    // Static lifetime, no ref counting needed
    return 1;
}

STDMETHODIMP_(ULONG)
OutputCallbacks::Release()
{
    return 0;
}

STDMETHODIMP
OutputCallbacks::Output(ULONG Mask, PCSTR Text)
{
    UNREFERENCED_PARAMETER(Mask);

    if (Text)
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        m_buffer.append(Text);
    }

    return S_OK;
}

std::string OutputCallbacks::GetBufferAndClear()
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    std::string result = std::move(m_buffer);
    m_buffer.clear();
    return result;
}

void OutputCallbacks::Clear()
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_buffer.clear();
}
