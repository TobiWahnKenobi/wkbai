// wkbai - the in-game help: a page drawn from its Markdown (MD4C), with links, pictures, tables
// and the game set's live numbers
// Licensed under the GNU General Public License 3
//
// MD4C (https://github.com/mity/md4c, MIT, Copyright (c) 2016-2024 Martin Mitáš) parses the
// Markdown; lunasvg (https://github.com/sammycage/lunasvg, MIT, Copyright (c) 2020-2025 Samuel
// Ugochukwu) draws the SVG diagrams. Both come from vcpkg; see THIRD-PARTY-NOTICES.md.

#include "HelpInternal.h"
#include "Help.h"
#include "../file.h"
#include "../window.h"
#include "../Language.h"
#include "../gfx/renderer.h"
#include "../gfx/bitmap.h"
#include "../gameset/gameset.h"
#include "../gameset/GameObjBlueprint.h"
#include "../imgui/imgui.h"
#include <md4c.h>
#include <lunasvg/lunasvg.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

namespace Help::detail {

// ---------------------------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------------------------

namespace { ImFont* s_fonts[F_COUNT] = {}; }

ImFont* Font(int kind)
{
	if (s_fonts[kind])
		return s_fonts[kind];
	if (kind == F_H1 || kind == F_H2 || kind == F_H3)
		if (s_fonts[F_BOLD])
			return s_fonts[F_BOLD];
	return s_fonts[F_BODY] ? s_fonts[F_BODY] : ImGui::GetFont();
}

// ---------------------------------------------------------------------------------------------
// The document: blocks of spans
// ---------------------------------------------------------------------------------------------

namespace {

enum { S_BOLD = 1, S_ITALIC = 2, S_CODE = 4, S_STRIKE = 8 };

struct Span {
	std::string text;
	uint8_t style = 0;
	int link = -1;       // Doc::links
	int image = -1;      // Doc::images
	bool newline = false;
};

struct Cell {
	std::vector<Span> spans;
	int align = 0;       // 0 left, 1 centre, 2 right
};

struct Block {
	enum Kind { PARA, HEADING, CODE, RULE, TABLE, FACTS, IMAGE } kind = PARA;
	int level = 0;
	int indent = 0;
	std::string bullet;
	int quote = 0;
	bool staticFacts = false;
	std::string anchor;
	std::vector<Span> spans;
	std::string code;
	std::vector<std::vector<Cell>> rows;
	int headerRows = 0;
	int image = -1;
	// the layout's cache
	float layoutWidth = -1, height = 0;
};

struct Doc {
	std::vector<Block> blocks;
	std::vector<std::string> links;
	struct Image { std::string url, alt; };
	std::vector<Image> images;
	bool hasFacts = false;
};

void appendUtf8(std::string& s, unsigned cp)
{
	if (cp < 0x80) s += (char)cp;
	else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
	else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
	else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}

std::string entity(const std::string& e)
{
	static const std::pair<const char*, unsigned> named[] = {
		{ "&lt;", '<' }, { "&gt;", '>' }, { "&amp;", '&' }, { "&quot;", '"' }, { "&apos;", '\'' },
		{ "&nbsp;", 0xA0 }, { "&mdash;", 0x2014 }, { "&ndash;", 0x2013 }, { "&hellip;", 0x2026 },
		{ "&rarr;", 0x2192 }, { "&larr;", 0x2190 }, { "&times;", 0xD7 }, { "&deg;", 0xB0 },
	};
	std::string out;
	for (const auto& [n, cp] : named)
		if (e == n) { appendUtf8(out, cp); return out; }
	if (e.size() > 3 && e[1] == '#') {
		const bool hex = e[2] == 'x' || e[2] == 'X';
		const unsigned cp = (unsigned)strtoul(e.c_str() + (hex ? 3 : 2), nullptr, hex ? 16 : 10);
		if (cp) { appendUtf8(out, cp); return out; }
	}
	return e;
}

struct Builder {
	Doc doc;
	std::vector<std::pair<bool, int>> lists; // ordered, next number
	int quote = 0;
	int cur = -1;              // the block taking text (paragraph, heading)
	int code = -1;
	std::string pendingBullet;
	bool inHtmlBlock = false;
	bool staticFacts = false;
	std::vector<uint8_t> styles;
	std::vector<int> linkStack;
	int imageSpan = -1;        // the image whose alt text is coming
	int table = -1;
	bool inHead = false;
	Cell* cell = nullptr;

	uint8_t style() const { uint8_t s = 0; for (uint8_t x : styles) s |= x; return s; }

	int newBlock(Block::Kind k)
	{
		Block b;
		b.kind = k;
		b.indent = (int)lists.size();
		b.quote = quote;
		b.staticFacts = staticFacts;
		doc.blocks.push_back(std::move(b));
		return (int)doc.blocks.size() - 1;
	}

	std::vector<Span>* target()
	{
		if (cell)
			return &cell->spans;
		if (cur < 0 && !lists.empty()) {
			cur = newBlock(Block::PARA);
			doc.blocks[cur].bullet = pendingBullet;
			pendingBullet.clear();
		}
		return cur >= 0 ? &doc.blocks[cur].spans : nullptr;
	}

