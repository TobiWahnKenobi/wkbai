// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "WaterDesign.h"
#include "file.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>

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
	return defaultSea && waterways.empty() && waterfalls.empty() && autoWaterfalls
		&& std::all_of(surface.begin(), surface.end(), [](char c) { return c == AUTO; });
}

int WaterDesign::nearestWaterway(float x, float z, float& dirX, float& dirZ, float& distance, float& toMouth) const
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
			for (const auto& p : w.at("points"))
				ww.points.emplace_back(p.at(0).get<float>(), 0.0f, p.at(1).get<float>());
			if (ww.points.size() >= 2)
				waterways.push_back(std::move(ww));
		}
		waterfalls.clear();
		for (const auto& f : j.value("waterfalls", nlohmann::json::array()))
			waterfalls.push_back({ Vector3(f.at("from").at(0).get<float>(), 0, f.at("from").at(1).get<float>()),
				Vector3(f.at("to").at(0).get<float>(), 0, f.at("to").at(1).get<float>()) });
		autoWaterfalls = j.value("autoWaterfalls", true);
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
	j["version"] = 1;
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
		for (const Vector3& p : w.points)
			pts.push_back({ p.x, p.z });
		ws.push_back({ { "points", pts }, { "width", w.width } });
	}
	j["waterways"] = ws;
	auto fs = nlohmann::ordered_json::array();
	for (const PlacedFall& f : waterfalls)
		fs.push_back({ { "from", { f.from.x, f.from.z } }, { "to", { f.to.x, f.to.z } } });
	j["waterfalls"] = fs;
	j["autoWaterfalls"] = autoWaterfalls;
	// one row of the surface per line, so the file stays readable and diffs well
	return j.dump(1);
}
