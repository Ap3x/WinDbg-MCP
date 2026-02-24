#include "mcp_server.h"
#include "dbg_engine.h"

#include <fastmcpp/mcp/handler.hpp>
#include <fastmcpp/tools/tool.hpp>
#include <fastmcpp/tools/manager.hpp>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>

using Json = nlohmann::json;

// Helper: parse a hex string like "0x1234" or "1234" to uint64_t
static uint64_t ParseAddress(const std::string& s)
{
    return std::stoull(s, nullptr, 0);
}

// Helper: parse hex byte string "4142ff" into vector<uint8_t>
static std::vector<uint8_t> ParseHexBytes(const std::string& hex)
{
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
    {
        uint8_t b = static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16));
        bytes.push_back(b);
    }
    return bytes;
}

// Helper: wrap a string result as MCP content array
static Json TextResult(const std::string& text)
{
    return Json{
        {"content", Json::array({Json{{"type", "text"}, {"text", text}}})}
    };
}

// Helper: wrap an error result
static Json ErrorResult(const std::string& text)
{
    return Json{
        {"content", Json::array({Json{{"type", "text"}, {"text", text}}})},
        {"isError", true}
    };
}

// JSON Schema helpers
static Json StringProp(const std::string& desc)
{
    return Json{{"type", "string"}, {"description", desc}};
}

static Json IntProp(const std::string& desc)
{
    return Json{{"type", "integer"}, {"description", desc}};
}