	void addText(const std::string& t, uint8_t extra = 0)
	{
		if (imageSpan >= 0) {
			doc.images[imageSpan].alt += t;
			return;
		}
		std::vector<Span>* spans = target();
		if (!spans)
			return;
		const uint8_t st = style() | extra;
		const int link = linkStack.empty() ? -1 : linkStack.back();
		if (!spans->empty() && spans->back().style == st && spans->back().link == link && spans->back().image < 0 && !spans->back().newline)
			spans->back().text += t;
		else {
			Span s;
			s.text = t;
			s.style = st;
			s.link = link;
			spans->push_back(std::move(s));
		}
	}

	void endPara()
	{
		if (cur < 0)
			return;
		Block& b = doc.blocks[cur];
		// a paragraph that is one picture: the picture as a block of its own
		int img = -1, others = 0;
		for (const Span& s : b.spans) {
			if (s.image >= 0) { if (img < 0) img = s.image; else others++; }
			else if (s.text.find_first_not_of(" \t\n") != std::string::npos) others++;
		}
		if (b.kind == Block::PARA && img >= 0 && others == 0) {
			b.kind = Block::IMAGE;
			b.image = img;
			b.spans.clear();
		}
		cur = -1;
	}
};

int enterBlock(MD_BLOCKTYPE type, void* detail, void* ud)
{
	Builder& B = *(Builder*)ud;
	switch (type) {
	case MD_BLOCK_QUOTE: B.quote++; break;
	case MD_BLOCK_UL: B.endPara(); B.lists.push_back({ false, 0 }); break;
	case MD_BLOCK_OL: B.endPara(); B.lists.push_back({ true, (int)((MD_BLOCK_OL_DETAIL*)detail)->start }); break;
	case MD_BLOCK_LI: {
		B.endPara();
		auto& l = B.lists.back();
		B.pendingBullet = l.first ? std::to_string(l.second++) + "." : "\xE2\x80\xA2"; // "1." or a bullet
		break;
	}
	case MD_BLOCK_HR: B.endPara(); B.newBlock(Block::RULE); break;
	case MD_BLOCK_H:
		B.endPara();
		B.cur = B.newBlock(Block::HEADING);
		B.doc.blocks[B.cur].level = (int)((MD_BLOCK_H_DETAIL*)detail)->level;
		break;
	case MD_BLOCK_CODE: {
		B.endPara();
		const MD_BLOCK_CODE_DETAIL* d = (MD_BLOCK_CODE_DETAIL*)detail;
		const std::string info(d->info.text ? d->info.text : "", d->info.size);
		B.code = B.newBlock(info == "facts" ? Block::FACTS : Block::CODE);
		if (info == "facts")
			B.doc.hasFacts = true;
		break;
	}
	case MD_BLOCK_HTML: B.endPara(); B.inHtmlBlock = true; break;
	case MD_BLOCK_P:
		if (B.cur >= 0 && B.doc.blocks[B.cur].spans.empty() && B.doc.blocks[B.cur].kind == Block::PARA)
			break; // the list item's own paragraph, already started
		B.endPara();
		B.cur = B.newBlock(Block::PARA);
		B.doc.blocks[B.cur].bullet = B.pendingBullet;
		B.pendingBullet.clear();
		break;
	case MD_BLOCK_TABLE: B.endPara(); B.table = B.newBlock(Block::TABLE); break;
	case MD_BLOCK_THEAD: B.inHead = true; break;
	case MD_BLOCK_TBODY: B.inHead = false; break;
	case MD_BLOCK_TR:
		if (B.table >= 0) {
			B.doc.blocks[B.table].rows.emplace_back();
			if (B.inHead)
				B.doc.blocks[B.table].headerRows++;
		}
		break;
	case MD_BLOCK_TH:
	case MD_BLOCK_TD:
		if (B.table >= 0 && !B.doc.blocks[B.table].rows.empty()) {
			auto& row = B.doc.blocks[B.table].rows.back();
			row.emplace_back();
			const MD_ALIGN a = ((MD_BLOCK_TD_DETAIL*)detail)->align;
			row.back().align = a == MD_ALIGN_CENTER ? 1 : a == MD_ALIGN_RIGHT ? 2 : 0;
			B.cell = &row.back();
			if (type == MD_BLOCK_TH)
				B.styles.push_back(S_BOLD);
		}
		break;
	default: break;
	}
	return 0;
}

int leaveBlock(MD_BLOCKTYPE type, void*, void* ud)
{
	Builder& B = *(Builder*)ud;
	switch (type) {
	case MD_BLOCK_QUOTE: B.endPara(); B.quote--; break;
	case MD_BLOCK_UL: case MD_BLOCK_OL: B.endPara(); B.lists.pop_back(); break;
	case MD_BLOCK_LI: B.endPara(); B.pendingBullet.clear(); break;
	case MD_BLOCK_H: case MD_BLOCK_P: B.endPara(); break;
	case MD_BLOCK_CODE: B.code = -1; break;
	case MD_BLOCK_HTML: B.inHtmlBlock = false; break;
	case MD_BLOCK_TABLE: {
		// rows with nothing to read go: an empty header ("| | |"), or a comment line under the
		// table that MD4C takes for one more row ("<!-- /facts:static -->")
		Block& t = B.doc.blocks[B.table];
		for (size_t r = t.rows.size(); r-- > 0;) {
			bool empty = true;
			for (const Cell& cell : t.rows[r])
				for (const Span& sp : cell.spans)
					if (sp.image >= 0 || sp.text.find_first_not_of(" \t") != std::string::npos)
						empty = false;
			if (empty) {
				t.rows.erase(t.rows.begin() + r);
				if ((int)r < t.headerRows) t.headerRows--;
			}
		}
		B.table = -1;
		break;
	}
	case MD_BLOCK_TH: B.cell = nullptr; if (!B.styles.empty()) B.styles.pop_back(); break;
	case MD_BLOCK_TD: B.cell = nullptr; break;
	default: break;
	}
	return 0;
}

int enterSpan(MD_SPANTYPE type, void* detail, void* ud)
{
	Builder& B = *(Builder*)ud;
	switch (type) {
	case MD_SPAN_EM: B.styles.push_back(S_ITALIC); break;
	case MD_SPAN_STRONG: B.styles.push_back(S_BOLD); break;
	case MD_SPAN_CODE: B.styles.push_back(S_CODE); break;
	case MD_SPAN_DEL: B.styles.push_back(S_STRIKE); break;
	case MD_SPAN_A: {
		const MD_SPAN_A_DETAIL* d = (MD_SPAN_A_DETAIL*)detail;
		B.doc.links.emplace_back(d->href.text, d->href.size);
		B.linkStack.push_back((int)B.doc.links.size() - 1);
		break;
	}
	case MD_SPAN_IMG: {
		const MD_SPAN_IMG_DETAIL* d = (MD_SPAN_IMG_DETAIL*)detail;
		B.doc.images.push_back({ std::string(d->src.text, d->src.size), "" });
		const int idx = (int)B.doc.images.size() - 1;
		if (std::vector<Span>* spans = B.target()) {
			Span s;
			s.image = idx;
			s.link = B.linkStack.empty() ? -1 : B.linkStack.back();
			spans->push_back(s);
		}
		B.imageSpan = idx;
		break;
	}
	default: B.styles.push_back(0); break;
	}
	return 0;
}

int leaveSpan(MD_SPANTYPE type, void*, void* ud)
{
	Builder& B = *(Builder*)ud;
	if (type == MD_SPAN_A) { if (!B.linkStack.empty()) B.linkStack.pop_back(); }
	else if (type == MD_SPAN_IMG) B.imageSpan = -1;
	else if (!B.styles.empty()) B.styles.pop_back();
	return 0;
}

int onText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* ud)
{
	Builder& B = *(Builder*)ud;
	const std::string t(text, size);
	if (B.inHtmlBlock) {
		// <!-- facts:static --> ... <!-- /facts:static -->: the numbers for the HTML manual; in the
		// game the live table stands in their place
		if (t.find("/facts:static") != std::string::npos) B.staticFacts = false;
		else if (t.find("facts:static") != std::string::npos) B.staticFacts = true;
		return 0;
	}
	if (B.code >= 0) {
		B.doc.blocks[B.code].code += t;
		return 0;
	}
	switch (type) {
	case MD_TEXT_HTML:
		// a heading's own anchor: "## Was sie zeigt <!-- #what-it-shows -->"
		if (B.cur >= 0 && B.doc.blocks[B.cur].kind == Block::HEADING) {
			const size_t h = t.find('#');
			const size_t e = t.find("-->");
			if (t.compare(0, 4, "<!--") == 0 && h != std::string::npos && e != std::string::npos && h < e) {
				std::string a = t.substr(h + 1, e - h - 1);
				a.erase(std::remove(a.begin(), a.end(), ' '), a.end());
				B.doc.blocks[B.cur].anchor = a;
			}
		}
		break;
	case MD_TEXT_ENTITY: B.addText(entity(t)); break;
	case MD_TEXT_BR:
		if (std::vector<Span>* spans = B.target()) {
			Span s;
			s.newline = true;
			spans->push_back(s);
		}
		break;
	case MD_TEXT_SOFTBR: B.addText(" "); break;
	case MD_TEXT_NULLCHAR: break;
	default: B.addText(t); break;
	}
	return 0;
}

std::shared_ptr<Doc> parse(const std::string& body)
{
	Builder B;
	MD_PARSER parser = {};
	parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | MD_FLAG_PERMISSIVEURLAUTOLINKS;
	parser.enter_block = enterBlock;
	parser.leave_block = leaveBlock;
	parser.enter_span = enterSpan;
	parser.leave_span = leaveSpan;
	parser.text = onText;
	md_parse(body.c_str(), (MD_SIZE)body.size(), &parser, &B);
	B.endPara();
	for (Block& b : B.doc.blocks)
		if (b.kind == Block::HEADING && b.anchor.empty()) {
			std::string text;
			for (const Span& s : b.spans) text += s.text;
			b.anchor = Slug(text);
		}
	return std::make_shared<Doc>(std::move(B.doc));
}

// ---------------------------------------------------------------------------------------------
// Pictures
// ---------------------------------------------------------------------------------------------

struct Picture {
	texture tex = nullptr;
	int w = 0, h = 0;
	bool tried = false;
	std::string missing;  // why there is none (shown in its frame)
};

std::unordered_map<std::string, Picture> s_pictures;
std::map<std::string, std::shared_ptr<Doc>> s_docs; // by language + path
bool s_svgFonts = false;

bool readRealFile(const std::filesystem::path& p, std::string& out)
{
	std::ifstream f(p, std::ios::binary);
	if (!f)
		return false;
	std::stringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	return true;
}

// a relative picture path of a page as a path of the game's file system ("help/img/x.svg"); a
// translation's own copy of a picture under help/img/<lang>/ first
std::string picturePath(const Page& page, const std::string& url)
{
	std::string base = "help/" + page.lang + "/" + page.path;
	std::vector<std::string> parts;
	std::stringstream ss(base);
	for (std::string p; std::getline(ss, p, '/');)
		parts.push_back(p);
	parts.pop_back(); // the page's own name
	std::stringstream us(url);
	for (std::string p; std::getline(us, p, '/');) {
		if (p == "..") { if (!parts.empty()) parts.pop_back(); }
		else if (p != "." && !p.empty()) parts.push_back(p);
	}
	std::string out;
	for (const std::string& p : parts)
		out += (out.empty() ? "" : "/") + p;
	if (page.lang != "en" && out.compare(0, 9, "help/img/") == 0) {
		const std::string own = "help/img/" + page.lang + "/" + out.substr(9);
		if (FileExists(own.c_str()))
			return own;
	}
	return out;
}

texture upload(IRenderer* gfx, Bitmap& bmp) { return gfx->CreateTexture(bmp, 1); }

Picture& picture(IRenderer* gfx, const Page& page, const std::string& url, float wantWidth)
{
	const bool shot = url.compare(0, 5, "shot:") == 0, game = url.compare(0, 5, "game:") == 0;
	const std::string key = shot || game ? url : picturePath(page, url);
	Picture& p = s_pictures[key];
	if (p.tried)
		return p;
	p.tried = true;
	std::string ext = key.substr(key.rfind('.') + 1);
	for (char& c : ext) c = (char)tolower(c);
	try {
		if (shot) {
			// taken on this computer from this copy of the game (tools/help/capture_shots.py)
			std::string data;
			if (!readRealFile(g_userPath / "help" / "shots" / (url.substr(5) + ".png"), data)) {
				p.missing = Lib().ui("screenshotMissing", url.substr(5));
				return p;
			}
			Bitmap bmp = Bitmap::loadImage(data.data(), data.size());
			p.w = bmp.width; p.h = bmp.height;
			p.tex = upload(gfx, bmp);
		}
		else if (ext == "svg") {
			std::string data;
			if (!ReadText(key, data)) {
				p.missing = key;
				return p;
			}
			if (!s_svgFonts) {
				// the diagrams' text, in the system's fonts ("Segoe UI, Arial, sans-serif")
				s_svgFonts = true;
				const char* win = getenv("WINDIR");
				const std::string fonts = std::string(win ? win : "C:\\Windows") + "\\Fonts\\";
				for (const char* fam : { "Segoe UI", "Arial", "sans-serif", "" }) {
					lunasvg_add_font_face_from_file(fam, false, false, (fonts + "segoeui.ttf").c_str());
					lunasvg_add_font_face_from_file(fam, true, false, (fonts + "segoeuib.ttf").c_str());
				}
			}
			auto svg = lunasvg::Document::loadFromData(data.data(), data.size());
			if (!svg || svg->width() <= 0) {
				p.missing = key;
				return p;
			}
			const float aspect = svg->height() / svg->width();
			const int w = (int)std::clamp(wantWidth * g_uiScale, 64.0f, 2048.0f);
			const int h = std::max(1, (int)(w * aspect));
			lunasvg::Bitmap rb = svg->renderToBitmap(w, h);
			rb.convertToRGBA();
			Bitmap bmp;
			bmp.width = w; bmp.height = h; bmp.format = BMFORMAT_R8G8B8A8;
			bmp.pixels.resize((size_t)w * h * 4);
			for (int y = 0; y < h; y++)
				memcpy(bmp.pixels.data() + (size_t)y * w * 4, rb.data() + (size_t)y * rb.stride(), (size_t)w * 4);
			p.w = (int)(w / g_uiScale); p.h = (int)(h / g_uiScale);
			p.tex = upload(gfx, bmp);
		}
		else if (ext == "mp4" || ext == "webm") {
			p.missing = "video";
		}
		else {
			const std::string vfs = game ? url.substr(5) : key;
			if (!FileExists(vfs.c_str())) {
				p.missing = vfs;
				return p;
			}
			Bitmap bmp = Bitmap::loadBitmap(vfs.c_str());
			p.w = bmp.width; p.h = bmp.height;
			p.tex = upload(gfx, bmp);
		}
	}
	catch (const std::exception& e) {
		p.missing = e.what();
	}
	return p;
}

// ---------------------------------------------------------------------------------------------
// Layout and drawing
// ---------------------------------------------------------------------------------------------

constexpr ImU32 COL_LINK = IM_COL32(236, 170, 92, 255);
constexpr ImU32 COL_LINK_HOT = IM_COL32(255, 205, 130, 255);
constexpr ImU32 COL_MUTED = IM_COL32(170, 160, 145, 255);
constexpr ImU32 COL_RULE = IM_COL32(110, 95, 75, 255);
constexpr ImU32 COL_PANEL = IM_COL32(48, 40, 32, 255);
constexpr ImU32 COL_HEAD = IM_COL32(64, 52, 38, 255);
constexpr ImU32 COL_FLASH = IM_COL32(236, 170, 92, 60);

struct Ctx {
	IRenderer* gfx;
	const Page* page;
	const Doc* doc;
	ImDrawList* dl;
	bool draw = false;
	bool hovered = false;     // the page's window has the mouse
	LinkAction* action;
	const GameSet* gs;
	const Language* game;
};

ImFont* spanFont(uint8_t style, int base)
{
	if (style & S_CODE) return Font(F_CODE);
	if (base != F_BODY) return Font(base);
	if (style & S_BOLD) return Font(F_BOLD);
	if (style & S_ITALIC) return Font(F_ITALIC);
	return Font(F_BODY);
}

float fontSize(ImFont* f) { return f->LegacySize > 0 ? f->LegacySize : ImGui::GetFontSize(); }

void linkClicked(Ctx& c, int link)
{
	const std::string& url = c.doc->links[link];
	LinkAction a;
	std::string path, anchor;
	if (url.compare(0, 5, "help:") == 0) { a.kind = LinkAction::ID; a.a = url.substr(5); }
	else if (url.compare(0, 5, "show:") == 0) { a.kind = LinkAction::SHOW; a.a = url.substr(5); }
	else if (url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0 || url.compare(0, 7, "mailto:") == 0) { a.kind = LinkAction::WEB; a.a = url; }
	else {
		std::string u = url;
		for (size_t q; (q = u.find("%20")) != std::string::npos;) u.replace(q, 3, " ");
		if (PageLink(c.page->path, u, path, anchor)) { a.kind = LinkAction::PAGE; a.a = path; a.b = anchor; }
	}
	*c.action = a;
}

// Lays out (and draws, with c.draw) spans in a box of width maxW at (x0, y0) screen; the height
float flow(Ctx& c, const std::vector<Span>& spans, float x0, float y0, float maxW, int baseFont, ImU32 colour)
{
	struct Item { float w, h; ImFont* f; float size; std::string text; ImU32 col; int link; int image; uint8_t style; };
	ImFont* base = Font(baseFont);
	const float lineH = std::round(fontSize(base) * 1.38f);
	std::vector<Item> line;
	float x = 0, y = 0, lineW = 0;
	auto flush = [&]() {
		float h = lineH;
		for (const Item& it : line)
			h = std::max(h, it.h);
		if (c.draw) {
			float ix = x0;
			for (const Item& it : line) {
				const float iy = y0 + y + (h - it.h) * 0.5f;
				const ImVec2 a(ix, iy), b(ix + it.w, iy + it.h);
				bool hot = false;
				if (it.link >= 0 && c.hovered && ImGui::IsMouseHoveringRect(a, b)) {
					hot = true;
					ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
					const std::string& url = c.doc->links[it.link];
					if (url.compare(0, 4, "http") == 0 || url.compare(0, 5, "show:") == 0)
						ImGui::SetTooltip("%s", url.c_str());
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
						linkClicked(c, it.link);
				}
				if (it.image >= 0) {
					Picture& p = picture(c.gfx, *c.page, c.doc->images[it.image].url, it.w);
					if (p.tex)
						c.dl->AddImage((ImTextureID)p.tex, a, b);
				}
				else {
					if (it.style & S_CODE)
						c.dl->AddRectFilled(ImVec2(a.x - 1, a.y), ImVec2(b.x + 1, b.y), COL_PANEL, 3.0f);
					const ImU32 col = it.link >= 0 ? (hot ? COL_LINK_HOT : COL_LINK) : it.col;
					c.dl->AddText(it.f, it.size, a, col, it.text.c_str());
					if (hot || (it.style & S_STRIKE))
						c.dl->AddLine(ImVec2(a.x, (it.style & S_STRIKE) ? (a.y + b.y) / 2 : b.y - 1), ImVec2(b.x, (it.style & S_STRIKE) ? (a.y + b.y) / 2 : b.y - 1), col);
				}
				ix += it.w;
			}
		}
		y += h;
		lineW = std::max(lineW, x);
		line.clear();
		x = 0;
	};
	for (const Span& s : spans) {
		if (s.newline) { flush(); continue; }
		if (s.image >= 0) {
			Picture& p = picture(c.gfx, *c.page, c.doc->images[s.image].url, lineH * 2);
			float h = lineH * 1.6f, w = h;
			if (p.tex && p.h > 0) { h = std::min((float)p.h, lineH * 2.2f); w = h * p.w / p.h; }
			else if (!p.tex) { w = 0; h = 0; }
			if (x + w > maxW && x > 0) flush();
			line.push_back({ w, h, nullptr, 0, "", 0, s.link, s.image, 0 });
			x += w;
			continue;
		}
		ImFont* f = spanFont(s.style, baseFont);
		const float size = fontSize(f);
		const ImU32 col = (s.style & S_CODE) ? IM_COL32(220, 210, 190, 255) : colour;
		// words with the spaces after them
		size_t i = 0;
		while (i < s.text.size()) {
			size_t e = s.text.find(' ', i);
			e = e == std::string::npos ? s.text.size() : s.text.find_first_not_of(' ', e);
			if (e == std::string::npos) e = s.text.size();
			std::string word = s.text.substr(i, e - i);
			i = e;
			const std::string bare = word.substr(0, word.find_last_not_of(' ') + 1);
			const float wBare = f->CalcTextSizeA(size, FLT_MAX, 0, bare.c_str()).x;
			if (x + wBare > maxW && x > 0) {
				flush();
				if (bare.empty()) continue; // no spaces at a line's start
				word = bare + std::string(word.size() - bare.size(), ' ');
			}
			const float w = f->CalcTextSizeA(size, FLT_MAX, 0, word.c_str()).x;
			line.push_back({ w, size * 1.18f, f, size, word, col, s.link, -1, s.style });
			x += w;
		}
	}
	if (!line.empty() || y == 0)
		flush();
	return y;
}

std::string number(float v)
{
	char buf[32];
	if (std::fabs(v - std::round(v)) < 0.01f) snprintf(buf, sizeof(buf), "%d", (int)std::lround(v));
	else snprintf(buf, sizeof(buf), "%.2f", v);
	std::string s = buf;
	if (s.find('.') != std::string::npos) { while (s.back() == '0') s.pop_back(); if (s.back() == '.') s.pop_back(); }
	return s;
}

// The live numbers of a blueprint from the loaded game set: (label, value) rows
std::vector<std::pair<std::string, std::string>> liveFacts(const GameSet* gs, const std::string& name)
{
	std::vector<std::pair<std::string, std::string>> rows;
	const GameObjBlueprint* bp = gs->findBlueprint(Tags::GAMEOBJCLASS_CHARACTER, name);
	if (!bp) bp = gs->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, name);
	if (!bp) return rows;
	auto item = [&](const GameObjBlueprint* b, const char* n) -> float {
		const int x = gs->items.names.getIndex(n);
		if (x < 0 || !b) return 0;
		auto it = b->startItemValues.find(x);
		return it == b->startItemValues.end() ? 0.0f : it->second;
	};
	const Library& L = Lib();
	const bool character = bp->bpClass == Tags::GAMEOBJCLASS_CHARACTER;
	// the cost: the unit's own, or the foundation's that a building is put up from
	const GameObjBlueprint* costOf = bp;
	if (!character && !item(bp, "Food Cost") && !item(bp, "Wood Cost") && !item(bp, "Gold Cost"))
		if (const GameObjBlueprint* f = gs->findBlueprint(Tags::GAMEOBJCLASS_BUILDING, name + " Foundation"))
			costOf = f;
	std::string cost;
	static const std::pair<const char*, const char*> costItems[] = { { "Food Cost", "resFood" }, { "Wood Cost", "resWood" }, { "Gold Cost", "resGold" }, { "Stone Cost", "resStone" } };
	for (const auto& [it, key] : costItems)
		if (const float v = item(costOf, it); v > 0)
			cost += (cost.empty() ? "" : ", ") + number(v) + " " + L.ui(key);
	if (!cost.empty()) rows.push_back({ L.ui("factCost"), cost });
	if (character) {
		if (const float v = item(bp, "Train Time")) rows.push_back({ L.ui("factTrainTime"), number(v) + " s" });
		if (const float v = item(bp, "Food Consumption")) rows.push_back({ L.ui("factFoodUpkeep"), number(v) });
		if (const float v = std::max(item(bp, "Hit Point Capacity"), item(bp, "Hit Points"))) rows.push_back({ L.ui("factHitPoints"), number(v) });
	}
	else if (const float v = std::max(item(bp, "Structural Integrity Capacity"), item(bp, "Structural Integrity")))
		rows.push_back({ L.ui("factStrength"), number(v) });
	if (const float v = item(bp, "Armour")) {
		std::string a = number(v);
		const float f = item(bp, "Front Armour"), s = item(bp, "Left Armour"), r = item(bp, "Rear Armour");
		if (f || s || r) a += " (" + L.ui("factArmourSides", number(f), number(s)) + ", " + L.ui("factArmourRear", number(r)) + ")";
		rows.push_back({ L.ui("factArmour"), a });
	}
	if (const float v = item(bp, "H2H Damage")) {
		std::string a = number(v);
		if (const float t = item(bp, "Attack Time")) a += " " + L.ui("factEvery", number(t));
		rows.push_back({ L.ui("factMelee"), a });
	}
	if (const float v = item(bp, "Missile Damage")) {
		std::string a = number(v);
		if (const float t = item(bp, "Missile Attack Time")) a += " " + L.ui("factEvery", number(t));
		if (const float r = item(bp, "Maximum Missile Range")) a += ", " + L.ui("factRange", number(item(bp, "Minimum Missile Range")) + "-" + number(r));
		rows.push_back({ L.ui("factMissile"), a });
	}
	if (const float v = item(bp, "Base Movement Speed")) rows.push_back({ L.ui("factSpeed"), number(v) });
	if (const float v = item(bp, "Sight Range")) rows.push_back({ L.ui("factSight"), number(v) });
	return rows;
}

