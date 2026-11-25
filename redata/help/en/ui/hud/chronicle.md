---
title: Statistics and timeline
ids: [ui/hud/statistics, ui/hud/statistics/graph#the-graph, ui/hud/timeline#timeline, ui/hud/timeline/chronicle#chronicle-view, ui/hud/timeline/statistics#statistics-view, ui/hud/timeline/export#export, ui/hud/announcements#announcements]
order: 90
---

# Statistics and timeline

Two windows of wkbai's own follow the game as it goes. Open and close them with **F6** and **F7**, or with the [Statistics and Timeline buttons](top-bar.md#statistics-and-timeline) on the top bar. You can move them and change their size.

## Statistics

![The statistics window with the gold graph](shot:hud-statistics)

**F6.** A table of the latest numbers, one row per player:

| Column | What |
|---|---|
| Food, Wood, Gold | In store |
| Peasants, Soldiers, Buildings | How many it has |
| Units lost, Buildings lost | Since the start (killed, destroyed or taken) |

The numbers are taken every 10 game seconds.

### The graph

Under the table, pick one value in **Graph** (Food, Wood, Gold, Peasants, Soldiers or Buildings). Each player's line is drawn in its colour over the game so far. The highest value is at the top left, the game time at the bottom right.

**Who is shown:** you and your allies. Once you have won or been defeated (still watching), and in a replay, every player.

## Timeline

![The timeline told as a chronicle](shot:hud-timeline)

**F7.** The events of the game: battles, sieges, raids, villages founded and destroyed. Each is named after the region it happened in ("Battle of Ergor", "Siege of Karak Ungor", "Raid on Fenmoor"). wkbai gives every map's regions names of their own.

In a game you see only the events you saw (with fog of war on): in sight, or with your own units in them. In a replay, and after the game's end, all of them.

Two views, switched at the top of the window (the choice is kept, option [Timeline told as a chronicle](help:option/timelineChronicle)):

### Chronicle view

A historian's account, oldest first, like a journal. Each entry has its date, its title and a paragraph with the players in their colours. The window follows the newest entry while you read at its end.

### Statistics view

The plain numbers, newest first: the time, the event, and for each side how many fought and what it lost by type ("Saint Jane: 23 fighting, lost 7 (4 Spearman, 3 Archer)"). Below, where they apply: **Most kills**, **Razed by**, **Present** (heroes there) and **Fallen** (heroes who died there).

### Clicking

- **An entry's title:** the camera looks at the place.
- **A unit's name** (a link): your own unit, still alive, is selected and looked at; another's is looked at when you can see it; else the camera goes to where it happened.

### Export

**Export...** at the right of the view switch saves the timeline as a file, to keep or to share:

- **Markdown (.md):** plain text with light marks (names in bold, units in italics), for forums, wikis and notes;
- **Web page (.html):** opens in any browser, with the players in their colours on a dark page.

The file holds what the window shows (the events you saw, or all of them in a replay and after the game), and both views in one: each event's date, title and paragraph, then its numbers. It goes to the **chronicles** folder in [wkbai's own folder](../../files.md#your-own-folder) (`%APPDATA%\wkbai\chronicles`), named after the date and time ("Chronicle 2026-11-24 21-05-12.html"). The line under the switch then names it: **Open** shows the file, **Folder** the folder, **x** hides the line.

## Announcements

When a hero rises or falls on your side (or an ally's), a line slides in at the top of the screen with its name, and fades after 6 seconds. In a replay every event is announced this way: see [Replay controls](replay-controls.md#events).
