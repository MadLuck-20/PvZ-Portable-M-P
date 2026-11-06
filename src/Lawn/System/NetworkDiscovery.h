#ifndef __NETWORK_DISCOVERY_H__
#define __NETWORK_DISCOVERY_H__

#include <cstdint>
#include <string>
#include <vector>

namespace PvZNetwork
{
	struct DiscoveredGame
	{
		std::string name;
		std::string platform;
		std::string address;
		uint16_t port = 24800;
		uint32_t id = 0;
	};

	// Starts listening for LAN/VPN game announcements. Safe to call repeatedly.
	bool StartDiscovery(std::string* error = nullptr);
	void StopDiscovery();
	void SetHostAnnouncement(bool enabled, const std::string& playerName, const std::string& platform, uint16_t gamePort);
	void UpdateDiscovery(uint32_t ticks);
	std::vector<DiscoveredGame> GetNetworkGames();
}

#endif
