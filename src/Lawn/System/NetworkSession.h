#ifndef __NETWORK_SESSION_H__
#define __NETWORK_SESSION_H__

#include <cstdint>
#include <string>
#include <vector>
#include "NetworkDiscovery.h"

namespace PvZNetwork
{
	// One remote P2 peer. StartHost/Connect are intended for a future menu UI;
	// Update is pumped automatically by Player2Input once per game update.
	bool StartHost(uint16_t port, std::string* error = nullptr, const std::string& playerName = "Player");
	bool Connect(const std::string& address, uint16_t port, std::string* error = nullptr);
	void Stop();
	void Update();
	bool IsOnline();
	bool IsHost();
	bool IsReady();
	std::string GetStatus();
	std::string GetLocalPlatform();
	std::string GetPeerPlatform();
	std::vector<DiscoveredGame> GetDiscoveredGames();
}

#endif
