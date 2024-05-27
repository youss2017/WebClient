//
// Created by youssef on 5/21/2024.
//

#ifndef WEBCLIENT_WEB_PACKET_H
#define WEBCLIENT_WEB_PACKET_H
#include <vector>
#include <optional>
#include <span>
#include <string>

/*
 *    Opcode:  4 bits
      Defines the interpretation of the "Payload data".  If an unknown
      opcode is received, the receiving endpoint MUST _Fail the
      WebSocket Connection_.  The following values are defined.
      *  %x0 denotes a continuation frame
      *  %x1 denotes a text frame
      *  %x2 denotes a binary frame
      *  %x3-7 are reserved for further non-control frames
      *  %x8 denotes a connection close
      *  %x9 denotes a ping
      *  %xA denotes a pong
      *  %xB-F are reserved for further control frames
 */
enum class web_socket_opcode {
    ContinuationFrame = 0x0,
    TextFrame = 0x1,
    BinaryFrame = 0x2,
    ConnectionCloseFrame = 0x8,
    PingFrame = 0x9,
    PongFrame = 0xA,
    ERROR
};

enum class web_packet_parse_code {
    complete = 0,
    missing_mask,
    missing_payload,
    error
};

struct web_packet {
    bool FinalFragment = true;
    bool RSV1 = false;
    bool RSV2 = false;
    bool RSV3 = false;
    bool MaskFlag = false;
    mutable bool IsMaskApplied = false;
    web_socket_opcode OpCode = web_socket_opcode::ERROR;
    uint8_t Mask[4] = {};
    uint16_t PayloadLength = 0;
    mutable std::vector<uint8_t> Payload;

    void SetPayloadFromString(const std::string& text);
    void ApplyMask() const;
    void EnsureUnmasked() const;
    std::string GetPayloadAsString() const;
    [[nodiscard]] std::vector<uint8_t> ToBinaryStream() const;
    static std::pair<std::optional<web_packet>, web_packet_parse_code> FromBinaryStream(const std::span<uint8_t>& data, uint32_t& offset);
    static web_packet_parse_code CompletePacketFromBinaryStream
                                 (const std::span<uint8_t>& data, uint32_t& offset,
                                 web_packet_parse_code previous_parse_code,
                                 web_packet& previous_packet);
    static web_packet GetPingPacket();

    static web_packet TextPacket(const std::string& text);
};


#endif //WEBCLIENT_WEB_PACKET_H
