// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "condition.h"
#include "gameset.h"
#include "../util/GSFileParser.h"
#include "../Language.h"
#include "../tags.h"

void GSCondition::readHintValues(GSFileParser& gsf, const GameSet& gs, std::vector<ValueDeterminer*>& hintValues)
{
	while (!gsf.eol) {
		// Battles hints can also name a blueprint, e.g. `DEFINED_VALUE "Wood Cost" BUILDING "Unaligned Wall"`.
		// Its use in the hint text is not known yet; skip it so it isn't read as value determiners.
		const char* oldcur = gsf.cursor;
		std::string word = gsf.nextString();
		if (Tags::GAMEOBJCLASS_tagDict.getTagID(word.c_str()) != -1) {
			gsf.nextString(true);
			continue;
		}
		gsf.cursor = oldcur;
		hintValues.push_back(ReadValueDeterminer(gsf, gs));
	}
}

namespace {
// A CONDITION with several TEST lines holds when all of them hold, e.g. Fort.cpp's
// "Can Build Fort Wood": TEST "Can Build Fort Wood" (the guildhall research) and TEST
// "Can Build Watch Tower", with the hint "Arcanum_requires_Black_Mass_and_Observatory" for
// Arcanum.cpp's two TESTs. Keeping only the last TEST let the Fort be built before its research.
struct AllTestsDeterminer : ValueDeterminer {
	std::vector<ValueDeterminer*> tests;
	float eval(ScriptContext* ctx) override {
		for (ValueDeterminer* t : tests)
			if (!t->booleval(ctx))
				return 0.0f;
		return 1.0f;
	}
	void parse(GSFileParser&, const GameSet&) override {}
};
}

void GSCondition::parse(GSFileParser & gsf, GameSet & gs)
{
	gsf.advanceLine();
	AllTestsDeterminer* all = nullptr;
	while (!gsf.eof) {
		auto tag = gsf.nextTag();
		if (tag == "TEST") {
			ValueDeterminer* next = ReadValueDeterminer(gsf, gs);
			if (!test)
				test = next;
			else {
				if (!all) {
					all = new AllTestsDeterminer;
					all->tests.push_back(test);
					test = all;
				}
				all->tests.push_back(next);
			}
		}
		else if (tag == "HINT") {
			hint = gsf.nextString();
			readHintValues(gsf, gs, hintValues);
		}
		else if (tag == "END_CONDITION")
			break;
		gsf.advanceLine();
	}
}

std::string GSCondition::getFormattedHint(const std::string& hint, const std::vector<ValueDeterminer*>& hintValues, const Language& lang, ScriptContext* ctx)
{
	std::string langHint = lang.getText(hint);
	std::string result;
	result.reserve(langHint.size() + hintValues.size() * 8);
	size_t valIndex = 0;
	for (size_t i = 0; i < langHint.size(); ++i) {
		char c = langHint[i];
		if (c == '%') {
			switch (langHint[i + 1]) {
			case '%': result.push_back('%'); break;
			case 'd': result.append(std::to_string((int)hintValues.at(valIndex++)->eval(ctx))); break;
			case 0: return result;
			}
			++i;
		}
		else if (c == '\\') {
			switch (langHint[i + 1]) {
			case '\\': result.push_back('\\'); break;
			case 'n': result.push_back('\n'); break;
			case 0: return result;
			}
			++i;
		}
		else {
			result.push_back(c);
		}
	}
	return result;
}

std::string GSCondition::getFormattedHint(const Language& lang, ScriptContext* ctx) const
{
	return getFormattedHint(hint, hintValues, lang, ctx);
}

