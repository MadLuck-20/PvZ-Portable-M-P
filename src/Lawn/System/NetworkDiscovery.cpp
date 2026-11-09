#include "NetworkDiscovery.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#if defined(_WIN32)
# define WIN32_LEAN_AND_MEAN
# include <winsock2.h>
# include <ws2tcpip.h>
  using DiscoverySocket = SOCKET;
  static constexpr DiscoverySocket kInvalidDiscoverySocket = INVALID_SOCKET;
  static int DiscoverySocketError() { return WSAGetLastError(); }
  static void CloseDiscoverySocket(DiscoverySocket s) { if (s != kInvalidDiscoverySocket) closesocket(s); }
  static bool SetDiscoveryNonBlocking(DiscoverySocket s) { u_long mode = 1; return ioctlsocket(s, FIONBIO, &mode) == 0; }
#else
# include <arpa/inet.h>
# include <cerrno>
# include <fcntl.h>
# include <netdb.h>
# include <netinet/in.h>
# include <sys/socket.h>
# include <unistd.h>
# if !defined(__ANDROID__) && !defined(__APPLE__)
#  include <ifaddrs.h>
# endif
# if defined(__ANDROID__) || defined(__APPLE__)
#  include <ifaddrs.h>
# endif
# include <net/if.h>
  using DiscoverySocket = int;
  static constexpr DiscoverySocket kInvalidDiscoverySocket = -1;
  static int DiscoverySocketError() { return errno; }
  static void CloseDiscoverySocket(DiscoverySocket s) { if (s != kInvalidDiscoverySocket) close(s); }
  static bool SetDiscoveryNonBlocking(DiscoverySocket s) { const int f = fcntl(s, F_GETFL, 0); return f >= 0 && fcntl(s, F_SETFL, f | O_NONBLOCK) == 0; }
#endif

namespace PvZNetwork
{
namespace
{
#if defined(_WIN32)
	bool EnsureDiscoverySocketRuntime()
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
	bool EnsureDiscoverySocketRuntime() { return true; }
#endif
	constexpr uint16_t kDiscoveryPort = 24801;
	constexpr uint32_t kDiscoveryMagic = 0x50565A44; // PVZD
	constexpr uint8_t kDiscoveryVersion = 2;
	constexpr uint8_t kPacketProbe = 1;
	constexpr uint8_t kPacketAnnouncement = 2;
	constexpr uint32_t kGameTimeoutMs = 5000;
	constexpr uint32_t kAnnounceIntervalMs = 1000;

	struct Entry
	{
		DiscoveredGame game;
		uint32_t lastSeen = 0;
	};

	DiscoverySocket gSocket = kInvalidDiscoverySocket;
	std::map<uint32_t, Entry> gGames;
	bool gAnnouncing = false;
	std::string gPlayerName = "Player";
	std::string gPlatform = "Unknown";
	uint16_t gGamePort = 24800;
	uint32_t gInstanceId = 0;
	uint32_t gLastAnnouncement = 0;
	bool gHasAnnouncementTime = false;
	uint32_t gLastProbe = 0;
	bool gHasProbeTime = false;