float drawTable(Ctx& c, const std::vector<std::vector<Cell>>& rows, int headerRows, float x0, float y0, float maxW)
{
	size_t cols = 0;
	for (const auto& r : rows) cols = std::max(cols, r.size());
	if (!cols) return 0;
	const float pad = 6;
	// the columns' natural widths, shrunk in proportion when they don't fit
	std::vector<float> natural(cols, 30.0f);
	const bool drawWas = c.draw;
	c.draw = false;
	for (const auto& r : rows)
		for (size_t i = 0; i < r.size(); i++) {
			float w = 0;
			for (const Span& s : r[i].spans) {
				if (s.image >= 0) { w += 48; continue; }
				ImFont* f = spanFont(s.style, F_BODY);
				w += f->CalcTextSizeA(fontSize(f), FLT_MAX, 0, s.text.c_str()).x;
			}
			natural[i] = std::max(natural[i], std::min(w, maxW * 0.6f) + pad * 2 + 2);
		}
	float total = 0;
	for (float w : natural) total += w;
	std::vector<float> width = natural;
	if (total > maxW)
		for (float& w : width) w = std::max(48.0f, w * maxW / total);
	std::vector<float> heights;
	for (const auto& r : rows) {
		float h = 0;
		for (size_t i = 0; i < r.size(); i++)
			h = std::max(h, flow(c, r[i].spans, 0, 0, width[i] - pad * 2, F_BODY, 0));
		heights.push_back(h + pad * 2);
	}
	c.draw = drawWas;
	float y = y0;
	for (size_t ri = 0; ri < rows.size(); ri++) {
		float x = x0;
		if (c.draw) {
			float rowW = 0;
			for (float w : width) rowW += w;
			if ((int)ri < headerRows)
				c.dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + rowW, y + heights[ri]), COL_HEAD);
			for (size_t i = 0; i < cols; i++) {
				c.dl->AddRect(ImVec2(x, y), ImVec2(x + width[i] + 1, y + heights[ri] + 1), COL_RULE);
				if (i < rows[ri].size())
					flow(c, rows[ri][i].spans, x + pad, y + pad, width[i] - pad * 2, F_BODY, ImGui::GetColorU32(ImGuiCol_Text));
				x += width[i];
			}
		}
		y += heights[ri];
	}
	return y - y0;
}

