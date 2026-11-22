// PowerGridDoctor
//
// Finds workshop power grid entries that point at objects which no longer exist
// (the "No form for FFxxxxxx" case) and reports *every* place that still refers
// to them: the grid adjacency map (node keys and the connection sets of other
// nodes), the grid's pending load data, the currently-powered list and the
// ExtraPowerLinks of the wired objects. With bFix=1 it also removes them.
//
// Report-only by default. Output: Documents\My Games\Fallout4\F4SE\PowerGridDoctor.log

#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "PGD.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace std::literals;

// CommonLibF4 doesn't define equality for GridConnection, but BSTSet needs it
// to compile. Found through argument-dependent lookup by std::equal_to.
namespace RE::PowerUtils
{
	[[nodiscard]] inline bool operator==(const GridConnection& a_lhs, const GridConnection& a_rhs) noexcept
	{
		return a_lhs.connection == a_rhs.connection && a_lhs.connector == a_rhs.connector;
	}
}

namespace PGD
{
	// ------------------------------------------------------------------
	// Settings
	// ------------------------------------------------------------------

	struct Settings
	{
		bool fix{ false };
		bool dumpAll{ false };
		bool scanOnWorkshopMenu{ true };
	};

	Settings g_settings;

	std::string Lower(std::string a_str)
	{
		std::ranges::transform(a_str, a_str.begin(), [](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
		return a_str;
	}

	void LoadSettings()
	{
		// The game's working directory is the Fallout 4 install folder.
		std::ifstream file{ "Data/F4SE/Plugins/PowerGridDoctor.ini" };
		if (!file) {
			REX::INFO("PowerGridDoctor.ini not found, using defaults (report only)");
			return;
		}

		std::string line;
		while (std::getline(file, line)) {
			if (const auto comment = line.find_first_of(";#"); comment != std::string::npos) {
				line.resize(comment);
			}
			std::erase_if(line, [](unsigned char a_c) { return std::isspace(a_c) != 0; });
			if (line.empty() || line.front() == '[') {
				continue;
			}

			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}

			const auto key = Lower(line.substr(0, eq));
			const auto value = Lower(line.substr(eq + 1));
			const bool enabled = value == "1" || value == "true";

			if (key == "bfix") {
				g_settings.fix = enabled;
			} else if (key == "bdumpall") {
				g_settings.dumpAll = enabled;
			} else if (key == "bscanonworkshopmenu") {
				g_settings.scanOnWorkshopMenu = enabled;
			}
		}
	}

	// ------------------------------------------------------------------
	// Form inspection
	// ------------------------------------------------------------------

	enum class Status
	{
		kOK,
		kNone,          // id 0 (e.g. no wire between two snapped objects)
		kMissing,       // no form with this id exists any more
		kDeleted,       // reference still in memory but flagged deleted
		kNotReference,  // id resolves to something that is not a placed object
	};

	std::string_view StatusName(Status a_status)
	{
		switch (a_status) {
		case Status::kOK:
			return "ok"sv;
		case Status::kNone:
			return "none"sv;
		case Status::kMissing:
			return "MISSING (no form)"sv;
		case Status::kDeleted:
			return "DELETED reference"sv;
		case Status::kNotReference:
			return "NOT A REFERENCE"sv;
		}
		return "?"sv;
	}

	Status Inspect(std::uint32_t a_id)
	{
		if (a_id == 0) {
			return Status::kNone;
		}
		const auto form = RE::TESForm::GetFormByID(a_id);
		if (!form) {
			return Status::kMissing;
		}
		const auto ref = form->IsReference();
		if (!ref) {
			return Status::kNotReference;
		}
		if (ref->IsDeleted()) {
			return Status::kDeleted;
		}
		return Status::kOK;
	}

	bool IsProblem(Status a_status)
	{
		return a_status == Status::kMissing || a_status == Status::kDeleted || a_status == Status::kNotReference;
	}

	// Only these are removed in fix mode. "Not a reference" is reported but left
	// alone, because we don't know what the game would expect there.
	bool IsFixable(Status a_status)
	{
		return a_status == Status::kMissing || a_status == Status::kDeleted;
	}

	RE::TESObjectREFR* GetLiveRef(std::uint32_t a_id)
	{
		if (Inspect(a_id) != Status::kOK) {
			return nullptr;
		}
		return RE::TESForm::GetFormByID(a_id)->IsReference();
	}

	RE::ExtraPowerLinks* GetPowerLinks(RE::TESObjectREFR* a_ref)
	{
		return a_ref && a_ref->extraList ? a_ref->extraList->GetByType<RE::ExtraPowerLinks>() : nullptr;
	}

	// Objects placed by a plugin can carry ExtraPowerLinks ids written with the
	// plugin's own master-list index instead of its load order index. Example:
	// Vault 88's starter connectors in DLCworkshop03.esm (load index 05) link to
	// 01005633, which really means 05005633. The game copes with this, so these
	// are not problems. Returns the id the link really points at, if that's the case.
	std::optional<std::uint32_t> UnmappedPluginLink(std::uint32_t a_owner, std::uint32_t a_id)
	{
		if (Inspect(a_id) != Status::kMissing) {
			return std::nullopt;
		}
		const std::uint32_t fileMask = (a_owner >> 24) == 0xFE ? 0xFFFFF000u : 0xFF000000u;  // light plugins: FExxx
		const std::uint32_t candidate = (a_owner & fileMask) | (a_id & ~fileMask);
		if (candidate == a_id || Inspect(candidate) != Status::kOK) {
			return std::nullopt;
		}
		return candidate;
	}

	// Objects placed by a plugin (not built in this save) keep their wire links in
	// the plugin file. A dead link there (for example to a DLC starter wire the
	// player scrapped) isn't used by the grid, and removing it doesn't survive a
	// reload, because the game reads the links from the plugin again. Report these
	// as harmless instead of as problems.
	bool IsPluginPlaced(std::uint32_t a_id)
	{
		return (a_id >> 24) != 0xFF;
	}

	std::string Describe(std::uint32_t a_id)
	{
		switch (Inspect(a_id)) {
		case Status::kNone:
			return "[00000000] (none)";
		case Status::kMissing:
			return std::format("[{:08X}] *** NO FORM - object no longer exists ***", a_id);
		case Status::kDeleted:
			return std::format("[{:08X}] *** DELETED reference ***", a_id);
		case Status::kNotReference:
			{
				const auto form = RE::TESForm::GetFormByID(a_id);
				// Numeric type only: the form-type name table has no address on every game version.
				return std::format("[{:08X}] *** not a reference (form type {}) ***", a_id, static_cast<std::uint32_t>(form->GetFormType()));
			}
		case Status::kOK:
			break;
		}

		const auto ref = RE::TESForm::GetFormByID(a_id)->IsReference();
		const auto base = ref->GetObjectReference();

		const char* name = ref->GetDisplayFullName();

		std::string refOrigin = "created in save";
		if ((a_id >> 24) != 0xFF) {
			if (const auto file = ref->GetFile(0)) {
				refOrigin = file->GetFilename();
			}
		}

		std::string baseOrigin = "?";
		if (base) {
			if (const auto file = base->GetFile(0)) {
				baseOrigin = file->GetFilename();
			}
		}

		const auto& pos = ref->data.location;
		return std::format(
			"[{:08X}] \"{}\" base [{:08X}] from {}, ref {}, pos ({:.0f}, {:.0f}, {:.0f}){}",
			a_id,
			name ? name : "",
			base ? base->GetFormID() : 0u,
			baseOrigin,
			refOrigin,
			pos.x,
			pos.y,
			pos.z,
			ref->IsDisabled() ? " [DISABLED]" : "");
	}

	// ------------------------------------------------------------------
	// Analysis
	// ------------------------------------------------------------------

	struct Finding
	{
		Status                   status{ Status::kOK };
		std::vector<std::string> referencedFrom;
		std::set<std::uint32_t>  heldBy;  // live objects that still refer to the dead id
	};

	using Findings = std::map<std::uint32_t, Finding>;

	void Note(Findings& a_findings, std::uint32_t a_id, std::string a_where, std::uint32_t a_heldBy = 0, std::uint32_t a_heldBy2 = 0)
	{
		const auto status = Inspect(a_id);
		if (!IsProblem(status)) {
			return;
		}
		auto& finding = a_findings[a_id];
		finding.status = status;
		finding.referencedFrom.push_back(std::move(a_where));
		for (const auto holder : { a_heldBy, a_heldBy2 }) {
			if (holder != 0 && holder != a_id && Inspect(holder) == Status::kOK) {
				finding.heldBy.insert(holder);
			}
		}
	}

	// Walks every structure that can hold a power grid form id. a_members
	// receives every id seen, so their ExtraPowerLinks can be checked too.
	Findings Analyze(RE::Workshop::ExtraData& a_workshop, std::set<std::uint32_t>& a_members, std::size_t* a_unmappedLinks = nullptr)
	{
		Findings findings;

		for (std::uint32_t g = 0; g < a_workshop.powerGrid.size(); ++g) {
			const auto grid = a_workshop.powerGrid[g];
			if (!grid) {
				continue;
			}

			for (const auto& [node, connections] : grid->adjacencyMap) {
				a_members.insert(node);
				Note(findings, node, std::format("grid #{}: node key", g));

				if (!connections) {
					continue;
				}
				for (const auto& c : *connections) {
					a_members.insert(c.connection);
					Note(findings, c.connection,
						std::format("grid #{}: connection from node [{:08X}] (via [{:08X}])", g, node, c.connector), node);
					if (c.connector != 0) {
						a_members.insert(c.connector);
						Note(findings, c.connector,
							std::format("grid #{}: connector between [{:08X}] and [{:08X}]", g, node, c.connection), node, c.connection);
					}
				}
			}

			for (std::uint32_t i = 0; i < grid->loadGameData.size(); ++i) {
				const auto& d = grid->loadGameData[i];
				a_members.insert(d.node);
				a_members.insert(d.connection.connection);
				Note(findings, d.node, std::format("grid #{}: pending load data [{}] node", g, i));
				Note(findings, d.connection.connection, std::format("grid #{}: pending load data [{}] connection", g, i), d.node);
				if (d.connection.connector != 0) {
					a_members.insert(d.connection.connector);
					Note(findings, d.connection.connector, std::format("grid #{}: pending load data [{}] connector", g, i), d.node, d.connection.connection);
				}
			}

			for (std::uint32_t i = 0; i < grid->currentlyPowered.size(); ++i) {
				const auto id = grid->currentlyPowered[i];
				a_members.insert(id);
				Note(findings, id, std::format("grid #{}: currently powered [{}]", g, i));
			}
		}

		// The wired objects' own link lists. If one of these still names the
		// dead object, the grid may be rebuilt with it on every load.
		for (const auto id : a_members) {
			const auto links = GetPowerLinks(GetLiveRef(id));
			if (!links) {
				continue;
			}
			for (std::uint32_t i = 0; i < links->powerLinks.size(); ++i) {
				const auto& link = links->powerLinks[i];
				if (UnmappedPluginLink(id, link.formID) || (IsPluginPlaced(id) && IsProblem(Inspect(link.formID)))) {
					if (a_unmappedLinks) {
						++*a_unmappedLinks;
					}
					continue;
				}
				Note(findings, link.formID,
					std::format("ExtraPowerLinks [{}] on object [{:08X}] (link type {})", i, id, link.linkType), id);
			}
		}

		return findings;
	}

	void DumpGrids(RE::Workshop::ExtraData& a_workshop)
	{
		for (std::uint32_t g = 0; g < a_workshop.powerGrid.size(); ++g) {
			const auto grid = a_workshop.powerGrid[g];
			if (!grid) {
				REX::INFO("  grid #{}: <null>", g);
				continue;
			}

			REX::INFO("  grid #{}: {} nodes, {} pending load entries, {} powered, capacity {:.1f}, load {:.1f}",
				g, grid->adjacencyMap.size(), grid->loadGameData.size(), grid->currentlyPowered.size(),
				grid->capacity, grid->load);

			for (const auto& [node, connections] : grid->adjacencyMap) {
				REX::INFO("    node {}", Describe(node));
				const auto links = GetPowerLinks(GetLiveRef(node));
				if (links) {
					for (const auto& link : links->powerLinks) {
						std::string note;
						if (const auto real = UnmappedPluginLink(node, link.formID)) {
							note = std::format("   (plugin-local id for [{:08X}], harmless)", *real);
						} else if (IsProblem(Inspect(link.formID))) {
							note = IsPluginPlaced(node) ? "   (dead link stored in plugin data, harmless, left alone)" : "   <-- BAD";
						}
						REX::INFO("      link  -> [{:08X}] type {}{}", link.formID, link.linkType, note);
					}
				}
				if (!connections) {
					continue;
				}
				for (const auto& c : *connections) {
					const bool bad = IsProblem(Inspect(c.connection)) || IsProblem(Inspect(c.connector));
					REX::INFO("      conn  -> {} via [{:08X}]{}", Describe(c.connection), c.connector, bad ? "   <-- BAD" : "");
				}
			}

			for (const auto& d : grid->loadGameData) {
				const bool bad = IsProblem(Inspect(d.node)) || IsProblem(Inspect(d.connection.connection)) ||
				                 IsProblem(Inspect(d.connection.connector));
				REX::INFO("    pending load: [{:08X}] -> [{:08X}] via [{:08X}]{}",
					d.node, d.connection.connection, d.connection.connector, bad ? "   <-- BAD" : "");
			}
		}
	}

	// ------------------------------------------------------------------
	// Repair
	// ------------------------------------------------------------------

	std::size_t Repair(RE::Workshop::ExtraData& a_workshop, const Findings& a_findings, const std::set<std::uint32_t>& a_members)
	{
		const auto isBad = [&](std::uint32_t a_id) {
			const auto it = a_findings.find(a_id);
			return it != a_findings.end() && IsFixable(it->second.status);
		};

		std::size_t removed = 0;

		for (std::uint32_t g = 0; g < a_workshop.powerGrid.size(); ++g) {
			const auto grid = a_workshop.powerGrid[g];
			if (!grid) {
				continue;
			}

			// 1. Connections inside other nodes' sets that point at a dead object.
			//    Erasing can move entries around, so restart after each erase.
			for (auto& [node, connections] : grid->adjacencyMap) {
				if (!connections) {
					continue;
				}
				for (bool again = true; again;) {
					again = false;
					for (auto it = connections->begin(); it != connections->end(); ++it) {
						if (isBad(it->connection) || isBad(it->connector)) {
							REX::INFO("  FIX grid #{}: removed connection [{:08X}] -> [{:08X}] via [{:08X}]",
								g, node, it->connection, it->connector);
							connections->erase(it);
							++removed;
							again = true;
							break;
						}
					}
				}
			}

			// 2. Node keys for dead objects. The connection set they owned is
			//    deliberately leaked rather than freed: a few bytes, no risk.
			std::vector<std::uint32_t> deadNodes;
			for (const auto& [node, connections] : grid->adjacencyMap) {
				if (isBad(node)) {
					deadNodes.push_back(node);
				}
			}
			for (const auto node : deadNodes) {
				grid->adjacencyMap.erase(node);
				REX::INFO("  FIX grid #{}: removed node [{:08X}]", g, node);
				++removed;
			}

			// 3. Pending load data.
			for (auto i = grid->loadGameData.size(); i-- > 0;) {
				const auto& d = grid->loadGameData[i];
				if (isBad(d.node) || isBad(d.connection.connection) || isBad(d.connection.connector)) {
					REX::INFO("  FIX grid #{}: removed pending load entry [{:08X}] -> [{:08X}] via [{:08X}]",
						g, d.node, d.connection.connection, d.connection.connector);
					grid->loadGameData.erase(grid->loadGameData.begin() + i);
					++removed;
				}
			}

			// 4. Currently powered list.
			for (auto i = grid->currentlyPowered.size(); i-- > 0;) {
				if (isBad(grid->currentlyPowered[i])) {
					REX::INFO("  FIX grid #{}: removed [{:08X}] from currently powered", g, grid->currentlyPowered[i]);
					grid->currentlyPowered.erase(grid->currentlyPowered.begin() + i);
					++removed;
				}
			}
		}

		// 5. ExtraPowerLinks on live wired objects.
		for (const auto id : a_members) {
			const auto links = GetPowerLinks(GetLiveRef(id));
			if (!links) {
				continue;
			}
			for (auto i = links->powerLinks.size(); i-- > 0;) {
				if (isBad(links->powerLinks[i].formID)) {
					REX::INFO("  FIX: removed link to [{:08X}] from ExtraPowerLinks of [{:08X}]", links->powerLinks[i].formID, id);
					links->powerLinks.erase(links->powerLinks.begin() + i);
					++removed;
				}
			}
		}

		return removed;
	}

	// ------------------------------------------------------------------
	// Scanning
	// ------------------------------------------------------------------

	std::vector<RE::TESObjectREFR*> FindWorkshops()
	{
		std::vector<RE::TESObjectREFR*> result;

		const auto& [map, lock] = RE::TESForm::GetAllForms();
		RE::BSAutoReadLock guard{ lock };
		if (!map) {
			return result;
		}

		for (const auto& [id, form] : *map) {
			if (!form) {
				continue;
			}
			const auto ref = form->IsReference();
			if (!ref || ref->IsDeleted() || !ref->extraList) {
				continue;
			}
			if (ref->extraList->HasType<RE::Workshop::ExtraData>()) {
				result.push_back(ref);
			}
		}

		std::ranges::sort(result, {}, [](const RE::TESObjectREFR* a_ref) { return a_ref->GetFormID(); });
		return result;
	}

	// ------------------------------------------------------------------
	// Results shared with the menu page (UI.cpp)
	// ------------------------------------------------------------------

	// Problem ids last written to the log per workshop, so repeated scans
	// don't write the same details again. Cleared when a save loads.
	std::map<std::uint32_t, std::set<std::uint32_t>> g_lastLogged;

	std::mutex                  g_statusLock;
	std::vector<WorkshopStatus> g_status;
	std::string                 g_lastMessage;

	void SetLastMessage(std::string a_message)
	{
		std::scoped_lock lock{ g_statusLock };
		g_lastMessage = std::move(a_message);
	}

	std::string WorkshopName(RE::TESObjectREFR* a_workshop)
	{
		if (const auto location = a_workshop->GetCurrentLocation()) {
			const auto name = RE::TESFullName::GetFullName(*location);
			if (!name.empty()) {
				return std::string{ name };
			}
		}
		return std::format("Workshop {:08X}", a_workshop->GetFormID());
	}

	RE::Workshop::ExtraData* GetWorkshopData(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref || a_ref->IsDeleted() || !a_ref->extraList) {
			return nullptr;
		}
		return a_ref->extraList->GetByType<RE::Workshop::ExtraData>();
	}

