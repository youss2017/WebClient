//
// Created by youssef on 5/17/2024.
//

#include "web_server.h"
#include "CppUtility.hpp"
#include <iostream>
#include <thread>
#include <ranges>
#include <optional>
#include <string_view>
#include <algorithm>
using namespace std;

web_server::web_server(int port) {
    sw::Startup();

    m_server = sw::Socket(sw::SocketType::TCP);
    m_server.SetReuseAddrOption(true)
            .Bind(sw::SocketInterface::Any, port)
            .Listen(1024)
            .SetBlockingMode(false)
            .SetNagleAlgorthim(false);

#if 0
    thread ping_websockets([&] {
        while(m_server.IsValid()) {
            this_thread::sleep_for(chrono::seconds(5));
            PingWebSockets();
        }
    });
    ping_websockets.detach();
#endif
}

void web_server::Serve() {
    // Give chance to other threads to lock mutex
    this_thread::sleep_for(chrono::milliseconds(1));
    _safe_lock();
    try {
        AcceptClient();
        WaitForData(50);
        ProcessClients();
        RemoveDisconnectedClients();
        if (m_clients.empty()) {
            this_thread::sleep_for(chrono::milliseconds(10));
        }
    } catch (const exception& e) {
        LOG(ERR, "Server encountered an internal error, exception: {}", e.what());
    }
    _safe_unlock();
}

void web_server::AcceptClient() {
    sw::Socket connection = m_server.Accept();
    if (connection.IsValid()) {
        LOG(INFO, "{}, connected.", connection.GetEndpoint().ToString());
        connection.SetBlockingMode(false);
        client_ctx client;
        client.connection = connection;
        client.Name = connection.GetEndpoint().ToString();
        m_clients.push_back(client);
    }
}

void web_server::WaitForData(int32_t timeout) {
    if(m_clients.empty())
        return;
    vector<sw::Socket> connections(m_clients.size());
    size_t i = 0;
    for(auto& client : m_clients) {
        connections[i] = client.connection;
    }
    sw::Socket::WaitForData(connections, timeout);
}

void web_server::ProcessClients() {
    for (auto& client : m_clients) {
        uint8_t szHeader[config::MaxHeaderSize] = {};
        int32_t headerSize = client.connection.Recv(szHeader, config::MaxHeaderSize, false);
        span<uint8_t> header(szHeader, headerSize);
        // The smallest possible http request is the following:
        // GET / HTTP/1.1\r\n\r\n ---  18 characters
        if (headerSize + client.IncompleteRequest.size() >= 18 && !client.isWebsocket) {
            client.connectedTime = chrono::steady_clock::now();
            auto request = http_request::ParseHttpRequest(header);
            if(!request.has_value()) {
                continue;
            }
            HandleRequest(client, *request);
        } else if(headerSize > 0 && client.isWebsocket) {
            HandleWebSocketRequest(client, header);
        }
        else if (headerSize > 0) {
            // (TODO): Implement
        }
    }
}

void web_server::RemoveDisconnectedClients() {
    auto now = chrono::steady_clock::now();
    m_clients.remove_if([&](client_ctx& client) {
        if(!client.connection.IsConnected()) {
            LOG(INFO, "Client [{}] disconnected.\tTotal Client(s): {}", client.connection.GetEndpoint().ToString(), m_clients.size() - 1);
            client.connection.Disconnect().Close();
            return true;
        } else if(chrono::duration_cast<chrono::seconds>(now - client.connectedTime).count() > config::ClientTimeoutDuration) {
            LOG(INFO, "Closed connection to Client [{}] after {} seconds.\tTotal Client(s): {}", client.connection.GetEndpoint().ToString(), config::ClientTimeoutDuration, m_clients.size() - 1);
            client.connection.Disconnect().Close();
            return true;
        }
        return false;
    });
}

