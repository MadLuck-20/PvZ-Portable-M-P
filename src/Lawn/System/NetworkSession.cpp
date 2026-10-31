#include "NetworkSession.h"
#include "NetworkProtocol.h"
#include "NetworkTransport.h"
#include "Player2Input.h"

#include <SDL.h>
#include <algorithm>
#include <cstdint>
#include <string>

namespace PvZNetwork
{
namespace
{
	Transport gTransport;
	bool gReady = false;
	bool gHelloSent = false;
	uint32_t gSendSequence = 1;
	uint32_t gLastInputSequence = 0;
	std::string gStatus = "Offline";

	constexpr uint32_t kLeftTriggerBit = uint32_t(1) << 30;
	constexpr uint32_t kRightTriggerBit = uint32_t(1) << 31;

	void SetStatus(const char* status) { gStatus = status; }

	bool SendSimple(MessageType type)
	{
		Message message;
		message.type = type;
		message.sequence = gSendSequence++;
		return gTransport.Send(message);
	}

	void ClearRemoteInput()
	{
		Player2Input::SetRemoteInput(false, 0, 0, 0, 0.0f, 0.0f, false, false);
	}

	void SendLocalInput()
	{
		InputSnapshot input;
		for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX && i < 30; ++i)
			if (Player2Input::IsDown(static_cast<SDL_GameControllerButton>(i)))
				input.buttons |= uint32_t(1) << i;
		if (Player2Input::IsTriggerDown(true)) input.buttons |= kLeftTriggerBit;
		if (Player2Input::IsTriggerDown(false)) input.buttons |= kRightTriggerBit;
		input.cursorX = static_cast<int16_t>(std::clamp(Player2Input::GetX(), -32768, 32767));
		input.cursorY = static_cast<int16_t>(std::clamp(Player2Input::GetY(), -32768, 32767));
		float stickX = 0.0f, stickY = 0.0f;
		Player2Input::GetLeftStick(stickX, stickY);
		input.stickX = static_cast<int8_t>(std::clamp(static_cast<int>(stickX * 127.0f), -127, 127));
		input.stickY = static_cast<int8_t>(std::clamp(static_cast<int>(stickY * 127.0f), -127, 127));

		Message message;
		message.type = MessageType::Input;
		message.sequence = gSendSequence++;
		if (!EncodeInput(input, message.payload) || !gTransport.Send(message))
			SetStatus("Connected (input queue full)");
	}
}

bool StartHost(uint16_t port, std::string* error)
{
	Stop();
	if (!gTransport.StartHost(port, error))
	{
		gStatus = error ? *error : gTransport.GetLastError();
		return false;
	}
	gStatus = "Waiting for player";
	return true;
}

bool Connect(const std::string& address, uint16_t port, std::string* error)
{
	Stop();
	if (!gTransport.Connect(address, port, error))
	{
		gStatus = error ? *error : gTransport.GetLastError();
		return false;
	}
	gStatus = "Connecting";
	return true;
}

void Stop()
{
	gTransport.Disconnect();
	gReady = false;
	gHelloSent = false;
	gSendSequence = 1;
	gLastInputSequence = 0;
	ClearRemoteInput();
	SetStatus("Offline");
}

void Update()
{
	const TransportState before = gTransport.GetState();
	gTransport.Poll();
	const TransportState state = gTransport.GetState();

	if (state == TransportState::Error)
	{
		gStatus = gTransport.GetLastError();
		gReady = false;
		ClearRemoteInput();
		return;
	}
	if (state == TransportState::Stopped)
	{
		if (before == TransportState::Connected || before == TransportState::Connecting)
		{
			gReady = false;
			ClearRemoteInput();
			SetStatus("Disconnected");
		}
		return;
	}

	if (gTransport.GetRole() == TransportRole::Client && state == TransportState::Connected && !gHelloSent)
	{
		if (SendSimple(MessageType::Hello))
		{
			gHelloSent = true;
			SetStatus("Connected; waiting for host");
		}
	}

	Message message;
	while (gTransport.PopMessage(message))
	{
		if (message.type == MessageType::Hello && gTransport.GetRole() == TransportRole::Host)
		{
			gReady = SendSimple(MessageType::HelloAck);
			SetStatus(gReady ? "Online (host)" : "Connected; handshake failed");
		}
		else if (message.type == MessageType::HelloAck && gTransport.GetRole() == TransportRole::Client)
		{
			gReady = true;
			SetStatus("Online (client)");
		}
		else if (message.type == MessageType::Input && gTransport.GetRole() == TransportRole::Host && gReady)
		{
			if (message.sequence <= gLastInputSequence)
				continue;
			InputSnapshot input;
			if (!DecodeInput(message.payload.data(), message.payload.size(), input))
				continue;
			gLastInputSequence = message.sequence;
			const uint32_t buttons = input.buttons & ~(kLeftTriggerBit | kRightTriggerBit);
			Player2Input::SetRemoteInput(true, buttons, input.cursorX, input.cursorY,
				input.stickX / 127.0f, input.stickY / 127.0f,
				(input.buttons & kLeftTriggerBit) != 0, (input.buttons & kRightTriggerBit) != 0);
		}
		else if (message.type == MessageType::Disconnect)
		{
			gTransport.Disconnect();
			gReady = false;
			ClearRemoteInput();
			SetStatus("Peer disconnected");
			return;
		}
	}

	if (gReady && gTransport.GetRole() == TransportRole::Client)
		SendLocalInput();
	if (gReady && gTransport.GetRole() == TransportRole::Host)
		SetStatus("Online (host)");
}

bool IsOnline() { return gTransport.GetState() != TransportState::Stopped && gTransport.GetState() != TransportState::Error; }
bool IsHost() { return gTransport.GetRole() == TransportRole::Host; }
bool IsReady() { return gReady && gTransport.IsConnected(); }
std::string GetStatus() { return gStatus; }
}
