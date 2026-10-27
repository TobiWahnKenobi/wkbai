-- wkbre2 menus: the main menu and its screens, and the in-game menu (Esc).
--
-- Drawn with the original's interface art and bitmap fonts. Positions are in pixels of a
-- 1920x1080 window (the original draws its menus at native pixel size; these are measured
-- from it) and scale with the window height; x is relative to the window's centre.
--
-- Extending:
--  * a main menu entry: add a line to MainMenuItems (the column re-spaces itself);
--  * an in-game menu entry: add a line to InGameMenuItems;
--  * a setting: register it in interface/MenuOptions.cpp (it appears on its Options page);
--  * a screen: add a table with draw() to Screens and open it with push("name").

---------------------------------------------------------------------------- Art and text

local ART = {
	button = "Interface\\InGame\\Menus\\Background ",           -- Left/Middle/Right, _High, _Dep
	tab = "Interface\\FrontEnd\\Tab_",                          -- Left/Middle/Right, _High, _Dep
	slider = "Interface\\InGame\\Menus\\Slider.tga",
	sliderGrey = "Interface\\InGame\\Menus\\Slider_Grey.tga",
	knob = "Interface\\InGame\\Menus\\Slider Button.tga",
	knobGrey = "Interface\\InGame\\Menus\\Slider Button_Grey.tga",
	frame = "Interface\\InGame\\Menus\\Backgrounds\\",         -- the gold dialog frame
	listSelection = "Interface\\InGame\\Menus\\List_Selection.tga",
	listRollover = "Interface\\InGame\\Menus\\List_Rollover.tga",
	sky = "Interface\\FrontEnd\\Background_Scroll_1.tga",
	map = "Interface\\FrontEnd\\Background_Scroll_2.tga",
	army = "Interface\\FrontEnd\\Background_Static_2.tga",
	sea = "Interface\\FrontEnd\\Sea_Background_Static.tga",
	logo = "Interface\\FrontEnd\\Warrior Kings Logo.tga",
}

local FONT_TITLE = "Morpheus 40"
local FONT_BUTTON = "Morpheus 20"
local FONT_SMALL_BUTTON = "Morpheus 15"
local FONT_LIST = "Proportional 15"
local FONT_TEXT = "Proportional 20"

-- 0xAABBGGRR
local WHITE = 0xFFFFFFFF
local GREY = 0xFF8C8C8C
local DARK_GREY = 0xFF5A5A5A
local YELLOW = 0xFF46C8E6       -- the list text
local PANEL_FILL = 0x46100A30
local PANEL_BORDER = 0xFF1A14B4
local DIM = 0x99000000

local function T(key) return getText(key) end

---------------------------------------------------------------------------- Layout and input

local W, H, S = 1920, 1080, 1
local function X(rx) return W / 2 + (rx - 960) * S end
local function Y(ry) return ry * S end
local function L(v) return v * S end

local mouse = { x = 0, y = 0, down = false, pressed = false, released = false, wheel = 0, blocked = false }
local activeDrag = nil -- the slider being dragged

local function inside(x, y, w, h)
	return mouse.x >= x and mouse.x < x + w and mouse.y >= y and mouse.y < y + h
end

local function hovered(x, y, w, h)
	return not mouse.blocked and activeDrag == nil and inside(x, y, w, h)
end

---------------------------------------------------------------------------- Widgets

-- Text centred in a box
local function centredText(font, text, x, y, w, h, colour)
	local scale = S
	local tw = textWidth(font, text, scale)
	local th = fontHeight(font, scale)
	drawText(font, x + (w - tw) / 2, y + (h - th) / 2, text, colour, scale)
end

-- Where the silver rim lies in the three-part art (in its pixels): the box a widget is given
-- is the rim's box, as in the original (its 203x38 menu buttons are the rim's size)
local BUTTON_RIM = { width = 52, height = 52, top = 7, bottom = 42, left = 10, right = 43 }
local TAB_RIM = { width = 52, height = 55, top = 3, bottom = 50, left = 6, right = 45 }

-- Left cap, stretched middle, right cap, scaled so that the rim fills the box
local function threeSlice(prefix, suffix, rim, x, y, w, h, colour)
	local k = h / (rim.bottom - rim.top)
	local ah, ay = rim.height * k, y - rim.top * k
	local cw = rim.width * k
	local x0 = x - rim.left * k
	local x1 = x + w + (rim.width - rim.right) * k
	if x1 - x0 < 2 * cw then cw = (x1 - x0) / 2 end
	drawImage(prefix .. "Left" .. suffix .. ".tga", x0, ay, cw, ah, colour)
	drawImage(prefix .. "Middle" .. suffix .. ".tga", x0 + cw, ay, x1 - x0 - 2 * cw, ah, colour)
	drawImage(prefix .. "Right" .. suffix .. ".tga", x1 - cw, ay, cw, ah, colour)
end

-- A menu button; returns true when clicked
local function button(text, x, y, w, h, enabled, selected)
	if enabled == nil then enabled = true end
	local hot = enabled and hovered(x, y, w, h)
	local suffix = ""
	if selected or (hot and mouse.down) then suffix = "_Dep" elseif hot then suffix = "_High" end
	threeSlice(ART.button, suffix, BUTTON_RIM, x, y, w, h)
	local font = (h < L(30)) and FONT_SMALL_BUTTON or FONT_BUTTON
	centredText(font, text, x, y, w, h, enabled and WHITE or GREY)
	return hot and mouse.released
end

-- A slider over a 0..1 position; returns the new position
local function slider(id, pos, x, y, w, enabled)
	local h = w / 3
	drawImage(enabled and ART.slider or ART.sliderGrey, x, y, w, h)
	-- the track runs between the rounded ends of the bar
	local x0, x1 = x + w * 0.06, x + w * 0.94
	local k = h * 0.7
	if enabled then
		if activeDrag == id then
			if mouse.down then
				pos = math.max(0, math.min(1, (mouse.x - x0) / (x1 - x0)))
			else
				activeDrag = nil
			end
		elseif hovered(x, y, w, h) and mouse.pressed then
			activeDrag = id
			pos = math.max(0, math.min(1, (mouse.x - x0) / (x1 - x0)))
		end
	end
	local kx = x0 + pos * (x1 - x0)
	drawImage(enabled and ART.knob or ART.knobGrey, kx - k / 2, y + (h - k) / 2, k, k)
	return pos
