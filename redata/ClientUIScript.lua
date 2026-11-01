-- This Lua code is executed every frame on the client,
-- and is responsible for drawing and handling the in-game user interface.
-- This way, the UI can be easily modded.

-- Global variables
frame = 0
if getWKVersion() == GSVERSION_WKONE then
	progressColor = 0xFFFFC000
	progressColorDark = 0xFF806000
else
	progressColor = 0xFF0000FF
	progressColorDark = 0xFF000080
end

-- A resource or other item as a whole number (items are floats: 2000.0 showed as "2000.0")
function whole(v)
	return tostring(math.floor(v + 0.5))
end

-- The game speed actually reached: game seconds per real second over the last second (the
-- simulation may fall behind a high speed setting)
speedMeasure = { real = 0, game = 0, value = 1 }
function measuredSpeed()
	local real, game = getSystemTime(), getGameTime()
	if speedMeasure.real == 0 or real < speedMeasure.real then
		speedMeasure.real, speedMeasure.game = real, game
	elseif real - speedMeasure.real >= 1 then
		speedMeasure.value = (game - speedMeasure.game) / (real - speedMeasure.real)
		speedMeasure.real, speedMeasure.game = real, game
	end
	return speedMeasure.value
end

-- "1x", "2x", "0.5x", one decimal when needed
function speedText(v)
	if math.abs(v - math.floor(v + 0.5)) < 0.05 then
		return tostring(math.floor(v + 0.5)) .. "x"
	end
	return string.format("%.1fx", v)
end

-- The selection information panel (WKB.exe's CSelectionInformationPanel, 0x78D4F0). Its layout
-- is in fractions of a 1600 x 1200 screen there; here in pixels of Bottom_Left_Panel.tga.
local infoPanel = "Interface/InGame/Info Panel/"
-- the experience arc's 16 segments: x, y, width, height (exe tables 0x92DC20 and 0x92DBA0)
local experienceBars = {
	{ 58, 261, 25, 24 }, { 77, 273, 22, 21 }, { 96, 283, 20, 19 }, { 117, 287, 18, 17 },
	{ 136, 286, 16, 18 }, { 152, 281, 19, 20 }, { 168, 273, 21, 20 }, { 182, 262, 21, 22 },
	{ 194, 249, 21, 23 }, { 204, 236, 20, 20 }, { 211, 219, 20, 21 }, { 218, 201, 15, 19 },
	{ 219, 178, 14, 21 }, { 214, 160, 18, 20 }, { 206, 141, 20, 21 }, { 194, 125, 22, 22 },
}
-- a value with an upgrade's share: "8" or "8+2" (the original's "%.0f" and "%.0f+%.0f")
local function withUpgrade(base, upgrade)
	if upgrade == 0 or upgrade == 1 then return whole(base) end
	return whole(base) .. "+" .. whole(base * (upgrade - 1))
end
-- the military unit types' armour and weapon upgrades, by "I'm A Military Unit"
local upgradeItems = {
	{ "Heavy Infantry Armour Upgrade", "Heavy Infantry Weapon Upgrade" },
	{ "Light Infantry Armour Upgrade", "Light Infantry Weapon Upgrade" },
	{ "Heavy Cavalry Armour Upgrade", "Heavy Cavalry Weapon Upgrade" },
	{ "Light Cavalry Armour Upgrade", "Light Cavalry Weapon Upgrade" },
}