void web_server::SendErrorResponse(client_ctx &client, server_error_flag flag) {
    string body = cpp::Format("<h1 style='color: red;'><center>Bad Request -- {:8x}</center></h1>", (uint32_t)flag);
    http_response response;
    response.code = http_code::http_404_not_found;
    response.headers["Content-Type"] = "text/html";
    response.body = { body.begin(), body.end() };
    SendResponse(client, {}, response);
}

std::string web_server::GetMimeCode(const string &extension, const string &fallback) {
    static unordered_map<string, string> mime_mapping;
    if(mime_mapping.empty()) {
        mime_mapping[".html"] = "text/html";
        mime_mapping[".txt"] = "text/plain";
        mime_mapping[".css"] = "text/css";
        mime_mapping[".js"] = "text/javascript";
        mime_mapping[".ico"] = "image/x-icon";
        mime_mapping[".jpg"] = "image/jpeg";
        mime_mapping[".jpeg"] = "image/jpeg";
        mime_mapping[".png"] = "image/x-png";
        mime_mapping[".gif"] = "image/gif";
        mime_mapping[".svg"] = "image/svg+xml";
        mime_mapping[".ttf"] = "font/ttf";
        mime_mapping[".cpp"] = "text/x-c";
    }
    if(auto code = mime_mapping.find(extension); code != mime_mapping.end()) {
        return code->second;
    }
    return fallback;
}

void web_server::WebSocketHandshake
(client_ctx &client, http_request &request) {
    auto key = request.fields.at("Sec-WebSocket-Key");
    auto concat = key + config::WebSocketGUID;
    auto hash = cpp::SHA1::hash_words({ concat.begin(), concat.end() } );
    for(auto& word : hash) {
        // ensure big-endian
        word = sw::HostToNetworkOrder(word);
    }
    auto hash64 = cpp::Base64::Encode(reinterpret_cast<uint8_t*>(hash.data()), hash.size() * 4);

    http_response response;
    response.code = http_code::http_101_switch_protocol;
    response.headers = DefaultHeaders;
    response.headers["Upgrade"] = "websocket";
    response.headers["Connection"] = "Upgrade";
    response.headers["Sec-WebSocket-Accept"] = hash64;
    client.isWebsocket = true;
    client.WebSocketResource = request.resource;
    SendResponse(client, request, response);
}

std::optional<std::vector<uint8_t>>
web_server::LoadStaticAsset(const http_request &request, string &mime_code, http_code &code) {
    auto is_equal = [&](initializer_list<const char*> comparisons) {
        return ranges::any_of(comparisons,
                              [&](const char* item) { return cpp::EqualIgnoreCase(item, request.resource); });
    };
    code = http_code::http_404_not_found;
    optional<vector<uint8_t>> content;
    if(is_equal({"/", "/index.html"})) {
        mime_code = "text/html";
        content = cpp::ReadAllBytes("../wwwroot/index.html");
    } else {
        string path = "../wwwroot" + request.resource;
        content = cpp::ReadAllBytes(path);
        if(auto extOffset = cpp::LastIndexOf(request.resource, ".");
                extOffset != string::npos) {
            auto ext = cpp::LowerCase(request.resource.substr(extOffset));
            mime_code = GetMimeCode(ext, "application/octet-stream");
        }
    }
    if (!content) {
        auto _404page_template = cpp::ReadAllText("../wwwroot/404.html");
        if(_404page_template)
        {
            auto html_friendly_request = cpp::ReplaceAll(request.ToString(), "\n", "<br/>");
            auto _404page = cpp::Format(_404page_template.value(), html_friendly_request);
            content = { _404page.begin(), _404page.end() };
            mime_code = "text/html";
        }
    }
    else
        code = http_code::http_200_ok;
    return content;
}

