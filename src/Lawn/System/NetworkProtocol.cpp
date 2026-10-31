#include "NetworkProtocol.h"
#include <limits>

namespace PvZNetwork
{
	namespace
	{
		void Put16(std::vector<uint8_t>& out, uint16_t v)
		{
			out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
			out.push_back(static_cast<uint8_t>(v & 0xff));
		}
		void Put32(std::vector<uint8_t>& out, uint32_t v)
		{
			out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
			out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
			out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
			out.push_back(static_cast<uint8_t>(v & 0xff));
		}
		uint16_t Get16(const uint8_t* p)
		{
			return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
		}
		uint32_t Get32(const uint8_t* p)
		{
			return (static_cast<uint32_t>(p[0]) << 24) |
				(static_cast<uint32_t>(p[1]) << 16) |
				(static_cast<uint32_t>(p[2]) << 8) | p[3];
		}
	}

	bool Encode(const Message& message, std::vector<uint8_t>& output)
	{
		if (message.payload.size() > kMaxPayloadBytes ||
			static_cast<uint16_t>(message.type) < 1 || static_cast<uint16_t>(message.type) > 7)
			return false;
		output.clear();
		output.reserve(kHeaderBytes + message.payload.size());
		Put32(output, kMagic);
		Put16(output, kProtocolVersion);
		Put16(output, static_cast<uint16_t>(message.type));
		Put32(output, static_cast<uint32_t>(message.payload.size()));
		Put32(output, message.sequence);
		output.insert(output.end(), message.payload.begin(), message.payload.end());
		return true;
	}

	bool Decode(const uint8_t* data, size_t size, Message& output, size_t& consumed)
	{
		consumed = 0;
		if (data == nullptr || size < kHeaderBytes || Get32(data) != kMagic ||
			Get16(data + 4) != kProtocolVersion)
			return false;
		const uint16_t type = Get16(data + 6);
		const uint32_t payloadSize = Get32(data + 8);
		if (type < 1 || type > 7 || payloadSize > kMaxPayloadBytes ||
			size < kHeaderBytes + static_cast<size_t>(payloadSize))
			return false;
		output.type = static_cast<MessageType>(type);
		output.sequence = Get32(data + 12);
		output.payload.assign(data + kHeaderBytes, data + kHeaderBytes + payloadSize);
		consumed = kHeaderBytes + payloadSize;
		return true;
	}

	bool EncodeInput(const InputSnapshot& input, std::vector<uint8_t>& output)
	{
		output.clear();
		output.reserve(10);
		Put32(output, input.buttons);
		Put16(output, static_cast<uint16_t>(input.cursorX));
		Put16(output, static_cast<uint16_t>(input.cursorY));
		output.push_back(static_cast<uint8_t>(input.stickX));
		output.push_back(static_cast<uint8_t>(input.stickY));
		return true;
	}

	bool DecodeInput(const uint8_t* data, size_t size, InputSnapshot& output)
	{
		if (data == nullptr || size != 10)
			return false;
		output.buttons = Get32(data);
		output.cursorX = static_cast<int16_t>(Get16(data + 4));
		output.cursorY = static_cast<int16_t>(Get16(data + 6));
		output.stickX = static_cast<int8_t>(data[8]);
		output.stickY = static_cast<int8_t>(data[9]);
		return true;
	}
}
