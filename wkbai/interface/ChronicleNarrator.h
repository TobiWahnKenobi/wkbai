// wkbai - the timeline told as a chronicle: each event as a paragraph a historian could have
// written ("In the spring of the year 1000, the hosts of Saint Jane and Player 2 met at Nen
// Hithoel. ..."), from the same facts as the statistics view (the RE repo's
// docs/engine/unit-history.md, section 10)

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "GameChronicle.h"

struct EnvironmentSettings;

struct ChronicleNarrator {
	// a piece of the text: plain, a player's name (player != 0, drawn in its colour) or a unit
	// named in the event (mention >= 0: its index in the event's mentions, drawn as a link)
	struct Piece {
		std::string text;
		uint32_t player = 0;
		int mention = -1;
	};
	struct Context {
		std::function<std::string(uint32_t)> playerName;
		// the calendar: with the seasons, the season and the year (from 1000); with day and night,
		// the day and the time of day; else the war's progress
		bool seasons = false, dayNight = false;
		float seasonLengthS = 1800.0f, dayLengthS = 1200.0f, yearShift = 0.0f;
		int startSeason = 0, startHour = 8;
		void fromSettings(const EnvironmentSettings& env, bool active);
	};

	// the event's paragraph. The wording is chosen by the event's number and time: every client
	// and every reading of a save tells it the same way.
	static std::vector<Piece> narrate(const GameChronicle::Event& e, const Context& c);
	// the date line over it: "Spring, the year 1000, dusk" / "Day 3, morning" / "0:42"
	static std::string dateLine(float time, const Context& c);
	// the pieces as one string (tests, logs)
	static std::string plain(const std::vector<Piece>& pieces);
};