static void RegisterTools(fastmcpp::tools::ToolManager& tm,
                          std::unordered_map<std::string, std::string>& descs,
                          std::shared_ptr<DbgEngine> engine)
{
    // No outputSchema - avoids structuredContent requirement in MCP 2025+
    Json out = Json();

    // 1. execute_command - Run any WinDbg command
    {
        Json schema{
            {"type", "object"},
            {"properties", Json{
                {"command", StringProp("The WinDbg command to execute (e.g. 'r', 'k', 'lm', 'dt nt!_EPROCESS')")}
            }},
            {"required", Json::array({"command"})}
        };
        fastmcpp::tools::Tool tool("execute_command", schema, out,
            [engine](const Json& input) -> Json {
                auto cmd = input.at("command").get<std::string>();
                return TextResult(engine->ExecuteCommand(cmd));
            });
        tool.set_description("Execute any WinDbg/debugger command and return its text output");
        tm.register_tool(tool);
        descs["execute_command"] = "Execute any WinDbg/debugger command and return its text output";
    }

    // 2. read_memory - Read memory at address
    {
        Json schema{
            {"type", "object"},
            {"properties", Json{
                {"address", StringProp("Virtual address to read from (hex, e.g. '0x7ff61234')")},
                {"size", IntProp("Number of bytes to read (default 256, max 65536)")}
            }},
            {"required", Json::array({"address"})}
        };
        fastmcpp::tools::Tool tool("read_memory", schema, out,
            [engine](const Json& input) -> Json {
                uint64_t addr = ParseAddress(input.at("address").get<std::string>());
                uint32_t size = 256;
                if (input.contains("size"))
                    size = input.at("size").get<uint32_t>();
                return TextResult(engine->ReadMemory(addr, size));
            });
        tool.set_description("Read memory at a virtual address and return a hex dump with ASCII");
        tm.register_tool(tool);
        descs["read_memory"] = "Read memory at a virtual address and return a hex dump with ASCII";
    }

    // 3. write_memory - Write bytes to memory
    {
        Json schema{
            {"type", "object"},
            {"properties", Json{
                {"address", StringProp("Virtual address to write to (hex)")},
                {"bytes", StringProp("Hex string of bytes to write (e.g. '90909090' for NOPs)")}
            }},
            {"required", Json::array({"address", "bytes"})}
        };
        fastmcpp::tools::Tool tool("write_memory", schema, out,
            [engine](const Json& input) -> Json {
                uint64_t addr = ParseAddress(input.at("address").get<std::string>());
                auto hexBytes = input.at("bytes").get<std::string>();
                auto data = ParseHexBytes(hexBytes);
                bool ok = engine->WriteMemory(addr, data);
                return ok ? TextResult("Write successful (" + std::to_string(data.size()) + " bytes)")
                          : ErrorResult("Write failed");
            });
        tool.set_description("Write bytes to a virtual address in the target process");
        tm.register_tool(tool);
        descs["write_memory"] = "Write bytes to a virtual address in the target process";
    }

    // 4. get_registers - Get CPU register values
    {
        Json schema{{"type", "object"}, {"properties", Json::object()}};
        fastmcpp::tools::Tool tool("get_registers", schema, out,
            [engine](const Json&) -> Json {
                return TextResult(engine->GetRegisters());
            });
        tool.set_description("Get all CPU register values");
        tm.register_tool(tool);
        descs["get_registers"] = "Get all CPU register values";
    }

    // 5. set_register - Set a register value
    {
        Json schema{
            {"type", "object"},
            {"properties", Json{
                {"register", StringProp("Register name (e.g. 'rax', 'eip', 'rcx')")},
                {"value", StringProp("Value to set (hex, e.g. '0x1234')")}
            }},
            {"required", Json::array({"register", "value"})}
        };
        fastmcpp::tools::Tool tool("set_register", schema, out,
            [engine](const Json& input) -> Json {
                auto reg = input.at("register").get<std::string>();
                uint64_t val = ParseAddress(input.at("value").get<std::string>());
                bool ok = engine->SetRegister(reg, val);
                return ok ? TextResult("Register " + reg + " set successfully")
                          : ErrorResult("Failed to set register " + reg);
            });
        tool.set_description("Set a CPU register to a specific value");
        tm.register_tool(tool);
        descs["set_register"] = "Set a CPU register to a specific value";
    }

    // 6. set_breakpoint - Set a breakpoint
    {
        Json schema{
            {"type", "object"},
            {"properties", Json{
                {"expression", StringProp("Breakpoint location: address (0x...), symbol (kernel32!CreateFileW), or source location")},
                {"condition", StringProp("Optional conditional expression for the breakpoint")}
            }},
            {"required", Json::array({"expression"})}
        };
        fastmcpp::tools::Tool tool("set_breakpoint", schema, out,
            [engine](const Json& input) -> Json {
                auto expr = input.at("expression").get<std::string>();
                std::string cond;
                if (input.contains("condition"))
                    cond = input.at("condition").get<std::string>();
                return TextResult(engine->SetBreakpoint(expr, cond));
            });
        tool.set_description("Set a breakpoint at an address, symbol, or source location");
        tm.register_tool(tool);
        descs["set_breakpoint"] = "Set a breakpoint at an address, symbol, or source location";
    }

    // 7. remove_breakpoint - Remove a breakpoint
    {
        Json schema{
            {"type", "object"},
            {"properties", Json{
                {"id", IntProp("Breakpoint ID to remove")}
            }},
            {"required", Json::array({"id"})}
        };
        fastmcpp::tools::Tool tool("remove_breakpoint", schema, out,
            [engine](const Json& input) -> Json {
                uint32_t id = input.at("id").get<uint32_t>();
                return TextResult(engine->RemoveBreakpoint(id));
            });
        tool.set_description("Remove a breakpoint by its ID");
        tm.register_tool(tool);
        descs["remove_breakpoint"] = "Remove a breakpoint by its ID";
    }

    // 8. list_breakpoints
    {
        Json schema{{"type", "object"}, {"properties", Json::object()}};
        fastmcpp::tools::Tool tool("list_breakpoints", schema, out,
            [engine](const Json&) -> Json {
                return TextResult(engine->ListBreakpoints());
            });
        tool.set_description("List all breakpoints with their IDs, addresses, and status");
        tm.register_tool(tool);
        descs["list_breakpoints"] = "List all breakpoints with their IDs, addresses, and status";
    }

    // 9. get_callstack - Get call stack
    {
        Json schema{
            {"type", "object"},
            {"properties", Json{
                {"frames", IntProp("Maximum number of stack frames to return (default 20)")}
            }}
        };
        fastmcpp::tools::Tool tool("get_callstack", schema, out,
            [engine](const Json& input) -> Json {
                uint32_t frames = 20;
                if (input.contains("frames"))
                    frames = input.at("frames").get<uint32_t>();
                return TextResult(engine->GetCallStack(frames));
            });
        tool.set_description("Get the call stack of the current thread");
        tm.register_tool(tool);
        descs["get_callstack"] = "Get the call stack of the current thread";
    }

    // 10. disassemble - Disassemble at address
    {
        Json schema{
            {"type", "object"},
            {"properties", Json{
                {"address", StringProp("Address to disassemble from (hex). Use 'eip'/'rip' for current instruction pointer.")},
                {"count", IntProp("Number of instructions to disassemble (default 10)")}
            }},
            {"required", Json::array({"address"})}
        };
        fastmcpp::tools::Tool tool("disassemble", schema, out,
            [engine](const Json& input) -> Json {
                auto addrStr = input.at("address").get<std::string>();
                uint32_t count = 10;
                if (input.contains("count"))
                    count = input.at("count").get<uint32_t>();

                std::ostringstream cmd;
                cmd << "u " << addrStr << " L" << std::dec << count;
                return TextResult(engine->ExecuteCommand(cmd.str()));
            });
        tool.set_description("Disassemble instructions at a given address");
        tm.register_tool(tool);
        descs["disassemble"] = "Disassemble instructions at a given address";
    }

    // 11. list_modules
    {
        Json schema{{"type", "object"}, {"properties", Json::object()}};
        fastmcpp::tools::Tool tool("list_modules", schema, out,
            [engine](const Json&) -> Json {
                return TextResult(engine->ListModules());
            });
        tool.set_description("List all loaded modules with base address and size");
        tm.register_tool(tool);
        descs["list_modules"] = "List all loaded modules with base address and size";
    }

    // 12. list_threads
    {
        Json schema{{"type", "object"}, {"properties", Json::object()}};
        fastmcpp::tools::Tool tool("list_threads", schema, out,
            [engine](const Json&) -> Json {
                return TextResult(engine->ListThreads());
            });
        tool.set_description("List all threads in the target process");
        tm.register_tool(tool);
        descs["list_threads"] = "List all threads in the target process";
    }

    // 13. evaluate_expression
    {
        Json schema{
            {"type", "object"},
            {"properties", Json{
                {"expression", StringProp("Debugger expression to evaluate (e.g. 'poi(esp)', '@rax+0x10', 'sizeof(nt!_EPROCESS)')")}
            }},
            {"required", Json::array({"expression"})}
        };
        fastmcpp::tools::Tool tool("evaluate_expression", schema, out,
            [engine](const Json& input) -> Json {
                auto expr = input.at("expression").get<std::string>();
                return TextResult(engine->EvaluateExpression(expr));
            });
        tool.set_description("Evaluate a debugger expression and return the result");
        tm.register_tool(tool);
        descs["evaluate_expression"] = "Evaluate a debugger expression and return the result";
    }

    // 14. search_memory
    {
        Json schema{
            {"type", "object"},
            {"properties", Json{
                {"address", StringProp("Start address for search (hex)")},
                {"length", StringProp("Length of memory range to search (hex)")},
                {"pattern", StringProp("Hex byte pattern to search for (e.g. '4d5a9000')")}
            }},
            {"required", Json::array({"address", "length", "pattern"})}
        };
        fastmcpp::tools::Tool tool("search_memory", schema, out,
            [engine](const Json& input) -> Json {
                uint64_t addr = ParseAddress(input.at("address").get<std::string>());
                uint64_t len = ParseAddress(input.at("length").get<std::string>());
                auto pattern = ParseHexBytes(input.at("pattern").get<std::string>());
                return TextResult(engine->SearchMemory(addr, len, pattern));
            });
        tool.set_description("Search memory for a byte pattern within a specified range");
        tm.register_tool(tool);
        descs["search_memory"] = "Search memory for a byte pattern within a specified range";
    }

    // 15. get_symbol
    {
        Json schema{
            {"type", "object"},
            {"properties", Json{
                {"expression", StringProp("Address (hex) to resolve to symbol, or symbol name to resolve to address")}
            }},
            {"required", Json::array({"expression"})}
        };
        fastmcpp::tools::Tool tool("get_symbol", schema, out,
            [engine](const Json& input) -> Json {
                auto expr = input.at("expression").get<std::string>();
                return TextResult(engine->GetSymbol(expr));
            });
        tool.set_description("Resolve a symbol name to address or an address to the nearest symbol");
        tm.register_tool(tool);
        descs["get_symbol"] = "Resolve a symbol name to address or an address to the nearest symbol";
    }
}