end

-- Tabs in a row; returns the index of the selected tab
local function tabs(labels, selected, x, y, h, enabledList)
	local cx = x
	for i, label in ipairs(labels) do
		local w = textWidth(FONT_BUTTON, label, S) + L(56)
		local enabled = not enabledList or enabledList[i] ~= false
		local hot = enabled and hovered(cx, y, w, h)
		local suffix = (i == selected) and "_Dep" or (hot and "_High" or "")
		threeSlice(ART.tab, suffix, TAB_RIM, cx, y, w, h)
		centredText(FONT_BUTTON, label, cx, y, w, h, (i == selected) and WHITE or (enabled and GREY or DARK_GREY))
		if hot and mouse.released then selected = i end
		cx = cx + w + L(2)
	end
	return selected
end

-- An image repeated over a box, at the given size per tile
local function tiled(path, x, y, w, h, size, colour)
	local ty = 0
	while ty < h do
		local th = math.min(size, h - ty)
		local tx = 0
		while tx < w do
			local tw = math.min(size, w - tx)
			drawImageUV(path, x + tx, y + ty, tw, th, 0, 0, tw / size, th / size, colour)
			tx = tx + size
		end
		ty = ty + size
	end
end

-- The panels of the original's list screens: a dark red tint with a red border; solid: the
-- dark brown of its settings pages
local function panel(x, y, w, h, solid)
	if solid then
		tiled(ART.frame .. "Middle.tga", x, y, w, h, L(50), 0xE6FFFFFF)
	else
		drawRect(x, y, w, h, PANEL_FILL)
	end
	drawFrame(x, y, w, h, PANEL_BORDER, L(2))
end

-- The gold frame of the original's dialogs (nine pieces of 50x50)
local function goldFrame(x, y, w, h)
	local c = L(50)
	tiled(ART.frame .. "Middle.tga", x + c / 2, y + c / 2, w - c, h - c, c)
	drawImage(ART.frame .. "Top.tga", x + c, y, w - 2 * c, c)
	drawImage(ART.frame .. "Bottom.tga", x + c, y + h - c, w - 2 * c, c)
	drawImage(ART.frame .. "Left.tga", x, y + c, c, h - 2 * c)
	drawImage(ART.frame .. "Right.tga", x + w - c, y + c, c, h - 2 * c)
	drawImage(ART.frame .. "Top_Left.tga", x, y, c, c)
	drawImage(ART.frame .. "Top_Right.tga", x + w - c, y, c, c)
	drawImage(ART.frame .. "Bottom_Left.tga", x, y + h - c, c, c)
	drawImage(ART.frame .. "Bottom_Right.tga", x + w - c, y + h - c, c, c)
end