// One block at (x0, y0) screen, the width given; its height
float layoutBlock(Ctx& c, Block& b, float x0, float y0, float width, const ViewState& view)
{
	const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
	const float bodyLH = std::round(fontSize(Font(F_BODY)) * 1.38f);
	const float indent = b.indent * 22.0f + (b.indent > 0 ? 18.0f : 0) + b.quote * 16.0f;
	float h = 0;
	switch (b.kind) {
	case Block::PARA: {
		if (c.draw && !b.bullet.empty()) {
			ImFont* f = Font(F_BODY);
			const float bw = f->CalcTextSizeA(fontSize(f), FLT_MAX, 0, b.bullet.c_str()).x;
			c.dl->AddText(f, fontSize(f), ImVec2(x0 + indent - 6 - bw, y0 + (bodyLH - fontSize(f) * 1.18f) * 0.5f), COL_MUTED, b.bullet.c_str());
		}
		h = flow(c, b.spans, x0 + indent, y0, width - indent, F_BODY, b.quote ? COL_MUTED : text) + bodyLH * 0.35f;
		break;
	}
	case Block::HEADING: {
		const int font = b.level <= 1 ? F_H1 : b.level == 2 ? F_H2 : F_H3;
		const float before = b.level <= 1 ? 4 : bodyLH * 0.7f;
		if (c.draw && b.anchor == view.flashAnchor && ImGui::GetTime() < view.flashUntil)
			c.dl->AddRectFilled(ImVec2(x0 - 6, y0 + before - 2), ImVec2(x0 + width, y0 + before + std::round(fontSize(Font(font)) * 1.4f)), COL_FLASH, 4);
		h = before + flow(c, b.spans, x0 + indent, y0 + before, width - indent, font, IM_COL32(245, 225, 190, 255));
		if (b.level <= 1) {
			if (c.draw) c.dl->AddLine(ImVec2(x0, y0 + h + 3), ImVec2(x0 + width, y0 + h + 3), COL_RULE);
			h += 8;
		}
		h += bodyLH * 0.3f;
		break;
	}
	case Block::CODE: {
		ImFont* f = Font(F_CODE);
		const float lh = std::round(fontSize(f) * 1.3f);
		std::vector<std::string> lines;
		std::stringstream ss(b.code);
		for (std::string l; std::getline(ss, l);) lines.push_back(l);
		h = lines.size() * lh + 12;
		if (c.draw) {
			c.dl->AddRectFilled(ImVec2(x0 + indent, y0), ImVec2(x0 + width, y0 + h), COL_PANEL, 4);
			c.dl->PushClipRect(ImVec2(x0 + indent, y0), ImVec2(x0 + width, y0 + h), true);
			for (size_t i = 0; i < lines.size(); i++)
				c.dl->AddText(f, fontSize(f), ImVec2(x0 + indent + 8, y0 + 6 + i * lh), IM_COL32(220, 210, 190, 255), lines[i].c_str());
			c.dl->PopClipRect();
		}
		h += bodyLH * 0.4f;
		break;
	}
	case Block::RULE:
		h = bodyLH;
		if (c.draw) c.dl->AddLine(ImVec2(x0, y0 + h / 2), ImVec2(x0 + width, y0 + h / 2), COL_RULE);
		break;
	case Block::TABLE:
		h = drawTable(c, b.rows, b.headerRows, x0 + indent, y0, width - indent) + bodyLH * 0.5f;
		break;
	case Block::FACTS: {
		// "blueprint: <name>": the live numbers while a game set is loaded
		std::string name = b.code.substr(b.code.find(':') == std::string::npos ? 0 : b.code.find(':') + 1);
		name.erase(0, name.find_first_not_of(" \t"));
		name.erase(name.find_last_not_of(" \t\n") + 1);
		if (c.gs) {
			std::vector<std::vector<Cell>> rows;
			for (const auto& [k, v] : liveFacts(c.gs, name)) {
				std::vector<Cell> row(2);
				row[0].spans.push_back({ k, S_BOLD });
				row[1].spans.push_back({ v });
				rows.push_back(std::move(row));
			}
			if (!rows.empty()) {
				h = flow(c, { Span{ Lib().ui("liveNumbers"), S_ITALIC } }, x0 + indent, y0, width - indent, F_BODY, COL_MUTED);
				h += drawTable(c, rows, 0, x0 + indent, y0 + h, width - indent) + bodyLH * 0.5f;
			}
		}
		break;
	}
	case Block::IMAGE: {
		const Doc::Image& img = c.doc->images[b.image];
		const std::string& url = img.url;
		Picture& p = picture(c.gfx, *c.page, url, width - indent);
		float w = width - indent, ih = w * 9 / 16;
		if (p.tex && p.w > 0) {
			// screenshots fill the width, diagrams and art no more than their own size
			w = (url.compare(0, 5, "shot:") == 0) ? width - indent : std::min((float)p.w, width - indent);
			ih = w * p.h / p.w;
		}
		else
			ih = std::min(ih, 160.0f);
		if (c.draw) {
			const ImVec2 a(x0 + indent, y0), z(x0 + indent + w, y0 + ih);
			if (p.tex)
				c.dl->AddImage((ImTextureID)p.tex, a, z);
			else {
				c.dl->AddRectFilled(a, z, COL_PANEL, 4);
				c.dl->AddRect(a, z, COL_RULE, 4);
				const std::string why = p.missing == "video" ? Lib().ui("videoNotYet") : p.missing;
				flow(c, { Span{ why, S_ITALIC } }, a.x + 12, a.y + 12, w - 24, F_BODY, COL_MUTED);
			}
		}
		h = ih + 4;
		if (!img.alt.empty())
			h += flow(c, { Span{ img.alt, S_ITALIC } }, x0 + indent, y0 + h, width - indent, F_BODY, COL_MUTED);
		h += bodyLH * 0.5f;
		break;
	}
	}
	return h;
}

} // namespace