void web_server::HandleRequest(client_ctx &client, http_request &request) {
    // rgb(25,82,99)
    if(request.fields.contains("Upgrade") &&
       request.fields.contains("Sec-WebSocket-Key") &&
       request.fields.at("Upgrade") == "websocket") {
        cout << "Client upgrading to websocket.\r\n";
        WebSocketHandshake(client, request);
        return;
    }

    // middle_ware?
    for(auto& http_callback : m_http_callbacks) {
        optional<http_response> response;
        auto status = http_callback(request, response);
        if(status == middleware_route_status::disconnect_client) {
            client.connection.Disconnect().Close();
            return;
        }
        if(status == middleware_route_status::default_response)
            continue;
        if(status == middleware_route_status::dynamic_response) {
            if(!response) {
                LOG(WARNING, "Middleware returned dynamic_response but response is empty, ignoring callback.");
                continue;
            }
            SendResponse(client, request, *response);
            return;
        }
    }

    string mime_code;
    http_code code;
    auto content = LoadStaticAsset(request, mime_code, code);
    http_response response;

    auto fallback = string("<h1>Internal Server Error</h1>");
    auto body = content.value_or(vector<uint8_t>{fallback.begin(), fallback.end()});
    if(mime_code.empty())
        mime_code = "text/html";
    response.code = code;
    response.headers["Content-Type"] = mime_code;
    response.body = body;
    SendResponse(client, request, response);
    LOG(INFOBOLD, "Client [{}] -> ({}) {}\t({}) {}", client.connection.GetEndpoint().ToString(), config::get_http_code(code), request.resource, mime_code,
        cpp::FriendlyMemorySize(body.size()));
}

std::string getCurrentDateTime() {
    // Get the current time
    std::time_t now_time_t = std::time(nullptr);

    // Convert to tm structure for local time
    std::tm local_tm = *std::localtime(&now_time_t);

    // Create a char array to hold the formatted date and time
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local_tm);

    return std::string(buffer);
}

void web_server::HandleWebSocketRequest(client_ctx &client, span<uint8_t> data) {
    // Is this a normal HTTP Request?
    const char* szVerbs[] = { "GET", "POST", "PUT", "PATCH", "DELETE" };
    for(int i = 0; (i < 5) && data.size() >= 3; i++) {
        if(memcmp(data.data(), szVerbs[i], 3) == 0) {
            assert(0); // TODO: Redirect for regular HTTP processing.
        }
    }

    uint32_t offset = 0;
    do {
        optional<web_packet> packet;
        web_packet_parse_code status;
        if(client.PreviousParseCode == web_packet_parse_code::complete) {
            auto pair = web_packet::FromBinaryStream(data, offset);
            packet = pair.first;
            status = pair.second;
        } else {
            LOG(WARNING, "Attempting to complete packet.");
            client.PreviousParseCode =
            web_packet::CompletePacketFromBinaryStream(data, offset, client.PreviousParseCode, client.IncompletePacket);
            packet = client.IncompletePacket;
            status = client.PreviousParseCode;
        }
        if (status == web_packet_parse_code::error || !packet) {
            client.connection.Disconnect();
            break;
        }

        if(status != web_packet_parse_code::complete) {
            LOG(WARNING, "Recv incomplete web-packet");
            client.IncompletePacket = packet.value();
            client.PreviousParseCode = status;
            break;
        }

        for(const auto& callback : m_websocket_callbacks) {
            auto callback_status = callback(client, *packet);
            if(callback_status == websocket_callback_status::processed)
                return;
        }

        if(packet->OpCode == web_socket_opcode::ConnectionCloseFrame) {
            LOG(INFOBOLD, "{} (WebSocket) sent disconnection packet.", client.connection.GetEndpoint().ToString());
            client.connection.Disconnect();
        } else if(packet->OpCode == web_socket_opcode::PingFrame) {
            web_packet pong = *packet;
            pong.EnsureUnmasked();
            pong.MaskFlag = false;
            pong.OpCode = web_socket_opcode::PongFrame;
            client.SendPacket(pong);
        } else if(packet->OpCode == web_socket_opcode::PongFrame) {
            // pong, we have recved pong, what do we do now?
        }

    } while(offset < data.size());
}