	// Checks one workshop, logs the result and, if asked, repairs it.
	WorkshopStatus ProcessWorkshop(RE::TESObjectREFR* a_workshop, RE::Workshop::ExtraData& a_extra, bool a_repair, bool a_logHealthy, std::size_t& a_removed)
	{
		WorkshopStatus status;
		status.id = a_workshop->GetFormID();
		status.name = WorkshopName(a_workshop);

		std::set<std::uint32_t> members;
		std::size_t             unmappedLinks = 0;
		const auto              findings = Analyze(a_extra, members, &unmappedLinks);

		status.grids = a_extra.powerGrid.size();
		status.objects = members.size();
		status.problems = findings.size();
		status.pluginLocalLinks = unmappedLinks;

		if (findings.empty() && !g_settings.dumpAll) {
			if (a_logHealthy) {
				REX::INFO("workshop [{:08X}] {}: {} grid(s), {} objects, OK{}",
					status.id, status.name, status.grids, status.objects,
					unmappedLinks ? std::format(" ({} harmless links in plugin data, left alone)", unmappedLinks) : std::string{});
			}
			return status;
		}

		std::set<std::uint32_t> problemIds;
		for (const auto& [id, finding] : findings) {
			problemIds.insert(id);
		}

		if (!a_repair && !g_settings.dumpAll && !findings.empty()) {
			const auto last = g_lastLogged.find(status.id);
			if (last != g_lastLogged.end() && last->second == problemIds) {
				REX::INFO("workshop [{:08X}] {}: same {} problem id(s) as the last scan (details above)",
					status.id, status.name, findings.size());
				return status;
			}
		}
		g_lastLogged[status.id] = problemIds;

		const auto& pos = a_workshop->data.location;
		REX::INFO("workshop [{:08X}] {} at ({:.0f}, {:.0f}, {:.0f}): {} grid(s), {} objects, {} problem id(s){}",
			status.id, status.name, pos.x, pos.y, pos.z, status.grids, status.objects, findings.size(),
			unmappedLinks ? std::format(", {} harmless links in plugin data", unmappedLinks) : std::string{});

		// The full grid can be thousands of lines; only write it when asked.
		if (g_settings.dumpAll) {
			DumpGrids(a_extra);
		}

		if (findings.empty()) {
			return status;
		}

		for (const auto& [id, finding] : findings) {
			REX::INFO("  PROBLEM [{:08X}] {}, referenced from:", id, StatusName(finding.status));
			for (const auto& where : finding.referencedFrom) {
				REX::INFO("    - {}", where);
			}
			for (const auto holder : finding.heldBy) {
				REX::INFO("    held by {}", Describe(holder));
			}
		}

		if (a_repair) {
			const auto removed = Repair(a_extra, findings, members);
			a_removed += removed;

			std::set<std::uint32_t> after;
			const auto              remaining = Analyze(a_extra, after);
			REX::INFO("  FIX: removed {} entries, {} problem id(s) remain", removed, remaining.size());

			status.problems = remaining.size();
			status.objects = after.size();
			status.lastAction = std::format("Repaired: {} entries removed, {} problem(s) left. Save your game.", removed, remaining.size());
		}

		return status;
	}

