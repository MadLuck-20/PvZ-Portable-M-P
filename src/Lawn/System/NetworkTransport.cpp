#include "NetworkTransport.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
# define WIN32_LEAN_AND_MEAN
# include <winsock2.h>
# include <ws2tcpip.h>
  using PvzSocket = SOCKET;
  static constexpr PvzSocket kInvalidSocket = INVALID_SOCKET;
  static int SocketError() { return WSAGetLastError(); }
  static void CloseSocket(PvzSocket s) { if (s != kInvalidSocket) closesocket(s); }
  static bool SetNonBlocking(PvzSocket s) { u_long mode = 1; return ioctlsocket(s, FIONBIO, &mode) == 0; }
  static bool IsWouldBlock(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
#else
# include <arpa/inet.h>
# include <fcntl.h>
# include <netdb.h>
# include <netinet/in.h>
# include <sys/socket.h>
# include <unistd.h>
  using PvzSocket = int;
  static constexpr PvzSocket kInvalidSocket = -1;
  static int SocketError() { return errno; }
  static void CloseSocket(PvzSocket s) { if (s != kInvalidSocket) close(s); }
  static bool SetNonBlocking(PvzSocket s) { const int f = fcntl(s, F_GETFL, 0); return f >= 0 && fcntl(s, F_SETFL, f | O_NONBLOCK) == 0; }
  static bool IsWouldBlock(int e) { return e == EWOULDBLOCK || e == EAGAIN || e == EINPROGRESS; }
#endif

namespace PvZNetwork
{
namespace
{
#if defined(_WIN32)
	bool EnsureSocketRuntime()
	{
		static bool initialized = false;
		static bool success = false;
		if (!initialized)
		{
			WSADATA data{};
			success = WSAStartup(MAKEWORD(2, 2), &data) == 0;
			initialized = true;
		}
		return success;
	}
#else
	bool EnsureSocketRuntime() { return true; }
#endif

	std::string ErrorText(const char* operation, int code)
	{
		return std::string(operation) + " failed (socket error " + std::to_string(code) + ")";
	}

	bool SetSocketOptions(PvzSocket s)
	{
		int yes = 1;
		setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
		setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
		return SetNonBlocking(s);
	}
}

struct Transport::Impl
{
	PvzSocket listener = kInvalidSocket;
	PvzSocket peer = kInvalidSocket;
	TransportRole role = TransportRole::None;
	TransportState state = TransportState::Stopped;
	std::string lastError;
	std::vector<uint8_t> receiveBuffer;
	std::deque<std::vector<uint8_t>> sendQueue;
	size_t sendOffset = 0;
	std::deque<Message> incoming;

	void Fail(const std::string& message)
	{
		lastError = message;
		state = TransportState::Error;
		CloseSocket(peer); peer = kInvalidSocket;
		CloseSocket(listener); listener = kInvalidSocket;
		sendQueue.clear(); receiveBuffer.clear(); sendOffset = 0;
	}

	void ClosePeer()
	{
		CloseSocket(peer); peer = kInvalidSocket;
		receiveBuffer.clear(); sendQueue.clear(); sendOffset = 0;
		if (role == TransportRole::Host && listener != kInvalidSocket)
			state = TransportState::Listening;
		else
			state = TransportState::Stopped;
	}
};

Transport::Transport() : mImpl(new Impl) {}
Transport::~Transport() { Disconnect(); delete mImpl; }

bool Transport::StartHost(uint16_t port, std::string* error)
{
	Disconnect();
	if (!EnsureSocketRuntime()) { if (error) *error = "Socket runtime initialization failed"; return false; }
	addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_protocol = IPPROTO_TCP; hints.ai_flags = AI_PASSIVE;
	addrinfo* results = nullptr;
	const std::string service = std::to_string(port);
	const int gai = getaddrinfo(nullptr, service.c_str(), &hints, &results);
	if (gai != 0) { if (error) *error = "Could not resolve local bind address"; return false; }
	PvzSocket s = kInvalidSocket;
	for (addrinfo* it = results; it; it = it->ai_next)
	{
		s = static_cast<PvzSocket>(socket(it->ai_family, it->ai_socktype, it->ai_protocol));
		if (s == kInvalidSocket) continue;
		SetSocketOptions(s);
		if (bind(s, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0 && listen(s, 1) == 0) break;
		CloseSocket(s); s = kInvalidSocket;
	}
	freeaddrinfo(results);
	if (s == kInvalidSocket) { const auto msg = ErrorText("Host listen", SocketError()); if (error) *error = msg; mImpl->lastError = msg; return false; }
	mImpl->listener = s; mImpl->role = TransportRole::Host; mImpl->state = TransportState::Listening; mImpl->lastError.clear();
	return true;
}

bool Transport::Connect(const std::string& address, uint16_t port, std::string* error)
{
	Disconnect();
	if (!EnsureSocketRuntime()) { if (error) *error = "Socket runtime initialization failed"; return false; }
	addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_protocol = IPPROTO_TCP;
	addrinfo* results = nullptr;
	const std::string service = std::to_string(port);
	const int gai = getaddrinfo(address.c_str(), service.c_str(), &hints, &results);
	if (gai != 0) { if (error) *error = "Could not resolve host address: " + address; return false; }
	PvzSocket s = kInvalidSocket; int connectResult = -1; int lastCode = 0;
	for (addrinfo* it = results; it; it = it->ai_next)
	{
		s = static_cast<PvzSocket>(socket(it->ai_family, it->ai_socktype, it->ai_protocol));
		if (s == kInvalidSocket) { lastCode = SocketError(); continue; }
		if (!SetSocketOptions(s)) { lastCode = SocketError(); CloseSocket(s); s = kInvalidSocket; continue; }
		connectResult = connect(s, it->ai_addr, static_cast<int>(it->ai_addrlen));
		if (connectResult == 0 || IsWouldBlock(SocketError())) break;
		lastCode = SocketError(); CloseSocket(s); s = kInvalidSocket;
	}
	freeaddrinfo(results);
	if (s == kInvalidSocket) { const auto msg = ErrorText("Connect", lastCode); if (error) *error = msg; mImpl->lastError = msg; return false; }
	mImpl->peer = s; mImpl->role = TransportRole::Client;
	mImpl->state = connectResult == 0 ? TransportState::Connected : TransportState::Connecting;
	mImpl->lastError.clear();
	return true;
}

void Transport::Poll()
{
	if (mImpl->state == TransportState::Error || mImpl->state == TransportState::Stopped) return;
	if (mImpl->role == TransportRole::Host && mImpl->peer == kInvalidSocket && mImpl->listener != kInvalidSocket)
	{
		sockaddr_storage address{};
#if defined(_WIN32)
		int length = sizeof(address);
#else
		socklen_t length = sizeof(address);
#endif
		PvzSocket accepted = accept(mImpl->listener, reinterpret_cast<sockaddr*>(&address), &length);
		if (accepted != kInvalidSocket)
		{
			if (!SetSocketOptions(accepted)) { CloseSocket(accepted); mImpl->Fail(ErrorText("Set accepted socket nonblocking", SocketError())); return; }
			mImpl->peer = accepted; mImpl->state = TransportState::Connected;
		}
	}
	if (mImpl->peer == kInvalidSocket) return;

	if (mImpl->state == TransportState::Connecting)
	{
		int socketError = 0;
#if defined(_WIN32)
		int len = sizeof(socketError);
#else
		socklen_t len = sizeof(socketError);
#endif
		if (getsockopt(mImpl->peer, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &len) != 0)
			 socketError = SocketError();
		if (socketError == 0) mImpl->state = TransportState::Connected;
		else if (!IsWouldBlock(socketError)) { mImpl->Fail(ErrorText("Finish connect", socketError)); return; }
	}
	if (mImpl->state != TransportState::Connected) return;

	uint8_t buffer[8192];
	for (;;)
	{
#if defined(_WIN32)
		const int count = recv(mImpl->peer, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
#else
		const int count = static_cast<int>(recv(mImpl->peer, buffer, sizeof(buffer), 0));
#endif
		if (count > 0)
		{
			if (mImpl->receiveBuffer.size() + static_cast<size_t>(count) > 256 * 1024)
			{ mImpl->Fail("Incoming network buffer exceeded maximum frame size"); return; }
			mImpl->receiveBuffer.insert(mImpl->receiveBuffer.end(), buffer, buffer + count);
			continue;
		}
		if (count == 0) { mImpl->ClosePeer(); return; }
		const int code = SocketError();
		if (!IsWouldBlock(code)) { mImpl->Fail(ErrorText("Receive", code)); return; }
		break;
	}

	while (!mImpl->receiveBuffer.empty())
	{
		Message message; size_t consumed = 0;
		if (!Decode(mImpl->receiveBuffer.data(), mImpl->receiveBuffer.size(), message, consumed))
		{
			// Decode also returns false for an incomplete payload. Distinguish that
			// case from a malformed header before deciding to close the peer.
			if (mImpl->receiveBuffer.size() < kHeaderBytes) break;
			const auto& b = mImpl->receiveBuffer;
			const uint32_t magic = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]);
			const uint16_t version = static_cast<uint16_t>((uint16_t(b[4]) << 8) | b[5]);
			const uint32_t payloadSize = (uint32_t(b[8]) << 24) | (uint32_t(b[9]) << 16) | (uint32_t(b[10]) << 8) | uint32_t(b[11]);
			if (magic != kMagic || version != kProtocolVersion || payloadSize > kMaxPayloadBytes)
			{ mImpl->Fail("Malformed or unsupported network frame"); return; }
			if (b.size() < kHeaderBytes + static_cast<size_t>(payloadSize)) break;
			mImpl->Fail("Invalid network message type or frame"); return;
		}
		mImpl->incoming.push_back(std::move(message));
		mImpl->receiveBuffer.erase(mImpl->receiveBuffer.begin(), mImpl->receiveBuffer.begin() + consumed);
	}

	while (!mImpl->sendQueue.empty())
	{
		auto& frame = mImpl->sendQueue.front();
		const char* data = reinterpret_cast<const char*>(frame.data() + mImpl->sendOffset);
		const size_t remaining = frame.size() - mImpl->sendOffset;
#if defined(_WIN32)
		const int sent = send(mImpl->peer, data, static_cast<int>(remaining), 0);
#else
		int flags = 0;
# if defined(MSG_NOSIGNAL)
		flags |= MSG_NOSIGNAL;
# endif
		const int sent = static_cast<int>(send(mImpl->peer, data, remaining, flags));
#endif
		if (sent > 0)
		{
			mImpl->sendOffset += static_cast<size_t>(sent);
			if (mImpl->sendOffset == frame.size()) { mImpl->sendQueue.pop_front(); mImpl->sendOffset = 0; }
			continue;
		}
		if (sent == 0) { mImpl->ClosePeer(); return; }
		const int code = SocketError();
		if (!IsWouldBlock(code)) { mImpl->Fail(ErrorText("Send", code)); return; }
		break;
	}
}

bool Transport::Send(const Message& message)
{
	if (mImpl->state != TransportState::Connected) return false;
	std::vector<uint8_t> frame;
	if (!Encode(message, frame)) return false;
	// Bound queued data so a stalled peer cannot grow memory indefinitely.
	size_t queued = 0; for (const auto& item : mImpl->sendQueue) queued += item.size();
	if (queued + frame.size() > 256 * 1024) return false;
	mImpl->sendQueue.push_back(std::move(frame));
	return true;
}

bool Transport::PopMessage(Message& message)
{
	if (mImpl->incoming.empty()) return false;
	message = std::move(mImpl->incoming.front()); mImpl->incoming.pop_front(); return true;
}

void Transport::Disconnect()
{
	if (!mImpl) return;
	CloseSocket(mImpl->peer); mImpl->peer = kInvalidSocket;
	CloseSocket(mImpl->listener); mImpl->listener = kInvalidSocket;
	mImpl->role = TransportRole::None; mImpl->state = TransportState::Stopped;
	mImpl->receiveBuffer.clear(); mImpl->sendQueue.clear(); mImpl->incoming.clear(); mImpl->sendOffset = 0; mImpl->lastError.clear();
}

TransportState Transport::GetState() const { return mImpl->state; }
TransportRole Transport::GetRole() const { return mImpl->role; }
bool Transport::IsConnected() const { return mImpl->state == TransportState::Connected; }
const std::string& Transport::GetLastError() const { return mImpl->lastError; }
}