function drawSelectionInformation(obj)
	local bp = getBlueprint(obj)
	local px, py = 0, getWindowHeight() - 326
	local player = getPlayerID()
	local isCharacter = bp.bpClass == GAMEOBJCLASS_CHARACTER
	local isBuilding = bp.bpClass == GAMEOBJCLASS_BUILDING
	if not isCharacter and not isBuilding then return end

	-- hit points (structural integrity for buildings) under the heart
	local hp, cap
	if isCharacter then
		hp, cap = getItem(obj, "Hit Points"), getItem(obj, "Hit Point Capacity")
	else
		hp, cap = getItem(obj, "Structural Integrity"), getItem(obj, "Structural Integrity Capacity")
	end
	if cap > 0 then
		drawImage(px + 123, py + 92, 12, 13, infoPanel .. "Heart.tga")
		local text = whole(hp) .. "/" .. whole(cap)
		drawText(px + 129 - getTextWidth(text) / 2, py + 105, text)
	end
	if not isCharacter then return end

	-- the rows on the left: an icon and its value
	local row = 0
	local function statRow(icon, text)
		drawImage(px + 54, py + 147 + row * 30, 16, 21, infoPanel .. icon)
		drawText(px + 78, py + 150 + row * 30, text)
		row = row + 1
	end

	local military = getItem(obj, "I'm A Military Unit")
	if military > 0 then
		-- experience: segment i lit while i/16 of the maximum is below it
		local exp, maxExp = getItem(obj, "Experience"), getItem(obj, "Maximum Experience")
		if maxExp > 0 then
			for i = 1, 16 do
				if (i - 1) / 16 * maxExp >= exp then break end
				local b = experienceBars[i]
				drawImage(px + b[1], py + b[2], b[3], b[4], infoPanel .. "bar " .. i .. ".tga")
			end
		end
		local up = upgradeItems[math.floor(military)]
		local armourUp, weaponUp = 0, 0
		if up and player ~= 0 then
			armourUp, weaponUp = getItem(player, up[1]), getItem(player, up[2])
		end
		statRow("Shield.tga", withUpgrade(getItem(obj, "Front Armour"), armourUp))
		local h2h, missile = getItem(obj, "H2H Damage"), getItem(obj, "Missile Damage")
		if h2h > 0 or missile <= 0 then
			statRow("Sword.tga", withUpgrade(h2h, weaponUp))
		end
		if missile > 0 then
			statRow("Bow.tga", withUpgrade(missile, weaponUp))
		end
	elseif getItem(obj, "I Am A Merchant") > 0 then
		statRow("Trade Goods.tga", whole(getItem(obj, "Trade Goods")))
		statRow("Coin.tga", whole(getItem(obj, "Gold")))
	else
		-- peasants and carts: what they carry
		statRow("Chicken.tga", whole(getItem(obj, "Food")))
		statRow("Tree.tga", whole(getItem(obj, "Wood")))
		statRow("Coin.tga", whole(getItem(obj, "Gold")))
		statRow("Rock.tga", whole(getItem(obj, "Stone")))
	end
end

-- The peasant activity interface above the minimap (WKB.exe's CEconomicStatsWnd, drawn at
-- 0x77BC70): the top arc of the minimap panel cycles closed, the idle peasants' tube, all five.
-- Each tube (Tube 1.tga) holds its activity's icon at the right end, the count in cyan, and a red
-- bar growing from the right with it; rods (Strip 1/2) frame the five.
local activityRows = {
	{ "Interface/Icons/SIC_Shop.tga", "Shop Keeper Count", "Gold Miner Count", "Select shop keepers and gold miners" },
	{ "Interface/Icons/SIC_Farm.tga", "Number of Farmers", nil, "Select farmers" },
	{ "Interface/Icons/SIC_Chop_Wood.tga", "Wood Chopper Count", "Stone Miner Count", "Select wood choppers and stone miners" },
	{ "Interface/Icons/SIC_Build_Civic.tga", "Builder Count", nil, "Select builders" },
	{ "Interface/InGame/Lazy Peasant.tga", "Idle Peasant Count", nil, "Select idle peasants" },
}

