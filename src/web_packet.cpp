//
// Created by youssef on 5/21/2024.
//

#include "web_packet.h"
#include "CppUtility.hpp"
using namespace std;

void web_packet::ApplyMask() const {
    if(!MaskFlag)
        return;
    for (size_t i = 0; i < Payload.size(); ++i) {
        Payload[i] ^= Mask[i % 4];
    }
    IsMaskApplied = !IsMaskApplied;
}

std::vector<uint8_t> web_packet::ToBinaryStream() const {

    if(OpCode == web_socket_opcode::ERROR) {
        throw runtime_error("Invalid WebSocket OpCode.");
    }

    vector<uint8_t> stream;
    stream.reserve(2 + Payload.size() + 2 + 8);

    uint8_t byte0 = (FinalFragment << 7) |
                    (RSV1 << 6) |
                    (RSV2 << 5) |
                    (RSV3 << 4) |
                    (uint8_t(OpCode) & 0x0F);

    stream.push_back(byte0);

    if(Payload.size() < 126) {
        stream.push_back(uint8_t(Payload.size()) | (MaskFlag << 7));
    } else if(Payload.size() < UINT16_MAX) {
        stream.push_back(126 | (MaskFlag << 7));
        stream.push_back(uint16_t(Payload.size()) >> 8);
        stream.push_back(uint16_t(Payload.size()) & 0x00FF);
    } else {
        stream.push_back(127 | (MaskFlag << 7));
        assert(0 && "Not Implemented.");
    }

    if(MaskFlag) {
        for(uint8_t item : Mask) {
            stream.push_back(item);
        }
    }

    if(!Payload.empty()) {
        if(!IsMaskApplied)
            ApplyMask();
        for(uint8_t item : Payload) {
            stream.push_back(item);
        }
    }

    return std::move(stream);
}

std::pair<std::optional<web_packet>, web_packet_parse_code>
web_packet::FromBinaryStream(const span<uint8_t> &data, uint32_t& index)
{
    web_packet result;
    /*
           0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-------+-+-------------+-------------------------------+
     |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
     |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
     |N|V|V|V|       |S|             |   (if payload len==126/127)   |
     | |1|2|3|       |K|             |                               |
     +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
     |     Extended payload length continued, if payload len == 127  |
     + - - - - - - - - - - - - - - - +-------------------------------+
     |                               |Masking-key, if MASK set to 1  |
     +-------------------------------+-------------------------------+
     | Masking-key (continued)       |          Payload Data         |
     +-------------------------------- - - - - - - - - - - - - - - - +
     :                     Payload Data continued ...                :
     + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
     |                     Payload Data continued ...                |
     +---------------------------------------------------------------+
     */
    result.FinalFragment = data[index] & 0b10000000;
    result.RSV1 = data[index] & 0b01000000;
    result.RSV2 = data[index] & 0b00100000;
    result.RSV3 = data[index] & 0b00010000;
    result.MaskFlag = data[index + 1] & 0b10000000;
    result.OpCode = web_socket_opcode(data[index] & 0b00001111); // only-4 bits max
    result.PayloadLength = data[index + 1] & 0b01111111; // only 7-bit max

    // skip 2 bytes
    index += 2;

    if (result.PayloadLength > 1024) {
        // payload too large, close connection
        LOG(ERR, "WebSocket Packet Error, payload too large. Closing Connection.");
        return { {}, web_packet_parse_code::error };
    }

    if (result.PayloadLength == 126) {
        // 16-bit length (next 2-bytes)
        result.PayloadLength = uint16_t(data[2]) << 8 | data[3];
        index += 2;
    } else if (result.PayloadLength == 127) {
        // 64-bit length (next 8-bytes)
        // bytes are in network order (big endian)
        for (int i = 0; i < 8; i++) {
            result.PayloadLength = (result.PayloadLength << 8) | data[index++];
        }
    }

    if (result.MaskFlag) {
        if(data.size() < index+4) {
            return { result, web_packet_parse_code::missing_mask };
        }
        int counter = 0;
        for(size_t i = index; i < index + 4; i++) {
            result.Mask[counter++] = data[i];
        }
        index += 4;
    }

    if (!result.PayloadLength) {
        return { result, web_packet_parse_code::complete };
    }

    result.IsMaskApplied = true;
    // rest is data, copy into buffer
    if(result.PayloadLength <= data.size() - index) {
        result.Payload.resize(result.PayloadLength);
        memcpy(result.Payload.data(), &data[index], result.PayloadLength);
        result.ApplyMask();
        index += result.PayloadLength;
    } else if(data.size() - index > 0) {
        size_t recvPayloadSize = data.size() - index;
        result.Payload.resize(recvPayloadSize);
        memcpy(result.Payload.data(), &data[index], recvPayloadSize);
        index += recvPayloadSize;
        return { result, web_packet_parse_code::missing_payload };
    } else {
        return { result, web_packet_parse_code::missing_payload };
    }

    return { result, web_packet_parse_code::complete };
}

void web_packet::EnsureUnmasked() const {
    if(!MaskFlag)
        return;
    if(IsMaskApplied)
        ApplyMask();
}

web_packet web_packet::GetPingPacket() {
    web_packet packet;
    packet.OpCode = web_socket_opcode::PingFrame;
    return packet;
}

void web_packet::SetPayloadFromString(const string &text) {
    Payload = { text.begin(), text.end() };
}

web_packet_parse_code
web_packet::CompletePacketFromBinaryStream(const span<uint8_t> &data, uint32_t &index,
                                           web_packet_parse_code previous_parse_code, web_packet &previous_packet) {
    if(previous_parse_code == web_packet_parse_code::complete)
        return web_packet_parse_code::complete;
    if(previous_parse_code == web_packet_parse_code::missing_mask) {
        if(!previous_packet.MaskFlag)
            return web_packet_parse_code::error;
        // The data stream still does not have the mask, therefore, we cancel the parsing
        if(data.size() <= index+4)
            return web_packet_parse_code::error;
        int counter = 0;
        for(size_t i = index; i < index + 4; i++) {
            previous_packet.Mask[counter++] = data[i];
        }
        index += 4;
        if(previous_packet.PayloadLength > 0)
            previous_parse_code = web_packet_parse_code::missing_payload;
        else
            return web_packet_parse_code::complete;
    }
    if(previous_parse_code == web_packet_parse_code::missing_payload) {
        for(size_t counter = previous_packet.Payload.size();
            counter < previous_packet.PayloadLength &&
            counter < data.size();
            counter++) {
            previous_packet.Payload.push_back(data[counter]);
        }
        if(previous_packet.Payload.size() != previous_packet.PayloadLength)
            return web_packet_parse_code::missing_payload;
        previous_packet.ApplyMask();
        return web_packet_parse_code::complete;
    }
    return web_packet_parse_code::error;
}

std::string web_packet::GetPayloadAsString() const {
    return { Payload.begin(), Payload.end() };
}

web_packet web_packet::TextPacket(const string &text) {
    web_packet packet;
    packet.OpCode = web_socket_opcode::TextFrame;
    packet.SetPayloadFromString(text);
    return packet;
}

