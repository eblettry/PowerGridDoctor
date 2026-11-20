#pragma once

// Interface between the scanner (main.cpp, uses CommonLibF4) and the menu page
// (UI.cpp, uses F4SE Menu Framework + windows.h). Kept free of game types so
// the two never need to share headers.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace PGD
{
	struct WorkshopStatus
	{
		std::uint32_t id{ 0 };
		std::string   name;
		std::size_t   grids{ 0 };
		std::size_t   objects{ 0 };
		std::size_t   problems{ 0 };
		std::size_t   pluginLocalLinks{ 0 };
		std::string   lastAction;
	};

	// Thread safe. Copy of the results of the most recent scan.
	std::vector<WorkshopStatus> GetStatusSnapshot();
	std::string                 GetLastMessage();

	// Thread safe. Queued and run on the game's main thread.
	void RequestScan();
	void RequestRepair(std::uint32_t a_workshopID);

	// UI.cpp
	void RegisterMenu();
}
