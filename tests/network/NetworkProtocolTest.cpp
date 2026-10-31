#include "../../src/Lawn/System/NetworkProtocol.h"
#include <cassert>
#include <cstdint>
#include <vector>

int main()
{
	using namespace PvZNetwork;
	Message source;
	source.type = MessageType::Input;
	source.sequence = 0x12345678;
	source.payload = { 1, 2, 3, 4, 5 };
	std::vector<uint8_t> bytes;
	assert(Encode(source, bytes));
	Message decoded;
	size_t consumed = 0;
	assert(Decode(bytes.data(), bytes.size(), decoded, consumed));
	assert(consumed == bytes.size());
	assert(decoded.type == source.type && decoded.sequence == source.sequence);
	assert(decoded.payload == source.payload);

	// Partial TCP frames must not be accepted as complete messages.
	assert(!Decode(bytes.data(), bytes.size() - 1, decoded, consumed));
	assert(consumed == 0);

	// Input snapshots preserve signed cursor/axis values.
	InputSnapshot input;
	input.buttons = 0xA5A55A5A;
	input.cursorX = -120;
	input.cursorY = 840;
	input.stickX = -100;
	input.stickY = 99;
	assert(EncodeInput(input, bytes));
	InputSnapshot roundTrip;
	assert(DecodeInput(bytes.data(), bytes.size(), roundTrip));
	assert(roundTrip.buttons == input.buttons);
	assert(roundTrip.cursorX == input.cursorX && roundTrip.cursorY == input.cursorY);
	assert(roundTrip.stickX == input.stickX && roundTrip.stickY == input.stickY);
	assert(!DecodeInput(bytes.data(), bytes.size() - 1, roundTrip));
	return 0;
}
