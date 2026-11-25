// wkbai - the timeline's Export: its events as a Markdown file or a web page

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include "GameChronicle.h"
#include "ChronicleNarrator.h"

struct TimelineExport {
	struct Input {
		const GameChronicle* chronicle = nullptr;
		bool all = false;           // every event (a replay, the game over); else those seen
		std::string viewer;         // whose view, for the note when not all
		std::function<std::string(uint32_t)> name;
		std::function<uint32_t(uint32_t)> colour; // ImU32: red in the low byte
		ChronicleNarrator::Context context;
	};
	// The events as text: the date, the title, the chronicle's paragraph and the numbers of the
	// Statistics view for each, so one file holds both views. html: a web page with the players
	// in their colours on a dark page like the game's windows; else Markdown.
	static std::string text(const Input& in, bool html, const std::string& when);
	// written to <user folder>/chronicles/Chronicle <date time>.md or .html; "" on failure
	static std::filesystem::path write(const Input& in, bool html);
};