	uint32_t MakeInstanceId()
	{
		// Mix a time-based value with process-local addresses so two devices running
		// the same build do not normally advertise the same identifier.
		const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
		uint64_t mixed = static_cast<uint64_t>(now);
		mixed ^= static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&gSocket)) << 7;
		mixed ^= static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&gGames)) >> 3;
		uint32_t value = static_cast<uint32_t>(mixed ^ (mixed >> 32));
		return value ? value : 1u;
	}

	bool EnsureSocket(std::string* error)
	{
		if (!EnsureDiscoverySocketRuntime())
		{
			if (error) *error = "Could not initialize the network socket runtime";
			return false;
		}
		if (gSocket != kInvalidDiscoverySocket)
			return true;
		gSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (gSocket == kInvalidDiscoverySocket)
		{
			if (error) *error = "Could not create LAN discovery socket (socket error " + std::to_string(DiscoverySocketError()) + ")";
			return false;
		}
		int yes = 1;
		setsockopt(gSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
		setsockopt(gSocket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
		sockaddr_in local{};
		local.sin_family = AF_INET;
		local.sin_port = htons(kDiscoveryPort);
		local.sin_addr.s_addr = htonl(INADDR_ANY);
		if (bind(gSocket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0)
		{
			if (error) *error = "Could not bind LAN discovery port 24801 (socket error " + std::to_string(DiscoverySocketError()) + ")";
			CloseDiscoverySocket(gSocket);
			gSocket = kInvalidDiscoverySocket;
			return false;
		}
		if (!SetDiscoveryNonBlocking(gSocket))
		{
			if (error) *error = "Could not make LAN discovery socket non-blocking";
			CloseDiscoverySocket(gSocket);
			gSocket = kInvalidDiscoverySocket;
			return false;
		}
		gInstanceId = MakeInstanceId();
		return true;
	}

	void Append16(std::vector<uint8_t>& out, uint16_t value)
	{
		out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
		out.push_back(static_cast<uint8_t>(value & 0xFF));
	}
	void Append32(std::vector<uint8_t>& out, uint32_t value)
	{
		out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
		out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
		out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
		out.push_back(static_cast<uint8_t>(value & 0xFF));
	}
	uint16_t Read16(const uint8_t* p) { return static_cast<uint16_t>((uint16_t(p[0]) << 8) | p[1]); }
	uint32_t Read32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); }

	std::vector<uint8_t> MakeProbe()
	{
		std::vector<uint8_t> packet;
		packet.reserve(6);
		Append32(packet, kDiscoveryMagic);
		packet.push_back(kDiscoveryVersion);
		packet.push_back(kPacketProbe);
		return packet;
	}

	std::vector<uint8_t> MakeAnnouncement()
	{
		const std::string name = gPlayerName.substr(0, 48);
		const std::string platform = gPlatform.substr(0, 24);
		std::vector<uint8_t> packet;
		packet.reserve(14 + name.size() + platform.size());
		Append32(packet, kDiscoveryMagic);
		packet.push_back(kDiscoveryVersion);
		packet.push_back(kPacketAnnouncement);
		Append16(packet, gGamePort);
		Append32(packet, gInstanceId);
		packet.push_back(static_cast<uint8_t>(name.size()));
		packet.push_back(static_cast<uint8_t>(platform.size()));
		packet.insert(packet.end(), name.begin(), name.end());
		packet.insert(packet.end(), platform.begin(), platform.end());
		return packet;
	}

	void SendAnnouncement(const std::vector<uint8_t>& packet)
	{
		sockaddr_in destination{};
		destination.sin_family = AF_INET;
		destination.sin_port = htons(kDiscoveryPort);
	#if defined(_WIN32)
		destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);
		sendto(gSocket, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
			reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
	#else
		// Send on every broadcast-capable interface. This includes virtual VPN
		// adapters such as ZeroTier when the OS exposes their broadcast address.
		bool sentOnInterface = false;
		ifaddrs* interfaces = nullptr;
		if (getifaddrs(&interfaces) == 0)
		{
			for (ifaddrs* it = interfaces; it; it = it->ifa_next)
			{
				if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET || !(it->ifa_flags & IFF_UP) ||
					(it->ifa_flags & IFF_LOOPBACK) || !it->ifa_netmask || it->ifa_netmask->sa_family != AF_INET)
					continue;
				auto* local = reinterpret_cast<sockaddr_in*>(it->ifa_addr);
				auto* mask = reinterpret_cast<sockaddr_in*>(it->ifa_netmask);
				uint32_t broadcastAddress = 0;
				if ((it->ifa_flags & IFF_BROADCAST) && it->ifa_broadaddr)
					broadcastAddress = reinterpret_cast<sockaddr_in*>(it->ifa_broadaddr)->sin_addr.s_addr;
				else
					broadcastAddress = local->sin_addr.s_addr | ~mask->sin_addr.s_addr;
				if (broadcastAddress == local->sin_addr.s_addr || broadcastAddress == htonl(INADDR_ANY))
					continue;
				destination.sin_addr.s_addr = broadcastAddress;
				const int sent = static_cast<int>(sendto(gSocket, packet.data(), packet.size(), 0,
					reinterpret_cast<const sockaddr*>(&destination), sizeof(destination)));
				if (sent >= 0) sentOnInterface = true;
			}
			freeifaddrs(interfaces);
		}
		if (!sentOnInterface)
		{
			destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);
			sendto(gSocket, packet.data(), packet.size(), 0,
				reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
		}
	#endif
	}

	void SendUnicastProbes()
	{
#if !defined(_WIN32)
		ifaddrs* interfaces = nullptr;
		if (getifaddrs(&interfaces) != 0)
			return;

		const std::vector<uint8_t> probe = MakeProbe();
		for (ifaddrs* it = interfaces; it; it = it->ifa_next)
		{
			if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET ||
				!(it->ifa_flags & IFF_UP) || (it->ifa_flags & IFF_LOOPBACK) ||
				!it->ifa_netmask || it->ifa_netmask->sa_family != AF_INET)
				continue;

			const auto* local = reinterpret_cast<const sockaddr_in*>(it->ifa_addr);
			const auto* maskAddr = reinterpret_cast<const sockaddr_in*>(it->ifa_netmask);
			uint32_t mask = ntohl(maskAddr->sin_addr.s_addr);
			int prefix = 0;
			bool zeroSeen = false;
			for (int bit = 31; bit >= 0; --bit)
			{
				const bool set = (mask & (uint32_t(1) << bit)) != 0;
				if (set && zeroSeen) { prefix = -1; break; }
				if (set) ++prefix; else zeroSeen = true;
			}
			// Mobile VPN APIs sometimes expose a host route (/32). In that case
			// probe the common /24 managed-address range rather than doing no scan.
			if (prefix < 24 || prefix > 30)
				prefix = 24;

			const uint32_t ip = ntohl(local->sin_addr.s_addr);
			const uint32_t subnetMask = prefix == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix));
			const uint32_t network = ip & subnetMask;
			const uint32_t broadcast = network | ~subnetMask;
			for (uint32_t host = network + 1; host < broadcast; ++host)
			{
				if (host == ip)
					continue;
				sockaddr_in destination{};
				destination.sin_family = AF_INET;
				destination.sin_port = htons(kDiscoveryPort);
				destination.sin_addr.s_addr = htonl(host);
				sendto(gSocket, probe.data(), probe.size(), 0,
					reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
			}
		}
		freeifaddrs(interfaces);