function drawPeasantActivity()
	local mode = getPeasantActivityMode()
	local player = getPlayerID()
	if mode < 0 or player == 0 then return end
	local x0 = getWindowWidth() - 316
	local idleY = getWindowHeight() - 343 - 40
	local first = 5
	if mode == 1 then first = 1 end
	if mode == 1 then
		drawImage(x0 + 14, idleY - 4 * 60 - 4, 8, 4 * 60 + 60, "Interface/InGame/Strip 1.tga")
		drawImage(x0 + 276, idleY - 4 * 60 - 4, 8, 4 * 60 + 60, "Interface/InGame/Strip 2.tga")
	end
	for i = first, 5 do
		local row = activityRows[i]
		local y = idleY - (5 - i) * 60
		local a = getItem(player, row[2])
		local b = 0
		local text = whole(a)
		if row[3] then
			b = getItem(player, row[3])
			text = text .. " + " .. whole(b)
		end
		drawImage(x0, y, 308, 52, "Interface/InGame/Tube 1.tga")
		local w = math.min((a + b) * 3, 220)
		if w > 0 then drawRect(x0 + 250 - w, y + 18, w, 16, 0xFF1010C8) end
		drawTextColor(x0 + 30, y + 18, text, 0xFFF2E12A)
		drawImage(x0 + 258, y + 4, 44, 44, row[1])
		-- the icon selects those peasants (shift: adds them); the click doesn't reach the map
		local mx, my = getMouseX(), getMouseY()
		if x0 + 258 <= mx and mx < x0 + 302 and y + 4 <= my and my < y + 48 then
			setTooltip(row[4])
			if handleButton() == 1 then selectPeasantsByActivity(i) end
		end
	end
end

-- The top panel (Standard Panel.tga, 1024 x 85) in two parts at its own size: the left one up to
-- the end of the Tributes button (x 520), the right one with the resources against the window's
-- right edge, and between them the Celtic band (x 530-610) repeated. Narrower windows than 1024
-- get the whole panel scaled, as before.
local topPanel = "Interface/InGame/Top Panel/Standard Panel.tga"
local TOP_SPLIT, TOP_TILE0, TOP_TILE1 = 520, 530, 610

function topPanelScale()
	local w = getWindowWidth()
	if w >= 1024 then return 1 end
	return w / 1024
end

-- a panel x (0-1023) on the window
function topPanelX(x)
	local w = getWindowWidth()
	if w < 1024 then return x * w / 1024 end
	if x < TOP_SPLIT then return x end
	return w - 1024 + x
end

function drawTopPanel()
	local w = getWindowWidth()
	if w < 1024 then
		drawImage(0, 0, w, 85 * w / 1024, topPanel)
		return
	end
	drawImagePart(0, 0, TOP_SPLIT, 85, topPanel, 0, 0, TOP_SPLIT / 1024, 1)
	local gapEnd = w - (1024 - TOP_SPLIT)
	local x = TOP_SPLIT
	while x < gapEnd do
		local tile = math.min(TOP_TILE1 - TOP_TILE0, gapEnd - x)
		drawImagePart(x, 0, tile, 85, topPanel, TOP_TILE0 / 1024, 0, (TOP_TILE0 + tile) / 1024, 1)
		x = x + tile
	end
	drawImagePart(gapEnd, 0, 1024 - TOP_SPLIT, 85, topPanel, TOP_SPLIT / 1024, 0, 1, 1)
	drawTopButtons()
end

-- The five buttons in the left part (WKB.exe 0x790300: their rectangles as fractions of the
-- panel). Settings opens the in-game menu; the others are grey, as in a skirmish.
local topButtons = {
	{ "Settings", 218, true },
	{ "Mission Objectives", 278, false },
	{ "Global Diplomacy", 337, false },
	{ "Diplomacy", 395, false },
	{ "Tributes", 455, false },
}