void ClearViewCache()
{
	s_docs.clear();
	// the textures stay alive (the renderer may still hold them this frame); a new language uses
	// the same pictures anyway
}

LinkAction DrawPage(IRenderer* gfx, const Page& page, ViewState& view, const GameSet* gs, const Language* game)
{
	LinkAction action;
	std::shared_ptr<Doc>& doc = s_docs[page.lang + ":" + page.path];
	if (!doc)
		doc = parse(page.body);
	Ctx c{ gfx, &page, doc.get(), ImGui::GetWindowDrawList() };
	c.hovered = ImGui::IsWindowHovered();
	c.action = &action;
	c.gs = gs;
	c.game = game;

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	const float width = std::max(200.0f, ImGui::GetContentRegionAvail().x - 8);
	// the notice on a page shown in English, or translated from an older English page
	if (page.state == Page::ENGLISH || page.state == Page::OUT_OF_DATE) {
		const ImVec2 o = ImGui::GetCursorScreenPos();
		c.draw = true;
		const std::string note = Lib().ui(page.state == Page::ENGLISH ? "notTranslated" : "outOfDate");
		const float h = flow(c, { Span{ note, S_ITALIC } }, o.x + 8, o.y + 6, width - 16, F_BODY, COL_MUTED) + 12;
		c.dl->AddRect(o, ImVec2(o.x + width, o.y + h), COL_RULE, 4);
		ImGui::Dummy(ImVec2(width, h + 8));
	}
	const ImVec2 clipMin = c.dl->GetClipRectMin(), clipMax = c.dl->GetClipRectMax();
	for (Block& b : doc->blocks) {
		if (b.staticFacts && gs && doc->hasFacts)
			continue; // the live table stands in its place
		if (b.kind == Block::FACTS && !gs)
			continue;
		const ImVec2 o = ImGui::GetCursorScreenPos();
		if (b.kind == Block::HEADING && !view.anchor.empty() && b.anchor == view.anchor) {
			ImGui::SetScrollY(ImGui::GetCursorPosY() - 4);
			view.flashAnchor = view.anchor;
			view.flashUntil = ImGui::GetTime() + 1.6;
			view.anchor.clear();
		}
		if (b.layoutWidth != width || b.kind == Block::FACTS) {
			c.draw = false;
			b.height = layoutBlock(c, b, o.x, o.y, width, view);
			b.layoutWidth = width;
		}
		if (o.y + b.height >= clipMin.y && o.y <= clipMax.y) {
			c.draw = true;
			layoutBlock(c, b, o.x, o.y, width, view);
		}
		ImGui::Dummy(ImVec2(width, std::max(1.0f, b.height)));
	}
	ImGui::Dummy(ImVec2(width, 40));
	ImGui::PopStyleVar();
	return action;
}

}

