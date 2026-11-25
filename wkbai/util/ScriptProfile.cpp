// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

// The script share of the simulation, from the sampler's stacks (ScriptProfile.h).

#include "ScriptProfile.h"
#include "../platform.h"
#include "../file.h"
#include "../gameset/gameset.h"
#include "../gameset/actions.h"
#include "../gameset/condition.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

namespace {
	// what a frame's function is
	struct FrameInfo {
		std::string name;     // the symbol
		std::string node;     // a script function: its node type (the class) or function
		const char* category = nullptr; // a script function: value, finder, action, condition, position, context
		bool heap = false;    // the heap (malloc, free, new, delete, HeapAlloc...)
		bool growth = false;  // a container growing (vector's reallocation)
		bool simulate = false; // Server::simulate
	};

	// "A::B<C::D>::e" -> { "A", "B<C::D>", "e" }
	std::vector<std::string> SplitScope(const std::string& s)
	{
		std::vector<std::string> parts;
		int depth = 0;
		size_t start = 0;
		for (size_t i = 0; i < s.size(); i++) {
			const char c = s[i];
			if (c == '<' || c == '(' || c == '[') depth++;
			else if ((c == '>' || c == ')' || c == ']') && depth > 0) depth--;
			else if (c == '`') {
				// `anonymous namespace' and the like: up to the quote
				const size_t q = s.find('\'', i);
				if (q != std::string::npos) i = q;
			}
			else if (depth == 0 && c == ':' && i + 1 < s.size() && s[i + 1] == ':') {
				parts.push_back(s.substr(start, i - start));
				start = i + 2;
				i++;
			}
		}
		parts.push_back(s.substr(start));
		return parts;
	}

	const char* ScriptCategory(const char* file, const std::string& name)
	{
		const char* slash = std::max(strrchr(file, '\\'), strrchr(file, '/'));
		const char* base = slash ? slash + 1 : file;
		auto is = [&](const char* n) { return _stricmp(base, n) == 0; };
		if (is("values.cpp") || is("values.h")) return "value";
		if (is("finder.cpp") || is("finder.h")) return "finder";
		if (is("actions.cpp") || is("actions.h") || is("ObjectCreation.cpp")) return "action";
		if (is("condition.cpp")) return "condition";
		if (is("position.cpp") || is("position.h")) return "position";
		if (is("ScriptContext.cpp") || is("ScriptContext.h")) return "context";
		if (is("CommonEval.h")) {
			if (name.find(",ValueDeterminer>") != std::string::npos) return "value";
			if (name.find(",ObjectFinder>") != std::string::npos) return "finder";
			if (name.find(",PositionDeterminer>") != std::string::npos) return "position";
			return "context";
		}
		return nullptr;
	}

	// the node type of a script function: its class, else the function
	std::string NodeName(const std::string& sym)
	{
		std::vector<std::string> parts = SplitScope(sym);
		std::vector<std::string> kept;
		for (auto& p : parts)
			if (p.empty() || p[0] != '`') kept.push_back(p);
		if (kept.empty()) return sym;
		// CommonEval<D,T>::eval: D
		if (kept[0].rfind("CommonEval<", 0) == 0) {
			const size_t comma = kept[0].find(',');
			if (comma != std::string::npos) return kept[0].substr(11, comma - 11);
		}
		if (kept.size() == 1) return kept[0];
		const std::string& method = kept[1];
		if (method == "eval" || method == "run" || method.rfind("common_eval", 0) == 0 || method.rfind("<lambda", 0) == 0)
			return kept[0];
		return kept[0] + "::" + method;
	}

	bool IsHeap(const std::string& n)
	{
		static const char* names[] = { "malloc", "_malloc_base", "free", "_free_base", "realloc", "_realloc_base", "calloc", "_calloc_base",
			"RtlAllocateHeap", "RtlFreeHeap", "RtlReAllocateHeap", "HeapAlloc", "HeapFree", "HeapReAlloc", "_aligned_malloc", "_aligned_free" };
		for (const char* h : names)
			if (n == h) return true;
		return n.rfind("operator new", 0) == 0 || n.rfind("operator delete", 0) == 0;
	}

