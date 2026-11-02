-- wkbai's advanced AI, steering a Kingmaker AI General (choose it for an AI position in the lobby).
--
-- The Kingmaker scripts still plan the economy, the buildings and which soldiers to train; this
-- script decides how they are used:
-- - Defence of every settlement and of the peasants out working, from the start: enemy soldiers
--   near a town (other than the palace's, which the scripts defend) or near a peasant are met by
--   enough soldiers from home to outmatch them (1.5 times their strength), the nearest first.
--   When no help would win, the peasants there walk home. Defenders go home once the enemies
--   are gone (or after 90 s).
-- - Attacks by strength: every 30 s, a home guard of 10 and a fifth of the soldiers stays; the
--   rest (12 or more) goes when there is an enemy settlement it outmatches (1.4 times what
--   guards it), the one worth most for its guard (a palace 3, a village 2, others 1.5), nearer
--   ones first. With more than 150 at home, the best one anyway.
-- - A wave that is losing (under half the strength around it, having lost a fifth) comes home;
--   one whose target fell goes on to the nearest enemy building within 150 m.
--
-- To make your own AI: copy this file to a mod as LuaAI\<name>.lua (or LuaAI\Advanced.lua to
-- change this one) and change it. The "ai" table the game gives is described in the wkbai
-- documentation (docs/engine/lua-ai.md); units, buildings, towns and players are their ids.

AI_INTERVAL = 3 -- seconds of game time between calls of think

local DEFEND_OUTMATCH = 1.5   -- defenders sent until they are this much stronger
local DEFEND_HOPELESS = 0.8   -- below this, the peasants flee instead
local ATTACK_OUTMATCH = 1.4   -- a wave goes only against a guard it outmatches so much
local HOME_GUARD = 10         -- soldiers who always stay home, plus a fifth of them
local MIN_WAVE = 12
local OVERFLOW = 150          -- with this many at home, the best target anyway
local WAVE_CHECK = 30
local RECALL_STRENGTH = 0.5   -- a wave under half the strength around it comes home
local RECALL_LOSSES = 0.8     -- once it has lost a fifth of its units
-- hold the scripts' trickle of attackers at a muster point until they outmatch a target together
-- (off: on 4 Peaks the scripts' stream of single attackers razed 33 buildings in 35 minutes,
-- the muster none; an enemy army near home kept the scripts in Defensive Mode meanwhile)
local MUSTER = false

local defenders = {}          -- unit id -> { x, z, since }
local waves = {}              -- { army, target, tx, tz, size }
local nextWaveCheck = 0
local counts = { threats = 0, defenders = 0, fled = 0, held = 0, recalled = 0, retargeted = 0 }

local function dist2(ax, az, bx, bz)
	local dx, dz = ax - bx, az - bz
	return dx * dx + dz * dz
end

-- ids sorted by distance to a point (ties by id, so the order never depends on anything else)
local function byDistance(ai, ids, x, z)
	local d = {}
	for _, id in ipairs(ids) do
		local ux, uz = ai.position(id)
		d[id] = dist2(ux, uz, x, z)
	end
	table.sort(ids, function(a, b)
		if d[a] ~= d[b] then return d[a] < d[b] end
		return a < b
	end)
end

local function nearest(ai, ids, x, z)
	local best, bestD
	for _, id in ipairs(ids) do
		local ux, uz = ai.position(id)
		local d = dist2(ux, uz, x, z)
		if not best or d < bestD or (d == bestD and id < best) then best, bestD = id, d end
	end
	return best
end

local function defend(ai, now, palace, px, pz)
	-- the threats: enemy soldiers near a town or a peasant, each counted once
	local seen, threats = {}, {}
	local function add(x, z, radius)
		if palace and dist2(x, z, px, pz) < 250 * 250 then return end
		local list = {}
		for _, e in ipairs(ai.enemiesNear(x, z, radius)) do
			if not seen[e] then
				seen[e] = true
				list[#list + 1] = e
			end
		end
		if #list > 0 then threats[#threats + 1] = { x = x, z = z, enemies = list } end
	end
	for _, town in ipairs(ai.towns()) do add(town.x, town.z, 100) end
	local peasants = ai.peasants()
	local step = math.max(1, #peasants // 40)
	for i = 1, #peasants, step do
		local x, z = ai.position(peasants[i])
		add(x, z, 30)
	end

	-- defenders whose enemies are gone (or out 90 s) go home
	for id, d in pairs(defenders) do
		if not ai.alive(id) then
			defenders[id] = nil
		elseif #ai.enemiesNear(d.x, d.z, 60) == 0 or now - d.since > 90 then
			ai.sendHome({ id })
			defenders[id] = nil
		end
	end

	local available = {}
	for _, u in ipairs(ai.home()) do
		if not defenders[u] then available[#available + 1] = u end
	end
	for _, t in ipairs(threats) do
		local meeting = {}
		for id, d in pairs(defenders) do
			if dist2(d.x, d.z, t.x, t.z) < 80 * 80 then meeting[#meeting + 1] = id end
		end
		if ai.strength(meeting, t.enemies) < DEFEND_OUTMATCH * ai.strength(t.enemies, meeting) then
			byDistance(ai, available, t.x, t.z)
			local sent = {}
			for _, id in ipairs(meeting) do sent[#sent + 1] = id end
			local used = 0
			while used < #available and ai.strength(sent, t.enemies) < DEFEND_OUTMATCH * ai.strength(t.enemies, sent) do
				used = used + 1
				sent[#sent + 1] = available[used]
			end
			if ai.strength(sent, t.enemies) < DEFEND_HOPELESS * ai.strength(t.enemies, sent) then
				-- no help that would win: the peasants there walk home
				local flee = {}
				for _, p in ipairs(peasants) do
					local x, z = ai.position(p)
					if dist2(x, z, t.x, t.z) < 40 * 40 then flee[#flee + 1] = p end
				end
				ai.sendHome(flee)
				counts.fled = counts.fled + #flee
			else
				for i = 1, used do
					local u = available[i]
					local ux, uz = ai.position(u)
					local e = nearest(ai, t.enemies, ux, uz)
					if e then ai.attack({ u }, e) end
					defenders[u] = { x = t.x, z = t.z, since = now }
				end
				local rest = {}
				for i = used + 1, #available do rest[#rest + 1] = available[i] end
				available = rest
				counts.threats = counts.threats + 1
				counts.defenders = counts.defenders + used
				ai.log(string.format("%d enemies at (%.0f,%.0f), %d defenders sent", #t.enemies, t.x, t.z, used))
			end
		end
	end
end

-- the enemy settlement worth attacking most for what guards it, among those the wave would
-- outmatch (any, if anyway): its building nearest to home
local function chooseTarget(ai, wave, hx, hz, anyway)
	local best, bestScore, bx, bz
	for _, enemy in ipairs(ai.enemies()) do
		for _, town in ipairs(ai.towns(enemy)) do
			local worth = town.palace and 3 or (town.village and 2 or 1.5)
			local guards = ai.enemiesNear(town.x, town.z, 150, true)
			local defence = ai.strength(guards, wave)
			if anyway or ai.strength(wave, guards) >= ATTACK_OUTMATCH * defence then
				local distance = math.sqrt(dist2(town.x, town.z, hx, hz))
				local score = worth / ((defence + 1) * (1 + distance / 600))
				if not best or score > bestScore then
					local b = nearest(ai, town.buildings, hx, hz)
					if b then
						best, bestScore = b, score
						bx, bz = ai.position(b)
					end
				end
			end
		end
	end
	return best, bx, bz
end

-- The muster: the Kingmaker scripts send their soldiers out as soon as they are trained, one to
-- an attacking army (31 such armies at 25 minutes on 4 Peaks, almost nobody home), and they die
-- one by one at the enemy's towers. Soldiers of the scripts' attacking armies who are still
-- within 400 m of home and not fighting (no enemy within 80 m) are held at a muster point 60 m
-- from the palace towards the nearest enemy, in an army of this AI's own (the scripts can't send
-- them off again), until the muster and the soldiers at home outmatch a target together.
local muster
local function musterPoint(ai, px, pz)
	local best, bestD
	for _, e in ipairs(ai.enemies()) do
		for _, t in ipairs(ai.towns(e)) do
			local d = dist2(t.x, t.z, px, pz)
			if not best or d < bestD then best, bestD = t, d end
		end
	end
	if not best or bestD < 1 then return px, pz end
	local d = math.sqrt(bestD)
	return px + (best.x - px) / d * 60, pz + (best.z - pz) / d * 60
end

local function gather(ai, px, pz)
	local ours = {}
	for _, w in ipairs(waves) do ours[w.army] = true end
	local take, musterUnits = {}, {}
	for _, a in ipairs(ai.armies()) do
		if a.id == muster then
			musterUnits = a.units
		elseif not ours[a.id] then
			for _, u in ipairs(a.units) do
				local x, z = ai.position(u)
				local unit = ai.unit(u)
				if unit and unit.soldier and dist2(x, z, px, pz) < 400 * 400 and #ai.enemiesNear(x, z, 80) == 0 then take[#take + 1] = u end
			end
		end
	end
	-- an enemy army at the door: the muster joins the scripts' defence
	if ai.defensiveMode() then
		if #musterUnits > 0 then ai.sendHome(musterUnits) end
		return
	end
	if #take > 0 then
		local mx, mz = musterPoint(ai, px, pz)
		muster = ai.hold(take, mx, mz, muster)
		counts.mustered = (counts.mustered or 0) + #take
	end
end

local function attack(ai, now, px, pz)
	local home = ai.home()
	if ai.defensiveMode() then return end
	table.sort(home) -- the oldest (lowest ids) stay
	local keep = HOME_GUARD + #home // 5
	local wave = {}
	for i = keep + 1, #home do wave[#wave + 1] = home[i] end
	for _, a in ipairs(ai.armies()) do
		if a.id == muster then
			for _, u in ipairs(a.units) do wave[#wave + 1] = u end
		end
	end
	local outside = 0
	for _, a in ipairs(ai.armies()) do if a.id ~= muster then outside = outside + #a.units end end
	ai.log(string.format("attack check: %d at home (%d stay), %d in the muster's wave, %d in other armies", #home, keep, #wave, outside))
	if #wave < MIN_WAVE then return end
	local target, tx, tz = chooseTarget(ai, wave, px, pz, #wave > OVERFLOW)
	if not target then
		counts.held = counts.held + 1
		return
	end
	local army = ai.launch(wave, target)
	if army then
		waves[#waves + 1] = { army = army, target = target, tx = tx, tz = tz, size = #wave }
		ai.log(string.format("wave of %d at %s (%.0f,%.0f)", #wave, ai.unit(target) and ai.unit(target).type or "?", tx, tz))
	end
end

local function manageWaves(ai)
	local unitsOf = {}
	for _, a in ipairs(ai.armies()) do unitsOf[a.id] = a.units end
	local kept = {}
	for _, w in ipairs(waves) do
		local units = unitsOf[w.army]
		if units and #units > 0 then
			local cx, cz = 0, 0
			for _, u in ipairs(units) do
				local x, z = ai.position(u)
				cx, cz = cx + x, cz + z
			end
			cx, cz = cx / #units, cz / #units
			local around = ai.enemiesNear(cx, cz, 70, true)
			if #around > 0 and #units < w.size * RECALL_LOSSES and ai.strength(units, around) < RECALL_STRENGTH * ai.strength(around, units) then
				ai.sendHome(units)
				counts.recalled = counts.recalled + 1
				ai.log(string.format("wave recalled: %d of %d left against %d", #units, w.size, #around))
			elseif not ai.alive(w.target) then
				local next = ai.buildingsNear(w.tx, w.tz, 150)[1]
				if next then
					ai.attack(units, next)
					w.target = next
					w.tx, w.tz = ai.position(next)
					counts.retargeted = counts.retargeted + 1
					kept[#kept + 1] = w
				else
					ai.sendHome(units)
				end
			else
				kept[#kept + 1] = w
			end
		end
	end
	waves = kept
end

local nextWaveManage = 0
-- The scripts turn peasants into militia when an attack outnumbers them three to one; on 4 Peaks
-- both AIs lost every peasant that way and never trained another. With fewer than 25 peasants
-- they stay peasants (the scripts' "Cannot Use Militia").
local MILITIA_PEASANTS = 25

function think(ai)
	local now = ai.time()
	ai.setItem(ai.id, "Cannot Use Militia", #ai.peasants() < MILITIA_PEASANTS and 1 or 0)
	local palace = ai.palace()
	local px, pz = 0, 0
	if palace then px, pz = ai.position(palace) end
	defend(ai, now, palace, px, pz)
	if now >= nextWaveManage then
		nextWaveManage = now + 6
		manageWaves(ai)
		if palace and MUSTER then gather(ai, px, pz) end
	end
	if now >= nextWaveCheck then
		nextWaveCheck = now + WAVE_CHECK
		attack(ai, now, px, pz)
	end
end

-- for the AI test's census
function census()
	return string.format("threats met %d (%d defenders), peasants sent home %d; mustered %d; waves held %d, recalled %d, retargeted %d, out %d",
		counts.threats, counts.defenders, counts.fled, counts.mustered or 0, counts.held, counts.recalled, counts.retargeted, #waves)
end
