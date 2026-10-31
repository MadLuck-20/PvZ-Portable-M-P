#ifndef __NETWORK_TRANSPORT_H__
#define __NETWORK_TRANSPORT_H__

#include "NetworkProtocol.h"
#include <cstdint>
#include <string>
#include <vector>

// One-peer TCP transport for LAN/VPN sessions. Call Poll() regularly from the
// main loop; it never waits for network data. The transport does not touch game
// objects and is deliberately separate from Board/P2 input handling.
namespace PvZNetwork
{
	enum class TransportRole { None, Host, Client };
	enum class TransportState { Stopped, Listening, Connecting, Connected, Error };

	class Transport
	{
	public:
		Transport();
		~Transport();
		Transport(const Transport&) = delete;
		Transport& operator=(const Transport&) = delete;

		// Host accepts one remote player. Port is in host byte order.
		bool StartHost(uint16_t port, std::string* error = nullptr);
		// Client connects to an IPv4/IPv6 address or hostname.
		bool Connect(const std::string& address, uint16_t port, std::string* error = nullptr);

		// Non-blocking network pump. Call once per frame (or more often).
		void Poll();
		bool Send(const Message& message);
		bool PopMessage(Message& message);
		void Disconnect();

		TransportState GetState() const;
		TransportRole GetRole() const;
		bool IsConnected() const;
		const std::string& GetLastError() const;

	private:
		struct Impl;
		Impl* mImpl;
	};
}

#endif