-- A scrolling list of text rows (a string, or { text, colour }); state = { selected = index,
-- scroll = first row }. Returns whether a row was double-clicked, and the row under the mouse.
local function listBox(state, items, x, y, w, h)
	local rowH = L(16)
	local rows = math.floor((h - L(8)) / rowH)
	if hovered(x, y, w, h) and mouse.wheel ~= 0 then
		state.scroll = state.scroll - mouse.wheel * 3
	end
	state.scroll = math.max(0, math.min(state.scroll, math.max(0, #items - rows)))
	local activated, hoveredRow = false, nil
	pushClip(x, y, w, h)
	for r = 1, rows do
		local i = state.scroll + r
		local item = items[i]
		if not item then break end
		local ry = y + L(4) + (r - 1) * rowH
		if i == state.selected then
			drawImage(ART.listSelection, x + L(2), ry, w - L(4), rowH)
		elseif hovered(x, ry, w, rowH) then
			drawImage(ART.listRollover, x + L(2), ry, w - L(4), rowH)
		end
		local text, colour = item, YELLOW
		if type(item) == "table" then text, colour = item[1], item[2] or YELLOW end
		drawText(FONT_LIST, x + L(8), ry, text, colour, S)
		if hovered(x, ry, w, rowH) then hoveredRow = i end
		if hovered(x, ry, w, rowH) and mouse.pressed then
			if state.selected == i and state.lastClick and getTime() - state.lastClick < 0.4 then
				activated = true
			end
			state.selected = i
			state.lastClick = getTime()
		end
	end
	popClip()
	return activated, hoveredRow
end

-- A one-line text entry (always focused: the screen has one); state = { text = "" }. Typed
-- characters in `refused` are ignored. Returns whether Enter was pressed.
local function textField(state, x, y, w, h, refused, maxLength)
	panel(x, y, w, h)
	for ch in string.gmatch(getTypedText(), "[%z\1-\127\194-\244][\128-\191]*") do
		if not (refused and string.find(refused, ch, 1, true)) and #state.text < (maxLength or 64) then
			state.text = state.text .. ch
		end
	end
	if isKeyPressed("Backspace") then
		state.text = string.gsub(state.text, "[%z\1-\127\194-\244][\128-\191]*$", "")
	end
	local th = fontHeight(FONT_TEXT, S)
	pushClip(x, y, w, h)
	local tx, ty = x + L(8), y + (h - th) / 2
	-- the end of a long text stays in view
	local tw = textWidth(FONT_TEXT, state.text, S)
	if tw > w - L(20) then tx = x + w - L(12) - tw end
	drawText(FONT_TEXT, tx, ty, state.text, YELLOW, S)
	if math.floor(getTime() * 2) % 2 == 0 then
		drawRect(tx + tw + L(1), ty + L(2), L(2), th - L(4), YELLOW)
	end
	popClip()
	return isKeyPressed("Return") or isKeyPressed("Keypad Enter")
end

-- Splits text into lines that fit a width (keeping its own line breaks)
local function wrapText(font, text, width)
	local lines = {}
	for paragraph in string.gmatch(text .. "\n", "(.-)\n") do
		local line = ""
		for word in string.gmatch(paragraph, "%S+") do
			local try = (line == "") and word or (line .. " " .. word)
			if textWidth(font, try, S) > width and line ~= "" then
				lines[#lines + 1] = line
				line = word
			else
				line = try
			end
		end
		lines[#lines + 1] = line
	end
	return lines
end

local function title(text)
	centredText(FONT_TITLE, text, X(0), Y(10), W, L(50), WHITE)
end

---------------------------------------------------------------------------- Backgrounds

-- The 1024x768 background art fills the window at its own aspect ratio (cropping rather than
-- stretching); anchor: 0 = top, 0.5 = centre, 1 = bottom
local function coverRect(path, anchor)
	local iw, ih = imageSize(path)
	if iw == 0 then return 0, 0, W, H end
	local k = math.max(W / iw, H / ih)
	local w, h = iw * k, ih * k
	return (W - w) / 2, (H - h) * anchor, w, h
end

-- A seamless image scrolled sideways by offset (0..1 of its width), drawn in two pieces that
-- meet at the wrap: the menu sampler clamps, so texture coordinates past 1 would smear the
-- last column
local function scrolled(path, x, y, w, h, offset)
	local split = (1 - offset) * w
	drawImageUV(path, x, y, split, h, offset, 0, 1, 1)
	drawImageUV(path, x + split, y, w - split, h, 0, 0, offset, 1)
end

-- The front end's background: the sunset sky and the map scrolling slowly behind the army
local function sunsetBackground()
	local t = getTime()
	local x, y, w, h = coverRect(ART.sky, 0.5)
	scrolled(ART.sky, x, y, w, h, (t * 0.004) % 1)
	x, y, w, h = coverRect(ART.map, 0.5)
	scrolled(ART.map, x, y, w, h, (t * 0.008) % 1)
	x, y, w, h = coverRect(ART.army, 1)
	drawImage(ART.army, x, y, w, h)
end

local function seaBackground()
	local x, y, w, h = coverRect(ART.sea, 0.5)
	drawImage(ART.sea, x, y, w, h)
end

---------------------------------------------------------------------------- Screens

local Screens = {}
local stack = {}
local dialog = nil -- { text, buttons = { { label, action } } }
local openedThisFrame = false -- the Esc that opened the in-game menu must not close it again

local function push(name)
	local screen = Screens[name]
	if not screen then return end
	stack[#stack + 1] = name
	if screen.open then screen.open() end
end

local function pop()
	stack[#stack] = nil
end

local function confirm(text, onYes)
	dialog = { text = text, buttons = {
		{ T("FRONT_END_DIALOG_BUTTON_YES"), function() dialog = nil; onYes() end },
		{ T("FRONT_END_DIALOG_BUTTON_NO"), function() dialog = nil end },
	} }
end

local function drawDialog()
	local w, h = L(440), L(240)
	local x, y = (W - w) / 2, (H - h) / 2
	drawRect(0, 0, W, H, 0x66000000)
	goldFrame(x, y, w, h)
	-- the text, wrapped at spaces
	local words, lines, line = {}, {}, ""
	for word in string.gmatch(dialog.text, "%S+") do words[#words + 1] = word end
	for _, word in ipairs(words) do
		local try = (line == "") and word or (line .. " " .. word)
		if textWidth(FONT_TEXT, try, S) > w - L(80) and line ~= "" then
			lines[#lines + 1] = line
			line = word
		else
			line = try
		end
	end
	if line ~= "" then lines[#lines + 1] = line end
	local lh = fontHeight(FONT_TEXT, S)
	for i, l in ipairs(lines) do
		centredText(FONT_TEXT, l, x, y + L(40) + (i - 1) * lh, w, lh, 0xFF9CE6F0)
	end
	local n = #dialog.buttons
	local bw, bh = L(170), L(36)
	local gap = (w - n * bw) / (n + 1)
	for i, b in ipairs(dialog.buttons) do
		if button(b[1], x + gap + (i - 1) * (bw + gap), y + h - L(80), bw, bh) then
			b[2]()
			return
		end
	end
end

-- A centred column of buttons: items = { { text or key, action, enabled } }. The column keeps
-- the original's spacing (74 pixels) and tightens it when more entries must fit.
local function buttonColumn(items, top, bottom)
	local bw, bh = L(203), L(38)
	local n = #items
	local pitch = L(74.25)
	if n > 1 then pitch = math.min(pitch, (bottom - top - bh) / (n - 1)) end
	for i, item in ipairs(items) do
		local enabled = item.enabled
		if type(enabled) == "function" then enabled = enabled() end
		if enabled == nil then enabled = true end
		local text = item.text or T(item.key)
		if button(text, X(960 - 101.5), top + (i - 1) * pitch, bw, bh, enabled) and item.action then
			item.action()
		end
	end
end

-- Main menu ---------------------------------------------------------------------------

MainMenuItems = {
	{ key = "FRONT_END_MAIN_MENU_LOAD_GAME", action = function() push("load") end },
	{ key = "FRONT_END_MAIN_MENU_CAMPAIGN", enabled = false },
	{ key = "FRONT_END_MAIN_MENU_SKIRMISH", action = function() push("skirmish") end },
	{ key = "FRONT_END_MAIN_MENU_MULTIPLAYER", action = function() push("multiplayer") end },
	{ key = "FRONT_END_MAIN_MENU_TUTORIAL", enabled = false },
	{ key = "FRONT_END_MAIN_MENU_OPTIONS", action = function() push("options") end },
	{ text = "Mods", action = function() request("mods") end },
	{ key = "FRONT_END_OPTIONS_GENERAL_CREATION", enabled = false },
	{ key = "FRONT_END_MAIN_MENU_CREDITS", enabled = false },
	{ key = "FRONT_END_MAIN_MENU_EXIT", action = function() request("quit") end },
}

Screens.main = {
	background = sunsetBackground,
	draw = function()
		-- the logo at its own aspect ratio; about the size of the original's (which stretches it)
		local k = 1.6
		drawImage(ART.logo, X(960 - 500 * k), Y(40), L(1000 * k), L(264 * k))
		buttonColumn(MainMenuItems, Y(377), Y(1010))
		drawText(FONT_LIST, W - L(100), H - L(26), "wkbre2", WHITE, S)
	end,
}

-- Load Game ----------------------------------------------------------------------------

local function savesOfType(ext)
	local list = {}
	for _, name in ipairs(getSaveGames()) do
		if string.lower(string.sub(name, -#ext)) == ext then list[#list + 1] = name end
	end
	return list
end

local function stripExtension(name) return (string.gsub(name, "%.[^%.]*$", "")) end

local loadState = { selected = 0, scroll = 0, tab = 1 }
Screens.load = {
	background = sunsetBackground,
	open = function() loadState.selected = 0; loadState.scroll = 0 end,
	draw = function()
		title(T("FRONT_END_LOAD_GAME_TITLE"))
		-- saved games are not sorted by mode yet: all are under Skirmish
		loadState.tab = tabs({ T("FRONT_END_LOAD_GAME_SKIRMISH"), T("FRONT_END_LOAD_GAME_CAMPAIGN"), T("FRONT_END_LOAD_GAME_MULTIPLAYER") },
			loadState.tab, X(90), Y(70), L(30), { true, false, false })
		local saves = savesOfType(".sav")
		local names = {}
		for i, s in ipairs(saves) do names[i] = stripExtension(s) end
		panel(X(90), Y(100), L(1740), L(910))
		local activated = listBox(loadState, names, X(92), Y(104), L(1736), L(902))
		local chosen = saves[loadState.selected]
		if button(T("FRONT_END_BUTTON_LOAD"), X(735), Y(1022), L(141), L(24), chosen ~= nil) or (activated and chosen) then
			request("start", chosen)
		end
		button(T("DIALOG_BUTTON_DELETE"), X(890), Y(1022), L(141), L(24), false)
		if button(T("FRONT_END_BUTTON_BACK"), X(1045), Y(1022), L(141), L(24)) then pop() end
	end,
}

-- Save Game As (in game) ----------------------------------------------------------------

-- A short message over the screen ("Game saved successfully"), for a few seconds
local notice = nil

-- what a file name can't have
local FILE_NAME_REFUSED = "\\/:*?\"<>|"

local saveState = { selected = 0, scroll = 0, text = "" }
Screens.save = {
	open = function()
		saveState.selected = 0
		saveState.scroll = 0
		saveState.text = getCurrentSave()
		saveState.pending = nil
	end,
	draw = function()
		local w, h = L(720), L(640)
		local x, y = X(960) - w / 2, (H - h) / 2
		goldFrame(x, y, w, h)
		centredText(FONT_BUTTON, T("FRONT_END_MAIN_MENU_SAVE_GAME_AS"), x, y + L(30), w, L(30), WHITE)
		-- the existing save games: picking one fills in its name (to overwrite it)
		local saves = savesOfType(".sav")
		local names = {}
		for i, sv in ipairs(saves) do names[i] = stripExtension(sv) end
		local before = saveState.selected
		local lx, ly, lw, lh = x + L(50), y + L(75), w - L(100), L(380)
		panel(lx, ly, lw, lh)
		local activated = listBox(saveState, names, lx + L(2), ly + L(2), lw - L(4), lh - L(4))
		if saveState.selected ~= before and names[saveState.selected] then saveState.text = names[saveState.selected] end
		drawText(FONT_TEXT, lx, ly + lh + L(16), T("FRONT_END_SAVE_GAME_FILENAME"), WHITE, S)
		local entered = textField(saveState, lx, ly + lh + L(44), lw, L(32), FILE_NAME_REFUSED)
		local name = string.match(saveState.text, "^%s*(.-)%s*$")
		local valid = name ~= "" and not string.match(name, "^%.+$")
		local save = button(T("FRONT_END_BUTTON_SAVE"), X(960) - L(150), y + h - L(70), L(141), L(26), valid)
		if (save or entered or activated) and valid and not dialog then
			local exists = false
			for _, n in ipairs(names) do if string.lower(n) == string.lower(name) then exists = true end end
			if exists then
				confirm(string.format(T("FRONT_END_SAVE_GAME_OVERWRITE"), name), function() request("save", name) end)
			else
				request("save", name)
			end
			saveState.pending = name
		end
		if button(T("FRONT_END_BUTTON_BACK"), X(960) + L(9), y + h - L(70), L(141), L(26)) then pop() end
	end,
}

-- The program's answer to a "save" request
function FE_Saved(ok, name)
	notice = { text = ok and T("GAME_SAVED_SUCCESSFULLY") or ("Could not save \"" .. name .. "\""), until_ = getTime() + 2.5 }
	-- back to the in-game menu once saved
	if ok and stack[#stack] == "save" then pop() end
end

-- Skirmish -----------------------------------------------------------------------------

local levelState = { selected = 1, scroll = 0 }
-- The skirmish maps ("2 Player 4 Peaks.lvl"), shown as the original does: "4 Peaks (2)";
-- filter: only the maps for that many players (0: all)
local function levelList(filter)
	local levels, names = {}, {}
	for _, l in ipairs(savesOfType(".lvl")) do
		local players, name = string.match(stripExtension(l), "^(%d+) Player (.+)$")
		if players and (not filter or filter == 0 or tonumber(players) == filter) then
			levels[#levels + 1] = l
			names[#names + 1] = name .. " (" .. players .. ")"
		end
	end
	return levels, names
end

local function levelPicker(x, y, w, h, filter)
	x, y, w, h = x or X(20), y or Y(80), w or L(1880), h or L(920)
	local levels, names = levelList(filter)
	panel(x, y, w, h)
	local before = levelState.selected
	local activated = listBox(levelState, names, x + L(2), y + L(4), w - L(4), h - L(8))
	local chosen = levels[levelState.selected]
	if chosen and levelState.selected ~= before then request("select_level", chosen) end
	return chosen, activated
end

-- The player colours (index 1 ... 8, as the game's) and their swatches
local COLOURS = { "Blue", "Yellow", "Red", "Green", "Purple", "Orange", "Light_Blue", "Brown" }
local function colourSwatch(index)
	return "Interface\\FrontEnd\\Player Colours\\" .. (COLOURS[index] or "Blue") .. ".tga"
end

-- Who plays at each start position: { type = "human" | "ai" | "empty", general = blueprint or nil (random) }
local skirmish = { tab = 1, slots = {}, slotList = { selected = 1, scroll = 0 }, tier = 1,
	generalList = { selected = 0, scroll = 0 }, level = nil, playerFilter = 0 }

local TIERS = { { "CONSCRIPT", "FRONT_END_CHAT_AI_CONSCRIPT" }, { "VETERAN", "FRONT_END_CHAT_AI_VETERAN" }, { "ELITE", "FRONT_END_CHAT_AI_ELITE" } }

-- A new map: the human at the first position, random AI Generals at the others
local function resetSlots(level)
	skirmish.level = level
	skirmish.slots = {}
	local players = tonumber(string.match(level or "", "^(%d+) Player")) or 2
	for i = 1, players do
		skirmish.slots[i] = { type = (i == 1) and "human" or "ai", colour = ((i - 1) % 8) + 1, team = 0 }
	end
	skirmish.slotList.selected = math.min(2, players)
end

local generalsByBlueprint = nil
local function generals()
	local list = getGenerals()
	if not generalsByBlueprint then
		generalsByBlueprint = {}
		for _, g in ipairs(list) do generalsByBlueprint[g.blueprint] = g end
	end
	return list
end

local function slotText(slot)
	if slot.type == "human" then return T("FRONT_END_CHAT_PLAYER_NAME"), 0xFFFFC070 end
	if slot.type == "empty" then return T("FRONT_END_CHAT_AI_PLAYER"), GREY end
	local g = slot.general and generalsByBlueprint and generalsByBlueprint[slot.general]
	return g and g.name or T("FRONT_END_CHAT_RANDOM_GENERAL"), YELLOW
end

-- Sent before the start: "H;c1;t0|A:<general>;c2;t0|A;c3;t0|E"
local function slotsRequest()
	local parts = {}
	for i, slot in ipairs(skirmish.slots) do
		if slot.type == "human" then parts[i] = "H"
		elseif slot.type == "empty" then parts[i] = "E"
		else parts[i] = "A:" .. (slot.general or "") end
		parts[i] = parts[i] .. ";c" .. (slot.colour or 0) .. ";t" .. (slot.team or 0)
	end
	request("slots", table.concat(parts, "|"))
end

local function playersTab()
	generals()
	local slots = skirmish.slots
	-- the start positions
	panel(X(20), Y(88), L(408), L(197))
	drawText(FONT_LIST, X(28), Y(94), "#  " .. T("FRONT_END_CHAT_PLAYER"), WHITE, S)
	drawText(FONT_LIST, X(300), Y(94), T("FRONT_END_CHAT_COLOUR"), WHITE, S)
	drawText(FONT_LIST, X(372), Y(94), T("FRONT_END_CHAT_TEAM"), WHITE, S)
	local rows = {}
	for i, slot in ipairs(slots) do
		local text, colour = slotText(slot)
		rows[i] = { i .. "  " .. text, colour }
	end
	listBox(skirmish.slotList, rows, X(22), Y(110), L(404), L(130))
	-- colour and team of each position: a click changes them
	for r = 1, 8 do
		local i = skirmish.slotList.scroll + r
		local slot = slots[i]
		if not slot then break end
		local ry = Y(110) + L(4) + (r - 1) * L(16)
		if slot.type ~= "empty" then
			drawImage(colourSwatch(slot.colour), X(304), ry + L(1), L(36), L(14))
			centredText(FONT_LIST, (slot.team or 0) > 0 and tostring(slot.team) or "-", X(372), ry, L(40), L(16), WHITE)
			if hovered(X(300), ry, L(44), L(16)) and mouse.released then slot.colour = (slot.colour % 8) + 1 end
			if hovered(X(368), ry, L(48), L(16)) and mouse.released then slot.team = ((slot.team or 0) + 1) % 5 end
		end
	end
	local sel = slots[skirmish.slotList.selected]
	if button(T("FRONT_END_CHAT_PLAYER_NAME"), X(28), Y(252), L(124), L(24), sel ~= nil and sel.type ~= "human") then
		-- one human: the previous position becomes free
		for _, other in ipairs(slots) do
			if other.type == "human" then other.type = "empty" end
		end
		slots[skirmish.slotList.selected] = { type = "human", colour = sel.colour, team = sel.team }
	end
	if button(T("FRONT_END_CHAT_KICK_GENERAL"), X(160), Y(252), L(124), L(24), sel ~= nil and sel.type == "ai") then
		slots[skirmish.slotList.selected] = { type = "empty", colour = sel.colour, team = sel.team }
	end
	if button(T("FRONT_END_CHAT_ADD_AI_GENERAL"), X(292), Y(252), L(128), L(24), sel ~= nil and sel.type ~= "ai") then
		slots[skirmish.slotList.selected] = { type = "ai", colour = sel.colour, team = sel.team }
	end
	sel = slots[skirmish.slotList.selected]

	-- the AI Generals of the chosen tier; a click puts one at the selected position
	for i, tier in ipairs(TIERS) do
		local x = X(438) + (i - 1) * L(490)
		if button(T(tier[2]), x, Y(90), L(482), L(16), true, skirmish.tier == i) then skirmish.tier = i end
	end
	local list = { { T("FRONT_END_CHAT_RANDOM_GENERAL"), YELLOW } }
	local entries = { false }
	local inTier = {}
	for _, g in ipairs(generals()) do
		if g.category == TIERS[skirmish.tier][1] then inTier[#inTier + 1] = g end
	end
	table.sort(inTier, function(a, b) return a.name < b.name end)
	for _, g in ipairs(inTier) do
		list[#list + 1] = { g.name, YELLOW }
		entries[#entries + 1] = g
	end
	-- the row of the selected position's general
	skirmish.generalList.selected = 0
	if sel and sel.type == "ai" then
		for i, g in ipairs(entries) do
			if (g == false and sel.general == nil) or (g and g.blueprint == sel.general) then skirmish.generalList.selected = i end
		end
	end
	panel(X(437), Y(108), L(1465), L(117))
	local before = skirmish.generalList.selected
	local _, hoveredRow = listBox(skirmish.generalList, list, X(439), Y(110), L(1461), L(113))
	if skirmish.generalList.selected ~= before and sel then
		local g = entries[skirmish.generalList.selected]
		slots[skirmish.slotList.selected] = { type = "ai", general = g and g.blueprint or nil, colour = sel.colour, team = sel.team }
	end

	-- portrait and biography of the general under the mouse, or of the selected position's
	local shown = hoveredRow and entries[hoveredRow]
	if hoveredRow == nil and sel and sel.type == "ai" and sel.general then shown = generalsByBlueprint[sel.general] end
	panel(X(20), Y(288), L(1882), L(712))
	if shown then
		if shown.portrait ~= "" then drawImage(shown.portrait, X(30), Y(298), L(150), L(200)) end
		drawText(FONT_LIST, X(192), Y(300), shown.name, YELLOW, S)
		local y = Y(320)
		for _, line in ipairs(wrapText(FONT_LIST, shown.biography or "", L(1690))) do
			drawText(FONT_LIST, X(192), y, line, 0xFFE6E6E6, S)
			y = y + L(16)
		end
	elseif hoveredRow == 1 then
		drawText(FONT_LIST, X(192), Y(300), T("FRONT_END_CHAT_RANDOM_GENERAL"), YELLOW, S)
	end
end


-- The game settings (QuickSkirmishMenu::setSetting), with the original's defaults
local SETTINGS = {
	mode = 0, startFood = 500, startWood = 500, startGold = 200, populationLimit = 200,
	randomiseResources = 2, kingPiece = 0, clearFogOfWar = 1, allowDiplomacy = 0, teamReconnaissance = 0,
	barbarianTribes = 0, resourcePoints = 2500, victoryPointLimit = 10000, valhallaModeFood = 20000,
	unitLives = 0, capturableFlags = 4,
}
skirmish.settings = {}
for k, v in pairs(SETTINGS) do skirmish.settings[k] = v end

local function settingsRequests()
	for name, value in pairs(skirmish.settings) do
		request("skirmish_setting", name .. "=" .. math.floor(value))
	end
end

-- A round button of the original (Check_Box_*): on, off or greyed out; returns true when clicked
local function roundButton(x, y, size, on, enabled)
	local hot = enabled and hovered(x, y, size, size)
	local art = "Interface\\FrontEnd\\Check_Box_" .. (not enabled and "Grey" or (on and "On" or (hot and "Blank_High" or "Blank"))) .. ".tga"
	drawImage(art, x, y, size, size)
	return hot and mouse.released
end

local function plusMinusButton(kind, x, y, size)
	local hot = hovered(x, y, size, size)
	local suffix = (hot and mouse.down) and "_Dep" or (hot and "_High" or "")
	drawImage("Interface\\FrontEnd\\" .. kind .. "_Button" .. suffix .. ".tga", x, y, size, size)
	return hot and mouse.released
end

-- A value with - and + buttons, as the original's Start Food etc.
local function stepperRow(label, icon, name, step, min, max, y)
	local s = skirmish.settings
	local lw = textWidth(FONT_LIST, label, S)
	drawText(FONT_LIST, X(170) - lw, y + L(18), label, WHITE, S)
	if icon then drawImage(icon, X(174), y + L(12), L(20), L(24)) end
	if plusMinusButton("Minus", X(200), y, L(52)) then s[name] = math.max(min, s[name] - step) end
	centredText(FONT_LIST, tostring(math.floor(s[name])), X(260), y, L(620), L(52), WHITE)
	if plusMinusButton("Plus", X(892), y, L(52)) then s[name] = math.min(max, s[name] + step) end
end

local RESOURCE_LEVELS = { "FRONT_END_SETTINGS_RESOURCE_LEVEL_OFF", "FRONT_END_SETTINGS_RESOURCE_LEVEL_LOW",
	"FRONT_END_SETTINGS_RESOURCE_LEVEL_MEDIUM", "FRONT_END_SETTINGS_RESOURCE_LEVEL_HIGH" }

local function settingsTab()
	local s = skirmish.settings
	-- the map: a filter by player count, the list (map preview still to come)
	panel(X(106), Y(123), L(742), L(364))
	-- the map's image, square and centred (the original letterboxes it the same way)
	if skirmish.level and not drawLevelPreview(skirmish.level, X(106) + (L(742) - L(364)) / 2, Y(123), L(364), L(364)) then
		local name = string.gsub(string.gsub(skirmish.level, "%.lvl$", ""), "^(%d+) Player (.+)$", "%2 (%1)")
		centredText(FONT_BUTTON, name, X(106), Y(123), L(742), L(364), WHITE)
	end
	if button(T("FRONT_END_SETTINGS_MAP_ALL"), X(965), Y(127), L(680), L(30), true, skirmish.playerFilter == 0) then
		skirmish.playerFilter = 0
	end
	for n = 2, 8 do
		local x = X(1655) + (n - 2) * L(35)
		local hot = hovered(x, Y(126), L(32), L(32))
		drawImage("Interface\\FrontEnd\\Check_Box_" .. ((skirmish.playerFilter == n) and "On" or (hot and "Blank_High" or "Blank")) .. ".tga", x, Y(126), L(32), L(32))
		centredText(FONT_LIST, tostring(n), x, Y(126), L(32), L(32), WHITE)
		if hot and mouse.released then skirmish.playerFilter = n end
	end
	-- a new filter keeps the chosen map selected where it is still listed
	if skirmish.playerFilter ~= skirmish.listedFilter then
		skirmish.listedFilter = skirmish.playerFilter
		local levels = levelList(skirmish.playerFilter)
		levelState.selected, levelState.scroll = 1, 0
		for i, l in ipairs(levels) do
			if l == skirmish.level then levelState.selected = i end
		end
	end
	local chosen = levelPicker(X(955), Y(167), L(945), L(323), skirmish.playerFilter)
	if chosen and chosen ~= skirmish.level then
		resetSlots(chosen)
		request("select_level", chosen)
	end

	-- the resources (Standard mode) or the Valhalla settings
	if s.mode == 0 then
		stepperRow(T("FRONT_END_SETTINGS_START_FOOD"), "Interface\\InGame\\Info Panel\\Chicken.tga", "startFood", 100, 0, 100000, Y(514))
		stepperRow(T("FRONT_END_SETTINGS_START_WOOD"), "Interface\\InGame\\Info Panel\\Tree.tga", "startWood", 100, 0, 100000, Y(590))
		stepperRow(T("FRONT_END_SETTINGS_START_GOLD"), "Interface\\InGame\\Info Panel\\Coin.tga", "startGold", 50, 0, 100000, Y(666))
		stepperRow(T("FRONT_END_SETTINGS_POPULATION_LIMIT"), "Interface\\InGame\\Info Panel\\Man.tga", "populationLimit", 10, 10, 1000, Y(742))
		local lw = textWidth(FONT_LIST, T("FRONT_END_SETTINGS_RESOURCE_LEVEL"), S)
		drawText(FONT_LIST, X(170) - lw, Y(838), T("FRONT_END_SETTINGS_RESOURCE_LEVEL"), WHITE, S)
		if button(T(RESOURCE_LEVELS[s.randomiseResources + 1]), X(200), Y(826), L(744), L(38)) then
			s.randomiseResources = (s.randomiseResources + 1) % 4
		end
	else
		stepperRow(T("FRONT_END_SETTINGS_RESOURCE_POINTS"), nil, "resourcePoints", 250, 0, 100000, Y(514))
		stepperRow(T("FRONT_END_SETTINGS_VICTORY_TIME"), nil, "victoryPointLimit", 1000, 1000, 1000000, Y(590))
		stepperRow(T("FRONT_END_SETTINGS_VALHALLA_MODE_FOOD"), nil, "valhallaModeFood", 1000, 0, 1000000, Y(666))
		stepperRow(T("FRONT_END_SETTINGS_UNIT_LIVES"), nil, "unitLives", 1, 0, 100, Y(742))
		stepperRow(T("FRONT_END_SETTINGS_CAPTURABLE_FLAGS"), nil, "capturableFlags", 1, 0, 20, Y(818))
	end

	-- the mode, and the switches
	if roundButton(X(958), Y(514), L(52), s.mode == 0, true) then s.mode = 0 end
	drawText(FONT_LIST, X(1022), Y(532), T("FRONT_END_SETTINGS_GAME_TYPE_STANDARD_MODE"), s.mode == 0 and YELLOW or WHITE, S)
	if roundButton(X(958), Y(590), L(52), s.mode == 1, true) then s.mode = 1 end
	drawText(FONT_LIST, X(1022), Y(608), T("FRONT_END_SETTINGS_GAME_TYPE_VALHALLA_MODE"), s.mode == 1 and YELLOW or WHITE, S)
	local switches = {
		{ "clearFogOfWar", "FRONT_END_SETTINGS_FOG_OF_WAR", Y(590) },
		{ "allowDiplomacy", "FRONT_END_SETTINGS_ALLOW_DIPLOMACY", Y(666) },
		{ "teamReconnaissance", "FRONT_END_SETTINGS_TEAM_RECONNAISSANCE", Y(742) },
		{ "barbarianTribes", "FRONT_END_SETTINGS_BARBARIAN_TRIBES", Y(818) },
	}
	if s.mode == 0 then
		table.insert(switches, 1, { "kingPiece", "FRONT_END_SETTINGS_GAME_TYPE_KING_PIECE", Y(514) })
	end
	for _, sw in ipairs(switches) do
		local name, key, y = sw[1], sw[2], sw[3]
		if roundButton(X(1393), y, L(52), s[name] ~= 0, true) then s[name] = (s[name] ~= 0) and 0 or 1 end
		drawText(FONT_LIST, X(1457), y + L(18), T(key), WHITE, S)
	end
end

Screens.skirmish = {
	background = sunsetBackground,
	open = function()
		local levels = levelList()
		local level = levels[levelState.selected]
		if level then request("select_level", level) end
		if level ~= skirmish.level then resetSlots(level) end
	end,
	draw = function()
		title(T("FRONT_END_LOBBY_TITLE_SKIRMISH"))
		skirmish.tab = tabs({ T("FRONT_END_LOBBY_CHAT"), T("FRONT_END_LOBBY_SETTINGS") }, skirmish.tab, X(8), Y(50), L(30))
		panel(X(8), Y(80), L(1907), L(988), true)
		if skirmish.tab == 1 then playersTab() else settingsTab() end
		local humans = 0
		for _, slot in ipairs(skirmish.slots) do if slot.type == "human" then humans = humans + 1 end end
		local canStart = skirmish.level ~= nil and humans == 1
		if button(T("FRONT_END_CHAT_START"), X(770), Y(1032), L(182), L(24), canStart) then
			settingsRequests()
			slotsRequest()
			request("start", skirmish.level)
		end
		if button(T("FRONT_END_BUTTON_BACK_TO_MAIN_MENU"), X(970), Y(1032), L(182), L(24)) then pop() end
	end,
}

-- Multiplayer --------------------------------------------------------------------------

Screens.multiplayer = {
	background = sunsetBackground,
	open = Screens.skirmish.open,
	draw = function()
		title(T("FRONT_END_MAIN_MENU_MULTIPLAYER"))
		local chosen = levelPicker()
		-- hosting and joining are still the ImGui windows
		if button(T("FRONT_END_MULTIPLAYER_BUTTON_CREATE"), X(1468), Y(1052), L(210), L(24), chosen ~= nil) then
			request("host")
		end
		if button(T("FRONT_END_MULTIPLAYER_BUTTON_JOIN_IP"), X(1250), Y(1052), L(210), L(24)) then
			request("join")
		end
		if button(T("FRONT_END_BUTTON_BACK"), X(1697), Y(1052), L(210), L(24)) then pop() end
	end,
}

-- Options ------------------------------------------------------------------------------
-- wkbre2's own settings, a page per tab (pages and settings come from MenuOptions.cpp), drawn
-- with the original's sliders and buttons.

local optionsState = { page = 1, scroll = 0 }

local function sliderPosition(o, v)
	if o.logarithmic and o.min > 0 then
		return math.log(v / o.min) / math.log(o.max / o.min)
	end
	return (v - o.min) / (o.max - o.min)
end

local function sliderValue(o, p)
	local v
	if o.logarithmic and o.min > 0 then
		v = o.min * math.exp(p * math.log(o.max / o.min))
	else
		v = o.min + p * (o.max - o.min)
	end
	if o.step and o.step > 0 then v = math.floor(v / o.step + 0.5) * o.step end
	return v
end

local function formatValue(o, v)
	if string.find(o.format, "%%%%") then return string.format(o.format, v * 100) end
	return string.format(o.format, v)
end

Screens.options = {
	background = sunsetBackground,
	open = function() beginEditOptions(); optionsState.scroll = 0 end,
	draw = function()
		title(T("FRONT_END_OPTIONS_TITLE"))
		local pages = getOptionPages()
		local labels = {}
		for i, p in ipairs(pages) do labels[i] = p.title end
		local before = optionsState.page
		optionsState.page = tabs(labels, optionsState.page, X(90), Y(70), L(30))
		if optionsState.page ~= before then optionsState.scroll = 0 end
		local px, py, pw, ph = X(90), Y(100), L(1740), L(900)
		panel(px, py, pw, ph, true)

		local page = pages[optionsState.page]
		local options = page and getOptions(page.id) or {}
		local rowH = L(64)
		local rows = math.floor((ph - L(20)) / rowH)
		if hovered(px, py, pw, ph) and mouse.wheel ~= 0 then optionsState.scroll = optionsState.scroll - mouse.wheel end
		optionsState.scroll = math.max(0, math.min(optionsState.scroll, math.max(0, #options - rows)))
		local restart = false
		for r = 1, rows do
			local o = options[optionsState.scroll + r]
			if not o then break end
			local enabled = isOptionEnabled(o.id)
			local ry = py + L(20) + (r - 1) * rowH
			local labelColour = enabled and WHITE or GREY
			-- the label right-aligned before the centre, the control after it
			local lw = textWidth(FONT_BUTTON, o.label, S)
			drawText(FONT_BUTTON, X(900) - lw, ry + (rowH - fontHeight(FONT_BUTTON, S)) / 2 - L(6), o.label, labelColour, S)
			local cx = X(940)
			local v = getOption(o.id)
			if o.type == "toggle" then
				local text = (v ~= 0) and T("FRONT_END_BUTTON_ON") or T("FRONT_END_BUTTON_OFF")
				if button(text, cx, ry + L(6), L(172), L(38), enabled) then setOption(o.id, (v ~= 0) and 0 or 1) end
			elseif o.type == "choice" then
				local index = math.floor(v + 0.5)
				local text = o.choices[index + 1] or "?"
				if button(text, cx, ry + L(6), L(220), L(38), enabled) then setOption(o.id, (index + 1) % #o.choices) end
			else
				local p = sliderPosition(o, v)
				local np = slider(o.id, p, cx - L(6), ry - L(4), L(210), enabled)
				if np ~= p then setOption(o.id, sliderValue(o, np)) end
				drawText(FONT_LIST, cx + L(220), ry + L(22), formatValue(o, getOption(o.id)), enabled and YELLOW or GREY, S)
			end
			if o.needsRestart then
				drawText(FONT_LIST, cx + L(240), ry + L(22), "(after a restart)", GREY, S)
				restart = true
			end
		end
		if #options > rows then
			drawText(FONT_LIST, px + pw - L(260), py + ph - L(22), "Scroll for more settings", GREY, S)
		end
		if button(T("FRONT_END_BUTTON_OK"), X(735), Y(1022), L(210), L(26)) then
			saveOptions()
			pop()
		elseif button(T("FRONT_END_BUTTON_CANCEL"), X(975), Y(1022), L(210), L(26)) or isKeyPressed("Escape") then
			revertOptions()
			pop()
		end
	end,
}

-- In-game menu (Esc) -------------------------------------------------------------------

InGameMenuItems = {
	{ key = "IN_GAME_Return_To_Game", action = function() pop() end },
	-- single player only (multiplayer games aren't saved yet); "Save Game" asks for a name the first time
	{ key = "IN_GAME_Save", enabled = function() return isSinglePlayer() end, action = function()
		if getCurrentSave() ~= "" then request("save", getCurrentSave()) else push("save") end
	end },
	{ key = "IN_GAME_Save_As", enabled = function() return isSinglePlayer() end, action = function() push("save") end },
	{ key = "IN_GAME_Load", action = function() push("load") end },
	{ key = "IN_GAME_Restart_Level", action = function()
		confirm(T("RESTART_LEVEL_ARE_YOU_SURE"), function() request("restart") end)
	end },
	{ key = "IN_GAME_Options", action = function() push("options") end },
	{ key = "IN_GAME_Exit_To_Main_Menu", action = function()
		confirm(T("IN_GAME_Exit_To_Main_Menu") .. "?", function() request("exit_to_menu") end)
	end },
	{ key = "IN_GAME_Exit_Application", action = function()
		confirm(T("IN_GAME_Exit_Application") .. "?", function() request("quit") end)
	end },
}

Screens.ingame = {
	draw = function()
		drawRect(0, 0, W, H, DIM)
		local n = #InGameMenuItems
		local h = L(74.25) * (n - 1) + L(38) + L(150)
		local w = L(330)
		local x, y = X(960) - w / 2, (H - h) / 2
		goldFrame(x, y, w, h)
		if isSinglePlayer() then
			centredText(FONT_BUTTON, T("INGAME_GAME_PAUSED"), x, y + L(42), w, L(30), 0xFF9CE6F0)
		end
		buttonColumn(InGameMenuItems, y + L(90), y + h - L(60))
	end,
}

---------------------------------------------------------------------------- Entry points

-- Called by the program every frame; place is "menu" (main menu) or "game"
function FE_Frame(place)
	W, H = getWindowWidth(), getWindowHeight()
	S = H / 1080
	mouse.x, mouse.y = getMouseX(), getMouseY()
	-- SDL's button numbers: 1 is the left button
	mouse.down, mouse.pressed, mouse.released = isMouseDown(1), isMousePressed(1), isMouseReleased(1)
	mouse.wheel = getMouseWheel()
	mouse.blocked = isMouseOverOtherUi()

	if place == "menu" and #stack == 0 then push("main") end
	if #stack == 0 then return end

	local name = stack[#stack]
	local screen = Screens[name]
	if place == "game" then
		-- the screens opened from the in-game menu draw over the dimmed game
		if name ~= "ingame" then drawRect(0, 0, W, H, DIM) end
	elseif screen.background then
		screen.background()
	end
	-- while a dialog is up, the screen below gets no clicks
	local blocked = mouse.blocked
	if dialog then mouse.blocked = true end
	screen.draw()
	mouse.blocked = blocked
	if dialog then drawDialog() end
	if notice then
		if getTime() > notice.until_ then
			notice = nil
		else
			local nw, nh = L(440), L(90)
			local nx, ny = (W - nw) / 2, H - L(200)
			goldFrame(nx, ny, nw, nh)
			centredText(FONT_TEXT, notice.text, nx, ny + L(30), nw, L(30), 0xFF9CE6F0)
		end
	end

	-- Esc goes back (Options handles it itself, as Cancel)
	if isKeyPressed("Escape") and name ~= "options" and not openedThisFrame then
		if dialog then dialog = nil
		elseif name ~= "main" then pop() end
	end
	openedThisFrame = false
end

-- In game: the menu is showing
function FE_IsOpen()
	return #stack > 0
end

function FE_Open(name)
	-- "screen.tab" opens a screen at one of its tabs (tests)
	local screenName, tab = string.match(name, "^([^.]+)%.(.+)$")
	if screenName == "skirmish" then
		name = screenName
		skirmish.tab = (tab == "settings") and 2 or 1
	end
	if #stack == 0 or stack[#stack] ~= name then push(name) end
	openedThisFrame = true
end

function FE_Close()
	stack = {}
	dialog = nil
	activeDrag = nil
end
