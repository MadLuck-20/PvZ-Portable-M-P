#ifndef __NETWORK_PROTOCOL_H__
#define __NETWORK_PROTOCOL_H__

#include <cstddef>
#include <cstdint>
#include <vector>

// Wire format shared by all PvZ Portable multiplayer transports.
// This file deliberately has no SDL or platform-socket dependency.
namespace PvZNetwork
{
	constexpr uint32_t kMagic = 0x50565A4D; // "PVZM"
	constexpr uint16_t kProtocolVersion = 1;
	constexpr uint32_t kMaxPayloadBytes = 64 * 1024;
	constexpr uint32_t kHeaderBytes = 16;

	enum class MessageType : uint16_t
	{
		Hello = 1,
		HelloAck = 2,
		Input = 3,
		StateSnapshot = 4,
		Ping = 5,
		Pong = 6,
		Disconnect = 7
	};

	struct Message
	{
		MessageType type = MessageType::Ping;
		uint32_t sequence = 0;
		std::vector<uint8_t> payload;
	};

	// Encodes one complete frame. All integer fields are network byte order.
	bool Encode(const Message& message, std::vector<uint8_t>& output);

	// Decodes exactly one frame. Returns false for incomplete, malformed,
	// unsupported-version, or oversized frames; consumed is zero on failure.
	bool Decode(const uint8_t* data, size_t size, Message& output, size_t& consumed);

	// Input snapshots represent intent, not game memory. The host validates
	// and applies them through the normal P2 gameplay path.
	struct InputSnapshot
	{
		uint32_t buttons = 0;
		int16_t cursorX = 0;
		int16_t cursorY = 0;
		int8_t stickX = 0;
		int8_t stickY = 0;
	};

	bool EncodeInput(const InputSnapshot& input, std::vector<uint8_t>& output);
	bool DecodeInput(const uint8_t* data, size_t size, InputSnapshot& output);
}

#endif
