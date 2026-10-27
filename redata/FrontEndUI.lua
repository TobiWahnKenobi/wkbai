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
local function slider(id, pos, x, y, w, enabled, height)
	local h = height or w / 3
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

-- A one-line text entry; state = { text = "" }. The field clicked last gets the typing (the
-- first one drawn until then). Typed characters in `refused` are ignored. Returns whether Enter
-- was pressed in it.
local focusedField = nil
local function textField(state, x, y, w, h, refused, maxLength)
	panel(x, y, w, h)
	if hovered(x, y, w, h) and mouse.pressed then focusedField = state end
	if focusedField == nil then focusedField = state end
	local focused = focusedField == state and not mouse.blocked
	if focused then
		for ch in string.gmatch(getTypedText(), "[%z\1-\127\194-\244][\128-\191]*") do
			if not (refused and string.find(refused, ch, 1, true)) and #state.text < (maxLength or 64) then
				state.text = state.text .. ch
			end
		end
		if isKeyPressed("Backspace") then
			state.text = string.gsub(state.text, "[%z\1-\127\194-\244][\128-\191]*$", "")
		end
	end
	local th = fontHeight(FONT_TEXT, S)
	pushClip(x, y, w, h)
	local tx, ty = x + L(8), y + (h - th) / 2
	-- the end of a long text stays in view
	local tw = textWidth(FONT_TEXT, state.text, S)
	if tw > w - L(20) then tx = x + w - L(12) - tw end
	drawText(FONT_TEXT, tx, ty, state.text, YELLOW, S)
	if focused and math.floor(getTime() * 2) % 2 == 0 then
		drawRect(tx + tw + L(1), ty + L(2), L(2), th - L(4), YELLOW)
	end
	popClip()
	return focused and (isKeyPressed("Return") or isKeyPressed("Keypad Enter"))
end