void web_server::PingWebSockets() {
    _safe_lock();
    for(auto& client : m_clients) {
        if(!client.isWebsocket)
            continue;
        web_packet packet = web_packet::GetPingPacket();
        auto stream = packet.ToBinaryStream();
        client.connection.Send(stream.data(), int32_t(stream.size()));
        LOG(INFO, "Pinging {}", client.connection.GetEndpoint().ToString());
    };
    _safe_unlock();
}

void web_server::AddHttpHandler(const middleware_callback &&callback)
{
    m_http_callbacks.push_back(callback);
}

void web_server::SendResponse(client_ctx& client, const http_request& request, http_response& response) {
    for(const auto& postprocess : m_postprocess_http) {
        postprocess(request, response);
    }
    auto header = response.HeaderToString();
    client.connection.Send(header);
    if (response.body) {
        client.connection.Send(response.body->data(), (int32_t)response.body->size());
    }
}

void web_server::AddWebSocketHandler(const web_server::websocket_callback &&callback) {
    m_websocket_callbacks.push_back(callback);
}

void web_server::SendAll(const web_packet &packet, const std::string& specific_port) {
    _safe_lock();
    auto stream = packet.ToBinaryStream();
    for(auto& client : m_clients) {
        if(!client.isWebsocket)
            continue;
        if(!specific_port.empty() &&
            specific_port != client.WebSocketResource)
            continue;
        client.connection.Send(stream.data(), (int32_t)stream.size());
    }
    _safe_unlock();
}

void web_server::_safe_lock() {
    if(m_current_lock_holder == cpp::GetCurrentThreadId())
        return;
    m_clients_lock.lock();
    m_current_lock_holder = cpp::GetCurrentThreadId();
}

void web_server::_safe_unlock() {
    if(m_current_lock_holder != cpp::GetCurrentThreadId())
        return;
    m_current_lock_holder = 0;
    m_clients_lock.unlock();
}

void web_server::AddHttpRouteHandler(const std::vector<std::string> &route,
                                     const middleware_callback &&callback,
                                     bool case_sensitive) {
    if(route.empty())
        return;
    AddHttpHandler([=](http_request &request, std::optional<http_response> &response) {
        bool is_route = ranges::any_of(route, [&](const auto &user_route) {
            if (case_sensitive)
                return user_route == request.resource;
            return cpp::EqualIgnoreCase(user_route, request.resource);
        });
        return is_route ? callback(request, response) : middleware_route_status::default_response;
    });
    (void)case_sensitive;
}

void web_server::AddPostProcess(const web_server::postprocess_callback &&callback) {
    m_postprocess_http.push_back(callback);
}

void web_server::AddRoutePostProcess(const vector<std::string> &route,
                                     const postprocess_callback &&callback,
                                     bool case_sensitive) {
    if(route.empty())
        return;
    AddPostProcess([=](const http_request &request, http_response& response) {
        bool is_route = ranges::any_of(route, [&](const auto &user_route) {
            if (case_sensitive)
                return user_route == request.resource;
            return cpp::EqualIgnoreCase(user_route, request.resource);
        });
        if(is_route)
            callback(request, response);
    });
    (void)case_sensitive;
}

void web_server::AddPortWebSocketHandler(const vector<std::string> &port, const web_server::websocket_callback &&callback,
                                    bool case_sensitive) {
    if(port.empty())
        return;
    AddWebSocketHandler([=](client_ctx& client, web_packet& packet) -> websocket_callback_status {
        bool is_route = ranges::any_of(port, [&](const auto &user_route) {
            if (case_sensitive)
                return user_route == client.WebSocketResource;
            return cpp::EqualIgnoreCase(user_route, client.WebSocketResource);
        });
        if(is_route)
            callback(client, packet);
        return websocket_callback_status::ignore;
    });
    (void)case_sensitive;
}

void client_ctx::SendPacket(const web_packet &packet) {
    auto stream = packet.ToBinaryStream();
    connection.Send(stream.data(), (int32_t)stream.size());
}
