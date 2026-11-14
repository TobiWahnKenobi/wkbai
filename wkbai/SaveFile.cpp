// wkbai - the save game file (SaveFile.h)

#include "SaveFile.h"
#include <cstdio>
#include <cstdlib>

namespace {
	constexpr std::string_view MAGIC = "WKBAI_SAVE ";

	// the line starting at `at` (without its line break, nor a \r before it); `next` after it
	std::string_view lineAt(std::string_view data, size_t at, size_t& next)
	{
		size_t end = data.find('\n', at);
		next = end == std::string_view::npos ? data.size() : end + 1;
		if (end == std::string_view::npos)
			end = data.size();
		std::string_view line = data.substr(at, end - at);
		if (!line.empty() && line.back() == '\r')
			line.remove_suffix(1);
		return line;
	}
}

bool SaveFile::IsSaveFile(std::string_view data)
{
	return data.substr(0, MAGIC.size()) == MAGIC;
}

void SaveFile::add(const std::string& name, int version, std::string text)
{
	if (text.empty())
		return;
	if (text.back() != '\n')
		text += '\n';
	sections.push_back({ name, version, std::move(text) });
}

const SaveSection* SaveFile::find(const std::string& name) const
{
	for (const SaveSection& s : sections)
		if (s.name == name)
			return &s;
	return nullptr;
}

std::string SaveFile::write() const
{
	std::string out = std::string(MAGIC) + std::to_string(FORMAT_VERSION) + "\n";
	for (const SaveSection& s : sections) {
		out += "SECTION " + s.name + " " + std::to_string(s.version) + " " + std::to_string(s.text.size()) + "\n";
		out += s.text;
		out += "END_SECTION " + s.name + "\n";
	}
	out += "END_SAVE\n";
	return out;
}

bool SaveFile::read(std::string_view data, std::string& error)
{
	sections.clear();
	if (!IsSaveFile(data)) {
		error = "not a wkbai save file";
		return false;
	}
	size_t at = 0;
	const std::string_view first = lineAt(data, 0, at);
	if (const int version = atoi(std::string(first.substr(MAGIC.size())).c_str()); version > FORMAT_VERSION)
		printf("Save game: file format %d, newer than this build's %d; reading what it knows\n", version, FORMAT_VERSION);
	while (at < data.size()) {
		size_t next;
		const std::string_view line = lineAt(data, at, next);
		if (line.empty()) {
			at = next;
			continue;
		}
		if (line == "END_SAVE")
			return true;
		char name[128] = {};
		int version = 0;
		size_t bytes = 0;
		if (line.substr(0, 8) != "SECTION " || sscanf(std::string(line).c_str(), "SECTION %127s %d %zu", name, &version, &bytes) != 3) {
			error = "expected a SECTION line, found \"" + std::string(line.substr(0, 60)) + "\"";
			return false;
		}
		const size_t start = next;
		const std::string end = "END_SECTION " + std::string(name);
		SaveSection s{ name, version, {} };
		// the length as written; if the file was changed (line breaks converted by an editor), the
		// section's end line instead
		size_t after = 0;
		if (start + bytes <= data.size() && data.substr(start + bytes, end.size()) == end) {
			s.text = std::string(data.substr(start, bytes));
			after = start + bytes;
		}
		else {
			size_t found = std::string_view::npos;
			for (size_t p = data.find(end, start); p != std::string_view::npos; p = data.find(end, p + 1))
				if (p == start || data[p - 1] == '\n') {
					const size_t q = p + end.size();
					if (q == data.size() || data[q] == '\r' || data[q] == '\n') { found = p; break; }
				}
			if (found == std::string_view::npos) {
				error = "section " + std::string(name) + " has no end";
				return false;
			}
			printf("Save game: section %s isn't %zu bytes long (the file was changed?); read up to its end line\n", name, bytes);
			// (the line breaks back to \n: the settings' values would end with \r)
			for (char c : data.substr(start, found - start))
				if (c == '\n' && !s.text.empty() && s.text.back() == '\r')
					s.text.back() = '\n';
				else
					s.text += c;
			after = found;
		}
		lineAt(data, after, at);
		sections.push_back(std::move(s));
	}
	return true; // (no END_SAVE: a file cut short after a whole section)
}
