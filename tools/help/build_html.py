"""wkbai's help pages (redata/help/<lang>/*.md) as a static HTML manual, and a link check.

    python -I tools/help/build_html.py [--lang en] [--out DIR] [--shots DIR]

- Writes DIR/<path>.html for every page, with the contents tree beside each one.
- Screenshots (shot:<name>) are taken from --shots (default DIR/shots), never from git.
- Prints every broken link, unknown help id and missing screenshot, and exits with 1 if a
  link or id is broken (missing screenshots are only listed).

Standard library only. It understands the Markdown the pages use (CommonMark's common part:
headings, paragraphs, lists, block quotes, tables, fenced code, links, images, emphasis).
"""
import argparse, html, json, os, re, sys, urllib.parse

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
HELP = os.path.join(ROOT, "redata", "help")


def slug(text):
    text = re.sub(r"[`*_\[\]()]", "", text).strip().lower().replace(" ", "-")
    return re.sub(r"[^a-z0-9\-]", "", text)


def parse_front(text):
    meta = {}
    if text.startswith("---"):
        end = text.find("\n---", 3)
        if end > 0:
            for line in text[3:end].strip().splitlines():
                if ":" not in line:
                    continue
                k, v = line.split(":", 1)
                v = v.strip()
                if v.startswith("[") and v.endswith("]"):
                    v = [x.strip().strip("'\"") for x in v[1:-1].split(",") if x.strip()]
                else:
                    v = v.strip("'\"")
                meta[k.strip()] = v
            text = text[end + 4:].lstrip("\n")
    return meta, text