	void ScanAll(std::string_view a_reason, bool a_logHealthy)
	{
		const bool repairAll = g_settings.fix;
		REX::INFO("==== Scan: {} (bFix={}) ====", a_reason, repairAll);

		const auto workshops = FindWorkshops();

		std::vector<WorkshopStatus> statuses;
		std::size_t                 withProblems = 0;
		std::size_t                 totalRemoved = 0;

		for (const auto workshop : workshops) {
			const auto extra = GetWorkshopData(workshop);
			if (!extra) {
				continue;
			}
			auto status = ProcessWorkshop(workshop, *extra, repairAll, a_logHealthy, totalRemoved);
			if (status.problems > 0 || !status.lastAction.empty()) {
				++withProblems;
			}
			statuses.push_back(std::move(status));
		}

		REX::INFO("==== Scan done: {} workshop(s), {} with problems{} ====",
			workshops.size(), withProblems,
			repairAll ? std::format(", {} entries removed", totalRemoved) : std::string{});

		{
			std::scoped_lock lock{ g_statusLock };
			// Keep the note of an earlier repair visible after a rescan.
			for (auto& status : statuses) {
				if (!status.lastAction.empty()) {
					continue;
				}
				const auto old = std::ranges::find(g_status, status.id, &WorkshopStatus::id);
				if (old != g_status.end()) {
					status.lastAction = old->lastAction;
				}
			}
			g_status = std::move(statuses);
			g_lastMessage = std::format("Last scan: {} settlement(s), {} with problems", workshops.size(), withProblems);
		}

		if (withProblems > 0) {
			if (const auto console = RE::ConsoleLog::GetSingleton()) {
				console->Log("PowerGridDoctor: {} workshop(s) with power grid problems{}. See PowerGridDoctor.log",
					withProblems, repairAll ? " (fix applied - save your game)" : "");
			}
		}
	}