	bool IsGrowth(const std::string& n)
	{
		return n.find("_Emplace_reallocate") != std::string::npos || n.find("_Resize_reallocate") != std::string::npos
			|| n.find("::_Reallocate") != std::string::npos || n.find("_Insert_range") != std::string::npos;
	}

	// a game-set file's lines (for the sequences' names)
	const std::vector<std::string>& FileLines(const std::string& fn)
	{
		static std::map<std::string, std::vector<std::string>> files;
		auto it = files.find(fn);
		if (it != files.end()) return it->second;
		std::vector<std::string>& lines = files[fn];
		if (!FileExists(fn.c_str())) return lines;
		char* text = nullptr; int size = 0;
		LoadFile(fn.c_str(), &text, &size, 1);
		if (!text) return lines;
		std::string cur;
		for (int i = 0; i < size; i++) {
			if (text[i] == '\n') { lines.push_back(cur); cur.clear(); }
			else if (text[i] != '\r') cur += text[i];
		}
		lines.push_back(cur);
		free(text);
		return lines;
	}

	std::string Trim(const std::string& s)
	{
		size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
		return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
	}

	// "ORDER "Attack" / START_SEQUENCE (Orders.txt:120)": the top-level definition above the
	// sequence's first action and the line that opens the sequence
	std::string SequenceName(const GameSet* gs, const ActionSequence* seq)
	{
		if (!seq->debugInfo || seq->debugInfo->actionLineIndices.empty() || !gs) return "a sequence without lines";
		const int fi = seq->debugInfo->fileIndex;
		const int line = seq->debugInfo->actionLineIndices[0];
		const std::string fn = fi >= 0 && fi < (int)gs->gsfFileList.size() ? gs->gsfFileList.getString(fi) : std::string("?");
		const std::vector<std::string>& lines = FileLines(fn);
		const char* slash = std::max(strrchr(fn.c_str(), '\\'), strrchr(fn.c_str(), '/'));
		const std::string where = std::string(slash ? slash + 1 : fn.c_str()) + ":" + std::to_string(line);
		if (line < 1 || line > (int)lines.size()) return where;
		auto indent = [](const std::string& l) { size_t n = 0; while (n < l.size() && (l[n] == ' ' || l[n] == '\t')) n++; return n; };
		auto comment = [](const std::string& t) { return t.empty() || t.rfind("//", 0) == 0 || t[0] == '*' || t.rfind("/*", 0) == 0; };
		// a top-level block's keyword (the one-line declarations between blocks are not)
		auto block = [](const std::string& t) {
			static const char* tags[] = { "ORDER", "TASK", "REACTION", "ACTION_SEQUENCE", "COMMAND", "OBJECT_CREATION", "PACKAGE_RECEIPT_TRIGGER",
				"CHARACTER", "BUILDING", "PLAYER", "LEVEL", "MARKER", "CONTAINER", "PROP", "CITY", "TOWN", "FORMATION", "ARMY", "MISSILE",
				"TERRAIN_ZONE", "USER", "GAME_TEXT_WINDOW", "3D_CLIP", "CAMERA_PATH", "ARMY_CREATION_SCHEDULE", "WORK_ORDER", "COMMISSION", "PLAN" };
			const std::string first = t.substr(0, t.find_first_of(" \t"));
			if (first.size() > 10 && first.compare(first.size() - 10, 10, "_EXTENSION") == 0) return true;
			for (const char* tag : tags) if (first == tag) return true;
			return false;
		};
		const size_t actionIndent = indent(lines[line - 1]);
		std::string opener, header;
		for (int i = line - 2; i >= 0; i--) {
			const std::string t = Trim(lines[i]);
			if (comment(t)) continue;
			const size_t in = indent(lines[i]);
			if (opener.empty() && in < actionIndent) opener = t;
			if (in == 0 && block(t)) { header = t; break; }
		}
		if (header.empty() || header == opener)
			return opener + " (" + where + ")";
		return header + " / " + opener + " (" + where + ")";
	}
}