class Site:
    def __init__(self, lang, out, shots):
        self.lang, self.out, self.shots = lang, out, shots
        self.base = os.path.join(HELP, lang)
        self.pages = {}   # rel path (no .md, '/') -> dict
        self.ids = {}     # help id -> (page, anchor)
        self.problems, self.missing_shots = [], set()
        for dirpath, _, files in os.walk(self.base):
            for f in files:
                if not f.endswith(".md"):
                    continue
                full = os.path.join(dirpath, f)
                rel = os.path.relpath(full, self.base).replace("\\", "/")[:-3]
                meta, body = parse_front(open(full, encoding="utf-8").read())
                heads = {slug(m.group(2)) for m in re.finditer(r"^(#{1,6})\s+(.+?)\s*#*\s*$", body, re.M)}
                self.pages[rel] = {"meta": meta, "body": body, "heads": heads,
                                   "title": meta.get("title") or rel.split("/")[-1]}
        for rel, p in self.pages.items():
            ids = p["meta"].get("ids", [])
            if isinstance(ids, str):
                ids = [ids]
            for i in [rel] + list(ids):
                hid, _, anchor = i.partition("#")
                if anchor and anchor not in p["heads"]:
                    self.problems.append(f"{rel}: id {i}: no heading '{anchor}'")
                if hid in self.ids and self.ids[hid][0] != rel:
                    self.problems.append(f"{rel}: id {hid} also on {self.ids[hid][0]}")
                self.ids.setdefault(hid, (rel, anchor))

    def resolve_id(self, hid):
        """An id, or its nearest parent with a page (the viewer's fallback)."""
        hid, _, anchor = hid.partition("#")
        probe = hid
        while probe:
            if probe in self.ids:
                rel, a = self.ids[probe]
                return rel, anchor or a, probe == hid
            if probe + "/index" in self.pages:
                return probe + "/index", anchor, probe == hid
            probe = probe.rpartition("/")[0]
        return "index", "", False

    def href(self, frm, rel, anchor=""):
        target = os.path.relpath(rel + ".html", os.path.dirname(frm) or ".").replace("\\", "/")
        return target + ("#" + anchor if anchor else "")

    # ---- inline ----
    def inline(self, frm, text):
        out, i = [], 0
        pat = re.compile(r"(!?)\[([^\]]*)\]\((<[^>]+>|[^)\s]+)(?:\s+\"[^\"]*\")?\)|`([^`]+)`|\*\*(.+?)\*\*|(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])|<(https?://[^>]+)>")
        for m in pat.finditer(text):
            out.append(html.escape(text[i:m.start()], quote=False))
            i = m.end()
            if m.group(4) is not None:
                out.append("<code>" + html.escape(m.group(4)) + "</code>")
            elif m.group(5) is not None:
                out.append("<strong>" + self.inline(frm, m.group(5)) + "</strong>")
            elif m.group(6) is not None:
                out.append("<em>" + self.inline(frm, m.group(6)) + "</em>")
            elif m.group(7) is not None:
                out.append(f'<a href="{html.escape(m.group(7))}">{html.escape(m.group(7))}</a>')
            else:
                url = m.group(3)
                url = urllib.parse.unquote(url[1:-1] if url.startswith("<") else url)
                if m.group(1):
                    out.append(self.image(frm, m.group(2), url))
                else:
                    out.append(self.link(frm, self.inline(frm, m.group(2)), url))
        out.append(html.escape(text[i:], quote=False))
        # Markdown keeps character references such as &lt; and &#8212; as they are
        return re.sub(r"&amp;(#\d+|#x[0-9a-fA-F]+|[a-zA-Z]+);", lambda m: "&" + m.group(1) + ";", "".join(out))

    def link(self, frm, label, url):
        if url.startswith(("http://", "https://", "mailto:")):
            return f'<a href="{html.escape(url)}" class="ext">{label}</a>'
        if url.startswith("help:"):
            rel, anchor, exact = self.resolve_id(url[5:])
            if not exact:
                self.problems.append(f"{frm}: help id {url[5:]} has no page (falls back to {rel})")
            return f'<a href="{self.href(frm, rel, anchor)}">{label}</a>'
        if url.startswith("show:"):
            return f'<span class="show" title="In the game: shows {html.escape(url[5:])} on screen">{label}</span>'
        if url.startswith("#"):
            if slug(url[1:]) not in self.pages[frm]["heads"] and url[1:] not in self.pages[frm]["heads"]:
                self.problems.append(f"{frm}: link {url}: no such heading")
            return f'<a href="{html.escape(url)}">{label}</a>'
        path, _, anchor = url.partition("#")
        if path.endswith(".md"):
            rel = os.path.normpath(os.path.join(os.path.dirname(frm), path[:-3])).replace("\\", "/")
            if rel not in self.pages:
                self.problems.append(f"{frm}: link {url}: no such page")
            elif anchor and anchor not in self.pages[rel]["heads"]:
                self.problems.append(f"{frm}: link {url}: no heading '{anchor}'")
            return f'<a href="{self.href(frm, rel, anchor)}">{label}</a>'
        self.problems.append(f"{frm}: link {url}: not a page, id or web address")
        return label

    def image(self, frm, alt, url):
        a = html.escape(alt)
        if url.startswith("shot:"):
            name = url[5:]
            src = os.path.join(self.shots, name + ".png")
            if os.path.exists(src):
                rel = os.path.relpath(src, os.path.dirname(os.path.join(self.out, frm))).replace("\\", "/")
                return f'<figure><img src="{rel}" alt="{a}"><figcaption>{a}</figcaption></figure>'
            self.missing_shots.add(name)
            return f'<figure class="noshot"><div>Screenshot <code>{html.escape(name)}</code><br>{a}</div></figure>'
        if url.startswith("game:"):
            return f'<span class="gameart" title="{html.escape(url[5:])}">[{a or "game art"}]</span>'
        full = os.path.normpath(os.path.join(self.base, os.path.dirname(frm), url))
        if not os.path.exists(full):
            self.problems.append(f"{frm}: picture {url}: no such file")
        rel = os.path.relpath(full, os.path.dirname(os.path.join(self.base, frm))).replace("\\", "/")
        # pictures are copied beside the pages, keeping the folder layout under redata/help
        dst = os.path.join(self.out, os.path.relpath(full, self.base))
        if os.path.exists(full):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(full, "rb") as s, open(dst, "wb") as d:
                d.write(s.read())
        if url.lower().endswith((".mp4", ".webm")):
            return f'<video src="{rel}" controls loop muted></video>'
        return f'<figure><img src="{rel}" alt="{a}"><figcaption>{a}</figcaption></figure>'

    # ---- blocks ----
    def blocks(self, frm, text):
        lines, out, i = text.split("\n"), [], 0
        para = []

        def flush():
            if para:
                out.append("<p>" + self.inline(frm, " ".join(x.strip() for x in para)) + "</p>")
                para.clear()
        while i < len(lines):
            line = lines[i]
            s = line.strip()
            if not s:
                flush(); i += 1; continue
            if s.startswith("<!--"):
                flush()
                while "-->" not in lines[i]:
                    i += 1
                i += 1; continue
            m = re.match(r"^(`{3,}|~{3,})\s*(\S*)", s)
            if m:
                flush()
                fence, info, code = m.group(1), m.group(2), []
                i += 1
                while i < len(lines) and not lines[i].strip().startswith(fence):
                    code.append(lines[i]); i += 1
                i += 1
                if info == "facts":
                    out.append('<div class="facts">In the game: live numbers from the game set (' + html.escape(" ".join(code).strip()) + ")</div>")
                else:
                    out.append("<pre><code>" + html.escape("\n".join(code)) + "</code></pre>")
                continue
            m = re.match(r"^(#{1,6})\s+(.+?)\s*#*$", s)
            if m:
                flush()
                n = len(m.group(1))
                out.append(f'<h{n} id="{slug(m.group(2))}">{self.inline(frm, m.group(2))}</h{n}>')
                i += 1; continue
            if re.match(r"^(-{3,}|\*{3,}|_{3,})$", s):
                flush(); out.append("<hr>"); i += 1; continue
            if s.startswith("|") and i + 1 < len(lines) and re.match(r"^\|?\s*:?-+", lines[i + 1].strip()):
                flush()
                cells = lambda r: [c.strip() for c in r.strip().strip("|").split("|")]
                head = cells(s)
                rows = []
                i += 2
                while i < len(lines) and lines[i].strip().startswith("|"):
                    rows.append(cells(lines[i])); i += 1
                t = "<table><tr>" + "".join("<th>" + self.inline(frm, c) + "</th>" for c in head) + "</tr>"
                for r in rows:
                    t += "<tr>" + "".join("<td>" + self.inline(frm, c) + "</td>" for c in r) + "</tr>"
                out.append(t + "</table>")
                continue
            if s.startswith(">"):
                flush()
                q = []
                while i < len(lines) and lines[i].strip().startswith(">"):
                    q.append(lines[i].strip()[1:].lstrip()); i += 1
                out.append("<blockquote>" + self.blocks(frm, "\n".join(q)) + "</blockquote>")
                continue
            if re.match(r"^([-*+]|\d+[.)])\s+", s) and not para:
                i = self.list_block(frm, lines, i, out)
                continue
            para.append(line); i += 1
        flush()
        return "\n".join(out)

    def list_block(self, frm, lines, i, out):
        indent = len(lines[i]) - len(lines[i].lstrip())
        ordered = bool(re.match(r"^\s*\d+[.)]", lines[i]))
        items = []
        while i < len(lines):
            line = lines[i]
            if not line.strip():
                # a blank line ends the list unless the next line goes on with it
                j = i + 1
                if j < len(lines) and lines[j].strip() and len(lines[j]) - len(lines[j].lstrip()) > indent:
                    if items: items[-1].append("")
                    i += 1; continue
                if j < len(lines) and re.match(r"^\s*([-*+]|\d+[.)])\s+", lines[j]) and len(lines[j]) - len(lines[j].lstrip()) == indent:
                    i += 1; continue
                break
            ind = len(line) - len(line.lstrip())
            m = re.match(r"^\s*([-*+]|\d+[.)])\s+(.*)", line)
            if m and ind == indent:
                items.append([m.group(2)])
            elif ind > indent and items:
                items[-1].append(line[indent + 2:] if len(line) > indent + 2 else line.strip())
            elif ind < indent or not items:
                break
            else:
                items[-1].append(line.strip())
            i += 1
        tag = "ol" if ordered else "ul"
        h = f"<{tag}>"
        for it in items:
            first, rest = it[0], it[1:]
            sub_start = next((k for k, x in enumerate(rest) if re.match(r"^\s*([-*+]|\d+[.)])\s+", x) or x.strip().startswith(("|", "```", ">"))), None)
            if sub_start is None:
                h += "<li>" + self.inline(frm, " ".join([first] + [x.strip() for x in rest if x.strip()])) + "</li>"
            else:
                lead = " ".join([first] + [x.strip() for x in rest[:sub_start]])
                h += "<li>" + self.inline(frm, lead) + self.blocks(frm, "\n".join(rest[sub_start:])) + "</li>"
        out.append(h + f"</{tag}>")
        return i

    # ---- the tree and the pages ----
    def tree_html(self, frm):
        kids = {}
        for rel in self.pages:
            parts = rel.split("/")
            parent = "/".join(parts[:-1]) if parts[-1] != "index" else "/".join(parts[:-2])
            if rel == "index":
                continue
            par = self.pages[rel]["meta"].get("parent")
            kids.setdefault(par if par else parent, []).append(rel)

        def order(rel):
            try:
                o = float(self.pages[rel]["meta"].get("order", 1000))
            except ValueError:
                o = 1000
            return (o, self.pages[rel]["title"].lower())

        def node(key, depth):
            h = ""
            for rel in sorted(kids.get(key, []), key=order):
                own = rel[:-6] if rel.endswith("/index") else rel
                cls = ' class="here"' if rel == frm else ""
                h += f'<li><a{cls} href="{self.href(frm, rel)}">{html.escape(self.pages[rel]["title"])}</a>'
                if rel.endswith("/index") and depth < 6:
                    sub = node(own, depth + 1)
                    if sub:
                        h += "<ul>" + sub + "</ul>"
                h += "</li>"
            return h
        return f'<ul class="tree"><li><a href="{self.href(frm, "index")}">Start</a></li>' + node("", 0) + "</ul>"

    def build(self):
        for rel, p in sorted(self.pages.items()):
            body = self.blocks(rel, p["body"])
            dst = os.path.join(self.out, rel + ".html")
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            css = os.path.relpath(os.path.join(self.out, "help.css"), os.path.dirname(dst)).replace("\\", "/")
            with open(dst, "w", encoding="utf-8") as f:
                f.write(f"""<!doctype html><html lang="{self.lang}"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(p['title'])} - wkbai help</title><link rel="stylesheet" href="{css}"></head>
<body><nav>{self.tree_html(rel)}</nav><main>{body}</main></body></html>""")
        with open(os.path.join(self.out, "help.css"), "w", encoding="utf-8") as f:
            f.write(CSS)


