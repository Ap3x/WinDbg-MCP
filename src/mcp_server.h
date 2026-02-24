#pragma once

#include <httplib.h>
#include <fastmcpp/tools/manager.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <thread>

class DbgEngine;

// MCP server that exposes WinDbg debugging capabilities via HTTP.
// Hosts a JSON-RPC POST endpoint at /mcp.
// A separate STDIO proxy EXE bridges Claude Desktop to this endpoint.
class McpServer
{
public:
    McpServer() = default;
    ~McpServer();

    // Start the HTTP server on the given port.
    bool Start(std::shared_ptr<DbgEngine> engine, int port = 9742);

    // Stop the server.
    void Stop();

    bool IsRunning() const;
    int Port() const { return m_port; }

private:
    std::unique_ptr<httplib::Server> m_svr;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::shared_ptr<DbgEngine> m_engine;
    int m_port = 0;

    // Must outlive the MCP handler lambda (which captures ToolManager by reference)
    fastmcpp::tools::ToolManager m_tools;
    std::unordered_map<std::string, std::string> m_descriptions;
};
