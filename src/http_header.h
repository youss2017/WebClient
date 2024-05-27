//
// Created by youssef on 5/21/2024.
//

#ifndef WEBCLIENT_HTTP_HEADER_H
#define WEBCLIENT_HTTP_HEADER_H
#include <optional>
#include <unordered_map>
#include <string>

enum class http_code {
    http_200_ok = 200,
    http_400_bad_request = 400,
    http_404_not_found = 404,
    http_101_switch_protocol = 101,
};

namespace config {
    constexpr size_t MaxHeaderSize = 1024 * 8;
    constexpr size_t MaxHeaderContentSize = 1024 * 128; // 128 kb
    constexpr const char* WebSocketGUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    constexpr const char* get_http_code(http_code code) {
        switch(code) {
            case http_code::http_200_ok: return "200 OK";
            case http_code::http_400_bad_request: return "400 Bad Request";
            case http_code::http_404_not_found: return "404 Not Found";
            case http_code::http_101_switch_protocol: return "101 Switching Protocols";
            default: return "404 Not Found.";
        }
    }
    constexpr size_t ClientTimeoutDuration = 3600; // seconds
}

enum class http_verb {
    GET, PUT, POST, PATCH, DELETE,
    ERROR
};

class http_request {
public:
    http_verb verb = http_verb::ERROR;
    std::string resource;
    std::unordered_map<std::string, std::string> query;
    std::unordered_map<std::string, std::string> fields;
    std::optional<std::vector<uint8_t>> content;

    [[nodiscard]] std::string ToString() const;
    static std::optional<http_request> ParseHttpRequest(const std::span<uint8_t>& header);
    static std::pair<std::string, std::unordered_map<std::string, std::string>> ParseHttpResource(const std::string_view& resource);

private:
    static http_verb FetchHttpVerb(const std::string_view& word);
    /*               resource without query, and query */
    static std::string RemoveDirectoryChange(const std::string& source);
};

struct http_response {
    http_code code = http_code::http_200_ok;
    std::unordered_map<std::string, std::string> headers;
    std::optional<std::vector<uint8_t>> body;

    void SetBody(const std::string& text);

    [[nodiscard]] std::string HeaderToString() const;
};

#endif //WEBCLIENT_HTTP_HEADER_H