std::string ScriptSampleReport(const GameSet* gs, double msPerStep, int top, std::string* text)
{
	SamplerHalt();
	const std::vector<SamplerSample>& samples = SamplerSamples();
	HANDLE process = GetCurrentProcess();
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
	SymInitialize(process, nullptr, TRUE);
	std::unordered_map<DWORD64, FrameInfo> byAddress;
	std::unordered_map<DWORD64, const char*> fileOf; // function start -> script category (or none)
	auto info = [&](DWORD64 a) -> const FrameInfo& {
		auto it = byAddress.find(a);
		if (it != byAddress.end()) return it->second;
		FrameInfo fi;
		char buf[sizeof(SYMBOL_INFO) + 1024] = {};
		SYMBOL_INFO* sym = (SYMBOL_INFO*)buf;
		sym->SizeOfStruct = sizeof(SYMBOL_INFO);
		sym->MaxNameLen = 1023;
		DWORD64 disp = 0;
		if (SymFromAddr(process, a, &disp, sym)) {
			fi.name = sym->Name;
			auto f = fileOf.find(sym->Address);
			if (f == fileOf.end()) {
				IMAGEHLP_LINE64 line{};
				line.SizeOfStruct = sizeof(line);
				DWORD ldisp = 0;
				const char* cat = nullptr;
				// (the function's own file: at its start, not an inlined callee's at the address)
				if (SymGetLineFromAddr64(process, sym->Address, &ldisp, &line) || SymGetLineFromAddr64(process, a, &ldisp, &line))
					cat = ScriptCategory(line.FileName, fi.name);
				f = fileOf.emplace(sym->Address, cat).first;
			}
			fi.category = f->second;
		}
		else
			fi.name = "?";
		if (fi.category) fi.node = NodeName(fi.name);
		fi.heap = IsHeap(fi.name);
		fi.growth = IsGrowth(fi.name);
		fi.simulate = fi.name == "Server::simulate";
		return byAddress[a] = std::move(fi);
	};

	// the definitions' names
	std::unordered_map<const void*, std::string> defNames;
	if (gs) {
		for (size_t i = 0; i < gs->equations.size(); i++) defNames[gs->equations[i]] = "EQUATION \"" + gs->equations.getString((int)i) + "\"";
		for (size_t i = 0; i < gs->conditions.size(); i++) defNames[gs->conditions[i].test] = "CONDITION \"" + gs->conditions.getString((int)i) + "\"";
		for (size_t i = 0; i < gs->valueTags.size(); i++) defNames[gs->valueTags[i].get()] = "VALUE_TAG \"" + gs->valueTags.getString((int)i) + "\"";
		for (size_t i = 0; i < gs->objectFinderDefinitions.size(); i++)
			defNames[gs->objectFinderDefinitions[i]] = "OBJECT_FINDER_DEFINITION \"" + gs->objectFinderDefinitions.getString((int)i) + "\"";
		defNames.erase(nullptr);
	}
	auto defName = [&](const SamplerSample::Mark& m) -> const std::string& {
		auto it = defNames.find(m.p);
		if (it != defNames.end()) return it->second;
		if (m.kind == ScriptMarks::SEQUENCE)
			return defNames[m.p] = "SEQUENCE " + SequenceName(gs, (const ActionSequence*)m.p);
		char b[64];
		snprintf(b, sizeof(b), "%s %p", m.kind == ScriptMarks::FINDER ? "finder" : "value", m.p);
		return defNames[m.p] = b;
	};

	struct Own { double n = 0, walk = 0, alloc = 0, engine = 0; };
	uint64_t simSamples = 0, scriptSamples = 0, walk = 0, alloc = 0, growth = 0, engine = 0, truncated = 0;
	std::map<std::string, uint64_t> bucketAll, bucketScript, categoryOwn, nodeIncl, callees, callers, defOwn, defIncl, allocBy;
	std::map<std::string, Own> nodeOwn;
	for (const SamplerSample& s : samples) {
		const size_t n = s.frames.size();
		std::vector<const FrameInfo*> fr(n);
		bool inSim = false;
		for (size_t i = 0; i < n; i++) {
			// (above the innermost frame a return address: its call is the byte before)
			fr[i] = &info(i == 0 ? s.frames[i] : s.frames[i] - 1);
			inSim |= fr[i]->simulate;
		}
		if (!inSim) continue;
		simSamples++;
		const std::string bucket = s.phase && *s.phase ? s.phase : "other";
		bucketAll[bucket]++;
		size_t first = n, last = n;
		for (size_t i = 0; i < n; i++)
			if (fr[i]->category) { if (first == n) first = i; last = i; }
		if (first == n) continue;
		scriptSamples++;
		bucketScript[bucket]++;
		if (n >= 128) truncated++;
		bool heap = false, grow = false;
		for (size_t i = 0; i < first; i++) { heap |= fr[i]->heap; grow |= fr[i]->growth; }
		Own& own = nodeOwn[fr[first]->node];
		own.n++;
		categoryOwn[fr[first]->category]++;
		if (heap || grow) {
			alloc++; own.alloc++;
			if (grow) growth++;
			allocBy[fr[first]->node]++;
		}
		else if (first == 0) { walk++; own.walk++; }
		else { engine++; own.engine++; callees[fr[first - 1]->name]++; }
		std::set<std::string> seen;
		for (size_t i = first; i <= last; i++)
			if (fr[i]->category && seen.insert(fr[i]->node).second) nodeIncl[fr[i]->node]++;
		if (last + 1 < n) callers[fr[last + 1]->name]++;
		// the definitions: the innermost named one; inclusive, every named equation, finder,
		// condition and value tag once and the outermost sequence (the one the engine ran)
		std::set<const void*> seenDefs;
		const SamplerSample::Mark* inner = nullptr;
		bool outerSequence = false;
		for (const auto& m : s.marks) {
			if (!m.p) continue;
			inner = &m;
			if (m.kind == ScriptMarks::SEQUENCE) {
				if (outerSequence) continue;
				outerSequence = true;
			}
			if (seenDefs.insert(m.p).second) defIncl[defName(m)]++;
		}
		defOwn[inner ? defName(*inner) : std::string("(no definition: a node's own code outside a named one)")]++;
	}
	SymCleanup(process);

	const double sim = (double)std::max<uint64_t>(1, simSamples);
	auto ms = [&](double c) { return c / sim * msPerStep; };
	auto topList = [&](const std::map<std::string, uint64_t>& m) {
		std::vector<std::pair<uint64_t, std::string>> v;
		for (auto& [k, c] : m) v.emplace_back(c, k);
		std::sort(v.rbegin(), v.rend());
		nlohmann::json out = nlohmann::json::array();
		for (int i = 0; i < top && i < (int)v.size(); i++)
			out.push_back({ { "name", v[i].second }, { "samples", v[i].first }, { "share", v[i].first / sim }, { "ms", ms((double)v[i].first) } });
		return out;
	};
	nlohmann::json r;
	r["samples"] = samples.size();
	r["simSamples"] = simSamples;
	r["msPerStep"] = msPerStep;
	r["scriptSamples"] = scriptSamples;
	r["scriptShare"] = scriptSamples / sim;
	r["scriptMs"] = ms((double)scriptSamples);
	r["ownCodeMs"] = ms((double)walk);       // the nodes' own code: the tree walk
	r["allocMs"] = ms((double)alloc);        // the heap under a script (and lists growing)
	r["listGrowthMs"] = ms((double)growth);  // of which a vector's reallocation was on the stack
	r["engineMs"] = ms((double)engine);      // engine code a script called
	r["truncatedStacks"] = truncated;
	for (auto& [k, c] : categoryOwn) r["byCategory"][k] = ms((double)c);
	for (auto& [k, c] : bucketAll) {
		auto it = bucketScript.find(k);
		r["buckets"][k] = { { "ms", ms((double)c) }, { "scriptMs", ms(it != bucketScript.end() ? (double)it->second : 0.0) } };
	}
	{
		std::vector<std::pair<double, std::string>> v;
		for (auto& [k, o] : nodeOwn) v.emplace_back(o.n, k);
		std::sort(v.rbegin(), v.rend());
		nlohmann::json out = nlohmann::json::array();
		for (int i = 0; i < top && i < (int)v.size(); i++) {
			const Own& o = nodeOwn[v[i].second];
			out.push_back({ { "name", v[i].second }, { "ms", ms(o.n) }, { "ownCodeMs", ms(o.walk) }, { "allocMs", ms(o.alloc) }, { "engineMs", ms(o.engine) } });
		}
		r["nodesOwn"] = out;
	}
	r["nodesInclusive"] = topList(nodeIncl);
	r["allocByNode"] = topList(allocBy);
	r["engineCalledByScripts"] = topList(callees);
	r["scriptCallers"] = topList(callers);
	r["definitionsOwn"] = topList(defOwn);
	r["definitionsInclusive"] = topList(defIncl);

	if (text) {
		char b[1024];
		std::string& t = *text;
		snprintf(b, sizeof(b), "%llu samples in the simulation (%zu taken): scripts %.1f%% = %.2f ms of %.2f ms per step; own code (tree walk) %.2f, heap %.2f (lists growing %.2f), engine called %.2f; %llu stacks cut at 128 frames\n",
			(unsigned long long)simSamples, samples.size(), 100.0 * scriptSamples / sim, ms((double)scriptSamples), msPerStep, ms((double)walk), ms((double)alloc),
			ms((double)growth), ms((double)engine), (unsigned long long)truncated);
		t += b;
		t += "  by bucket (sampled ms per step, of it scripts):";
		for (auto& [k, c] : bucketAll) {
			auto it = bucketScript.find(k);
			snprintf(b, sizeof(b), " %s %.2f (%.2f)", k.c_str(), ms((double)c), ms(it != bucketScript.end() ? (double)it->second : 0.0));
			t += b;
		}
		t += "\n  by kind (own):";
		for (auto& [k, c] : categoryOwn) { snprintf(b, sizeof(b), " %s %.2f", k.c_str(), ms((double)c)); t += b; }
		t += "\n";
		auto lines = [&](const char* title, const nlohmann::json& list, bool parts) {
			t += std::string("  ") + title + ":\n";
			for (const auto& e : list) {
				if (parts)
					snprintf(b, sizeof(b), "    %6.3f ms  (own code %.3f, heap %.3f, engine %.3f)  %s\n", e["ms"].get<double>(), e["ownCodeMs"].get<double>(),
						e["allocMs"].get<double>(), e["engineMs"].get<double>(), e["name"].get<std::string>().c_str());
				else
					snprintf(b, sizeof(b), "    %6.3f ms  %5.1f%%  %s\n", e["ms"].get<double>(), 100.0 * e["share"].get<double>(), e["name"].get<std::string>().substr(0, 300).c_str());
				t += b;
			}
		};
		lines("node types, own time (with what they call outside scripts)", r["nodesOwn"], true);
		lines("node types, inclusive", r["nodesInclusive"], false);
		lines("heap under a script, by node type", r["allocByNode"], false);
		lines("engine functions scripts call", r["engineCalledByScripts"], false);
		lines("engine callers of scripts", r["scriptCallers"], false);
		lines("definitions, own (innermost named)", r["definitionsOwn"], false);
		lines("definitions, inclusive", r["definitionsInclusive"], false);
	}
	return r.dump();
}
#else
std::string ScriptSampleReport(const GameSet*, double, int, std::string*) { return "{}"; }
#endif