	void RepairOne(std::uint32_t a_workshopID)
	{
		REX::INFO("==== Repair requested from the menu for workshop [{:08X}] ====", a_workshopID);

		const auto form = RE::TESForm::GetFormByID(a_workshopID);
		const auto workshop = form ? form->IsReference() : nullptr;
		const auto extra = GetWorkshopData(workshop);
		if (!extra) {
			REX::INFO("  workshop not found");
			SetLastMessage(std::format("Repair failed: workshop {:08X} not found", a_workshopID));
			return;
		}

		std::size_t removed = 0;
		auto        status = ProcessWorkshop(workshop, *extra, true, true, removed);
		if (status.lastAction.empty()) {
			status.lastAction = "Nothing to repair";
		}

		{
			std::scoped_lock lock{ g_statusLock };
			const auto       it = std::ranges::find(g_status, status.id, &WorkshopStatus::id);
			if (it != g_status.end()) {
				*it = status;
			} else {
				g_status.push_back(status);
			}
			g_lastMessage = std::format("{}: {}", status.name, status.lastAction);
		}

		if (const auto console = RE::ConsoleLog::GetSingleton()) {
			console->Log("PowerGridDoctor: {} - {}", status.name, status.lastAction);
		}
	}

	// ------------------------------------------------------------------
	// PGD.h interface
	// ------------------------------------------------------------------