-- The lobby chat: the latest messages over an entry line; Enter sends
local chatInput = { text = "" }
local function chatPanel(x, y, w, h)
	panel(x, y, w, h)
	local lh = L(16)
	local inputH = L(30)
	local lines = {}
	for _, message in ipairs(getChat()) do
		for _, line in ipairs(wrapText(FONT_LIST, message, w - L(16))) do lines[#lines + 1] = line end
	end
	local rows = math.floor((h - inputH - L(12)) / lh)
	local first = math.max(1, #lines - rows + 1)
	for i = first, #lines do
		drawText(FONT_LIST, x + L(8), y + L(4) + (i - first) * lh, lines[i], 0xFFE6E6E6, S)
	end
	if textField(chatInput, x + L(4), y + h - inputH - L(4), w - L(8), inputH, nil, 120) and chatInput.text ~= "" then
		request("chat", chatInput.text)
		chatInput.text = ""
	end
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
	focusedField = nil
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
	{ key = "FRONT_END_MAIN_MENU_CAMPAIGN", action = function() push("campaign") end },
	{ key = "FRONT_END_MAIN_MENU_SKIRMISH", action = function() push("skirmish") end },
	{ key = "FRONT_END_MAIN_MENU_MULTIPLAYER", action = function() push("multiplayer") end },
	{ key = "FRONT_END_MAIN_MENU_TUTORIAL", action = function() push("tutorial") end },
	{ key = "FRONT_END_MAIN_MENU_OPTIONS", action = function() push("options") end },
	{ text = "Mods", action = function() request("mods") end },
	{ key = "FRONT_END_OPTIONS_GENERAL_CREATION", action = function() push("generaleditor") end },
	{ key = "FRONT_END_MAIN_MENU_CREDITS", action = function() push("credits") end },
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

-- Tutorial -----------------------------------------------------------------------------
-- As the original's IDD_ADDON_TUTORIAL: the interactive tutorial and the four lessons, each a level

local TutorialItems = {
	{ key = "FRONT_END_TUTORIAL_INTERACTIVE", action = function() request("start", "Interactive Tutorial.lvl") end },
	{ key = "FRONT_END_TUTORIAL_1", action = function() request("start", "Basic Economy.lvl") end },
	{ key = "FRONT_END_TUTORIAL_2", action = function() request("start", "Advanced Economy.lvl") end },
	{ key = "FRONT_END_TUTORIAL_3", action = function() request("start", "Basic Military.lvl") end },
	{ key = "FRONT_END_TUTORIAL_4", action = function() request("start", "Advanced Military.lvl") end },
}

Screens.tutorial = {
	background = sunsetBackground,
	draw = function()
		title(T("FRONT_END_TUTORIAL_TITLE"))
		-- the original's places (of 640 x 480): the buttons from 130 to 290, Back at 370
		buttonColumn(TutorialItems, Y(292), Y(652))
		if button(T("FRONT_END_BUTTON_BACK"), X(960 - 101.5), Y(832), L(203), L(38)) then pop() end
	end,
}

-- Credits ------------------------------------------------------------------------------
-- The original's credits (built in its exe: the role titles from Language.txt, the names in the
-- code), scrolling up as there, then wkbre2's. Entries: { "heading" | "role" | "name" | "note", text or key }
local function C(kind, text) return { kind, text } end
local CREDITS = {
	C("heading", "FRONT_END_CREDITS_BLACK_CACTUS"),
	C("role", "FRONT_END_CREDITS_CREATIVE_DIRECTOR"), C("name", "Jamie Thomson"),
	C("role", "FRONT_END_CREDITS_PROJECT_MANAGER_DESIGNER"), C("name", "Steve Bristow"),
	C("role", "FRONT_END_CREDITS_LEAD_DESIGNER"), C("name", "Charlie Bewsher"),
	C("role", "FRONT_END_CREDITS_CAMPAIGN_VALHALLA_DESIGNER"), C("name", "Andy Trowers"),
	C("role", "FRONT_END_CREDITS_AI_DESIGN"), C("name", "Mike West"),
	C("role", "FRONT_END_CREDITS_ADDITIONAL_DESIGN"), C("name", "Nick Ricks"),
	C("role", "FRONT_END_CREDITS_LEAD_ARTIST"), C("name", "Mark Tatham"),
	C("role", "FRONT_END_CREDITS_SENIOR_ARTIST"), C("name", "Chi Chan"),
	C("role", "FRONT_END_CREDITS_SENIOR_ARTIST"), C("name", "Julia Wren"),
	C("role", "FRONT_END_CREDITS_ARTIST"), C("name", "Nick Thomas"),
	C("role", "FRONT_END_CREDITS_LEAD_PROGRAMMER"), C("name", "Paul Carroll"),
	C("role", "FRONT_END_CREDITS_SENIOR_PROGRAMMER"), C("name", "Alex Dowdeswell"),
	C("role", "FRONT_END_CREDITS_ENGINE_PROGRAMMER"), C("name", "Matt Curran"),
	C("role", "FRONT_END_CREDITS_SENIOR_PROGRAMMER_HARDWARE"), C("name", "Pete Howarth"),
	C("role", "FRONT_END_CREDITS_SENIOR_PROGRAMMER"), C("name", "Scott Davis"),
	C("role", "FRONT_END_CREDITS_SENIOR_PROGRAMMER"), C("name", "Jim Blackler"),
	C("role", "FRONT_END_CREDITS_PROGRAMMER"), C("name", "Bob Kang"),
	C("role", "FRONT_END_CREDITS_INTERNAL_QA"), C("name", "George Addo"),
	C("role", "FRONT_END_CREDITS_MARKETING_AND_PROMOTIONS"), C("name", "Dawn Pinkney"),
	C("role", "FRONT_END_CREDITS_COMMERCIAL_DIRECTOR"), C("name", "Ian Turnbull"),
	C("role", "FRONT_END_CREDITS_MANAGING_DIRECTOR"), C("name", "David Bailey"),
	C("heading", "FRONT_END_CREDITS_EMPIRE"),
	C("role", "FRONT_END_CREDITS_PRODUCER"), C("name", "Iain Hancock"),
	C("role", "FRONT_END_CREDITS_QA_MANAGER"), C("name", "Dave Cleaveley"),
	C("role", "FRONT_END_CREDITS_LEAD_TESTER"), C("name", "Phil Octave"),
	C("role", "FRONT_END_CREDITS_ASSISTANT_LEAD"), C("name", "Richard Naughten"), C("name", "Chris Matlub"),
	C("role", "FRONT_END_CREDITS_QA"), C("name", "Gareth West"), C("name", "Mark Jones"), C("name", "Ryan Kalis"),
	C("name", "Darren T Bennett"), C("name", "Vernon Richards"), C("name", "Olivier Banal"),
	C("role", "FRONT_END_CREDITS_LOCALISATION_MANAGER"), C("name", "Jayshree Mistry"),
	C("role", "FRONT_END_CREDITS_INSTALLER_PROGRAMMING"), C("name", "Dan Clarke"),
	C("role", "FRONT_END_CREDITS_ADDITIONAL_ARTWORK"), C("name", "Ben Willsher"), C("name", "Steve Packer"), C("name", "Dave Swan"),
	C("role", "FRONT_END_CREDITS_MUSIC_SPEECH_SFX"), C("name", "Earcom Ltd., Paul Weir"),
	C("note", "This product contains software technology licensed from GameSpy Industries, Inc. (c) 1999-2003 GameSpy Industries, Inc. GameSpy and the \"Powered by GameSpy\" design are trademarks of GameSpy Industries, Inc. All rights reserved."),
	C("heading", "wkbre2"),
	C("role", "Engine reimplementation"), C("name", "AdrienTD"),
	C("role", "Modernization fork (with AI and reverse engineering)"), C("name", "TobiWahnKenobi"),
}

local creditsState = { start = 0 }
Screens.credits = {
	background = sunsetBackground,
	open = function() creditsState.start = getTime() end,
	draw = function()
		title(T("FRONT_END_CREDITS_TITLE"))
		local top, bottom = Y(80), Y(1010)
		-- a dark column behind the text, readable over the bright sky
		drawRect(X(560), top, L(800), bottom - top, 0xA0000000)
		drawFrame(X(560), top, L(800), bottom - top, PANEL_BORDER, L(2))
		pushClip(0, top, W, bottom - top)
		-- from below the window upwards, 50 pixels (of 1080) a second, then again
		local speed = L(50)
		local y = bottom - (getTime() - creditsState.start) * speed
		local function line(font, text, colour, gap)
			for _, l in ipairs(wrapText(font, text, L(1200))) do
				local h = fontHeight(font, S)
				if y + h > top and y < bottom then centredText(font, l, 0, y, W, h, colour) end
				y = y + h
			end
			y = y + (gap or 0)
		end
		for i, entry in ipairs(CREDITS) do
			local kind, text = entry[1], entry[2]
			local resolved = string.match(text, "^FRONT_END_") and T(text) or text
			if kind == "heading" then
				if i > 1 then y = y + L(60) end
				line(FONT_TITLE, resolved, 0xFF9CE6F0, L(30))
			elseif kind == "role" then
				y = y + L(18)
				line(FONT_LIST, resolved, YELLOW, L(4))
			elseif kind == "name" then
				line(FONT_TEXT, resolved, WHITE)
			else
				y = y + L(60)
				line(FONT_LIST, resolved, GREY)
			end
		end
		popClip()
		if y < top then creditsState.start = getTime() end
		if button(T("FRONT_END_BUTTON_BACK"), X(869), Y(1032), L(182), L(24)) then pop() end
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

-- Multiplayer host: the connected guests by lobby id ({ id, name })
local function guestById(id)
	for _, g in ipairs(getLobbyGuests()) do if g.id == id then return g end end
	return nil
end

local function slotText(slot)
	if slot.type == "human" then
		return skirmish.multiplayer and getPlayerName() or T("FRONT_END_CHAT_PLAYER_NAME"), 0xFFFFC070
	end
	if slot.type == "guest" then
		local g = guestById(slot.guest)
		return g and g.name or "-", g and 0xFF80FF80 or GREY
	end
	if slot.type == "empty" then return T("FRONT_END_CHAT_AI_PLAYER"), GREY end
	local g = slot.general and generalsByBlueprint and generalsByBlueprint[slot.general]
	return g and g.name or T("FRONT_END_CHAT_RANDOM_GENERAL"), YELLOW
end

-- Sent before the start: "H;c1;t0|A:<general>;c2;t0|A;c3;t0|E"
local function slotsRequest()
	local parts = {}
	for i, slot in ipairs(skirmish.slots) do
		if slot.type == "human" then parts[i] = "H"
		elseif slot.type == "guest" then parts[i] = "G" .. slot.guest
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
	local bioW = skirmish.multiplayer and L(930) or L(1882)
	panel(X(20), Y(288), bioW, L(712))
	if skirmish.multiplayer then
		-- the guests: a click puts one at the selected position
		panel(X(960), Y(288), L(942), L(200))
		drawText(FONT_LIST, X(968), Y(294), T("FRONT_END_CHAT_PLAYER") .. " (" .. getPlayerName() .. ", host)", WHITE, S)
		local guests = getLobbyGuests()
		local rows = {}
		for i, g in ipairs(guests) do
			local at = nil
			for si, slot in ipairs(slots) do if slot.type == "guest" and slot.guest == g.id then at = si end end
			local state = at and (skirmish.ready[g.id] and "  ready" or "  not ready") or ""
			rows[i] = { g.name .. (at and ("  (" .. at .. ")") or "") .. state, at and 0xFF80FF80 or YELLOW }
		end
		skirmish.guestList = skirmish.guestList or { selected = 0, scroll = 0 }
		skirmish.guestList.selected = 0
		-- "Remove" at the end of the row under the mouse disconnects that guest
		local listX, listY, listW = X(962), Y(312), L(938)
		local removeX = listX + listW - L(100)
		local removing = nil
		for r = 1, #guests - skirmish.guestList.scroll do
			local ry = listY + L(4) + (r - 1) * L(16)
			if ry + L(16) > listY + L(174) then break end
			if hovered(listX, ry, listW, L(16)) then
				local hot = hovered(removeX, ry, L(100), L(16))
				drawText(FONT_LIST, removeX + L(10), ry, "Remove", hot and 0xFF6060FF or GREY, S)
				if hot and mouse.pressed then removing = guests[skirmish.guestList.scroll + r] end
			end
		end
		local _, row = listBox(skirmish.guestList, rows, listX, listY, listW, L(174))
		if removing then
			request("kick_guest", tostring(removing.id))
			skirmish.guestList.selected = 0
		end
		if skirmish.guestList.selected > 0 and sel then
			local g = guests[skirmish.guestList.selected]
			for _, other in ipairs(slots) do
				if other.type == "guest" and other.guest == g.id then other.type = "empty"; other.guest = nil end
			end
			slots[skirmish.slotList.selected] = { type = "guest", guest = g.id, colour = sel.colour, team = sel.team }
		end
		if #guests == 0 then
			drawText(FONT_LIST, X(970), Y(316), "Waiting for players to join ...", GREY, S)
		end
		chatPanel(X(960), Y(496), L(942), L(504))
	end
	if shown then
		if shown.portrait ~= "" then drawImage(shown.portrait, X(30), Y(298), L(150), L(200)) end
		drawText(FONT_LIST, X(192), Y(300), shown.name, YELLOW, S)
		local y = Y(320)
		for _, line in ipairs(wrapText(FONT_LIST, shown.biography or "", bioW - L(192))) do
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

-- "2 Player 4 Peaks.lvl" -> "4 Peaks (2)"
local function levelTitle(level)
	local players, name = string.match(stripExtension(level or ""), "^(%d+) Player (.+)$")
	return players and (name .. " (" .. players .. ")") or stripExtension(level or "")
end

local function openLobby(multiplayer)
	skirmish.multiplayer = multiplayer
	local levels = levelList()
	local level = levels[levelState.selected]
	if level then request("select_level", level) end
	if level ~= skirmish.level then resetSlots(level) end
	-- guests from an earlier lobby are gone
	for _, slot in ipairs(skirmish.slots) do
		if slot.type == "guest" then slot.type = "empty"; slot.guest = nil end
	end
end

-- Multiplayer host: what the guests asked for (NETSRVMSG_LOBBY_REQUEST): "slot <n>" takes a
-- position no player has, "colour <c>" and "team <t>" (0: none) change the guest's position,
-- "ready <0|1>"
skirmish.ready = {}
local function handleGuestRequests()
	for _, g in ipairs(getLobbyGuests()) do
		for _, r in ipairs(g.requests or {}) do
			local what, value = string.match(r, "^(%a+) (%d+)$")
			value = tonumber(value)
			if what == "slot" then
				local target = skirmish.slots[value]
				if target and (target.type == "ai" or target.type == "empty") then
					for _, other in ipairs(skirmish.slots) do
						if other.type == "guest" and other.guest == g.id then other.type = "empty"; other.guest = nil end
					end
					skirmish.slots[value] = { type = "guest", guest = g.id, colour = target.colour, team = target.team }
					skirmish.ready[g.id] = false
				end
			elseif what == "colour" and value >= 1 and value <= 8 then
				for _, slot in ipairs(skirmish.slots) do
					if slot.type == "guest" and slot.guest == g.id then slot.colour = value end
				end
			elseif what == "team" and value >= 0 and value <= 4 then
				for _, slot in ipairs(skirmish.slots) do
					if slot.type == "guest" and slot.guest == g.id then slot.team = value end
				end
			elseif what == "ready" then
				skirmish.ready[g.id] = value ~= 0
			end
		end
	end
end

-- Multiplayer host: the lobby as text for the guests ("level\nH|name|colour|team|ready\n...")
local lastLobbyText = nil
local function sendLobbyState()
	local lines = { levelTitle(skirmish.level) }
	for _, slot in ipairs(skirmish.slots) do
		local text = slotText(slot)
		local kind = ({ human = "H", guest = "G", ai = "A", empty = "E" })[slot.type] or "E"
		local ready = (slot.type == "guest" and skirmish.ready[slot.guest]) and 1 or 0
		lines[#lines + 1] = kind .. "|" .. string.gsub(text, "[|\n]", " ") .. "|" .. (slot.colour or 0) .. "|" .. (slot.team or 0) .. "|" .. ready
	end
	local text = table.concat(lines, "\n")
	if text ~= lastLobbyText then
		lastLobbyText = text
		request("lobby_state", text)
	end
end

local function drawLobby(multiplayer)
	if multiplayer then handleGuestRequests() end
	title(T(multiplayer and "FRONT_END_MAIN_MENU_MULTIPLAYER" or "FRONT_END_LOBBY_TITLE_SKIRMISH"))
	skirmish.tab = tabs({ T("FRONT_END_LOBBY_CHAT"), T("FRONT_END_LOBBY_SETTINGS") }, skirmish.tab, X(8), Y(50), L(30))
	panel(X(8), Y(80), L(1907), L(988), true)
	if skirmish.tab == 1 then playersTab() else settingsTab() end
	local humans = 0
	local canStart = skirmish.level ~= nil
	for _, slot in ipairs(skirmish.slots) do
		if slot.type == "human" then humans = humans + 1 end
		-- a guest who left can't play, and every guest has to be ready
		if slot.type == "guest" and not (guestById(slot.guest) and skirmish.ready[slot.guest]) then canStart = false end
	end
	canStart = canStart and humans == 1
	if multiplayer then
		-- every guest needs a position
		for _, g in ipairs(getLobbyGuests()) do
			local placed = false
			for _, slot in ipairs(skirmish.slots) do if slot.type == "guest" and slot.guest == g.id then placed = true end end
			if not placed then canStart = false end
		end
		sendLobbyState()
	end
	if button(T("FRONT_END_CHAT_START"), X(770), Y(1032), L(182), L(24), canStart) then
		settingsRequests()
		slotsRequest()
		request(multiplayer and "start_multiplayer" or "start", skirmish.level)
	end
	if button(T("FRONT_END_BUTTON_BACK"), X(970), Y(1032), L(182), L(24)) then
		Screens[multiplayer and "mphost" or "skirmish"].onEscape()
	end
end

Screens.skirmish = {
	background = sunsetBackground,
	open = function() openLobby(false) end,
	draw = function() drawLobby(false) end,
	onEscape = function() pop() end,
}

-- Multiplayer host: the skirmish lobby with the guests who joined and the chat
Screens.mphost = {
	background = sunsetBackground,
	open = function() openLobby(true); lastLobbyText = nil end,
	draw = function() drawLobby(true) end,
	onEscape = function() request("stop_hosting"); pop() end,
}

-- Multiplayer guest: the host's lobby and the chat, until the host starts; the guest can take a
-- free position, change its colour and say it is ready
local mpGuestReady = false
Screens.mpguest = {
	background = sunsetBackground,
	draw = function()
		title(T("FRONT_END_MAIN_MENU_MULTIPLAYER"))
		panel(X(8), Y(80), L(1907), L(988), true)
		local status = getLobbyStatus()
		local statusText = (status == "connected") and ("Connected as " .. getPlayerName())
			or (status == "connecting") and ("Connecting to " .. getJoinAddress() .. " ...")
			or ("No connection to " .. getJoinAddress())
		drawText(FONT_TEXT, X(28), Y(92), statusText, status == "lost" and 0xFF6060FF or WHITE, S)
		local state = getLobbyState()
		local lines = {}
		for line in string.gmatch(state .. "\n", "(.-)\n") do lines[#lines + 1] = line end
		panel(X(20), Y(130), L(930), L(870))
		if state ~= "" then
			drawText(FONT_TEXT, X(32), Y(140), lines[1] or "", YELLOW, S)
			drawText(FONT_LIST, X(32), Y(180), "#  " .. T("FRONT_END_CHAT_PLAYER"), WHITE, S)
			drawText(FONT_LIST, X(640), Y(180), T("FRONT_END_CHAT_COLOUR"), WHITE, S)
			drawText(FONT_LIST, X(760), Y(180), T("FRONT_END_CHAT_TEAM"), WHITE, S)
			-- a free position (an AI or nobody) is taken by a click; the guest's own colour and team
			-- change by a click on them (the host's lobby does it)
			local me = getPlayerName()
			for i = 2, #lines do
				local kind, name, colour, team, ready = string.match(lines[i], "^(%a)|(.-)|(%d+)|(%d+)|?(%d*)$")
				if kind then
					local ry = Y(200) + (i - 2) * L(22)
					local mine = kind == "G" and name == me
					local free = kind == "A" or kind == "E"
					if mine then mpGuestReady = ready == "1" end
					if free and hovered(X(28), ry, L(600), L(22)) then
						drawImage(ART.listRollover, X(28), ry, L(600), L(20))
						if mouse.released then request("lobby_request", "slot " .. (i - 1)) end
					end
					local c = mine and 0xFFFFC070 or (kind == "H" or kind == "G") and 0xFF80FF80 or (kind == "E" and GREY or YELLOW)
					drawText(FONT_LIST, X(32), ry, (i - 1) .. "  " .. name, c, S)
					if kind ~= "E" then
						drawImage(colourSwatch(tonumber(colour)), X(644), ry + L(1), L(36), L(14))
						if mine and hovered(X(640), ry, L(44), L(16)) and mouse.released then
							request("lobby_request", "colour " .. ((tonumber(colour) % 8) + 1))
						end
						if mine and hovered(X(756), ry, L(48), L(16)) then
							drawImage(ART.listRollover, X(756), ry, L(48), L(18))
							if mouse.released then request("lobby_request", "team " .. ((tonumber(team) + 1) % 5)) end
						end
						drawText(FONT_LIST, X(770), ry, tonumber(team) > 0 and team or "-", WHITE, S)
					end
					if kind == "G" then
						drawText(FONT_LIST, X(830), ry, ready == "1" and "ready" or "", 0xFF80FF80, S)
					end
				end
			end
			drawText(FONT_LIST, X(32), Y(980), "Click a free position to take it, your colour or team to change it.", GREY, S)
		elseif status == "connected" then
			drawText(FONT_LIST, X(32), Y(140), "Waiting for the host ...", GREY, S)
		end
		chatPanel(X(960), Y(130), L(942), L(870))
		-- Ready: the host starts once every guest is
		if button(mpGuestReady and "Not ready" or "Ready", X(669), Y(1032), L(182), L(24), status == "connected" and state ~= "") then
			request("lobby_request", "ready " .. (mpGuestReady and 0 or 1))
		end
		if button(T("FRONT_END_BUTTON_BACK"), X(869), Y(1032), L(182), L(24)) then Screens.mpguest.onEscape() end
	end,
	onEscape = function() request("leave_lobby") end,
}

-- Kingmaker campaign -------------------------------------------------------------------
-- The provinces of the original's campaign map (its table at 0x8c0268 in WKB.exe): the region
-- image's place on Orbis_Map.tga (1600 x 1236; the exe gives them x 1.875), the place of the
-- marker, and the neighbouring provinces (indices here).
local PROVINCES = {
	{ key = "Akaia", level = "CH2_Akaia.lvl", x = 1011.7, y = 902.4, mx = 1145.6, my = 958.9, neighbours = { 19, 20 } },
	{ key = "Amboise", level = "CH2_Amboise.lvl", x = 619.7, y = 517.9, mx = 684.3, my = 633.6, neighbours = { 17, 5, 8, 21, 12 } },
	{ key = "Andalus", level = "CH2_Andalus.lvl", x = 125.9, y = 882.7, mx = 164.8, my = 1002.1, neighbours = { 11 } },
	{ key = "Angland", level = "CH2_Angland.lvl", x = 243.7, y = 255.5, mx = 337.6, my = 404.8, neighbours = { 5 } },
	{ key = "Aquilos", level = "CH2_Aquilos.lvl", x = 369.1, y = 475.7, mx = 560.0, my = 619.7, neighbours = { 16, 4, 17, 2 } },
	{ key = "Bohemundia", level = "CH2_Bohemundia.lvl", x = 1072.5, y = 392.5, mx = 1188.3, my = 580.3, neighbours = { 14, 18, 22, 15, 9 } },
	{ key = "Burgosia", level = "CH2_Burgosia.lvl", x = 392.0, y = 946.1, mx = 490.1, my = 969.6, neighbours = { 10, 11, 20 } },
	{ key = "Cravant", level = "CH2_Cravant.lvl", x = 716.3, y = 467.7, mx = 857.1, my = 526.4, neighbours = { 2, 21 } },
	{ key = "Dacianus", level = "CH2_Dacianus.lvl", x = 926.4, y = 653.3, mx = 1110.9, my = 737.6, neighbours = { 14, 6, 15, 19 } },
	{ key = "Etruscia", level = "CH2_Etruscia.lvl", x = 747.7, y = 883.7, mx = 814.4, my = 929.6, neighbours = { 7, 13, 20 } },
	{ key = "Ibericus", level = "CH2_Ibericus.lvl", x = 161.6, y = 567.5, mx = 289.1, my = 759.5, neighbours = { 3, 7, 16 } },
	{ key = "Illyricus", level = "CH2_Illyricus.lvl", x = 734.9, y = 630.9, mx = 772.8, my = 725.9, neighbours = { 2, 21, 14, 19 } },
	{ key = "Liguriensis", level = "CH2_Liguriensis.lvl", x = 604.3, y = 810.7, mx = 676.8, my = 930.7, neighbours = { 17, 10 } },
	{ key = "Losatia", level = "CH2_Losatia.lvl", x = 910.4, y = 402.7, mx = 1082.7, my = 584.5, neighbours = { 21, 6, 9, 12 } },
	{ key = "Magiaria", level = "CH2_Magiaria.lvl", x = 1112.0, y = 694.9, mx = 1249.1, my = 733.9, neighbours = { 19, 9, 6, 22, 18 } },
	{ key = "Occitania", level = "CH2_Occitania.lvl", x = 401.6, y = 698.1, mx = 489.6, my = 747.7, neighbours = { 11, 17, 5 } },
	{ key = "Ostrovir", level = "CH2_Ostrovir.lvl", x = 586.1, y = 645.3, mx = 626.1, my = 714.1, neighbours = { 16, 5, 2, 13 } },
	{ key = "Rusland", level = "CH2_Rusland.lvl", x = 1220.3, y = 227.2, mx = 1393.6, my = 459.7, neighbours = { 6, 15, 22 } },
	{ key = "Ryetia", level = "CH2_Ryetia.lvl", x = 920.5, y = 759.5, mx = 1039.5, my = 779.2, neighbours = { 12, 9, 15, 1 } },
	{ key = "Samnos", level = "CH2_Samnos.lvl", x = 884.8, y = 917.9, mx = 924.8, my = 1027.2, neighbours = { 1, 7, 10 } },
	{ key = "Tharingia", level = "CH2_Tharingia.lvl", x = 800.0, y = 258.1, mx = 973.9, my = 498.1, neighbours = { 2, 8, 14, 12 } },
	{ key = "Valakia", level = "CH2_Valakia.lvl", x = 1270.4, y = 527.5, mx = 1329.6, my = 601.6, neighbours = { 6, 18, 15 } },
}

-- Campaigns\Kingmaker.cam: the first province's level and the AI Generals of each level (tiers)
local campaignDef = nil
local function campaignDefinition()
	if campaignDef then return campaignDef end
	campaignDef = { first = "", levels = {} }
	local current = nil
	for line in string.gmatch((readDataFile("Campaigns\\Kingmaker.cam") or "") .. "\n", "(.-)\n") do
		local word, rest = string.match(line, "^%s*(%S+)%s*(.-)%s*$")
		if word == "FIRST_LEVEL" then
			campaignDef.first = string.lower(string.match(rest, '"(.-)"') or "")
		elseif word == "LEVEL" then
			current = { tiers = {} }
			campaignDef.levels[string.lower(string.match(rest, '"(.-)"') or "")] = current
		elseif word == "CHOOSE_FROM" and current then
			current.tiers[#current.tiers + 1] = rest
		elseif word == "END_LEVEL" then
			current = nil
		end
	end
	return campaignDef
end

local campaign = { profile = nil, data = nil, selected = nil, list = { selected = 0, scroll = 0 }, name = { text = "" } }

-- The provinces that can be attacked: the first one, then the neighbours of the conquered ones
local function campaignState()
	local conquered, attackable = {}, {}
	for _, key in ipairs((campaign.data and campaign.data.conquered) or {}) do conquered[key] = true end
	local any = false
	for i, prov in ipairs(PROVINCES) do
		if conquered[prov.key] then
			any = true
			for _, n in ipairs(prov.neighbours) do
				if PROVINCES[n] and not conquered[PROVINCES[n].key] then attackable[PROVINCES[n].key] = true end
			end
		end
	end
	if not any then
		local first = campaignDefinition().first
		for _, prov in ipairs(PROVINCES) do
			if string.lower(prov.level) == first then attackable[prov.key] = true end
		end
	end
	return conquered, attackable
end

-- Starts a province: the player at the first position, the level's AI Generals (random ones of
-- their tiers) after, the other positions empty
local function startProvince(prov)
	local def = campaignDefinition().levels[string.lower(prov.level)] or { tiers = { "VETERAN" } }
	local parts = { "H;c1" }
	for i, tier in ipairs(def.tiers) do
		local pool = {}
		for _, g in ipairs(generals()) do if g.category == tier then pool[#pool + 1] = g end end
		local g = pool[math.random(math.max(1, #pool))]
		parts[#parts + 1] = (g and ("A:" .. g.blueprint) or "A") .. ";c" .. (i + 1)
	end
	for _ = 1, 8 do parts[#parts + 1] = "E" end
	request("slots", table.concat(parts, "|"))
	request("campaign_play", campaign.profile .. "|" .. prov.key)
	request("start", prov.level)
end

Screens.campaign = {
	background = sunsetBackground,
	open = function() campaign.list.selected = 0; campaign.name.text = "" end,
	draw = function()
		title(T("FRONT_END_CAMPAIGN_TITLE"))
		local profiles = getCampaignProfiles()
		local x, w = X(560), L(800)
		panel(x, Y(90), w, L(600))
		local activated = listBox(campaign.list, profiles, x + L(2), Y(92), w - L(4), L(596))
		local chosen = profiles[campaign.list.selected]
		drawText(FONT_TEXT, x, Y(710), T("FRONT_END_CAMPAIGN_ENTER_CAMPAIGN_NAME"), WHITE, S)
		local entered = textField(campaign.name, x, Y(740), w, L(32), FILE_NAME_REFUSED, 40)
		local name = string.match(campaign.name.text, "^%s*(.-)%s*$")
		if (button(T("FRONT_END_CAMPAIGN_NEW"), x, Y(800), L(250), L(30), name ~= "") or entered) and name ~= "" then
			if createCampaignProfile(name) then
				campaign.profile = name
				push("campaignmap")
			else
				dialog = { text = string.format(T("FRONT_END_CAMPAIGN_OVERWRITE_PROFILE"), name), buttons = {
					{ T("FRONT_END_DIALOG_BUTTON_YES"), function()
						dialog = nil
						deleteCampaignProfile(name)
						createCampaignProfile(name)
						campaign.profile = name
						push("campaignmap")
					end },
					{ T("FRONT_END_DIALOG_BUTTON_NO"), function() dialog = nil end },
				} }
			end
		end
		if (button(T("FRONT_END_CAMPAIGN_LOAD"), x + L(275), Y(800), L(250), L(30), chosen ~= nil) or activated) and chosen then
			campaign.profile = chosen
			push("campaignmap")
		end
		if button(T("DIALOG_BUTTON_DELETE"), x + L(550), Y(800), L(250), L(30), chosen ~= nil) then
			confirm(T("DIALOG_BUTTON_DELETE") .. " '" .. chosen .. "'?", function() deleteCampaignProfile(chosen) end)
		end
		if button(T("FRONT_END_BUTTON_BACK"), X(869), Y(1032), L(182), L(24)) then pop() end
	end,
}

local TIER_TEXT = { CONSCRIPT = "FRONT_END_CHAT_AI_CONSCRIPT", VETERAN = "FRONT_END_CHAT_AI_VETERAN", ELITE = "FRONT_END_CHAT_AI_ELITE" }

-- The campaign map: conquered provinces in the player's colour, the ones that can be attacked
-- marked; a click shows a province, Start attacks it
Screens.campaignmap = {
	background = seaBackground,
	open = function()
		campaign.data = campaign.profile and getCampaignProfile(campaign.profile) or nil
		campaign.selected = nil
	end,
	draw = function()
		if not campaign.data then pop(); return end
		local conquered, attackable = campaignState()
		-- the map, as large as it fits left of the panel
		local boxW, boxH = L(1300), H - Y(70) - L(20)
		local k = math.min(boxW / 1600, boxH / 1236)
		local mx, my = X(20) + (boxW - 1600 * k) / 2, Y(70)
		drawImage("Interface\\FrontEnd\\Orbis_Map.tga", mx, my, 1600 * k, 1236 * k)
		drawFrame(mx, my, 1600 * k, 1236 * k, PANEL_BORDER, L(2))
		-- the province under the mouse
		local hoveredProv = nil
		if hovered(mx, my, 1600 * k, 1236 * k) then
			local ux, uy = (mouse.x - mx) / k, (mouse.y - my) / k
			for _, prov in ipairs(PROVINCES) do
				local img = "Interface\\FrontEnd\\Regions\\" .. prov.key .. ".tga"
				if imageAlpha(img, math.floor(ux - prov.x), math.floor(uy - prov.y)) > 128 then hoveredProv = prov end
			end
		end
		if hoveredProv and mouse.released then campaign.selected = hoveredProv end
		for _, prov in ipairs(PROVINCES) do
			local img = "Interface\\FrontEnd\\Regions\\" .. prov.key .. ".tga"
			local w, h = imageSize(img)
			local colour = nil
			if conquered[prov.key] then colour = 0xFFFFFFFF
			elseif prov == campaign.selected then colour = 0xC0FFFFFF
			elseif prov == hoveredProv then colour = 0x70FFFFFF end
			if colour then drawImage(img, mx + prov.x * k, my + prov.y * k, w * k, h * k, colour) end
		end
		local pulse = 0.85 + 0.15 * math.sin(getTime() * 4)
		for _, prov in ipairs(PROVINCES) do
			if attackable[prov.key] then
				local size = L(56) * pulse
				drawImage("Interface\\FrontEnd\\Campaign_Target.tga", mx + prov.mx * k - size / 2, my + prov.my * k - size / 2, size, size * 0.89)
			elseif conquered[prov.key] then
				local size = L(40)
				drawImage("Interface\\FrontEnd\\Campaign_Location.tga", mx + prov.mx * k - size / 2, my + prov.my * k - size / 2, size, size)
			end
		end

		-- the panel: the campaign, then the province chosen
		local px, pw = X(1340), L(560)
		title(T("FRONT_END_CAMPAIGN_TITLE") .. ": " .. campaign.profile)
		panel(px, Y(70), pw, H - Y(70) - L(70), true)
		local items = campaign.data.items or {}
		local y = Y(84)
		local function line(font, text, colour)
			for _, l in ipairs(wrapText(font, text, pw - L(32))) do
				drawText(font, px + L(16), y, l, colour or WHITE, S)
				y = y + fontHeight(font, S)
			end
		end
		local count = 0
		for _ in pairs(conquered) do count = count + 1 end
		line(FONT_LIST, count .. " / " .. #PROVINCES, YELLOW)
		if next(items) then
			line(FONT_LIST, string.format("%s %d   %s %d   %s %d", T("FRONT_END_CAMPAIGN_STARTING_FOOD"), items["FOOD"] or items["Food"] or 0,
				T("FRONT_END_CAMPAIGN_STARTING_WOOD"), items["WOOD"] or items["Wood"] or 0, T("FRONT_END_CAMPAIGN_STARTING_GOLD"), items["GOLD"] or items["Gold"] or 0), 0xFFE6E6E6)
		end
		y = y + L(16)
		local prov = campaign.selected or hoveredProv
		if prov then
			line(FONT_BUTTON, T("FRONT_END_CAMPAIGN_" .. prov.key), 0xFF9CE6F0)
			y = y + L(6)
			if conquered[prov.key] then line(FONT_LIST, "Conquered", 0xFF80FF80) end
			local def = campaignDefinition().levels[string.lower(prov.level)]
			if def then
				local tiers = {}
				for _, t in ipairs(def.tiers) do tiers[#tiers + 1] = T(TIER_TEXT[t] or t) end
				line(FONT_LIST, #def.tiers .. " AI Generals: " .. table.concat(tiers, ", "), YELLOW)
			end
			y = y + L(10)
			line(FONT_LIST, T("FRONT_END_CAMPAIGN_CONQUER_THIS_PROVINCE"), WHITE)
			line(FONT_LIST, T("Province_Bonus_" .. prov.key), 0xFF80FF80)
			y = y + L(10)
			line(FONT_LIST, T("Province_" .. prov.key), 0xFFE6E6E6)
		end
		local canStart = campaign.selected ~= nil and attackable[campaign.selected.key] == true
		if button(T("FRONT_END_CAMPAIGN_START"), px + L(20), H - L(56), L(250), L(30), canStart) then
			startProvince(campaign.selected)
		end
		if button(T("FRONT_END_BUTTON_BACK"), px + pw - L(270), H - L(56), L(250), L(30)) then pop() end
	end,
}

-- AI General Editor --------------------------------------------------------------------
-- As the original's IDD_ADDON_GENERAL_CREATION (positions of its 640 x 480 dialog): a portrait,
-- the general's name and background, and the AI stats (0 ... 100) the Kingmaker AI reads for a
-- "player designed" general. Saved as a PLAYER "<name> Kingmaker" in the user folder's
-- Warrior Kings Game Set\AI Generals, which the game set links.

local GE_MILITARY = {
	{ "Economic - Military AI Stat", { "ECONOMIC", "MILITARY" } },
	{ "Raiding - Siege AI Stat", { "RAIDING", "SIEGE" } },
	{ "Pagan - Tech - Imperial AI Stat", { "PAGAN", "RENAISSANCE", "IMPERIAL" } },
	{ "Infantry - Cavalry AI Stat", { "INFANTRY", "CAVALRY" } },
	{ "Heavy - Light AI Stat", { "HEAVY", "LIGHT" } },
	{ "Leadership AI Stat", { "LEADERSHIP" } },
	{ "Fervour AI Stat", { "FERVOUR" } },
}
local GE_DIPLOMATIC = {
	{ "Toady - Neutral - Arrogant AI Stat", { "TOADY", "NEUTRAL", "ARROGANT" } },
	{ "Diplomatic Frequency AI Stat", { "DIPLOMATIC_FREQUENCY" } },
	{ "Greediness AI Stat", { "GREEDINESS" } },
	{ "Treachery AI Stat", { "TREACHERY" } },
	{ "Pride AI Stat", { "PRIDE" } },
	{ "Friendliness AI Stat", { "FRIENDLINESS" } },
	{ "Chattiness AI Stat", { "CHATTINESS" } },
}
local GE_CATEGORIES = { "CONSCRIPT", "VETERAN", "ELITE" }
local GE_ALIGNMENTS = { "Pagan", "Renaissance", "Imperial" }

local editor = { name = { text = "" }, background = { text = "" }, stats = {}, portrait = 1, category = 2, alignment = 2, editing = 0 }

local function editorPortraits()
	local list = {}
	for _, f in ipairs(listDataFiles("Interface\\FrontEnd\\Portraits")) do
		local lower = string.lower(f)
		if string.sub(lower, -4) == ".tga" and lower ~= "locked general.tga" and lower ~= "random general.tga" then
			list[#list + 1] = "Interface\\FrontEnd\\Portraits\\" .. f
		end
	end
	return list
end

local function customGenerals()
	local list = {}
	for _, g in ipairs(getGenerals()) do if g.custom then list[#list + 1] = g end end
	return list
end

-- A new general (all stats in the middle), or the custom general g
local function editorLoad(g)
	editor.stats = {}
	for _, st in ipairs(GE_MILITARY) do editor.stats[st[1]] = 50 end
	for _, st in ipairs(GE_DIPLOMATIC) do editor.stats[st[1]] = 50 end
	editor.name.text, editor.background.text = "", ""
	editor.portrait, editor.category, editor.alignment = 1, 2, 2
	if not g then return end
	editor.name.text = g.name
	editor.background.text = g.biography or ""
	for item, v in pairs(g.stats or {}) do
		for k in pairs(editor.stats) do if string.lower(k) == string.lower(item) then editor.stats[k] = v end end
	end
	for i, c in ipairs(GE_CATEGORIES) do if c == g.category then editor.category = i end end
	for i, a in ipairs(GE_ALIGNMENTS) do if a == g.alignment then editor.alignment = i end end
	for i, path in ipairs(editorPortraits()) do if string.lower(path) == string.lower(g.portrait or "") then editor.portrait = i end end
end

Screens.generaleditor = {
	background = sunsetBackground,
	open = function() editor.editing = 0; editorLoad(nil) end,
	draw = function()
		-- the original dialog's coordinates
		local function px(x) return X(960 + (x - 320) * 2.25) end
		local function py(y) return Y(y * 2.25) end
		local function pl(v) return L(v * 2.25) end
		title(T("FRONT_END_GENERAL_CREATION_TITLE"))
		panel(px(4), py(34), pl(632), pl(436), true)

		-- portrait
		local portraits = editorPortraits()
		editor.portrait = math.max(1, math.min(editor.portrait, #portraits))
		if portraits[editor.portrait] then drawImage(portraits[editor.portrait], px(10), py(90), pl(150), pl(200)) end
		drawFrame(px(10), py(90), pl(150), pl(200), PANEL_BORDER, L(2))
		if button("<", px(10), py(294), pl(40), pl(20), #portraits > 1) then editor.portrait = (editor.portrait - 2) % #portraits + 1 end
		if button(">", px(120), py(294), pl(40), pl(20), #portraits > 1) then editor.portrait = editor.portrait % #portraits + 1 end
		centredText(FONT_LIST, editor.portrait .. " / " .. #portraits, px(50), py(294), pl(70), pl(20), WHITE)

		-- name
		drawText(FONT_LIST, px(10), py(40), T("FRONT_END_GENERAL_CREATION_NAME"), WHITE, S)
		local nameField = textField(editor.name, px(10), py(58), pl(150), pl(24), FILE_NAME_REFUSED, 32)

		-- category, alignment, random
		if button(T(TIER_TEXT[GE_CATEGORIES[editor.category]] or GE_CATEGORIES[editor.category]), px(10), py(320), pl(150), pl(22)) then
			editor.category = editor.category % #GE_CATEGORIES + 1
		end
		if button(GE_ALIGNMENTS[editor.alignment], px(10), py(346), pl(150), pl(22)) then
			editor.alignment = editor.alignment % #GE_ALIGNMENTS + 1
		end
		if button(T("FRONT_END_GENERAL_CREATION_RANDOM"), px(10), py(372), pl(150), pl(22)) then
			for k in pairs(editor.stats) do editor.stats[k] = math.random(0, 100) end
			editor.portrait = math.random(math.max(1, #portraits))
		end

		-- the stats: a slider each, its labels under it
		local function column(list, x, heading)
			drawText(FONT_LIST, px(x), py(40), T("FRONT_END_GENERAL_CREATION_" .. heading), 0xFF9CE6F0, S)
			for i, st in ipairs(list) do
				local y = 58 + (i - 1) * 45
				local v = editor.stats[st[1]] or 50
				local pos = slider("ge" .. st[1], v / 100, px(x), py(y), pl(200), true, pl(20))
				editor.stats[st[1]] = math.floor(pos * 100 + 0.5)
				local labels = st[2]
				local lh = fontHeight(FONT_LIST, S)
				for li, key in ipairs(labels) do
					local text = T("FRONT_END_GENERAL_CREATION_" .. key)
					local w = textWidth(FONT_LIST, text, S)
					local tx = px(x)
					if #labels == 1 then tx = px(x) + (pl(200) - w) / 2
					elseif li == #labels then tx = px(x) + pl(200) - w
					elseif li > 1 then tx = px(x) + (pl(200) - w) / 2 end
					drawText(FONT_LIST, tx, py(y + 21), text, WHITE, S)
				end
				drawText(FONT_LIST, px(x + 205), py(y + 4), tostring(editor.stats[st[1]]), YELLOW, S)
			end
		end
		column(GE_MILITARY, 164, "MILITARY_BEHAVIOUR")
		column(GE_DIPLOMATIC, 399, "DIPLOMATIC_BEHAVIOUR")

		-- the generals made before: pick one to change it
		local custom = customGenerals()
		local current = custom[editor.editing]
		if button(current and current.name or T("FRONT_END_GENERAL_CREATION_NEW_GENERAL"), px(174), py(375), pl(200), pl(22), #custom > 0) then
			editor.editing = (editor.editing + 1) % (#custom + 1)
			editorLoad(custom[editor.editing])
		end
		if button(T("DIALOG_BUTTON_DELETE"), px(399), py(375), pl(95), pl(22), current ~= nil) then
			confirm(T("DIALOG_BUTTON_DELETE") .. " '" .. current.name .. "'?", function()
				deleteAIGeneral(current.name)
				generalsByBlueprint = nil
				editor.editing = 0
				editorLoad(nil)
			end)
		end
		if button(T("FRONT_END_GENERAL_CREATION_NEW_GENERAL"), px(504), py(375), pl(95), pl(22)) then
			editor.editing = 0
			editorLoad(nil)
		end

		-- background
		drawText(FONT_LIST, px(10), py(409), T("FRONT_END_GENERAL_CREATION_BACKGROUND"), WHITE, S)
		textField(editor.background, px(174), py(405), pl(425), pl(25), nil, 200)

		local testSave = editor.testSave
		if testSave then
			editor.testSave = nil
			editor.name.text = testSave
			for k in pairs(editor.stats) do editor.stats[k] = math.random(0, 100) end
		end
		local name = string.match(editor.name.text, "^%s*(.-)%s*$")
		if button(T("FRONT_END_GENERAL_CREATION_SAVE_GENERAL"), px(90), py(440), pl(220), pl(25), name ~= "") or testSave then
			local ok = saveAIGeneral({ name = name, category = GE_CATEGORIES[editor.category], alignment = GE_ALIGNMENTS[editor.alignment],
				portrait = portraits[editor.portrait] or "", background = editor.background.text, stats = editor.stats })
			generalsByBlueprint = nil
			notice = { text = ok and (name .. ": " .. T("FRONT_END_BUTTON_SAVE")) or ("Could not save \"" .. name .. "\""), until_ = getTime() + 2.5 }
			for i, g in ipairs(customGenerals()) do if g.name == name then editor.editing = i end end
		end
		if button(T("FRONT_END_BUTTON_BACK"), px(330), py(440), pl(220), pl(25)) then pop() end
	end,
}

-- Multiplayer --------------------------------------------------------------------------

-- Multiplayer host, a saved game: its human players, each taken by the host, a guest (the one of
-- the same name joins it by itself) or, when nobody takes it, an AI General
mpLoad = { save = nil, humans = {}, list = { selected = 1, scroll = 0 }, guestList = { selected = 0, scroll = 0 } }
local lastSavedLobbyText = nil
Screens.mpload = {
	background = sunsetBackground,
	open = function()
		mpLoad.humans = getSaveGameHumans(mpLoad.save or "")
		for _, h in ipairs(mpLoad.humans) do h.who = nil end
		mpLoad.list.selected = 1
		lastSavedLobbyText = nil
		request("select_level", mpLoad.save)
	end,
	draw = function()
		title(T("FRONT_END_MAIN_MENU_MULTIPLAYER") .. ": " .. stripExtension(mpLoad.save or ""))
		panel(X(8), Y(80), L(1907), L(988), true)
		local guests = getLobbyGuests()
		local humans = mpLoad.humans
		local function guestOf(who)
			if type(who) ~= "string" or string.sub(who, 1, 1) ~= "G" then return nil end
			return guestById(tonumber(string.sub(who, 2)))
		end
		-- who is still here, and the players' own names find their places
		local taken = {}
		for _, h in ipairs(humans) do
			if h.who and h.who ~= "AI" and h.who ~= "H" and not guestOf(h.who) then h.who = nil end
			if h.who then taken[h.who] = true end
		end
		for _, h in ipairs(humans) do
			if not h.who then
				if not taken["H"] and string.lower(h.name) == string.lower(getPlayerName()) then h.who = "H"; taken["H"] = true end
				for _, g in ipairs(guests) do
					if not h.who and not taken["G" .. g.id] and string.lower(g.name) == string.lower(h.name) then
						h.who = "G" .. g.id; taken[h.who] = true
					end
				end
			end
		end
		if not taken["H"] and humans[1] and not humans[1].who then humans[1].who = "H"; taken["H"] = true end

		-- the saved players and who takes them
		panel(X(20), Y(92), L(930), L(600))
		drawText(FONT_LIST, X(28), Y(98), "#  Saved player  ->  played by", WHITE, S)
		local rows = {}
		for i, h in ipairs(humans) do
			local by, c
			if h.who == "H" then by, c = getPlayerName() .. " (host)", 0xFF80FF80
			elseif guestOf(h.who) then by, c = guestOf(h.who).name, 0xFF80FF80
			else by, c = "AI General", YELLOW end
			rows[i] = { i .. "  " .. h.name .. "  ->  " .. by, c }
		end
		listBox(mpLoad.list, rows, X(22), Y(116), L(926), L(574))
		local sel = humans[mpLoad.list.selected]
		local function give(who)
			for _, h in ipairs(humans) do if h.who == who then h.who = "AI" end end
			sel.who = who
		end
		if button("Me", X(28), Y(700), L(200), L(28), sel ~= nil) then give("H") end
		if button("AI General", X(240), Y(700), L(200), L(28), sel ~= nil) then sel.who = "AI" end

		-- the guests: a click gives the selected player to one
		panel(X(960), Y(92), L(942), L(300))
		drawText(FONT_LIST, X(968), Y(98), T("FRONT_END_CHAT_PLAYER"), WHITE, S)
		local grows = {}
		for i, g in ipairs(guests) do
			local at = nil
			for hi, h in ipairs(humans) do if h.who == "G" .. g.id then at = hi end end
			grows[i] = { g.name .. (at and ("  (" .. at .. ")") or ""), at and 0xFF80FF80 or YELLOW }
		end
		mpLoad.guestList.selected = 0
		listBox(mpLoad.guestList, grows, X(962), Y(116), L(938), L(274))
		if mpLoad.guestList.selected > 0 and sel then give("G" .. guests[mpLoad.guestList.selected].id) end
		if #guests == 0 then drawText(FONT_LIST, X(970), Y(120), "Waiting for players to join ...", GREY, S) end
		chatPanel(X(960), Y(400), L(942), L(600))

		-- the guests see this lobby
		local lines = { stripExtension(mpLoad.save or "") }
		for i, h in ipairs(humans) do
			local kind, name = "A", h.name .. " (AI General)"
			if h.who == "H" then kind, name = "H", h.name .. " - " .. getPlayerName()
			elseif guestOf(h.who) then kind, name = "G", h.name .. " - " .. guestOf(h.who).name end
			lines[#lines + 1] = kind .. "|" .. string.gsub(name, "[|\n]", " ") .. "|" .. (h.colour or 0) .. "|0"
		end
		local text = table.concat(lines, "\n")
		if text ~= lastSavedLobbyText then lastSavedLobbyText = text; request("lobby_state", text) end

		-- the host plays one of them, and every guest has one
		local canStart = taken["H"] == true
		for _, g in ipairs(guests) do if not taken["G" .. g.id] then canStart = false end end
		local auto = mpLoad.autoStart and canStart and ((#guests > 0 and getTime() - mpLoad.autoStart > 4) or getTime() - mpLoad.autoStart > 20)
		if button(T("FRONT_END_CHAT_START"), X(770), Y(1032), L(182), L(24), canStart) or auto then
			mpLoad.autoStart = nil
			local parts = {}
			for _, h in ipairs(humans) do parts[#parts + 1] = h.id .. "=" .. (h.who or "AI") end
			request("mp_saved_players", table.concat(parts, "|"))
			request("start_multiplayer", mpLoad.save)
		end
		if button(T("FRONT_END_BUTTON_BACK"), X(970), Y(1032), L(182), L(24)) then Screens.mpload.onEscape() end
	end,
	onEscape = function() request("stop_hosting"); pop() end,
}

local mpName, mpAddress = { text = "" }, { text = "" }
local mpState = { tab = 1, saves = { selected = 0, scroll = 0 } }
Screens.multiplayer = {
	background = sunsetBackground,
	open = function()
		mpName.text = getPlayerName()
		mpAddress.text = getJoinAddress()
		focusedField = mpName
	end,
	draw = function()
		title(T("FRONT_END_MAIN_MENU_MULTIPLAYER"))
		-- a new game on a map, or a saved game to go on with
		mpState.tab = tabs({ T("FRONT_END_LOAD_GAME_SKIRMISH"), T("FRONT_END_LOAD_GAME_MULTIPLAYER") }, mpState.tab, X(20), Y(50), L(30))
		local chosen
		if mpState.tab == 1 then
			chosen = levelPicker(X(20), Y(80), L(1880), L(880))
		else
			local saves = savesOfType(".sav")
			local names = {}
			for i, sv in ipairs(saves) do names[i] = stripExtension(sv) end
			panel(X(20), Y(80), L(1880), L(880))
			listBox(mpState.saves, names, X(22), Y(82), L(1876), L(876))
			chosen = saves[mpState.saves.selected]
		end
		-- the player name (both), the host's address (joining: "address" or "address:port")
		drawText(FONT_LIST, X(24), Y(972), "Name", WHITE, S)
		textField(mpName, X(80), Y(966), L(360), L(28), "|", 24)
		drawText(FONT_LIST, X(470), Y(972), "Host address", WHITE, S)
		local enterJoin = textField(mpAddress, X(580), Y(966), L(420), L(28), " ", 64)
		local name = string.match(mpName.text, "^%s*(.-)%s*$")
		local function remember()
			request("player_name", name)
			request("join_address", mpAddress.text)
		end
		if button(T("FRONT_END_MULTIPLAYER_BUTTON_CREATE"), X(1468), Y(1052), L(210), L(24), chosen ~= nil and name ~= "") then
			remember()
			request("host")
			if mpState.tab == 1 then
				push("mphost")
			else
				mpLoad.save = chosen
				push("mpload")
			end
		end
		if (button(T("FRONT_END_MULTIPLAYER_BUTTON_JOIN_IP"), X(1250), Y(1052), L(210), L(24), name ~= "" and mpAddress.text ~= "") or enterJoin)
			and name ~= "" and mpAddress.text ~= "" then
			remember()
			request("join", mpAddress.text)
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
				-- a swatch of the fog colour after its last channel (colours are 0xAABBGGRR)
				if o.id == "fogColourB" and enabled then
					local function channel(id) return math.floor(math.max(0, math.min(1, getOption(id))) * 255 + 0.5) end
					local colour = 0xFF000000 + channel("fogColourB") * 0x10000 + channel("fogColourG") * 0x100 + channel("fogColourR")
					drawRect(cx + L(300), ry + L(8), L(60), L(36), PANEL_BORDER)
					drawRect(cx + L(302), ry + L(10), L(56), L(32), colour)
				end
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
	-- single player and the multiplayer host; "Save Game" asks for a name the first time
	{ key = "IN_GAME_Save", enabled = function() return canSaveGame() end, action = function()
		if getCurrentSave() ~= "" then request("save", getCurrentSave()) else push("save") end
	end },
	{ key = "IN_GAME_Save_As", enabled = function() return canSaveGame() end, action = function() push("save") end },
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
		elseif screen.onEscape then screen.onEscape()
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
	-- "mpload.<save game>" (tests): host that save game, starting by itself once every guest has a
	-- place or after 20 seconds alone
	if screenName == "mpload" then
		name = screenName
		mpLoad.save = tab
		mpLoad.autoStart = getTime()
	end
	-- "generaleditor.<name>" (tests): a random general of that name, saved
	if screenName == "generaleditor" then
		name = screenName
		editor.testSave = tab
	end
	-- "campaignmap.<profile>": the campaign map of a profile
	if screenName == "campaignmap" then
		name = screenName
		campaign.profile = tab
	end
	if #stack == 0 or stack[#stack] ~= name then push(name) end
	openedThisFrame = true
end

function FE_Close()
	stack = {}
	dialog = nil
	activeDrag = nil
end
