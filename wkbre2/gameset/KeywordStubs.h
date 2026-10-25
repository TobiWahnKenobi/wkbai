// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

// Keywords of the original engine that wkbre2 does not implement yet, but whose
// argument lists are known from the shipped Battles game set.
//
// Without this, an unimplemented keyword nested inside a line leaves its arguments
// unread, and the next reader takes them for keywords (e.g. "Nearest Manor" read as
// two value determiners). A stub reads its arguments with the normal readers, so the
// rest of the line stays in sync, and evaluates to a neutral result with a one-time
// warning instead of aborting.
//
// Signature letters: F = object finder, V = value, P = position,
// S = quoted string, M = mode word (BY_HIGHEST / BY_LOWEST).

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "values.h"
#include "finder.h"
#include "position.h"
#include "../util/GSFileParser.h"

struct KeywordStubSignature { const char* keyword; const char* args; };

inline const KeywordStubSignature valueStubSignatures[] = {
	{"CAN_TRAVERSE_NEIGHBOURING_TILE", "FF"},
	{"CAN_TRAVERSE_TILE", "FF"},
	{"COMMISSION_POSSIBLE", "SF"},
	{"DOING_SAME_ORDER", "FF"},
	{"IS_BEHIND", "FF"},
	{"NUM_ORDERS", "F"},
	{"RANDOM_FRACTION", ""},
	{"WITHIN_ARC", "FFVVV"},
	{"WITHIN_ARC_TOWARDS", "FFFVV"},
};

// Finder keywords without the "FINDER_" prefix
inline const KeywordStubSignature finderStubSignatures[] = {
	{"GRADE_SELECT_CANDIDATES", "MVV"},
	{"PACKAGE_RELATED_PARTY", ""},
	{"WITHIN_HORIZ_DISTANCE", "V"},
};

inline const KeywordStubSignature positionStubSignatures[] = {
	{"ATTACHMENT_POINT_BY_INDEX", "SFV"},
	{"DESTINATION_OF", "F"},
	{"NEAREST_VALID_POSITION_TOWARDS", "FPP"},
	{"POINT_ALONG_PATH", "FPPV"},
	{"POSITION_BETWEEN", "FFV"},
};

template <size_t N>
const char* FindStubSignature(const KeywordStubSignature (&table)[N], const std::string& keyword)
{
	for (const auto& sig : table)
		if (keyword == sig.keyword)
			return sig.args;
	return nullptr;
}

struct KeywordStubArgs {
	std::string keyword;
	const char* signature = "";
	std::vector<std::unique_ptr<ObjectFinder>> finders;
	std::vector<std::unique_ptr<ValueDeterminer>> values;
	std::vector<std::unique_ptr<PositionDeterminer>> positions;
	bool warned = false;

	KeywordStubArgs(std::string keyword, const char* signature) : keyword(std::move(keyword)), signature(signature) {}

	void parse(GSFileParser& gsf, const GameSet& gs) {
		for (const char* c = signature; *c; ++c) {
			switch (*c) {
			case 'F': finders.emplace_back(ReadFinder(gsf, gs)); break;
			case 'V': values.emplace_back(ReadValueDeterminer(gsf, gs)); break;
			case 'P': positions.emplace_back(PositionDeterminer::createFrom(gsf, gs)); break;
			case 'S': gsf.nextString(true); break;
			case 'M': gsf.nextString(); break;
			}
		}
	}

	void warnOnce() {
		if (!warned) {
			printf("WARNING: %s is not implemented yet, using a neutral result\n", keyword.c_str());
			warned = true;
		}
	}
};
