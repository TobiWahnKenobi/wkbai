"""Translations of wkbai's help: start a language, start pages, see what is missing or out of date.

    python -I tools/help/translation.py new    <lang> --name "Deutsch" --english-name German
    python -I tools/help/translation.py start  <lang> <page> [<page> ...]   (or --all)
    python -I tools/help/translation.py stamp  <lang> <page> [<page> ...]   (or --all)
    python -I tools/help/translation.py status <lang> [--list]

<page> is a path under redata/help/en without ".md", e.g. "ui/hud/minimap".

How a translation works (the full guide is redata/help/TRANSLATING.md):
- A language is a folder redata/help/<lang>/ beside en/, with the same page paths. A page that
  isn't translated yet is shown in English (the help says so on the page).
- A translated page keeps the English page's help ids: it lists none of its own.
- Headings keep their English anchors: `start` writes each heading as
  "## Zoom buttons <!-- #zoom-buttons -->"; translate the words and leave the comment, so that
  links and help ids that point at the heading still land there.
- "translation-of" in the front matter is a fingerprint of the English page the translation was
  made from. When the English page changes, `status` lists the translation as out of date; once
  it is brought up to date, `stamp` records the new fingerprint.
- The generated parts of unit, building, upgrade and command pages (between <!-- gen:... -->
  markers) are not translated by hand: tools/help/gamedata_pages.py --lang <lang> fills them, in
  the phrases of tools/help/strings/<lang>.json and with the names from that language's game.

Standard library only.
"""
import argparse, hashlib, json, os, re, shutil, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
HELP = os.path.join(ROOT, "redata", "help")
STRINGS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "strings")


def split_front(text):
    m = re.match(r"---\n(.*?)\n---\n?", text, re.S)
    return (m.group(1), text[m.end():]) if m else ("", text)


def fingerprint(en_text):
    """The English page's body (front matter aside), normalised, as 12 hex digits."""
    body = split_front(en_text.replace("\r\n", "\n"))[1].strip()
    return hashlib.sha1(body.encode("utf-8")).hexdigest()[:12]


def slug(text):
    text = re.sub(r"[`*_\[\]()]", "", text).strip().lower().replace(" ", "-")
    return re.sub(r"[^a-z0-9\-]", "", text)


def en_pages():
    base = os.path.join(HELP, "en")
    out = []
    for dp, _, fs in os.walk(base):
        for f in fs:
            if f.endswith(".md"):
                out.append(os.path.relpath(os.path.join(dp, f), base).replace("\\", "/")[:-3])
    return sorted(out)


def read(path):
    return open(path, encoding="utf-8").read().replace("\r\n", "\n")


def heading_anchors(body):
    """The anchors a page's headings have: an explicit <!-- #x --> or the slug of the text."""
    out = []
    in_code = False
    for line in body.split("\n"):
        if line.strip().startswith("```"):
            in_code = not in_code
        if in_code:
            continue
        m = re.match(r"^(#{1,6})\s+(.*?)\s*$", line)
        if m:
            a = re.search(r"<!--\s*#([\w-]+)\s*-->", m.group(2))
            out.append(a.group(1) if a else slug(re.sub(r"<!--.*?-->", "", m.group(2))))
    return out


