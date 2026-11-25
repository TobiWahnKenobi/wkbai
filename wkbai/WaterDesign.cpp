// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "WaterDesign.h"
#include "file.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

WaterDesign WaterDesign::blank(int width, int height)
{
	WaterDesign d;
	d.width = width;
	d.height = height;
	d.surface.assign((size_t)width * height, AUTO);
	return d;
}

bool WaterDesign::isEmpty() const
{
	const bool defaultSea = seas.size() == 1 && std::abs(seas[0].windDegrees - SeaRegion().windDegrees) < 1e-3f && std::abs(seas[0].waves - 1.0f) < 1e-3f;
	return defaultSea && waterways.empty() && waterfalls.empty() && autoWaterfalls && farland.empty()
		&& std::all_of(surface.begin(), surface.end(), [](char c) { return c == AUTO; });
}

bool WaterDesign::hasSlopedWater() const
{
	return std::any_of(waterways.begin(), waterways.end(), [](const Waterway& w) { return w.sloped(); });
}

float WaterDesign::Waterway::speedAt(int i, float t) const
{
	if (speeds.size() != points.size() || speeds.empty())
		return speed;
	i = std::clamp(i, 0, (int)speeds.size() - 1);
	const float a = speeds[i] > 0.0f ? speeds[i] : speed;
	const int j = std::min(i + 1, (int)speeds.size() - 1);
	const float b = speeds[j] > 0.0f ? speeds[j] : speed;
	// (where one end has none set and the waterway no speed of its own: the other end's)
	if (a <= 0.0f || b <= 0.0f)
		return std::max(a, b);
	return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

bool WaterDesign::Waterway::hasSpeed() const
{
	return speed > 0.0f || std::any_of(speeds.begin(), speeds.end(), [](float s) { return s > 0.0f; });
}

float WaterDesign::designedSpeed(float x, float z) const
{
	float dx, dz, d, m, t;
	int seg;
	const int w = nearestWaterway(x, z, dx, dz, d, m, &seg, &t);
	return w >= 0 ? waterways[w].speedAt(seg, t) : 0.0f;
}

const char* WaterDesign::FarHint::kindName(Kind k)
{
	switch (k) {
	case SEA: return "sea";
	case LAND: return "land";
	case MOUNTAINS: return "mountains";
	case LOWLAND: return "lowland";
	case RIVER: return "river";
	}
	return "sea";
}

bool WaterDesign::FarHint::kindFrom(const std::string& s, Kind& k)
{
	for (Kind c : { SEA, LAND, MOUNTAINS, LOWLAND, RIVER })
		if (s == kindName(c)) {
			k = c;
			return true;
		}
	return false;
}

float WaterDesign::FarHint::across(float dirDegrees) const
{
	float diff = std::fmod(std::abs(dirDegrees - degrees), 360.0f);
	if (diff > 180.0f)
		diff = 360.0f - diff;
	// full over the middle half of the spread, fading out to its edges (and a little beyond)
	const float half = std::max(spread, 1.0f) * 0.5f;
	const float k = std::clamp((half * 1.25f - diff) / (half * 0.75f), 0.0f, 1.0f);
	return k * k * (3 - 2 * k);
}

bool WaterDesign::directionFrom(const std::string& text, float& degrees)
{
	std::string s;
	for (char c : text)
		if (c != '-' && c != ' ' && c != '_')
			s += (char)tolower((unsigned char)c);
	// north +z (the top of heights.png), east +x: degrees from +x towards +z
	static const std::pair<const char*, float> names[] = { { "east", 0 }, { "e", 0 }, { "northeast", 45 }, { "ne", 45 },
		{ "north", 90 }, { "n", 90 }, { "northwest", 135 }, { "nw", 135 }, { "west", 180 }, { "w", 180 },
		{ "southwest", 225 }, { "sw", 225 }, { "south", 270 }, { "s", 270 }, { "southeast", 315 }, { "se", 315 } };
	for (const auto& [n, d] : names)
		if (s == n) {
			degrees = d;
			return true;
		}
	char* end = nullptr;
	const double v = std::strtod(s.c_str(), &end);
	if (!s.empty() && end && *end == 0) {
		degrees = (float)v;
		return true;
	}
	return false;
}

int WaterDesign::nearestWaterway(float x, float z, float& dirX, float& dirZ, float& distance, float& toMouth, int* segment, float* along) const
{
	int best = -1;
	distance = 1e30f;
	for (int w = 0; w < (int)waterways.size(); w++) {
		const auto& pts = waterways[w].points;
		// the length from each point to the mouth
		std::vector<float> rest(pts.size(), 0.0f);
		for (int i = (int)pts.size() - 2; i >= 0; i--)
			rest[i] = rest[i + 1] + std::hypot(pts[i + 1].x - pts[i].x, pts[i + 1].z - pts[i].z);
		for (size_t i = 0; i + 1 < pts.size(); i++) {
			const float ax = pts[i].x, az = pts[i].z, sx = pts[i + 1].x - ax, sz = pts[i + 1].z - az;
			const float len2 = sx * sx + sz * sz;
			if (len2 < 1e-6f)
				continue;
			const float t = std::clamp(((x - ax) * sx + (z - az) * sz) / len2, 0.0f, 1.0f);
			const float d = std::hypot(x - (ax + sx * t), z - (az + sz * t));
			if (d < distance && d <= waterways[w].width) {
				const float len = std::sqrt(len2);
				distance = d;
				best = w;
				dirX = sx / len;
				dirZ = sz / len;
				toMouth = rest[i + 1] + len * (1 - t);
				if (segment)
					*segment = (int)i;
				if (along)
					*along = t;
			}
		}
	}
	return best;
}

std::string WaterDesign::pathFor(const std::string& mapFile)
{
	const size_t dot = mapFile.find_last_of('.');
	return (dot == std::string::npos ? mapFile : mapFile.substr(0, dot)) + ".water";
}

bool WaterDesign::load(const std::string& mapFile, std::string& error)
{
	const std::string path = pathFor(mapFile);
	if (!FileExists(path.c_str()))
		return true;
	char* data;
	int size;
	LoadFile(path.c_str(), &data, &size);
	const std::string text(data, size);
	free(data);
	if (!fromText(text, error)) {
		error = path + ": " + error;
		return false;
	}
	return true;
}

bool WaterDesign::fromText(const std::string& text, std::string& error)
{
	nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
	if (j.is_discarded() || !j.is_object()) {
		error = "not JSON";
		return false;
	}
	if (j.value("format", "") != "wkbai water") {
		error = "not a wkbai water file";
		return false;
	}
	try {
		const auto tiles = j.at("tiles");
		width = tiles.at(0).get<int>();
		height = tiles.at(1).get<int>();
		surface.assign((size_t)width * height, AUTO);
		if (j.contains("surface")) {
			const auto& rows = j.at("surface");
			for (int z = 0; z < height && z < (int)rows.size(); z++) {
				const std::string row = rows[z].get<std::string>();
				for (int x = 0; x < width && x < (int)row.size(); x++) {
					const char c = row[x];
					surface[(size_t)z * width + x] = (c == RIVER || c == LAKE || seaRegionOf(c) >= 0) ? c : (char)AUTO;
				}
			}
		}
		seas.clear();
		for (const auto& s : j.value("seas", nlohmann::json::array())) {
			if (seas.size() >= (size_t)MAX_SEA_REGIONS)
				break;
			SeaRegion r;
			r.name = s.value("name", "");
			r.windDegrees = s.value("wind", r.windDegrees);
			r.waves = std::clamp(s.value("waves", 1.0f), 0.0f, 3.0f);
			seas.push_back(r);
		}
		if (seas.empty())
			seas.push_back(SeaRegion{ "Sea" });
		waterways.clear();
		for (const auto& w : j.value("waterways", nlohmann::json::array())) {
			Waterway ww;
			ww.width = std::clamp(w.value("width", ww.width), 2.5f, 200.0f);
			ww.speed = std::clamp(w.value("speed", 0.0f), 0.0f, 20.0f);
			ww.by = w.value("by", "");
			for (const auto& v : w.value("speeds", nlohmann::json::array()))
				ww.speeds.push_back(std::clamp(v.is_number() ? v.get<float>() : 0.0f, 0.0f, 20.0f));
			bool allLevels = true;
			for (const auto& p : w.at("points")) {
				ww.points.emplace_back(p.at(0).get<float>(), 0.0f, p.at(1).get<float>());
				if (p.size() >= 3 && p[2].is_number())
					ww.levels.push_back(p[2].get<float>());
				else
					allLevels = false;
			}
			// a level on every point, never rising downstream; else a river of the level water
			if (!allLevels || ww.levels.size() != ww.points.size())
				ww.levels.clear();
			for (size_t i = 1; i < ww.levels.size(); i++)
				ww.levels[i] = std::min(ww.levels[i], ww.levels[i - 1]);
			if (ww.speeds.size() != ww.points.size())
				ww.speeds.clear();
			if (ww.points.size() >= 2)
				waterways.push_back(std::move(ww));
		}
		waterfalls.clear();
		for (const auto& f : j.value("waterfalls", nlohmann::json::array()))
			waterfalls.push_back({ Vector3(f.at("from").at(0).get<float>(), 0, f.at("from").at(1).get<float>()),
				Vector3(f.at("to").at(0).get<float>(), 0, f.at("to").at(1).get<float>()) });
		autoWaterfalls = j.value("autoWaterfalls", true);
		farland.clear();
		for (const auto& f : j.value("farland", nlohmann::json::array())) {
			FarHint hint;
			if (!FarHint::kindFrom(f.value("kind", "sea"), hint.kind))
				throw std::runtime_error("farland: unknown kind " + f.value("kind", ""));
			const auto& toward = f.contains("toward") ? f.at("toward") : nlohmann::json();
			if (toward.is_number())
				hint.degrees = toward.get<float>();
			else if (!toward.is_string() || !directionFrom(toward.get<std::string>(), hint.degrees))
				throw std::runtime_error("farland: \"toward\" must be a compass name or degrees");
			hint.spread = std::clamp(f.value("spread", hint.spread), 5.0f, 360.0f);
			hint.from = std::clamp(f.value("from", hint.from), 0.0f, 2000.0f);
			hint.weight = std::clamp(f.value("weight", hint.weight), 0.0f, 1.0f);
			if (f.contains("at")) {
				hint.hasAt = true;
				hint.at = Vector3(f.at("at").at(0).get<float>(), 0, f.at("at").at(1).get<float>());
			}
			if (f.contains("level"))
				hint.level = f.at("level").get<float>();
			farland.push_back(hint);
		}
	}
	catch (const std::exception& e) {
		error = e.what();
		*this = WaterDesign();
		return false;
	}
	return true;
}

bool WaterDesign::save(const std::filesystem::path& path) const
{
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream f(path, std::ios::binary);
	f << toText() << '\n';
	return (bool)f;
}

std::string WaterDesign::toText() const
{
	nlohmann::ordered_json j;
	j["format"] = "wkbai water";
	const bool v2 = !farland.empty() || std::any_of(waterways.begin(), waterways.end(), [](const Waterway& w) { return !w.speeds.empty(); });
	j["version"] = v2 ? 2 : 1;
	j["tiles"] = { width, height };
	auto rows = nlohmann::ordered_json::array();
	for (int z = 0; z < height; z++)
		rows.push_back(std::string(surface.begin() + (size_t)z * width, surface.begin() + (size_t)(z + 1) * width));
	j["surface"] = rows;
	auto s = nlohmann::ordered_json::array();
	for (const SeaRegion& r : seas)
		s.push_back({ { "name", r.name }, { "wind", r.windDegrees }, { "waves", r.waves } });
	j["seas"] = s;
	auto ws = nlohmann::ordered_json::array();
	for (const Waterway& w : waterways) {
		auto pts = nlohmann::ordered_json::array();
		for (size_t i = 0; i < w.points.size(); i++) {
			const Vector3& p = w.points[i];
			if (w.sloped())
				pts.push_back({ p.x, p.z, std::round(w.levels[i] * 1000.0f) / 1000.0f });
			else
				pts.push_back({ p.x, p.z });
		}
		nlohmann::ordered_json way = { { "points", pts }, { "width", w.width } };
		if (w.speed > 0.0f)
			way["speed"] = w.speed;
		if (!w.speeds.empty())
			way["speeds"] = w.speeds;
		if (!w.by.empty())
			way["by"] = w.by;
		ws.push_back(way);
	}
	j["waterways"] = ws;
	auto fs = nlohmann::ordered_json::array();
	for (const PlacedFall& f : waterfalls)
		fs.push_back({ { "from", { f.from.x, f.from.z } }, { "to", { f.to.x, f.to.z } } });
	j["waterfalls"] = fs;
	j["autoWaterfalls"] = autoWaterfalls;
	if (!farland.empty()) {
		auto fl = nlohmann::ordered_json::array();
		for (const FarHint& h : farland) {
			nlohmann::ordered_json o = { { "kind", FarHint::kindName(h.kind) }, { "toward", h.degrees }, { "spread", h.spread },
				{ "from", h.from }, { "weight", h.weight } };
			if (h.hasAt)
				o["at"] = { h.at.x, h.at.z };
			if (h.level > -1e29f)
				o["level"] = h.level;
			fl.push_back(o);
		}
		j["farland"] = fl;
	}
	// one row of the surface per line, so the file stays readable and diffs well
	return j.dump(1);
}
