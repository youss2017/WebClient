#include "CppUtility.hpp"
#include <optional>
#include <thread>
#include <filesystem>
#include <numeric>
#include "web_server.h"
#include "sys_info.h"

using namespace std;
bool g_ContinueRunning = true;

int main()
{
    cpp::EnableUTF8();
    // setvbuf(stdout, NULL, _IONBF, 0);
    cpp::Logger::GetGlobalLogger().Options.VerboseMode = true;
    cpp::Logger::GetGlobalLogger().Options.IncludeDate = true;
    cpp::Logger::GetGlobalLogger().Options.IncludeFileAndLine = true;
    cpp::Logger::GetGlobalLogger().AddFileLogging("history.log");

    LOG(INFO, "Server Startup");

    web_server server;

    server.AddHttpRouteHandler({"/history.log"}, [](http_request &request,
                                                    optional<http_response> &outResponse) -> middleware_route_status {
        if (request.query.contains("clear") &&
            request.query["clear"] == "true") {
            cpp::WriteAllText("../cmake-build-debug/history.log", "");
        }
        auto text = cpp::ReadAllText("../cmake-build-debug/history.log").value_or("could not open history.log file.");
        if (text.empty())
            text = "[Empty]";
        http_response response;
        response.headers["Content-Type"] = "text/plain";
        response.SetBody(text);
        outResponse = response;
        return middleware_route_status::dynamic_response;
    });

    server.AddHttpRouteHandler({"/ls", "/dir"}, [](http_request &request,
                                                   optional<http_response> &outResponse) -> middleware_route_status {
        stringstream content;
        content <<
                R"(<!DOCTYPE html>
            <html lang="en-US">
            <head>
                <title>List of Files</title>
            <style>
                tr:nth-child(even) {background-color: #f2f2f2;}
                tr:hover {background-color: coral;}
                table {
                    margin-left: auto;
                    margin-right: auto;
                }
                a:hover, a:visited { color:blue }
                table {
                    border: 2px solid;
                    border-radius: 6px;
                }
                td {
                    padding: 5px;
                }
            </style>
            </head>
            <body>
                <table>
                <thead>
                <tr>
                <th></th>
                <th style="min-width: 250px">File Name</th>
                <th style="min-width: 150px">File Size</th>
                </tr>
                </thead>)";

        namespace fs = filesystem;

        std::vector<std::pair<fs::path, uintmax_t>> files;

        for (const auto &entry: fs::directory_iterator("../wwwroot")) {
            if (fs::is_regular_file(entry.path())) {
                uintmax_t size = fs::file_size(entry.path());
                files.emplace_back(entry.path(), size);
            }
        }

        // Sort files by size in descending order
        std::sort(files.begin(), files.end(),
                  [](const std::pair<fs::path, uintmax_t> &a, const std::pair<fs::path, uintmax_t> &b) {
                      return a.second > b.second;
                  });

        // Print sorted files
        int counter = 1;
        for (const auto &[file, size]: files) {
            auto name = file.filename().string();
            content << cpp::Format(
                    "<tr><td>{2}</td><td style=\"padding: 0.5em\"><a href='/{0}'>{0}</a></td><td style='text-align: center'>{1}</td>",
                    name, cpp::FriendlyMemorySize((double) cpp::GetFileSize(file.string())),
                    counter++) << '\n';
        }

        content << "</table></body></html>";

        http_response response;
        response.headers["Content-Type"] = "text/html";
        response.SetBody(content.str());
        outResponse = response;

        return middleware_route_status::dynamic_response;
    });


    server.AddPortWebSocketHandler({"/Stats"}, [&](client_ctx& client, web_packet& packet) -> websocket_callback_status {

        if(packet.OpCode != web_socket_opcode::TextFrame)
            return websocket_callback_status::ignore;

        web_packet response;
        response.OpCode = web_socket_opcode::TextFrame;

        string payload = string(packet.Payload.begin(), packet.Payload.end());
        if(payload.starts_with("/set_name")) {
            auto split = cpp::Split(payload, " ");
            if(split.size() > 1 && !split[1].empty()) {
                client.Name = split[1];
            }
            return websocket_callback_status::processed;
        } else if(payload.starts_with("/help")) {
            string msg = "/set_name [Name] --- Will set your public name.";
            response.SetPayloadFromString(msg);
            client.SendPacket(response);
            return websocket_callback_status::processed;
        }

        string msg = client.Name + ": " + payload;

        response.SetPayloadFromString(msg);
        server.SendAll(response, "/Stats");
        return websocket_callback_status::processed;
    });

    struct dynamic_page_ctx {
        bool show = false;
        int counter = 22;
    };

    server.AddPortWebSocketHandler({"/Dynamic"}, [&](client_ctx& client, web_packet& packet) {
        auto code = packet.GetPayloadAsString();
        if(code == "button_1_clicked") {
            auto& ctx = client.GetOrCreateUserData<dynamic_page_ctx>();
            ctx.counter++;
            client.SendPacket(web_packet::TextPacket(to_string(ctx.counter)));
        }
        return websocket_callback_status::processed;
    });

    cpp::StopWatch::CreateEventCallback(500ms, [&]() {
        string json = sys_info::GetAsJSON();
        web_packet packet;
        packet.OpCode = web_socket_opcode::TextFrame;
        packet.SetPayloadFromString(json);
        server.SendAll(packet, "/Stats");
        server.SendAll(packet, "/Dynamic");
        return cpp::StopWatchEvent::Continue;
    });

    while (g_ContinueRunning) {
        server.Serve();
    }

    return 0;
}