CSS = """
:root{--bg:#f4efe4;--fg:#2a2117;--muted:#6b5d4a;--line:#d6cbb5;--accent:#8a3b12;--panel:#ebe3d2}
@media (prefers-color-scheme:dark){:root{--bg:#1d1913;--fg:#ece3d2;--muted:#a89a84;--line:#3b3329;--accent:#e0a35a;--panel:#26211a}}
*{box-sizing:border-box}body{margin:0;display:flex;background:var(--bg);color:var(--fg);font:16px/1.55 Georgia,serif}
nav{width:290px;flex:none;height:100vh;position:sticky;top:0;overflow:auto;padding:16px;background:var(--panel);border-right:1px solid var(--line);font:14px/1.4 system-ui,sans-serif}
nav ul{list-style:none;padding-left:14px;margin:2px 0}nav>ul{padding:0}nav a{color:var(--fg);text-decoration:none}nav a.here{color:var(--accent);font-weight:600}
main{max-width:880px;padding:24px 40px 80px;min-width:0}a{color:var(--accent)}h1,h2,h3{font-family:Georgia,serif;line-height:1.2}h1{font-size:2em;border-bottom:1px solid var(--line);padding-bottom:6px}
code{font:0.9em ui-monospace,Consolas,monospace;background:var(--panel);padding:1px 4px;border-radius:3px}pre{background:var(--panel);padding:10px;overflow:auto}
table{border-collapse:collapse;margin:12px 0;display:block;overflow-x:auto}th,td{border:1px solid var(--line);padding:4px 8px;text-align:left;vertical-align:top}th{background:var(--panel)}
figure{margin:14px 0}figure img,video{max-width:100%;border:1px solid var(--line)}figcaption{color:var(--muted);font-size:0.9em}
figure.noshot div{border:1px dashed var(--line);padding:30px;text-align:center;color:var(--muted);font:14px system-ui,sans-serif}
.facts{border-left:4px solid var(--accent);background:var(--panel);padding:8px 12px;font:14px system-ui,sans-serif;color:var(--muted)}
.show{border-bottom:1px dotted var(--accent);cursor:help}.gameart{color:var(--muted)}blockquote{border-left:3px solid var(--line);margin-left:0;padding-left:14px;color:var(--muted)}
@media (max-width:760px){body{display:block}nav{width:auto;height:auto;position:static}main{padding:16px}}
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lang", default="en")
    ap.add_argument("--out", default=os.path.join("C:\\Dev", "help-html"))
    ap.add_argument("--shots")
    a = ap.parse_args()
    shots = a.shots or os.path.join(a.out, "shots")
    site = Site(a.lang, a.out, shots)
    site.build()
    for p in site.problems:
        print("PROBLEM", p)
    if site.missing_shots:
        print(f"{len(site.missing_shots)} screenshots not taken yet:", " ".join(sorted(site.missing_shots)))
    print(f"{len(site.pages)} pages, {len(site.ids)} help ids, {len(site.problems)} problems -> {a.out}")
    sys.exit(1 if site.problems else 0)


if __name__ == "__main__":
    main()
