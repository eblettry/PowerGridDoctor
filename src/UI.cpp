// Settlement page for F4SE Menu Framework.
//
// This file deliberately does not include CommonLibF4: the menu framework header
// pulls in <windows.h>, which clashes with CommonLib's own Windows declarations.

#ifndef UNICODE
#	define UNICODE
#endif
#ifndef _UNICODE
#	define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

// The framework header relies on the includer for these.
#include <atomic>
#include <cfloat>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>

namespace RE
{
	class InputEvent;  // only used as a pointer type by the framework header
}

#include "F4SEMenuFramework.h"

#include "PGD.h"

namespace
{
	const ImGuiMCP::ImVec4 kRed{ 1.0f, 0.35f, 0.35f, 1.0f };
	const ImGuiMCP::ImVec4 kGreen{ 0.45f, 0.9f, 0.45f, 1.0f };

	void __stdcall RenderSettlements()
	{
		using namespace ImGuiMCP;

		TextWrapped("Power grid status of every settlement in the loaded game. "
					"Repair removes references to objects that no longer exist, for that settlement only. "
					"Make a backup save first, and save after repairing.");
		Spacing();

		if (Button("Rescan all")) {
			PGD::RequestScan();
		}

		const auto message = PGD::GetLastMessage();
		if (!message.empty()) {
			SameLine();
			Text("%s", message.c_str());
		}

		Separator();

		const auto snapshot = PGD::GetStatusSnapshot();
		if (snapshot.empty()) {
			TextDisabled("No scan yet. Load a save.");
			return;
		}

		constexpr int flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
		if (!BeginTable("PowerGridDoctorSettlements", 5, flags)) {
			return;
		}

		TableSetupColumn("Settlement");
		TableSetupColumn("Workshop");
		TableSetupColumn("Objects");
		TableSetupColumn("Status");
		TableSetupColumn("Action");
		TableHeadersRow();

		for (const auto& ws : snapshot) {
			TableNextRow();

			TableNextColumn();
			Text("%s", ws.name.c_str());

			TableNextColumn();
			Text("%08X", ws.id);

			TableNextColumn();
			Text("%zu", ws.objects);

			TableNextColumn();
			if (ws.problems > 0) {
				TextColored(kRed, "%zu problem(s)", ws.problems);
			} else {
				TextColored(kGreen, "OK");
			}

			TableNextColumn();
			if (ws.problems > 0) {
				PushID(static_cast<int>(ws.id));
				if (Button("Repair")) {
					PGD::RequestRepair(ws.id);
				}
				PopID();
			}
			if (!ws.lastAction.empty()) {
				if (ws.problems > 0) {
					SameLine();
				}
				TextDisabled("%s", ws.lastAction.c_str());
			}
		}

		EndTable();

		Spacing();
		TextDisabled("Details for every scan and repair: Documents\\My Games\\Fallout4\\F4SE\\PowerGridDoctor.log");
	}
}

void PGD::RegisterMenu()
{
	if (!F4SEMenuFramework::IsInstalled()) {
		return;
	}

	// The header looks the framework up once at static init; look again in case
	// it wasn't loaded yet at that point.
	if (!menuFramework) {
		menuFramework = GetModuleHandle(L"F4SEMenuFramework");
	}
	if (!menuFramework) {
		return;
	}

	F4SEMenuFramework::SetSection("Power Grid Doctor");
	F4SEMenuFramework::AddSectionItem("Settlements", RenderSettlements);
}
