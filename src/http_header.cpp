//
// Created by youssef on 5/21/2024.
//
#include "web_server.h"
#include "CppUtility.hpp"
#include "http_header.h"

using namespace std;

http_verb http_request::FetchHttpVerb(const string_view &word) {
    if (word == "GET") return http_verb::GET;
    if (word == "POST") return http_verb::POST;
    if (word == "DELETE") return http_verb::DELETE;
    if (word == "PUT") return http_verb::PUT;
    if (word == "PATCH") return http_verb::PATCH;
    return http_verb::ERROR;
}

string http_request::ToString() const {
    const char* szVerbs[] = {
            "GET", "PUT", "POST", "PATCH", "DELETE", "ERROR"
    };
    std::stringstream ss;
    ss << szVerbs[static_cast<int>(verb)] << " " << resource << "\r\n";
    for(const auto& [name, value] : query) {
        ss << name << '=' << value << '&';
    }
    ss.seekp(-1, stringstream::cur);
    ss << endl;
    for(const auto& [name, value] : fields) {
        ss << name << ": " << value << endl;
    }
    if(content) {
        ss << "Content (Body): " << content->size() << " (bytes)" << endl;
    } else {
        ss << "No Body" << endl;
    }
    return ss.str();
}

std::optional<http_request> http_request::ParseHttpRequest(const std::span<uint8_t>& szHeader ) {
    // HTTP Header ends with two CRLF (\r\n)
    // The first line is the resource line
    size_t offset = 0;
    string_view header = { (const char*)szHeader.data(), szHeader.size() };
    auto next_word = [&](bool& eol) -> string_view {
        size_t word_length = 0;
        eol = false;
        size_t start = offset;
        for (size_t i = offset; i < header.size() - 2 /* the minus two ensure we never read outside of array */; i++) {
            const char c1 = header[i + 1];
            const char c2 = header[i + 2];
            if (header[i] == ' ') {
                offset = i + 1;
                // we have meet the end
                // Are we at the end of the line? If so the next character should be \r or \n
                if (c1 == '\r' || c1 == '\n') {
                    // Is request using CRLF? If so then append by two otherwise only one
                    offset = i + (c2 == '\n' ? 2 : 1);
                    eol = true;
                }
                return { &header[start], word_length };
            }
            if (header[i] == '\r' || header[i] == '\n') {
                // Is request using CRLF? If so then append by two otherwise only one
                offset = i + (c1 == '\n' ? 2 : 1);
                eol = true;
                return { &header[start], word_length };
            }
            // is this a valid character
            // (TODO): Add check
            word_length++;
        }
        return {};
    };

    enum class ParserState {
        FetchingHttpVerb,
        FetchingHttpResource,
        FetchingHttpVersion,
        FetchingFieldName,
        FetchingFieldContent
    };

    http_request request;
    auto state = ParserState::FetchingHttpVerb;
    string current_field;

    do {
        bool eol;
        auto word = next_word(eol);
        if (word.empty())
            break;

        switch (state) {
            case ParserState::FetchingHttpVerb:
                request.verb = FetchHttpVerb(word);
                if (request.verb == http_verb::ERROR) {
                    return {};
                }
                state = ParserState::FetchingHttpResource;
                break;
            case ParserState::FetchingHttpResource: {
                auto [resource, query] = ParseHttpResource(word);
                request.resource = RemoveDirectoryChange(resource);
                request.query = query;
                if(request.resource.empty())
                    request.resource = "/";
                state = ParserState::FetchingHttpVersion;
                break;
            }
            case ParserState::FetchingHttpVersion:
                // we do not use version
                state = ParserState::FetchingFieldName;
                break;
            case ParserState::FetchingFieldName:
                if(word.length() > 1) {
                    word = word.substr(0, word.length() - 1);
                }
                current_field = word;
                state = ParserState::FetchingFieldContent;
                break;
            case ParserState::FetchingFieldContent:
                auto& value = request.fields[current_field];
                if (value.empty()) {
                    value = word;
                }
                else {
                    value.push_back(' ');
                    value.append(word);
                }
                if (eol) {
                    current_field = "";
                    state = ParserState::FetchingFieldName;
                }
                break;
        }

    } while (true);
    offset += 2;

    if (offset < header.size()) {
        // body content
        vector<uint8_t> content(header.size() - offset);
        memcpy(content.data(), &szHeader[offset], content.size());
        request.content = std::move(content);
        LOG(WARNING, "{}", string(request.content->begin(), request.content->end()));
    }

    return { request };
}

std::pair<std::string, std::unordered_map<std::string, std::string>>
http_request::ParseHttpResource(const string_view &resource) {
    auto delimIndex = resource.find('?');
    if(delimIndex == string::npos)
        return { string(resource), {} };
    if(delimIndex == resource.size() - 1)
        return { string(resource.substr(1, resource.size() - 2)), {} };

    string_view pure_resource = { &resource.at(0), delimIndex };
    string_view query_string = { &resource.at(delimIndex + 1), resource.size() - delimIndex - 1 };
    unordered_map<string, string> query;

   do {
        auto queryDelimIndex = query_string.find('=');
        if(queryDelimIndex == query_string.size() - 1 || queryDelimIndex == 0) {
            if(query_string.size() == 1)
                break;
            query_string = query_string.substr(1);
            continue;
        }
        if(queryDelimIndex == string_view::npos) {
            if(!query_string.empty()) {
                query[string(query_string)] = "";
            }
            break;
        }
        if(query_string[0] == '&') {
            if(query_string.size() == 1)
                break;
            query_string = query_string.substr(1);
            continue;
        }
        string_view query_name = query_string.substr(0, queryDelimIndex);

        auto contentDelimIndex = query_string.find('&');

        if(contentDelimIndex == string_view::npos) {
            if(query_string.size() - (queryDelimIndex + 1) > 0)
                query[string(query_name)] = query_string.substr(queryDelimIndex + 1);
            else
                query[string(query_name)] = "";
            break;
        }

        query[string(query_name)] = query_string.substr(queryDelimIndex + 1, contentDelimIndex - (queryDelimIndex + 1));
        query_string = query_string.substr(contentDelimIndex + 1);

    } while(true);

    return { string(pure_resource), query };
}

std::string http_request::RemoveDirectoryChange(const string &source) {
    return cpp::ReplaceAll(
            cpp::ReplaceAll(
                    cpp::ReplaceAll(string(source), "\\", "/"),
                    "../", ""),
            "./", "");
}

string http_response::HeaderToString() const {
    stringstream response;
    response << "HTTP/1.1 " << config::get_http_code(code) << "\r\n";
    for(const auto& [name, content] : headers) {
        response << name << ": " << content << "\r\n";
    }
    if(body) {
        response << "Content-Length: " << body->size() << "\r\n";
    }
    response << "\r\n";
    return response.str();
}

void http_response::SetBody(const string &text) {
    body = { text.begin(), text.end() };
}
