// STDIO-to-HTTP proxy for WinDbg MCP.
// Claude Desktop launches this as a subprocess (STDIO transport).
// It reads JSON-RPC from stdin, POSTs to the WinDbg extension's HTTP
// endpoint, and writes the response to stdout.

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>

using Json = nlohmann::json;

static constexpr const char* DEFAULT_HOST = "127.0.0.1";
static constexpr int DEFAULT_PORT = 9742;
static constexpr int MAX_RETRIES = 3;
static constexpr int RETRY_DELAY_MS = 500;

// Build a JSON-RPC error response, preserving the request id
static Json MakeError(const Json& request, int code, const std::string& message)
{
    // MCP requires id to be string or number, never null
    Json id = 0;
    if (request.is_object() && request.contains("id") && !request["id"].is_null())
        id = request["id"];

    return Json{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {{"code", code}, {"message", message}}}
    };
}

int main(int argc, char* argv[])
{
    // Parse optional --port argument
    int port = DEFAULT_PORT;
    for (int i = 1; i < argc - 1; i++)
    {
        if (std::string(argv[i]) == "--port")
            port = std::atoi(argv[i + 1]);
    }

    // Disable stdout buffering for line-delimited JSON-RPC
    std::ios_base::sync_with_stdio(false);

    httplib::Client client(DEFAULT_HOST, port);
    client.set_connection_timeout(5);
    client.set_read_timeout(120); // Tool calls can take a while

    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line.empty())
            continue;

        // Parse the request so we can extract the id for error responses
        Json request;
        try
        {
            request = Json::parse(line);
        }
        catch (...)
        {
            Json error = {
                {"jsonrpc", "2.0"},
                {"id", 0},
                {"error", {{"code", -32700}, {"message", "Parse error"}}}
            };
            std::cout << error.dump() << "\n" << std::flush;
            continue;
        }

        // JSON-RPC notifications have no "id" field.
        // Per the spec, notifications must NOT receive a response.
        // Claude Desktop sends "notifications/initialized" after the
        // initialize handshake — we silently discard these.
        if (!request.contains("id"))
            continue;

        // Try to POST with retries
        bool success = false;
        for (int attempt = 0; attempt < MAX_RETRIES; attempt++)
        {
            auto res = client.Post("/mcp", line, "application/json");

            if (res && res->status == 200)
            {
                std::cout << res->body << "\n" << std::flush;
                success = true;
                break;
            }

            // Wait before retry
            if (attempt < MAX_RETRIES - 1)
                std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
        }

        if (!success)
        {
            auto error = MakeError(request, -32000,
                "Cannot connect to WinDbg MCP server on port " + std::to_string(port) +
                ". Make sure WinDbg is running and you have executed !mcp");
            std::cout << error.dump() << "\n" << std::flush;
        }
    }

    return 0;
}