#endif
	}

	void ReceiveAnnouncements(uint32_t now)
	{
		uint8_t buffer[256];
		for (;;)
		{
			sockaddr_in source{};
		#if defined(_WIN32)
			int sourceLength = sizeof(source);
			const int count = recvfrom(gSocket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
				reinterpret_cast<sockaddr*>(&source), &sourceLength);
		#else
			socklen_t sourceLength = sizeof(source);
			const int count = static_cast<int>(recvfrom(gSocket, buffer, sizeof(buffer), 0,
				reinterpret_cast<sockaddr*>(&source), &sourceLength));
		#endif
			if (count < 0)
				break;
			if (count < 6 || Read32(buffer) != kDiscoveryMagic ||
				buffer[4] != kDiscoveryVersion)
				continue;

			if (buffer[5] == kPacketProbe)
			{
				if (count == 6 && gAnnouncing)
				{
					const std::vector<uint8_t> response = MakeAnnouncement();
					sendto(gSocket, response.data(), response.size(), 0,
						reinterpret_cast<const sockaddr*>(&source), sizeof(source));
				}
				continue;
			}
			if (buffer[5] != kPacketAnnouncement || count < 14)
				continue;

			const uint16_t port = Read16(buffer + 6);
			const uint32_t id = Read32(buffer + 8);
			const uint8_t nameLength = buffer[12];
			const uint8_t platformLength = buffer[13];
			if (id == 0 || id == gInstanceId || nameLength > 48 || platformLength > 24 ||
				14 + nameLength + platformLength != count || port == 0)
				continue;
			char addressText[INET_ADDRSTRLEN] = {};
			if (!inet_ntop(AF_INET, &source.sin_addr, addressText, sizeof(addressText)))
				continue;
			Entry& entry = gGames[id];
			entry.game.id = id;
			entry.game.port = port;
			entry.game.address = addressText;
			entry.game.name.assign(reinterpret_cast<char*>(buffer + 14), nameLength);
			entry.game.platform.assign(reinterpret_cast<char*>(buffer + 14 + nameLength), platformLength);
			entry.lastSeen = now;
		}
	}
}

bool StartDiscovery(std::string* error) { return EnsureSocket(error); }

void StopDiscovery()
{
	CloseDiscoverySocket(gSocket);
	gSocket = kInvalidDiscoverySocket;
	gGames.clear();
	gAnnouncing = false;
	gHasAnnouncementTime = false;
	gHasProbeTime = false;
	gHasProbeTime = false;
}

void SetHostAnnouncement(bool enabled, const std::string& playerName, const std::string& platform, uint16_t gamePort)
{
	gAnnouncing = enabled;
	gPlayerName = playerName.empty() ? "Player" : playerName;
	gPlatform = platform.empty() ? "Unknown" : platform;
	gGamePort = gamePort;
	gHasAnnouncementTime = false;
}

void UpdateDiscovery(uint32_t ticks)
{
	if (!EnsureSocket(nullptr))
		return;
	ReceiveAnnouncements(ticks);
	if (!gHasProbeTime || ticks - gLastProbe >= 2500)
	{
		SendUnicastProbes();
		gLastProbe = ticks;
		gHasProbeTime = true;
	}
	if (gAnnouncing && (!gHasAnnouncementTime || ticks - gLastAnnouncement >= kAnnounceIntervalMs))
	{
		SendAnnouncement(MakeAnnouncement());
		gLastAnnouncement = ticks;
		gHasAnnouncementTime = true;
	}
	for (auto it = gGames.begin(); it != gGames.end();)
	{
		if (ticks - it->second.lastSeen > kGameTimeoutMs)
			it = gGames.erase(it);
		else
			++it;
	}
}

std::vector<DiscoveredGame> GetNetworkGames()
{
	std::vector<DiscoveredGame> games;
	games.reserve(gGames.size());
	for (const auto& pair : gGames)
		games.push_back(pair.second.game);
	std::sort(games.begin(), games.end(), [](const DiscoveredGame& a, const DiscoveredGame& b)
		{ return a.name < b.name || (a.name == b.name && a.address < b.address); });
	return games;
}
}