McpServer::~McpServer()
{
    Stop();
}

bool McpServer::Start(std::shared_ptr<DbgEngine> engine, int port)
{
    if (m_running)
        return false;

    m_engine = engine;
    m_port = port;

    // Register all tools into member variables (must outlive the handler lambda)
    m_tools = fastmcpp::tools::ToolManager();
    m_descriptions.clear();
    RegisterTools(m_tools, m_descriptions, m_engine);

    // Create MCP handler - captures m_tools by reference, so m_tools must
    // live as long as the handler (guaranteed since both are members of McpServer)
    auto handler = fastmcpp::mcp::make_mcp_handler(
        "windbg-mcp",
        "1.0.0",
        m_tools,
        m_descriptions
    );

    // Create httplib server with a single POST /mcp endpoint
    m_svr = std::make_unique<httplib::Server>();

    m_svr->Post("/mcp", [handler](const httplib::Request& req, httplib::Response& res) {
        try
        {
            auto request = Json::parse(req.body);
            auto response = handler(request);
            res.set_content(response.dump(), "application/json");
        }
        catch (const std::exception& e)
        {
            Json error = {
                {"jsonrpc", "2.0"},
                {"error", {{"code", -32700}, {"message", std::string("Parse error: ") + e.what()}}},
                {"id", 0}
            };
            res.set_content(error.dump(), "application/json");
        }
    });

    // Health check endpoint
    m_svr->Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    // Start in background thread
    m_thread = std::thread([this, port]() {
        m_running = true;
        m_svr->listen("127.0.0.1", port);
        m_running = false;
    });

    // Wait briefly for server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return m_running;
}

void McpServer::Stop()
{
    if (m_svr)
    {
        m_svr->stop();
        if (m_thread.joinable())
            m_thread.join();
        m_svr.reset();
    }
    m_running = false;
    m_engine.reset();
}

bool McpServer::IsRunning() const
{
    return m_running;
}