def cmd_new(a):
    folder = os.path.join(HELP, a.lang)
    os.makedirs(folder, exist_ok=True)
    meta_path = os.path.join(folder, "_language.json")
    en_meta = json.load(open(os.path.join(HELP, "en", "_language.json"), encoding="utf-8"))
    if not os.path.exists(meta_path):
        meta = {"code": a.lang, "name": a.name or a.lang, "englishName": a.english_name or a.lang,
                "fallback": "en", "detect": {k: "" for k in en_meta.get("detect", {})},
                "ui": {k: "" for k in en_meta["ui"]}}
        json.dump(meta, open(meta_path, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
        print("wrote", meta_path)
    sp = os.path.join(STRINGS, a.lang + ".json")
    if not os.path.exists(sp):
        tpl = json.load(open(os.path.join(STRINGS, "template.json"), encoding="utf-8"))
        tpl["language"] = a.lang
        json.dump(tpl, open(sp, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
        print("wrote", sp)
    print("Next: fill in both files, then start pages with: translation.py start", a.lang, "<page>")


def cmd_start(a):
    pages = en_pages() if a.all else a.pages
    for page in pages:
        src = os.path.join(HELP, "en", page + ".md")
        dst = os.path.join(HELP, a.lang, page + ".md")
        if not os.path.exists(src):
            print("no English page", page)
            continue
        if os.path.exists(dst):
            print("already started", page)
            continue
        text = read(src)
        front, body = split_front(text)
        title = re.search(r"^title:\s*(.*)$", front, re.M)
        order = re.search(r"^order:\s*(.*)$", front, re.M)
        parent = re.search(r"^parent:\s*(.*)$", front, re.M)
        fm = [f"title: {title.group(1).strip() if title else ''}"]
        if order:
            fm.append(f"order: {order.group(1).strip()}")
        if parent:
            fm.append(f"parent: {parent.group(1).strip()}")
        fm.append(f"translation-of: {fingerprint(text)}")
        lines, in_code = [], False
        for line in body.split("\n"):
            if line.strip().startswith("```"):
                in_code = not in_code
            m = None if in_code else re.match(r"^(#{1,6})\s+(.*?)\s*$", line)
            if m and "<!-- #" not in line:
                line = f"{m.group(1)} {m.group(2)} <!-- #{slug(m.group(2))} -->"
            lines.append(line)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "w", encoding="utf-8", newline="\n") as f:
            f.write("---\n" + "\n".join(fm) + "\n---\n" + "\n".join(lines))
        print("started", page)


def cmd_stamp(a):
    pages = [p for p in en_pages() if os.path.exists(os.path.join(HELP, a.lang, p + ".md"))] if a.all else a.pages
    for page in pages:
        dst = os.path.join(HELP, a.lang, page + ".md")
        text = read(dst)
        fp = fingerprint(read(os.path.join(HELP, "en", page + ".md")))
        front, body = split_front(text)
        if re.search(r"^translation-of:", front, re.M):
            front = re.sub(r"^translation-of:.*$", "translation-of: " + fp, front, flags=re.M)
        else:
            front += "\ntranslation-of: " + fp
        with open(dst, "w", encoding="utf-8", newline="\n") as f:
            f.write("---\n" + front + "\n---\n" + body)
        print("stamped", page, fp)


def cmd_status(a):
    folder = os.path.join(HELP, a.lang)
    if not os.path.isdir(folder):
        sys.exit(f"no folder {folder}: start the language with 'new'")
    problems, missing, stale, done = [], [], [], []
    meta_path = os.path.join(folder, "_language.json")
    if not os.path.exists(meta_path):
        problems.append("_language.json is missing")
    else:
        meta = json.load(open(meta_path, encoding="utf-8"))
        empty = [k for k, v in meta.get("ui", {}).items() if not v]
        if empty:
            problems.append(f"_language.json: {len(empty)} interface texts not translated: {', '.join(empty)}")
    sp = os.path.join(STRINGS, a.lang + ".json")
    if os.path.exists(sp):
        ph = json.load(open(sp, encoding="utf-8")).get("phrases", {})
        tpl = json.load(open(os.path.join(STRINGS, "template.json"), encoding="utf-8")).get("phrases", {})
        n_empty = sum(1 for k in tpl if not ph.get(k))
        if n_empty:
            problems.append(f"strings/{a.lang}.json: {n_empty} of {len(tpl)} generated phrases not translated")
    for page in en_pages():
        en_path = os.path.join(HELP, "en", page + ".md")
        tr_path = os.path.join(folder, page + ".md")
        if not os.path.exists(tr_path):
            missing.append(page)
            continue
        en_text, tr_text = read(en_path), read(tr_path)
        front, body = split_front(tr_text)
        fp = re.search(r"^translation-of:\s*(\w+)", front, re.M)
        if not fp or fp.group(1) != fingerprint(en_text):
            stale.append(page)
        else:
            done.append(page)
        if re.search(r"^ids:", front, re.M):
            problems.append(f"{page}: lists ids; a translation takes them from the English page")
        # every heading an English help id or link points at must still be there
        en_front = split_front(en_text)[0]
        wanted = set(re.findall(r"#([\w-]+)", " ".join(re.findall(r"^ids:.*$", en_front, re.M))))
        have = set(heading_anchors(body))
        for w in sorted(wanted - have):
            problems.append(f"{page}: no heading with the anchor '{w}' (keep the <!-- #{w} --> comment)")
    extra = []
    for dp, _, fs in os.walk(folder):
        for f in fs:
            if f.endswith(".md"):
                rel = os.path.relpath(os.path.join(dp, f), folder).replace("\\", "/")[:-3]
                if not os.path.exists(os.path.join(HELP, "en", rel + ".md")):
                    extra.append(rel)
    for e in extra:
        problems.append(f"{e}: no English page of that path (renamed or removed?)")
    total = len(en_pages())
    print(f"{a.lang}: {len(done)} of {total} pages translated and up to date, {len(stale)} out of date, {len(missing)} not started")
    if a.list:
        for p in stale:
            print("  out of date:", p)
        for p in missing:
            print("  not started:", p)
    for p in problems:
        print("PROBLEM", p)
    sys.exit(1 if problems else 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    n = sub.add_parser("new")
    n.add_argument("lang")
    n.add_argument("--name", default="")
    n.add_argument("--english-name", default="")
    for name in ("start", "stamp"):
        s = sub.add_parser(name)
        s.add_argument("lang")
        s.add_argument("pages", nargs="*")
        s.add_argument("--all", action="store_true")
    st = sub.add_parser("status")
    st.add_argument("lang")
    st.add_argument("--list", action="store_true")
    a = ap.parse_args()
    if a.cmd != "new" and a.lang == "en":
        sys.exit("en is the original, not a translation")
    {"new": cmd_new, "start": cmd_start, "stamp": cmd_stamp, "status": cmd_status}[a.cmd](a)


if __name__ == "__main__":
    main()