// ---------------------------------------------------------------------------------------------

void Help::AddFonts(ImFontAtlas* atlas)
{
	using namespace Help::detail;
	const char* win = getenv("WINDIR");
	const std::string dir = std::string(win ? win : "C:\\Windows") + "\\Fonts\\";
	// Latin with its extensions, Greek, Cyrillic, punctuation, arrows (for translations)
	static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x0100, 0x024F, 0x0370, 0x03FF, 0x0400, 0x04FF, 0x2010, 0x205E, 0x20AC, 0x20AC, 0x2190, 0x21FF, 0x2212, 0x2212, 0 };
	struct { int kind; const char* file; float size; } list[] = {
		{ F_BODY, "segoeui.ttf", 17 }, { F_BOLD, "segoeuib.ttf", 17 }, { F_ITALIC, "segoeuii.ttf", 17 },
		{ F_CODE, "consola.ttf", 15 }, { F_H1, "georgiab.ttf", 30 }, { F_H2, "georgiab.ttf", 23 }, { F_H3, "georgiab.ttf", 19 },
	};
	for (const auto& f : list) {
		const std::string path = dir + f.file;
		if (!std::filesystem::exists(path))
			continue;
		ImFontConfig cfg;
		cfg.RasterizerDensity = g_uiScale > 0 ? g_uiScale : 1.0f;
		s_fonts[f.kind] = atlas->AddFontFromFileTTF(path.c_str(), f.size, &cfg, ranges);
	}
}
