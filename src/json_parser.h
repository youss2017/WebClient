//
// Created by youssef on 5/15/2024.
//

#ifndef WEBCLIENT_JSON_PARSER_H
#define WEBCLIENT_JSON_PARSER_H
#include <unordered_map>
#include <string>

namespace json_parser {

    std::unordered_map<std::string, std::string> parse(const std::string& json);

}


#endif //WEBCLIENT_JSON_PARSER_H