	std::vector<WorkshopStatus> GetStatusSnapshot()
	{
		std::scoped_lock lock{ g_statusLock };
		return g_status;
	}

	std::string GetLastMessage()
	{
		std::scoped_lock lock{ g_statusLock };
		return g_lastMessage;
	}

	void RequestScan()
	{
		if (const auto tasks = F4SE::GetTaskInterface()) {
			SetLastMessage("Scanning...");
			tasks->AddTask([]() { ScanAll("rescan from menu"sv, false); });
		}
	}

	void RequestRepair(std::uint32_t a_workshopID)
	{
		if (const auto tasks = F4SE::GetTaskInterface()) {
			SetLastMessage("Repairing...");
			tasks->AddTask([a_workshopID]() { RepairOne(a_workshopID); });
		}
	}

	// ------------------------------------------------------------------
	// Events
	// ------------------------------------------------------------------

	class MenuWatcher final :
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static MenuWatcher* GetSingleton()
		{
			static MenuWatcher singleton;
			return std::addressof(singleton);
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event.opening && a_event.menuName == "WorkshopMenu"sv) {
				// Menu events arrive on a UI job thread. Run the scan on the
				// main thread so we don't read grids while the game edits them.
				if (const auto tasks = F4SE::GetTaskInterface()) {
					tasks->AddTask([]() { ScanAll("workshop menu opened"sv, false); });
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	void OnMessage(F4SE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case F4SE::MessagingInterface::kGameDataReady:
			if (g_settings.scanOnWorkshopMenu) {
				if (const auto ui = RE::UI::GetSingleton()) {
					ui->RegisterSink<RE::MenuOpenCloseEvent>(MenuWatcher::GetSingleton());
					REX::INFO("Watching for the workshop menu");
				}
			}
			break;
		case F4SE::MessagingInterface::kPostLoadGame:
			g_lastLogged.clear();
			ScanAll("save loaded"sv, true);
			break;
		case F4SE::MessagingInterface::kNewGame:
			{
				std::scoped_lock lock{ g_statusLock };
				g_status.clear();
				g_lastMessage.clear();
			}
			g_lastLogged.clear();
			break;
		default:
			break;
		}
	}
}

// Old-gen F4SE (0.6.x, game 1.10.163) asks plugins through F4SEPlugin_Query and
// ignores the version data. Next-gen and Anniversary F4SE use the version data
// (generated by the build) and ignore this. Exporting both lets one DLL load on
// all three. Game addresses come from the Address Library for whichever runtime
// is running.
F4SE_PLUGIN_QUERY(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
	if (!a_f4se || !a_info || a_f4se->IsEditor()) {
		return false;
	}
	if (a_f4se->RuntimeVersion() != F4SE::RUNTIME_1_10_163) {
		return false;
	}
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = "PowerGridDoctor";
	a_info->version = 2;
	return true;
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::InitInfo init{};
	init.logPattern = "[%T] %v";  // time only; the default adds milliseconds, thread and level to every line
	F4SE::Init(a_f4se, init);

	const auto runtime = REX::FModule::GetExecutingModule().GetFileVersion();
	REX::INFO("Fallout 4 {} ({})", runtime.string(),
		REX::FModule::IsRuntimeOG() ? "old-gen" : (REX::FModule::IsRuntimeNG() ? "next-gen" : "Anniversary"));

	PGD::LoadSettings();
	REX::INFO("PowerGridDoctor loaded: bFix={} bDumpAll={} bScanOnWorkshopMenu={}",
		PGD::g_settings.fix, PGD::g_settings.dumpAll, PGD::g_settings.scanOnWorkshopMenu);

	const auto messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(PGD::OnMessage)) {
		REX::ERROR("Failed to register the F4SE message listener");
		return false;
	}

	PGD::RegisterMenu();

	return true;
}
