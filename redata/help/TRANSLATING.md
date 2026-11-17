# Translating wkbai's help

The help is written in English (`en/`). A translation is a folder of its own beside it, such as `de/` or `fr/`, with the same page paths. You can translate as much or as little as you like: every page you haven't translated yet is shown in English, with a short notice at the top.

The tools are in `tools/help/`. Run them from the repository's root with `python -I`. They need nothing beyond Python 3.

## 1. Start the language

```
python -I tools/help/translation.py new de --name Deutsch --english-name German
```

This creates two files to fill in:

- **`redata/help/de/_language.json`**:
  - `name`: the language's own name, as the language menu shows it ("Deutsch").
  - `ui`: the help window's own texts: Contents, Search, Back, the right-click menu's "Help: {0}" and so on. `{0}` and `{1}` stand for a value the game fills in. Keep them in your text.
  - `detect`: how wkbai recognises a game in this language, so that it can open the help in the game's language by itself. Give the text your language's copy of the game shows for each of the language keys listed. For English, `FRONT_END_MAIN_MENU_SKIRMISH` is "Skirmish" and `FRONT_END_MAIN_MENU_EXIT` is "Exit". Every key must match.
- **`tools/help/strings/de.json`**: the fixed phrases of the generated unit, building, upgrade and command pages, such as "Trained at:", "Cost", "food" and "every".
  - `{0}` stands for a link.
  - An empty phrase stays English.
  - `decimal` is your decimal separator (`","` makes 2.5 into 2,5).

## 2. Translate pages

```
python -I tools/help/translation.py start de ui/hud/minimap controls/keys
```

Each page is copied from `en/` into `de/`, ready to translate (`--all` copies every page):

- **Front matter:** translate `title`. Leave `translation-of` alone. A translation lists no `ids`: it uses the English page's, so every right-click and F1 still finds it.
- **Headings:** each one ends in a comment such as `<!-- #what-it-shows -->`. Translate the words and **keep the comment**: it is the heading's fixed name, which links and help ids point at.
  - `## What it shows <!-- #what-it-shows -->` becomes `## Was sie zeigt <!-- #what-it-shows -->`
- **Links:** translate the text in brackets, never the target in parentheses:
  - `[the minimap](../hud/minimap.md)` becomes `[die Minikarte](../hud/minimap.md)`.
  - Targets starting with `help:` and `show:` also stay as they are.
- **Pictures:** translate the description in `![...]`, and leave `shot:` and `game:` names as they are.
  - **Screenshots:** each player's game takes them itself, so they show the player's own language without any work from you.
  - **Pictures with words in them** (our diagrams): put your own version at the same path under `redata/help/img/de/`. For example, `img/mechanics/resource-flow.svg` becomes `img/de/mechanics/resource-flow.svg`. It is then used in place of the English one.
- **Game names:** use the names your language's copy of the game shows for units, buildings, research and commands.
- **Generated parts:** on unit, building, upgrade and command pages, leave everything between `<!-- gen:... -->` and `<!-- /gen:... -->` untranslated. A tool fills it in (see 3).
- **Code and keys:** leave anything in `code` style as it is: setting names, files, keys like `Ctrl+F1`.
- **Style:** the same as the English pages. Use plain words and short sentences, and say what the player sees first.

## 3. The generated parts and the game's names

```
python -I tools/help/gamedata_pages.py --lang de --language "<your game>/Languages/Language.txt"
```

This fills the generated parts of the pages you have started:
- **Names** come from your language's copy of the game. Its `Language.txt` is inside the game's `data.bcp` (extract it with any BCP tool).
- **Phrases** come from `tools/help/strings/de.json`.

Run it again whenever you have filled in more phrases or started more pages. It never touches your own text.

In the game itself, the help always shows unit and building names as the loaded game names them.

## 4. Keeping up with the English help

```
python -I tools/help/translation.py status de --list
```

- **Counts:** how many pages are translated, how many are out of date, and how many are not started.
- **Out of date:** a page is out of date when its English page changed after you translated it. Compare the two, bring yours up to date, then record that:
  - `python -I tools/help/translation.py stamp de ui/hud/minimap`
- **Problems:** a heading comment that went missing, `ids` in a translation, a page whose English page was renamed, untranslated window texts.

The game shows an out-of-date page anyway, with a short notice that the English page has changed.

## 5. Check it

```
python -I tools/help/build_html.py --lang de
```

This writes the help as web pages to `C:\Dev\help-html\de` (`--out` for another folder) and lists every broken link. Open `index.html` in a browser to read through your translation.

## How the game picks the language

- **Help language: As the game** (the default): the help opens in the language whose `detect` texts match the installed game, else in English.
- **A language chosen** in that setting: that one, whatever the game's language.
- **Page by page:** each page comes from the chosen language and falls back to English. Search looks through both, and the results mark English pages.
