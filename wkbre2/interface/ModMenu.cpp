// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "ModMenu.h"
#include <algorithm>
#include "../imgui/imgui.h"

void ModMenu::open()
{
	mods = GetMods();
	customOrder = IsCustomModLoadOrder();
	dirty = false;
	visible = true;
}

bool ModMenu::draw()
{
	if (!visible)
		return false;

	bool applied = false;
	// Next to the "Select sav" window rather than on top of it
	ImGui::SetNextWindowPos(ImVec2(480, 60), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(620, 420), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Mods", &visible)) {
		if (customOrder)
			ImGui::TextWrapped("Custom load order: mods further down the list override the mods above them.");
		else
			ImGui::TextWrapped("Original order: like Warrior Kings, each file is taken from whichever archive or folder "
				"has the newest version of it. Move a mod to set your own load order instead.");
		ImGui::Separator();

		ImGui::TextDisabled("data.bcp (base game, always loaded first)");
		for (size_t i = 0; i < mods.size(); ++i) {
			ImGui::PushID((int)i);
			if (ImGui::Checkbox("##enabled", &mods[i].enabled))
				dirty = true;
			ImGui::SameLine();
			ImGui::BeginDisabled(i == 0);
			if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
				std::swap(mods[i], mods[i - 1]);
				customOrder = dirty = true;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(i + 1 == mods.size());
			if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
				std::swap(mods[i], mods[i + 1]);
				customOrder = dirty = true;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::TextUnformatted(mods[i].name.c_str());
			if (ImGui::IsItemHovered()) {
				std::string tip = mods[i].id;
				if (!mods[i].description.empty())
					tip += "\n" + mods[i].description;
				ImGui::SetTooltip("%s", tip.c_str());
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(%s)", mods[i].kind.c_str());
			ImGui::PopID();
		}
		if (mods.empty())
			ImGui::TextDisabled("No mods found. Put .bcp archives into the game folder, or mod folders into \"mods\".");

		ImGui::Separator();
		ImGui::BeginDisabled(!dirty);
		if (ImGui::Button("Apply")) {
			ApplyModConfiguration(mods, customOrder);
			open();
			applied = true;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!customOrder);
		if (ImGui::Button("Use original order")) {
			customOrder = false;
			dirty = true;
			std::stable_sort(mods.begin(), mods.end(), [](const ModInfo& a, const ModInfo& b) { return a.newestTime < b.newestTime; });
		}
		ImGui::EndDisabled();
		if (dirty) {
			ImGui::SameLine();
			ImGui::TextDisabled("Changes apply when you click Apply.");
		}
	}
	ImGui::End();
	return applied;
}
