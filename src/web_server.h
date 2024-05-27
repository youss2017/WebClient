//
// Created by youssef on 5/17/2024.
//

#ifndef WEBCLIENT_WEB_SERVER_H
#define WEBCLIENT_WEB_SERVER_H
#include "Socket.hpp"
#include <optional>
#include <unordered_map>
#include <chrono>
#include <span>
#include <mutex>
#include <functional>
#include "http_header.h"
#include "web_packet.h"

enum class server_error_flag {
    MalformedHTTPRequest,
    HTTPHeaderTooLarge,
    HTTPContentTooLarge
};

enum class middleware_route_status {
    dynamic_response,
    default_response,
    disconnect_client
};

enum class websocket_callback_status {
    processed,
    ignore
};

struct client_ctx_user_data {

};

struct client_ctx {
    sw::Socket connection;
    bool isWebsocket = false;
    std::string WebSocketResource;
    std::string Name;
    std::chrono::steady_clock::time_point connectedTime = std::chrono::steady_clock::now();
    std::vector<uint8_t> IncompleteRequest;
    web_packet IncompletePacket;
    web_packet_parse_code PreviousParseCode = web_packet_parse_code::complete;

    void SendPacket(const web_packet& packet);

    client_ctx() = default;

    client_ctx(const client_ctx& copy) noexcept {
        connection = copy.connection;
        isWebsocket = copy.isWebsocket;
        WebSocketResource = copy.WebSocketResource;
        Name = copy.Name;
        connectedTime = copy.connectedTime;
        IncompleteRequest = copy.IncompleteRequest;
        IncompletePacket = copy.IncompletePacket;
        PreviousParseCode = copy.PreviousParseCode;
        m_user_defined_data = copy.m_user_defined_data;
    }

    ~client_ctx() {
        m_destroy_user_data_object();
    }

    template<class T>
    T& GetOrCreateUserData()
    {
        if(!m_user_defined_data) {
            m_user_defined_data = std::make_shared<uint8_t[]>(sizeof(T));
            (void) new(m_user_defined_data.get()) T();

            m_destroy_user_data_object = [&]() {
                auto object = (T*)m_user_defined_data.get();
                object->~T();
            };
        }
        return *(T*)m_user_defined_data.get();
    }

    // Manually release user-data object (the object will also be released when client_ctx deconstructor is invoked,
    // which occurs when client is disconnected).
    void ClearUserDataObject() {
        m_destroy_user_data_object();
        m_user_defined_data = nullptr;
    }

private:
    std::function<void()> m_destroy_user_data_object = []() {};
    std::function<void()> m_copy_object = []() {};
    std::function<void()> m_move_object = []() {};
    std::function<void()> m_assignment_object = []() {};
    std::shared_ptr<uint8_t[]> m_user_defined_data = nullptr;
};

class web_server {

    using middleware_callback = std::function<middleware_route_status(http_request& request,
                                                                      std::optional<http_response>& response)>;

    using websocket_callback = std::function<websocket_callback_status(client_ctx& client, web_packet& packet)>;

    using postprocess_callback = std::function<void(const http_request& request, http_response& response)>;

public:
    explicit web_server(int port = 80);

    void Serve();
    void PingWebSockets();

    void SendAll(const web_packet& packet, const std::string& specific_port = "");

    void AddHttpHandler(const middleware_callback &&callback);
    void AddHttpRouteHandler(const std::vector<std::string> &route, const middleware_callback &&callback, bool case_sensitive = true);
    void AddPostProcess(const postprocess_callback&& callback);
    void AddRoutePostProcess(const std::vector<std::string> &route, const postprocess_callback &&callback, bool case_sensitive = true);

    void AddWebSocketHandler(const websocket_callback&& callback);
    void AddPortWebSocketHandler(const std::vector<std::string> &port, const websocket_callback&& callback, bool case_sensitive = true);

    static std::string GetMimeCode(const std::string& extension, const std::string& fallback);

public:
    std::unordered_map<std::string, std::string> DefaultHeaders;

private:
    void AcceptClient();
    void WaitForData(int32_t timeout);
    void ProcessClients();
    void RemoveDisconnectedClients();

    void SendErrorResponse(client_ctx& client, server_error_flag flag);
    static std::optional<std::vector<uint8_t>> LoadStaticAsset(const http_request& comparisons, std::string& mime_code, http_code& code);
    void WebSocketHandshake(client_ctx& client, http_request& request);
    void SendResponse(client_ctx& client, const http_request& request, http_response& response);
    void HandleRequest(client_ctx& client, http_request& request);
    void HandleWebSocketRequest(client_ctx& ctx, std::span<uint8_t> data);

private:
    void _safe_lock();
    void _safe_unlock();

private:
    sw::Socket m_server;
    std::mutex m_clients_lock;
    volatile uint64_t m_current_lock_holder = 0;
    std::list<middleware_callback> m_http_callbacks;
    std::list<websocket_callback> m_websocket_callbacks;
    std::list<postprocess_callback> m_postprocess_http;
    std::list<client_ctx> m_clients;
};


#endif //WEBCLIENT_WEB_SERVER_H