function drawTopButtons()
	local s = topPanelScale()
	for _, b in ipairs(topButtons) do
		local x, y, bw, bh = topPanelX(b[2]), 20 * s, 56 * s, 14 * s
		local image = "Interface/InGame/Top Panel/" .. b[1] .. " Button"
		local mx, my = getMouseX(), getMouseY()
		local hovered = x <= mx and mx < x + bw and y <= my and my < y + bh
		if not b[3] then
			image = image .. " Grey"
		elseif hovered then
			image = image .. " Highlighted"
		end
		if hovered then
			setTooltip(b[1])
			-- grey ones take the click too (it doesn't go through to the map)
			local clicked = handleButton()
			if b[3] and clicked == 1 then openInGameMenu() end
		end
		drawImage(x, y, bw, bh, image .. ".tga")
	end
end

-- This function is executed every frame
function CCUI_PerFrame()

	frame = frame + 1

	-- Draw the panels

	if getWKVersion() == GSVERSION_WKONE then
		local tpm = getWindowWidth() / 1024.0
		drawImage(11*tpm, 16, 1002*tpm, 45, "Interface/InGame/Top Panel/Standard Panel.tga")
	else
		drawTopPanel()
	end
	drawStainedGlassPanel(0, getWindowHeight()-326, 330, 326)
	drawImage(getWindowWidth()-345, getWindowHeight()-343, 345, 343, "Interface/InGame/Bottom_Right_Panel.tga")
	-- Minimap inside the panel's inner frame (pixels 53-287 x 53-286 of Bottom_Right_Panel.tga)
	drawMinimap(getWindowWidth()-345+53, getWindowHeight()-343+53, 235, 234)
	-- Military / civilian / resource toggles and the peasant activity button around it
	drawMinimapButtons(getWindowWidth()-345, getWindowHeight()-343)
	drawPeasantActivity()


	-- Top Panel Information

	player = getPlayerID()
	if player ~= 0 then
		-- x in the top panel's 1024 pixels: the right part's (drawTopPanel); widths as they are
		local tx, tpm = topPanelX, topPanelScale()
		if getWKVersion() == GSVERSION_WKONE then
			tpm = getWindowWidth() / 1024.0
			tx = function(x) return x * tpm end
		end
		drawText(tx(674), 24, tostring(math.floor(getItem(player, "Population Count"))))
		drawText(tx(674), 42, tostring(math.floor(getItem(player, "Total Population Count"))))
		drawText(tx(748), 24, whole(getItem(player, "Food")))
		drawText(tx(838), 24, whole(getItem(player, "Wood")))
		drawText(tx(930), 24, whole(getItem(player, "Gold")))
		-- the game speed under the top panel: as set, and as reached when that is lower
		local speedLine
		if isGamePaused() then
			speedLine = "Paused"
		else
			local set, reached = getGameSpeed(), measuredSpeed()
			speedLine = "Speed " .. speedText(set)
			if reached < set * 0.95 then
				speedLine = speedLine .. " (reached " .. speedText(reached) .. ")"
			end
		end
		drawText(12, 88, speedLine)
		local color = getColor(player) % 8
		if color == 0 then color = 8 end
		drawImage(tx(650+8) - 8, 22, 16, 36, "Interface/InGame/Top Panel/Player" .. tostring(color) .. ".tga")

		local foodCons = getItem(player, "Food Consumption")
		local foodProd = computeEquation("Support by Total Farmers by Player", player)
		if foodProd > 0 then
			local foodRatio = math.min(1, foodCons / foodProd)
			if foodRatio < 1 then
				local leftWidth = math.floor(64*foodRatio + 0.5)
				drawRect(tx(744), 42, leftWidth*tpm, 13, 0xFF004A7F)
				drawRect(tx(744+leftWidth), 42, (64-leftWidth)*tpm, 13, 0xFF007817)
			else
				local t = math.floor(getSystemTime() * 2)
				if (t & 1) == 0 then
					drawRect(tx(744), 42, 64*tpm, 13, 0xFF0000FF)
				end
			end
		end
		local foodText = string.format("%i/%i", foodCons, foodProd)
		drawText(tx(748), 42, foodText)
	end


	-- Selection

	sel = getFirstSelection()
	-- the selection's information in the window of the lower left panel, as the original's
	-- CSelectionInformationPanel (WKB.exe 0x78D4F0); with nothing selected, the object under the mouse
	local info = sel
	if info == 0 then info = getHoveredObject() end
	if info ~= 0 then drawSelectionInformation(info) end
	if sel ~= 0 then
		local bp = getBlueprint(sel)


		-- Commands

		-- the building selection tab ("ECONOMIC", "MILITARY", "ALIGNMENT")
		buildTab = buildTab or "ECONOMIC"

		local commandButton = function (x, y, w, h, cs, assignMode, count)
			local mx = getMouseX()
			local my = getMouseY()
			local tex = cs.texpath
			if x <= mx and mx < x+w and y <= my and my < y+h then
				if cs.info == CSInfo.ENABLED then
					if isMouseDown(1) and cs.buttonDepressed ~= "" then
						tex = cs.buttonDepressed
					elseif cs.buttonHighlighted ~= "" then
						tex = cs.buttonHighlighted
					end
				end
				setTooltip(getCommandHint(cs.command, sel))
				local clicked = handleButton()
				if clicked == 1 then
					launchCommand(cs.command, assignMode, count)
				elseif clicked == 3 then
					cancelCommand(cs.command, count)
				end
			end
			drawImage(x, y, w, h, tex)
		end

		-- Ordinary commands
		local x = 0
		local y = 0
		local vec = listSelectionCommands(function (x)
			return string.find(x, "Build ") ~= 1 and string.find(x, "Spawn ") ~= 1 and string.find(x, "Research ") ~= 1 and
			x ~= "Finish Building" and x ~= "Build"
		end )
		local vecLen = #vec
		-- Knuckles button positions, in pixel relative to top-left corner of Bottom_Left_Panel.tga
		local knucklesButtonX = { 14,  93, 173, 232, 251, 228 }
		local knucklesButtonY = { 37,  13,  31,  87, 167, 247 }
		local numKnucklesButtons = #knucklesButtonX
		for i = 1,vecLen do
			local cs = vec[i]
			local hi = 64
			if #cs.texpath > 0 then
				if i <= numKnucklesButtons then
					x = knucklesButtonX[i]
					y = getWindowHeight() - 326 + knucklesButtonY[i]
				end
				local assignMode = ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE
				if isCtrlHeld() then assignMode = ORDERASSIGNMODE_DO_FIRST
				elseif isShiftHeld() then assignMode = ORDERASSIGNMODE_DO_LAST end
				commandButton(x, y, hi, hi, cs, assignMode, 1)
				x = x+hi+2
			end
		end
		-- For buildings: Icon for command assigned to spawned unit
		local spawnedUnitIcon = getBuildingSpawnedUnitIcon(sel)
		if spawnedUnitIcon ~= "" then
			drawImage(0, getWindowHeight()-64, 64, 64, spawnedUnitIcon)
		end

		if bp.bpClass == GAMEOBJCLASS_CHARACTER then
			-- Build commands, as the original's building selection window: three tabs (the
			-- DISPLAY_CATEGORY of the building), two columns of buttons in a frame. A building
			-- without a category isn't offered.
			local vec = listSelectionCommands(function (x) return string.find(x, "Build ") == 1 end )
			if #vec > 0 then
				local k = 64 / 58 -- the original's 58 pixel buttons (at 1024 x 768) as ours of 64
				local px, py = 0, 44
				local art = "Interface\\InGame\\Top Panel\\"
				local tabs = { { "MILITARY", "Military Building Button Highlighted.tga", 4, "TOOLTIP_MILITARY_BUILDINGS" },
					{ "ALIGNMENT", "Civic Building Button Highlighted.tga", 42, "TOOLTIP_ALIGNMENT_BUILDINGS" },
					{ "ECONOMIC", "Economic Building Button Highlighted.tga", 80, "TOOLTIP_ECONOMIC_BUILDINGS" } }
				drawImage(px, py, 150 * k, 56 * k, art .. "Panel with Building Tabs.tga")
				for _, tab in ipairs(tabs) do
					local tx, ty, ts = px + tab[3] * k, py + 12 * k, 31 * k
					local mx, my = getMouseX(), getMouseY()
					local over = tx <= mx and mx < tx + ts and ty <= my and my < ty + ts
					if over then
						setTooltip(getText(tab[4]))
						if handleButton() == 1 then buildTab = tab[1] end
					end
					if buildTab == tab[1] or over then drawImage(tx, ty, ts, ts, art .. tab[2]) end
					if buildTab == tab[1] then drawImage(tx - 3.5 * k, ty - 5 * k, 38 * k, 42 * k, art .. "Tab Marker.tga") end
				end
				local shown = {}
				for i = 1,#vec do
					if #vec[i].texpath > 0 and getCommandBuildCategory(vec[i].command) == buildTab then shown[#shown + 1] = vec[i] end
				end
				local rows = math.max(7, (#shown + 1) // 2)
				local fy = py + 56 * k
				drawImage(px, fy, 127 * k, 424 * k * rows / 7, art .. "Frame.tga")
				for i, cs in ipairs(shown) do
					x = px + (11 + ((i-1) % 2) * 58) * k - 7 * k
					y = fy + (2 + ((i-1) // 2) * 58) * k
					local assignMode = ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE
					if isCtrlHeld() then assignMode = ORDERASSIGNMODE_DO_FIRST
					elseif isShiftHeld() then assignMode = ORDERASSIGNMODE_DO_LAST end
					commandButton(x, y, 56 * k, 56 * k, cs, assignMode, 1)
				end
			end
		elseif bp.bpClass == GAMEOBJCLASS_BUILDING then
			function commandProgress( x, y, width, height, cs )
				if cs.count > 0 then
					drawText(x+5, y+height-18, tostring(cs.count))
					if cs.isCurrentOrder then
						local progress = math.max(0, getItem(sel, "Hit Points Of Object Being Spawned"))
						local progressCapacity = math.max(1, getItem(sel, "Hit Point Capacity Of Object Being Spawned"))
						local progressFraction = math.max(0, math.min(1, progress / progressCapacity))
						local progressLength = math.floor(width * progressFraction)
						drawRect(x, y, progressLength, 10, progressColor)
						drawRect(x + progressLength, y, width - progressLength, 10, progressColorDark)
					end
				end
			end

			-- Spawn commands
			local vec = listSelectionCommands(function (x) return string.find(x, "Spawn ") == 1 end )
			for i = 1,#vec do
				local cs = vec[i]
				local hi = 64
				if #cs.texpath > 0 then
					x = 24
					y = 64 + (i-1) * (hi+2)
					local count = 1
					if isShiftHeld() then count = 10 end
					commandButton(x, y, hi, hi, cs, ORDERASSIGNMODE_DO_LAST, count)
					commandProgress(x, y, hi, hi, cs)
				end
			end

			-- Research commands
			local vec = listSelectionCommands(function (x) return string.find(x, "Research ") == 1 end )
			for i = 1,#vec do
				local cs = vec[i]
				local hi = 64
				if #cs.texpath > 0 then
					x = 24 + hi+2
					y = 64 + (i-1) * (hi+2)
					commandButton(x, y, hi, hi, cs, ORDERASSIGNMODE_DO_LAST, 1)
					commandProgress(x, y, hi, hi, cs)
				end
			end
		elseif bp.bpClass == GAMEOBJCLASS_FORMATION then
			local formationNames = { "Line", "Column", "Wedge", "Orb" }
			for i = 1,#formationNames do
				local x = 360 + (i-1)*70
				local y = getWindowHeight() - 100
				local w = 64
				local h = 64
				local mx = getMouseX()
				local my = getMouseY()
				local tex = ""
				if bp.name == formationNames[i] then
					tex = "Interface/Icons/I_Formation_" .. formationNames[i] .. "_Dep.tga"
				else
					tex = "Interface/Icons/I_Formation_" .. formationNames[i] .. ".tga"
				end
				if x <= mx and mx < x+w and y <= my and my < y+h then
					setTooltip(formationNames[i])
					local clicked = handleButton()
					if clicked == 1 then
						forceLaunchCommand(getCommand(formationNames[i]), ORDERASSIGNMODE_FORGET_EVERYTHING_ELSE)
					end
				end
				drawImage(x, y, w, h, tex)

			end
		end

	end

end -- function CCUI_PerFrame