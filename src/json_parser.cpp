//
// Created by youssef on 5/15/2024.
//

#include "json_parser.h"
#include <string_view>
using namespace std;

static string_view read_word(const string& szBuffer, size_t& offset, const char* szStartDelimiter, const char* szEndDelimiter) {
    auto is_valid_character = [](char c, const char* delimiter) {
        while(*delimiter) {
            if(*delimiter == c)
                return true;
            delimiter++;
        }
        return false;
    };
    bool found_first_letter = false;
    size_t start = offset;
    if(start == szBuffer.size())
        return {};
    for(size_t i = offset; i < szBuffer.size(); i++) {
        if(!found_first_letter) {
            // is this a character
            if(is_valid_character(szBuffer[i], szStartDelimiter))  {
                found_first_letter = true;
                start = i;
            }
        } else {
            if(is_valid_character(szBuffer[i], szEndDelimiter)) {
                offset = i + 1;
                return { &szBuffer[start], i - start };
            }
        }
    }
    offset = szBuffer.size();
    return { &szBuffer[start], szBuffer.size() - start };
};

std::unordered_map<std::string, std::string> json_parser::parse(const std::string &json) {

    enum class ParserState {
        FetchingFieldName,
        FetchingFieldValue
    };
    std::unordered_map<std::string, std::string> result;

    string fieldName, fieldValue;

    ParserState state = ParserState::FetchingFieldName;
    size_t offset = 0;
    do {
        string_view word;
        if(state == ParserState::FetchingFieldName) {
            word = read_word(json, offset, "\"", "\"");
        } else {// if(state == ParserState::FetchingFieldValue) {
            word = read_word(json, offset, ":", ",}");
        }
        if(word.empty())
            break;

        if(state == ParserState::FetchingFieldName) {
            fieldName = word;
            state = ParserState::FetchingFieldValue;
        } else {
            fieldValue = word;
            result[fieldName] = fieldValue;
            fieldName = "";
            fieldValue = "";
            state = ParserState::FetchingFieldName;
        }

    } while (true);

    return result;
}
