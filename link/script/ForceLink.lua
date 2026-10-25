-- ForceLink: an Akai Force (via flsurf on the Force + the "Force Link" port of forcespoof B mode) as an extra surface
-- for an MPC on 3.10. The Force shows its own 8x8 clip window, independent of what the MPC's own pads are doing.
-- v1 (2026-10-24): three pad modes, picked with the Force's MATRIX / NOTE / STEP SEQ buttons:
--   MATRIX           pads launch the clip under them; colours: clip = its colour (dim when stopped), blinking when queued
--   NOTE             pads play the MPC's own 64-pad mode: each Force pad is one quadrant of an MPC pad (2x2 Force pads
--                    per MPC pad, MPC pad 1 bottom-left); colours = the MPC pad colours. Quadrant order: QUAD below.
--   STEP SEQ         the top two rows mirror the MPC's 16 step-sequencer buttons (colours, brightness); a tap taps it
-- v0 matrix features:
--   row launch 1-8   launch the scene row; track select 1-8 select the track; track arm row 1-8 stop that track
--   arrows           move the 8x8 window (SHIFT: by 8)
--   Q-link knobs     the 8 MPC Q-links, names and values on the OLEDs
--   PLAY / REC       transport, PLAY lit while playing
-- Force note numbers: pads ch10 note 54 + row*8 + col (row 0 = top); buttons ch1 notes (FINDINGS 12.6).
-- Text to the Force's OLEDs: sysex 7D 46 <flcmd command> (flsurf draws it).

local deviceName = "ForceLink" -- must match the file name
local port = 0                 -- the one Force Link output
local dev = ForceLink

local PAD0, PADS = 54, 64
local BTN = {play = 0x52, stop = 0x51, rec = 0x49, shift = 0x31,
             up = 0x70, down = 0x71, left = 0x72, right = 0x73,
             matrix = 0x03, note = 0x04, step = 0x75, knobs = 0x01}
-- NOTE mode: quadrant index for the position inside a 2x2 block (1 = top-left, 2 = top-right, 3 = bottom-left,
-- 4 = bottom-right). A guess until checked on the device.
local QUAD = {2, 3, 0, 1}   -- checked 2026-10-24: the MPC's quadrant 0/1 are the bottom row
local mode = "matrix"
local ticks = 0 -- timer ticks (250 ms)
local stepSeq, stepTap, stepBright -- the step sequencer object and its functions, found in OnLoad
local LAUNCH0, TRACKSEL0, TRACKARM0 = 56, 96, 41

local aperture, clipProps -- found in OnLoad
local originCol, originRow -- the window's top-left in the global clip matrix (polling fallback)
local shift = false
local blinkOn = false
local padCache = {}

local function sysex(bytes) dev.midiOut.SendMIDISysex(bytes, port) end
local function text(cmd) sysex(string.char(0x7D, 0x46) .. cmd) end
local function led(cc, value) dev.midiOut.SendMIDICC(0, cc, value, port) end

local function argb(c)
    c = c or 0
    return math.floor(c / 65536) % 256, math.floor(c / 256) % 256, c % 256
end

local function stateName(s)
    if type(s) == "number" then
        local names = {"Stopped", "WillPlay", "Playing", "WillStopPlaying", "WillRecord", "WillStopRecording",
                       "WillOverdub", "WillStopOverdubbing"}
        return names[s + 1] or tostring(s)
    end
    return tostring(s)
end

local FULL, DIM = 0.8, 0.15   -- playing / stopped clip brightness

-- global matrix position of a pad in the Force's window
local function globalPos(col, row)
    if aperture and aperture.ToGlobalColumn then
        local ok1, gc = pcall(aperture.ToGlobalColumn, col)
        local ok2, gr = pcall(aperture.ToGlobalRow, row)
        if ok1 and ok2 then return gc, gr end
    end
    return col + (originCol or 0), row + (originRow or 0)
end

-- clip presence and play state from the global matrix (the per-clip State numbers are not decoded: their enum
-- order is unknown); 0 = no clip, 1 = stopped, 2 = playing/recording, 3 = queued (neither)
local function clipStatus(col, row)
    local m = mpc.clipMatrix
    if not (m and m.HasClip) then return 0 end
    local gc, gr = globalPos(col, row)
    local ok, has = pcall(m.HasClip, gc, gr)
    if not (ok and has) then return 0, gc, gr end
    if m.ClipIsPlaying(gc, gr) or m.ClipIsRecording(gc, gr) then return 2, gc, gr end
    if m.ClipIsStopped(gc, gr) then return 1, gc, gr end
    return 3, gc, gr
end

local function paintPad(col, row)
    local idx = row * 8 + col
    local r, g, b = 0, 0, 0
    local status, gc, gr = clipStatus(col, row)
    if status > 0 then
        local p = clipProps and clipProps[col] and clipProps[col][row]
        local cr, cg, cb = argb(p and p.Colour.value or mpc.clipMatrix.GetClipColour(gc, gr))
        local k = status == 2 and FULL or status == 1 and DIM or (blinkOn and FULL or DIM)
        r, g, b = cr * k, cg * k, cb * k
    end
    r, g, b = math.floor(r / 2), math.floor(g / 2), math.floor(b / 2) -- 8 bit -> the controller's 7 bit
    local key = r * 65536 + g * 256 + b
    if padCache[idx] == key then return end
    padCache[idx] = key
    sysex(string.char(0x47, 0x7F, 0x40, 0x65, 0x00, 0x04, idx, r, g, b))
end

local function setPad(idx, r, g, b) -- 8-bit colour, cached
    r, g, b = math.floor(r / 2), math.floor(g / 2), math.floor(b / 2)
    local key = r * 65536 + g * 256 + b
    if padCache[idx] == key then return end
    padCache[idx] = key
    sysex(string.char(0x47, 0x7F, 0x40, 0x65, 0x00, 0x04, idx, r, g, b))
end

-- NOTE mode: Force pad (col, row) -> MPC pad index (0 = bottom-left) and quadrant index
local function padQuad(col, row)
    local pad = math.floor((7 - row) / 2) * 4 + math.floor(col / 2)
    return pad, QUAD[(row % 2) * 2 + (col % 2) + 1]
end

local function hexRGB(s) -- "FF0000", "#FF0000" or "{FF0000}": the first six hex digits
    s = tostring(s or ""):match("%x%x%x%x%x%x")
    if not s then return 0, 0, 0 end
    return tonumber(s:sub(1, 2), 16), tonumber(s:sub(3, 4), 16), tonumber(s:sub(5, 6), 16)
end

-- NOTE mode: flsurf renumbers the pads 36 + 8 * row-from-bottom + col.
-- Drum track: laid out like the real Force (MPC 3.10 ADA2PadMap2x2, FINDINGS 20): banks A bottom-left, B bottom-right,
-- C top-left, D top-right, each 4x4 numbered from the bottom-left; each note is mapped to its absolute program pad
-- (virtual pad 0-63), so hits ignore the MPC's selected bank and the program's note map, and colours are that pad's.
-- Other tracks: the notes reach the track as plain MIDI.
local noteDrum = nil -- layout applied: true = drum pads mapped, false = plain notes, nil = not in NOTE mode

local function forcePadToVirtual(col, fromBottom)
    local bank = (col >= 4 and 1 or 0) + (fromBottom >= 4 and 2 or 0)
    return bank * 16 + col % 4 + 4 * (fromBottom % 4)
end

local function isDrumTrack()
    local ok, t = pcall(function() return mpc.tracks.currentTrack.Type.value end)
    return ok and type(t) == "string" and t:lower():find("drum") ~= nil
end

-- Melodic tracks, as the real Force (FINDINGS 21): chromatic rows a 4th apart, keyBase at the bottom-left, roots
-- (C; the track's scale/root are not readable by scripts) in the track colour, other pads dim white. flsurf plays
-- the notes and copies each pad press on channel 16 (pad number) for the press colours.
local keyBase = 48
local heldPads = {}   -- pad index 0-63 -> true while pressed (melodic NOTE)
local function keyPitch(row, col) return keyBase + col + 5 * (7 - row) end

local function mapNoteLayout(drum)
    local m = dev.mappings.pads
    text(drum and "notemap on" or ("notemap keys " .. keyBase))
    for n = PAD0, PAD0 + PADS - 1 do                   -- the channel-16 copies: to the script on melodic tracks
        if drum then
            if dev.mappings.script.RemoveNoteToScript then dev.mappings.script.RemoveNoteToScript(15, n) end
        else
            dev.mappings.script.AddNoteToScript(15, n)
        end
    end
    heldPads = {}
    for fromBottom = 0, 7 do
        for col = 0, 7 do
            local note = 36 + 8 * fromBottom + col
            if drum then m.AddNoteToVirtualPad(9, note, forcePadToVirtual(col, fromBottom))
            elseif m.RemoveNoteToVirtualPad then m.RemoveNoteToVirtualPad(9, note) end
        end
    end
    noteDrum = drum
end

local function paintNote()
    local pads = mpc.controlSurface.pads
    local drum = isDrumTrack()
    if drum ~= noteDrum then mapNoteLayout(drum) end
    if drum then
        for row = 0, 7 do
            for col = 0, 7 do
                local r, g, b = 0, 0, 0
                local ok, hex = pcall(pads.LightRGBColor, forcePadToVirtual(col, 7 - row) + 1) -- virtual pads 1-64
                if ok and hex and tostring(hex) ~= "" then r, g, b = hexRGB(hex) end
                setPad(row * 8 + col, r, g, b)
            end
        end
        return
    end
    -- melodic: colours as captured from the real Force (7-bit values doubled for setPad)
    local held, heldClass = {}, {}
    for p in pairs(heldPads) do
        local n = keyPitch(math.floor(p / 8), p % 8)
        held[n] = true; heldClass[n % 12] = true
    end
    local tr, tg, tb = 0xCC, 0x0A, 0xCC                       -- root colour: the track's, else magenta as captured
    local okc, c = pcall(function() return mpc.tracks.currentTrack.Colour.value end)
    if okc and type(c) == "number" and c ~= 0 then tr, tg, tb = argb(c) end
    for row = 0, 7 do
        for col = 0, 7 do
            local n = keyPitch(row, col)
            if held[n] then setPad(row * 8 + col, 0, 254, 0)                  -- this note: bright green
            elseif heldClass[n % 12] then setPad(row * 8 + col, 0, 84, 34)    -- same note, other octave: dim green
            elseif n % 12 == 0 then setPad(row * 8 + col, tr, tg, tb)         -- root (C)
            else setPad(row * 8 + col, 50, 50, 50) end                        -- other notes: dim white
        end
    end
end

-- button LEDs (CC on the Force: 0 off, 1 dim, 3 bright), cached
local ledCache = {}
local function setLed(cc, v)
    if ledCache[cc] == v then return end
    ledCache[cc] = v
    led(cc, v)
end

-- STEP SEQ page: 32 steps as two bars of MPC's own 16-step sequencer, plus pad select and 16 velocity levels.
-- Scripts cannot read sequence events, so the bar MPC's step sequencer is on (the playhead's bar) is live and the other
-- bar shows its last-seen state. Layout (Force rows from the top):
--   rows 0-1  bar A steps 1-16      rows 2-3  bar B steps 1-16 (A = 2 * pairIndex, B = A + 1)
--   rows 4-7, cols 0-3  the 16 MPC pads of the current bank (pad 1 bottom-left): tap selects and plays it
--   rows 4-7, cols 4-7  16 velocity levels for the selected pad (level 1 bottom-left .. 16 top-right): tap plays it
-- LEFT / RIGHT page through bar pairs.
local BAR_TICKS = 3840          -- 16 steps of 240 ticks (GetTCPeriod) at 960 PPQ, 4/4
local stepCache = {}            -- stepCache[bar][step 1..16] = {r, g, b}
local pairIndex = 0
local pendingTaps = {}          -- {bar, step}: taps on the bar MPC is not on, done when it gets there
local selectedPad = 0           -- 0-based MPC pad (current bank)
local selectedLevel = 16

local function call(f, ...) if f then local ok, r = pcall(f, ...); if ok then return r end end end

local function currentBar()
    local pos = call(mpc.sequencer.PlayheadPosition) or 0
    return math.floor(pos / BAR_TICKS)
end

local function numBars()
    local e = call(mpc.sequencer.GetEndOrLoop) or 2 * BAR_TICKS
    return math.max(1, math.floor((e + BAR_TICKS - 1) / BAR_TICKS))
end

local function readSteps(bar) -- the 16 lights of MPC's step sequencer into the cache for this bar
    local row = {}
    for i = 1, 16 do
        local r, g, b = 0, 0, 0
        if stepSeq and stepSeq.LightRGBColour then
            r, g, b = hexRGB(call(stepSeq.LightRGBColour, i))
            local k = stepBright and call(stepBright, i)
            if type(k) == "number" then r, g, b = r * k, g * k, b * k end
        end
        row[i] = {r, g, b}
    end
    stepCache[bar] = row
end

local function padIndex1(p) return p + 1 end -- the pads API counts from 1, like the step buttons

local lastLoggedBar
local function paintStepLegacy()
    local cur = currentBar()
    readSteps(cur)
    if cur ~= lastLoggedBar then                              -- what MPC's step lights show at each bar change
        lastLoggedBar = cur
        local lit = {}
        for i = 1, 4 do
            lit[#lit + 1] = i .. "=" .. tostring(call(stepSeq.LightRGBColour, i)) .. "/" .. tostring(stepBright and call(stepBright, i))
        end
        print("ForceLink: bar " .. cur .. " (pair " .. pairIndex .. ") steps " .. table.concat(lit, " "))
    end
    -- pending taps for the bar we are now on
    for i = #pendingTaps, 1, -1 do
        local t = pendingTaps[i]
        if t[1] == cur and ticks >= (t[3] or 0) then call(stepTap, t[2]); table.remove(pendingTaps, i); readSteps(cur) end
    end
    if mpc.sequencer.IsPlaying and call(mpc.sequencer.IsPlaying) then pairIndex = math.floor(cur / 2) end
    for half = 0, 1 do
        local bar = pairIndex * 2 + half
        local row = stepCache[bar]
        for i = 1, 16 do
            local c = row and row[i] or {0, 0, 0}
            local k = (bar == cur) and 1 or 0.6 -- the live bar a little brighter
            setPad(half * 16 + i - 1, c[1] * k, c[2] * k, c[3] * k)
        end
    end
    local pads = mpc.controlSurface.pads
    for p = 0, 15 do -- pad select: rows 4-7, cols 0-3, pad 1 bottom-left
        local row, col = 7 - math.floor(p / 4), p % 4
        local r, g, b = hexRGB(call(pads.LightRGBColor, padIndex1(p)))
        if r + g + b == 0 and pads[p] then r, g, b = argb(pads[p].Colour.value) end
        if r + g + b == 0 then r, g, b = 160, 160, 160 end   -- unlit in this mode: dim white
        local k = (p == selectedPad) and 1 or 0.25
        setPad(row * 8 + col, r * k, g * k, b * k)
    end
    for l = 1, 16 do -- velocity levels: rows 4-7, cols 4-7, level 1 bottom-left
        local row, col = 7 - math.floor((l - 1) / 4), 4 + (l - 1) % 4
        local v = math.floor(255 * l / 16)
        local k = (l == selectedLevel) and 1 or 0.5
        setPad(row * 8 + col, v * k, v * 0.4 * k, 0) -- an orange ramp
    end
    setLed(BTN.left, pairIndex > 0 and 1 or 0)
    setLed(BTN.right, (pairIndex + 1) * 2 < numBars() and 1 or 0)
end

-- Selecting a pad: pads.Tap(index 1-based, velocity) is what MPC counts as a hit (LastHitPad follows; checked on the
-- device 2026-10-24), so step entry then edits that pad. The velocity only affects how it plays: the API has no way to
-- set the step sequencer's entry velocity, and MPC's 16 LEVELS button is a toggle that proved too fragile to drive.
local function playSelectedPad(level)
    local vel = math.max(1, math.floor(127 * level / 16))
    call(mpc.controlSurface.pads.Tap, padIndex1(selectedPad), vel)
end

-- a tap on MPC's own step button `step` (1-16) of bar `bar`: now if that bar is MPC's step view, else when it gets there
local function legacyTap(bar, step)
    if bar == currentBar() then
        call(stepTap, step)
    elseif call(mpc.sequencer.IsPlaying) then
        pendingTaps[#pendingTaps + 1] = {bar, step}              -- done when the playhead reaches that bar
    else
        call(mpc.sequencer.JumpToPulse, bar * BAR_TICKS)         -- stopped: go to that bar; the tap waits until
        pendingTaps[#pendingTaps + 1] = {bar, step, ticks + 2}   -- MPC's step view is there (two timer ticks)
    end
end

local function stepPadPressedLegacy(i) -- i = Force pad index 0-63 in STEP mode
    local row, col = math.floor(i / 8), i % 8
    if row < 4 then
        local bar = pairIndex * 2 + math.floor(row / 2)
        local step = (row % 2) * 8 + col + 1
        legacyTap(bar, step)
    elseif col < 4 then
        selectedPad = (7 - row) * 4 + col
        playSelectedPad(selectedLevel)
    else
        selectedLevel = (7 - row) * 4 + (col - 4) + 1
        playSelectedPad(selectedLevel)
    end
    paintStepLegacy()
end


-- ---- STEP SEQ from real note data (the library patch, spoof/stepsnap.c; FINDINGS 27) ------------------------------
-- The library reads the clip's notes whenever the script asks events.GetSize(), and answers the view the script
-- subscribes to (F0 7D 53 ...): per step of the selected pad, the velocity of its note (0 = none). The page then is the
-- real Force's (FINDINGS 23): rows 0-3 the 32 steps (an empty step the pad's colour, very dim; a set one its velocity
-- colour; the playhead green), rows 4-7 cols 0-3 the pads (selected: white), cols 4-7 the 16 velocity levels.
-- Without the library (hello not received) or off a drum track in bank A, the legacy page above is used.
local STEP_TICKS, STEP_ORIGIN = 240, 0   -- 16th note at 960 PPQ; STEP_ORIGIN: tick of step 1 of bar 1 in the event list
local DEFAULT_NOTES = {36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51} -- this kit's A01-A16 (device 2026-10-25: pads 1-3 hold 36-38)
local snap = {on = false, byCount = {}, want = 0, src = nil, hist = {}, view = "", lastReply = -100, maxEnd = 0}
local fixedLevel                          -- nil: a new step gets the velocity it was tapped with
local created = {}                        -- "note:tick" -> the encoded string CreateNoteEvent returned (to delete it)
local encCache, pendingDel = {}, {}       -- MPC's own strings for notes it already held (from the library)
local lastViewSent = -100

local function bankA()
    local ok, b = pcall(function() return mpc.controlSurface.pads.CurrentBank.value end)
    return not ok or b == nil or b == 0
end

-- drum track: the pad's note; melodic track: lane p is the chromatic note keyBase + p (no scale: not readable by scripts)
local function padNote(p)
    if not isDrumTrack() then return math.max(0, math.min(127, keyBase + p)) end
    return bankA() and DEFAULT_NOTES[p + 1] or nil
end

-- the answer for the list MPC counts now, else the latest one (right after an edit the count has moved on, the answer not yet)
local function snapRow() return snap.byCount[snap.want] or snap.last end

-- bar pairs the page can show: those holding notes, plus one more to add to (the clip's own length is not readable)
local function snapPairs() return math.floor((snap.maxEnd or 0) / (2 * BAR_TICKS)) + 2 end

-- The page is chosen when STEP SEQ is entered and stays until it is left (a page that flips between the two while you
-- work adds notes through MPC's step buttons): the real-data page whenever the library is there and the track is a
-- drum track in pad bank A; else the old one. In the real-data page a pad bank / track it cannot do shows a dim page.
local snapMode = false
local function useSnap() return snapMode end
local function snapSupported() return padNote(selectedPad) ~= nil end

local function put7(v, n) local t = {}; for i = n, 1, -1 do t[i] = v % 128; v = math.floor(v / 128) end; return string.char(table.unpack(t)) end
local function get7(b, i, n) local v = 0; for k = 0, n - 1 do v = v * 128 + (b:byte(i + k) or 0) end; return v end

local function windowStart() return STEP_ORIGIN + pairIndex * 2 * BAR_TICKS end

local function sendView(force)
    local note = padNote(selectedPad)
    if not note then return end
    local key = note .. ":" .. windowStart()
    if not force and key == snap.view and ticks - lastViewSent < 16 then return end
    if key ~= snap.view then snap.byCount, snap.last = {}, nil end     -- a row for another view must not show
    snap.view, lastViewSent = key, ticks
    sysex(string.char(0x7D, 0x53, 0x01, note) .. put7(windowStart(), 5) .. put7(STEP_TICKS, 2) .. string.char(32))
end

-- ask MPC for the list sizes: each call makes the library read the list and answer
local function pollEvents()
    local c = mpc.clipMatrix and mpc.clipMatrix.selectedClip and mpc.clipMatrix.selectedClip.events
    local q = mpc.currentSequence and mpc.currentSequence.events
    local nc = c and call(c.GetSize) or 0
    local nq = q and call(q.GetSize) or 0
    if nc ~= snap.lognc or nq ~= snap.lognq then
        snap.lognc, snap.lognq = nc, nq
        print(string.format("ForceLink: events: clip %d, sequence %d (pad %d = note %s, histogram %s)", nc, nq, selectedPad,
                            tostring(padNote(selectedPad)), (function() local t = {} for n, k in pairs(snap.hist or {}) do t[#t + 1] = n .. "x" .. k end return table.concat(t, " ") end)()))
    end
    if nc > 0 then snap.want, snap.src = nc, c
    elseif nq > 0 then snap.want, snap.src = nq, q
    else snap.want, snap.src = 0, c or q end
end

function ForceLink.HandleMIDISysex(b)
    if type(b) ~= "string" or b:byte(1) ~= 0x7D or b:byte(2) ~= 0x53 then return end
    local op = b:byte(3)
    if op == 0x03 then
        if not snap.on then print("ForceLink: stepsnap active (library version " .. tostring(b:byte(4)) .. ")") end
        snap.on = true
    elseif op == 0x02 then
        local count, ns = get7(b, 6, 3), b:byte(9) or 0
        local row = {}
        for i = 0, ns - 1 do row[i] = b:byte(10 + i) or 0 end
        snap.byCount[count] = row
        snap.last = row
        snap.lastReply = ticks
        snap.on = true
    elseif op == 0x05 then                       -- MPC's own encoded string of a note, asked for to delete it
        local note, tick, len = b:byte(4), get7(b, 5, 5), get7(b, 10, 3)
        local key = note .. ":" .. tick
        encCache[key] = b:sub(13, 12 + len)
        print(string.format("ForceLink: encoded note %d at %d: %d chars", note, tick, len))
        if pendingDel[key] then
            pendingDel[key] = nil
            local ok, err = pcall(snap.src.DeleteEvent, encCache[key])
            print(string.format("ForceLink: step - note %d at %d (MPC's string): %s %s", note, tick, tostring(ok), tostring(err)))
            encCache[key] = nil
            pollEvents()
        end
    elseif op == 0x04 then
        local h = {}
        local nn = b:byte(4) or 0
        for i = 0, nn - 1 do h[b:byte(5 + i * 3)] = get7(b, 6 + i * 3, 2) end
        snap.hist = h
        local t = {}
        for n, k in pairs(h) do t[#t + 1] = n .. "x" .. k end
        table.sort(t)
        print("ForceLink: notes in the list: " .. table.concat(t, " "))
        snap.maxEnd = get7(b, 5 + nn * 3, 5)
    end
end

local function velColour(v)           -- the real Force's step colour for a velocity 1-127 (8-bit values for setPad)
    local t = v / 127
    return (67 + 60 * t) * 2, 59 * (1 - t) * 2, 0
end

local function paintStepSnap()
    local row = snapRow() or {}
    local pads = mpc.controlSurface.pads
    local pr, pg, pb = hexRGB(call(pads.LightRGBColor, padIndex1(selectedPad)))
    if pr + pg + pb == 0 and pads[selectedPad] then pr, pg, pb = argb(pads[selectedPad].Colour.value) end
    if pr + pg + pb == 0 then pr, pg, pb = 160, 160, 160 end
    for st = 0, 31 do
        local v = row[st] or 0
        local r, g, b
        if v > 0 then r, g, b = velColour(v)
        else r, g, b = pr * 0.05, pg * 0.05, pb * 0.05 end
        setPad(st, r, g, b)
    end
    for p = 0, 15 do                                                  -- pads: rows 4-7, cols 0-3
        local prow, pcol = 7 - math.floor(p / 4), p % 4
        local r, g, b = hexRGB(call(pads.LightRGBColor, padIndex1(p)))
        if r + g + b == 0 and pads[p] then r, g, b = argb(pads[p].Colour.value) end
        if r + g + b == 0 then r, g, b = 160, 160, 160 end
        if p == selectedPad then r, g, b = 254, 254, 254 else r, g, b = r * 0.25, g * 0.25, b * 0.25 end
        setPad(prow * 8 + pcol, r, g, b)
    end
    for l = 1, 16 do                                                  -- levels: rows 4-7, cols 4-7
        local lrow, lcol = 7 - math.floor((l - 1) / 4), 4 + (l - 1) % 4
        local r, g, b = velColour(math.floor(127 * l / 16 + 0.5))
        if fixedLevel == l then r, g, b = 254, 254, 254 end
        setPad(lrow * 8 + lcol, r, g, b)
    end
    setLed(BTN.left, pairIndex > 0 and 1 or 0)
    setLed(BTN.right, pairIndex + 1 < snapPairs() and 1 or 0)
end

local function paintStep()
    if not snapMode then return paintStepLegacy() end
    if snapSupported() then return paintStepSnap() end
    for p = 0, 63 do setPad(p, 6, 6, 6) end                           -- not a drum track in bank A: nothing to edit here
    setLed(BTN.left, 0); setLed(BTN.right, 0)
end

-- tap on step st (0-31): add a note at that tick, or remove it
local function toggleStep(st, tapVel)
    local note, row = padNote(selectedPad), snapRow() or {}
    local tick = windowStart() + st * STEP_TICKS
    local had = (row[st] or 0) > 0
    local ev = snap.src
    if not ev then return end
    local key = note .. ":" .. tick
    if not had then
        local vel = fixedLevel and math.floor(127 * fixedLevel / 16 + 0.5) or math.max(1, tapVel or 100)
        local ok, enc = pcall(ev.CreateNoteEvent, note, tick, vel / 127, STEP_TICKS // 2, 100)
        print(string.format("ForceLink: step %d + note %d at %d vel %d: %s %s", st, note, tick, vel, tostring(ok), tostring(enc and #enc or enc)))
        if ok and type(enc) == "string" then created[key] = enc end
    elseif created[key] then
        local ok, err = pcall(ev.DeleteEvent, created[key])
        print(string.format("ForceLink: step %d - note %d at %d (ours): %s %s", st, note, tick, tostring(ok), tostring(err)))
        created[key] = nil
    elseif encCache[key] then
        local ok, err = pcall(ev.DeleteEvent, encCache[key])
        print(string.format("ForceLink: step %d - note %d at %d (MPC's string): %s %s", st, note, tick, tostring(ok), tostring(err)))
        encCache[key] = nil
    else                                          -- a note MPC already held: ask the library for its own string
        pendingDel[key] = ticks
        sysex(string.char(0x7D, 0x53, 0x06, note) .. put7(tick, 5) .. put7(STEP_TICKS, 2))
        print(string.format("ForceLink: step %d - note %d at %d: asked MPC's string", st, note, tick))
    end
    pollEvents()
    sendView(true)
end

-- Press(index, velocity) is documented to take the velocity; Tap(index, velocity) seemed to ignore it (all 16 levels
-- sounded alike), so the pad is pressed now and released on the next timer tick
local pendingRelease = {}
local ourHit            -- the pad number (1-based) we last pressed ourselves, to tell the Live III's own hits apart
local ourPressTick   -- timer tick of our last press: a last-hit change soon after it is ours, not the Live III's
local echoHit, echoTick     -- the pad number our press comes back as (see HandleMIDINoteOn)
local lastHitBase = 1    -- LastHitPad counts from 1, like Tap (checked against our own presses, see followLiveHit)
local function playSelected(level)
    local vel = math.max(1 / 127, math.min(1.0, level / 16))        -- a float 0-1 (ints above 1 all sounded as 1.0)
    if not isDrumTrack() then return end                     -- melodic lanes are not auditioned (no pad of the Live III is theirs)
    local pads = mpc.controlSurface.pads
    local idx = padIndex1(selectedPad)
    ourHit, ourPressTick = idx, ticks
    echoHit, echoTick = idx - 1, ticks                 -- MPC hands our own press back to the script as a pad note
    if pads.Press and pads.Release then
        call(pads.Press, idx, vel)
        pendingRelease[#pendingRelease + 1] = idx
    else
        call(pads.Tap, idx, vel)
    end
end

-- the Live III's last hit pad (MPC pads, 1-based like Tap): the page follows it, so notes played or overdubbed there show
local lastHit
local function followLiveHit()
    if not isDrumTrack() then return end
    local lh = call(mpc.controlSurface.pads.LastHitPad)
    if type(lh) ~= "number" then return end
    if lastHit == nil then lastHit = lh; return end
    if lh ~= lastHit then
        lastHit = lh
        local ours = ourPressTick ~= nil and ticks - ourPressTick <= 3
        if ours and ourHit then                                    -- our own press shows which base LastHitPad counts from
            if lh == ourHit then lastHitBase = 1 elseif lh == ourHit - 1 then lastHitBase = 0 end
            print("ForceLink: LastHitPad counts from " .. lastHitBase .. " (we pressed " .. ourHit .. ", it says " .. lh .. ")")
        end
        if ours then ourPressTick = nil end
        ourHit = nil
        local p = lh - lastHitBase
        if not ours and p >= 0 and p <= 15 and p ~= selectedPad and snapSupported() then
            selectedPad = p
            print("ForceLink: following the Live III's pad " .. lh)
            sendView(true)
            pollEvents()
        end
    end
end

local function stepPadPressed(i, vel)
    if not snapMode then return stepPadPressedLegacy(i) end
    local row, col = math.floor(i / 8), i % 8
    local rec, od = call(mpc.sequencer.IsRecording), mpc.sequencer.OverdubState and mpc.sequencer.OverdubState.value
    print(string.format("ForceLink: pad %d (row %d col %d) pressed at velocity %d; recording=%s overdub=%s playing=%s", i, row, col,
                        vel or -1, tostring(rec), tostring(od), tostring(call(mpc.sequencer.IsPlaying))))
    if not snapSupported() then
        if mpc.ui and mpc.ui.ShowTUIFeedback then mpc.ui.ShowTUIFeedback("Force STEP SEQ: drum track, pad bank A only") end
        return
    end
    if row < 4 then
        toggleStep(row * 8 + col, vel)
    elseif col < 4 then
        selectedPad = (7 - row) * 4 + col
        playSelected(selectedLevel)
        pollEvents()
        sendView(true)
    else
        local l = (7 - row) * 4 + (col - 4) + 1
        fixedLevel = (fixedLevel ~= l) and l or nil
        selectedLevel = l
        playSelected(l)
    end
    paintStep()
end


local function paintButtons()
    -- row launch: dim when the row has clips, bright when one of them plays; stop row: dim when the track has clips
    local rowState, colHas = {}, {}
    for row = 0, 7 do
        for col = 0, 7 do
            local s = clipStatus(col, row)
            if s > 0 then
                rowState[row] = math.max(rowState[row] or 1, s == 2 and 3 or 1)
                colHas[col] = math.max(colHas[col] or 1, s == 2 and 3 or 1)
            end
        end
    end
    for i = 0, 7 do
        setLed(LAUNCH0 + i, rowState[i] or 0)
        setLed(TRACKARM0 + i, colHas[i] or 0)
        setLed(TRACKSEL0 + i, colHas[i] and 1 or 0)
    end
    -- arrows: lit where the window can move
    if aperture then
        local function can(p) return p and p.value and 1 or 0 end
        setLed(BTN.up, can(aperture.CanMoveUp)); setLed(BTN.down, can(aperture.CanMoveDown))
        setLed(BTN.left, can(aperture.CanMoveLeft)); setLed(BTN.right, can(aperture.CanMoveRight))
    end
end

local function paintAll()
    if mode == "note" then return paintNote() end
    if mode == "step" then return paintStep() end
    for row = 0, 7 do for col = 0, 7 do paintPad(col, row) end end
    paintButtons()
end

-- pads go to the script (MATRIX, STEP SEQ) or, renumbered 36-99 by flsurf, to the selected track as plain notes (NOTE)
local function mapPads(toNotes)
    local m = dev.mappings
    if toNotes then
        for n = 36, PAD0 + PADS - 1 do if m.script.RemoveNoteToScript then m.script.RemoveNoteToScript(9, n) end end
        mapNoteLayout(isDrumTrack())
    else
        text("notemap off")
        if noteDrum then mapNoteLayout(false) end
        if noteDrum == false and m.script.RemoveNoteToScript then
            for n = PAD0, PAD0 + PADS - 1 do m.script.RemoveNoteToScript(15, n) end
        end
        noteDrum = nil
        for n = PAD0, PAD0 + PADS - 1 do m.script.AddNoteToScript(9, n) end
    end
end

-- Like the Force: STEP SEQ on an empty clip slot makes a clip there
local function ensureClip()
    local cm = mpc.clipMatrix
    if not cm or not cm.GetSelectedClipColumn or not cm.CreateClip then return end
    local col, row = call(cm.GetSelectedClipColumn), call(cm.GetSelectedClipRow)
    if type(col) ~= "number" or type(row) ~= "number" or col < 0 or row < 0 then return end
    if call(cm.HasClip, col, row) then return end
    local ok = call(cm.CreateClip, col, row)
    print(string.format("ForceLink: no clip at %d,%d: CreateClip -> %s", col, row, tostring(ok)))
    if ok ~= false then call(cm.SelectClip, col, row) end
end

local function setMode(newMode)
    if newMode == mode then return end
    if (newMode == "note") ~= (mode == "note") then mapPads(newMode == "note") end
    mode = newMode
    if mode == "step" then ensureClip() end
    snapMode = false
    if mode == "step" and snap.on and snapSupported() then
        snapMode = true
        -- the real-data page needs MPC in a pad mode, not its own step mode (there its pads are step buttons and
        -- its pad lights step lights): leave the mode alone, and start the conversation with the library
        snap.lastReply = ticks
        sendView(true)
        pollEvents()
    elseif mode == "step" and mpc.engineMode and mpc.engineMode.RequestModeChange then
        -- MPC's step sequencer only takes taps in its own mode: ask for it (name not documented; first that works)
        local cm = mpc.engineMode.CurrentMode
        for _, name in ipairs({"Step Sequencer", "Step Seq", "StepSequencer", "Step Sequence"}) do
            pcall(mpc.engineMode.RequestModeChange, name)
            if cm and tostring(cm.value):lower():find("step") then break end
        end
        print("ForceLink: step mode request -> CurrentMode " .. tostring(cm and cm.value))
    end
    led(BTN.matrix, mode == "matrix" and 3 or 1)
    led(BTN.note, mode == "note" and 3 or 1)
    led(BTN.step, mode == "step" and 3 or 1)
    padCache = {}
    paintAll()
end

local function listenGrid()
    if not clipProps then return end
    for col = 0, 7 do
        for row = 0, 7 do
            local p = clipProps[col] and clipProps[col][row]
            if p then
                p.Colour:listen(function() paintPad(col, row) end, deviceName)
                p.State:listen(function() paintPad(col, row) end, deviceName)
            end
        end
    end
end

-- Q-links: MPC has 16 in 4 banks of 4 (the Live III shows one bank on its 4 knobs). The Force shows 2 pages of 8,
-- independent of the Live III: page 1 = banks 1 + 2 (knobs 1-4, 5-8), page 2 = banks 3 + 4; KNOBS toggles.
local forcePage = 0
-- MPC's Q-link numbering: the Live III's knob j (0-3, left to right) in bank b is Q-link 4 * (3 - j) + b
-- (device, 2026-10-25: bank 1 = 12, 8, 4, 0; bank 2 = 13, 9, 5, 1)
local function knobBank(k) return forcePage * 2 + (k >= 4 and 1 or 0) end
local function knobQ(k) return 4 * (3 - k % 4) + knobBank(k) end
local function knobOf(i) for k = 0, 7 do if knobQ(k) == i then return k end end end
local qlinkHeld = false -- SHIFT + KNOBS holds the MPC's Q-LINK button until KNOBS is released

-- MPC Q-link index, drawn on the Force OLED of its knob as the real Force draws it (FINDINGS 20, 24): idle, the name
-- and a bar; while the knob is touched (and half a second after), the value in the large font and a thin bar. Pan-like
-- values ("C", "12L", "5R") get a bar from the centre.
local touched, valueUntil = {}, {}   -- per Force knob 0-7
local function isPan(v) return v == "C" or (v and v:match("^%d+[LR]$") ~= nil) end

local function showQLink(i)
    local k = knobOf(i)
    if not k then return end
    local q = mpc.controlSurface.qlinks[i]
    if not q then return end
    local name = q.TargetName.value
    local value = q.ValueString.value or ""
    local ok, pos = pcall(q.GetPosition)
    if not ok or type(pos) ~= "number" then pos = -1 end
    local bi = isPan(value) and 1 or 0
    if not name or name == "" then
        text(string.format("qname %d Q-LINK %d|-1|0", k + 1, knobBank(k) * 4 + k % 4 + 1)) -- no target: the Live's number
    elseif touched[k] or (valueUntil[k] or -1) >= ticks then
        text(string.format("qval %d %s|%d|%d", k + 1, value, math.floor(pos), bi))
    else
        text(string.format("qname %d %s|%d|%d", k + 1, name, math.floor(pos), bi))
    end
end

-- Force knob touch (notes 0x53-0x5A): value view while touched, half a second after release
local function knobTouch(k, down)
    touched[k] = down or nil
    if not down then valueUntil[k] = ticks + 2 end
    showQLink(knobQ(k))
end

local function bindKnobs()
    local m = dev.mappings.controlSurface.qlinks
    local n = mpc.controlSurface.qlinks.GetNumQLinks()
    for k = 0, 7 do
        if m.RemoveInfiniteCCToQLink then m.RemoveInfiniteCCToQLink(0, 0x10 + k) end
        -- MPC's own relative Q-link handling (as for a real Force); flsurf caps each step at 1
        if knobQ(k) < n then m.AddInfiniteCCToQLink(0, 0x10 + k, knobQ(k)) end
        if knobQ(k) < n then showQLink(knobQ(k)) end
    end
end

local function listenQLinks()
    local n = math.min(16, mpc.controlSurface.qlinks.GetNumQLinks())
    for i = 0, n - 1 do
        local q = mpc.controlSurface.qlinks[i]
        q.TargetName:listen(function() showQLink(i) end, deviceName)
        q.ValueString:listen(function()
            showQLink(i)
        end, deviceName)
    end
    bindKnobs()
end

-- SHIFT + STEP SEQ: log how a loaded sequence's events can be read (read-only), for the 32-step page
local function describe(v)
    if type(v) ~= "table" and type(v) ~= "userdata" then return type(v) .. " " .. tostring(v) end
    local names = {}
    local ok = pcall(function() for k, x in pairs(v) do names[#names + 1] = tostring(k) .. "=" .. type(x) end end)
    table.sort(names)
    return type(v) .. " " .. tostring(v) .. (ok and (" {" .. table.concat(names, ",") .. "}") or "")
end

-- Walk a GetState result (a table, or a Scripting::State userdata) into log lines, depth- and size-capped
local function dumpState(prefix, v, add, depth, budget)
    if budget.n <= 0 then return end
    if depth > 7 or (type(v) ~= "table" and type(v) ~= "userdata") then
        budget.n = budget.n - 1
        add(prefix .. "=" .. type(v) .. " " .. tostring(v))
        return
    end
    local keys = {}
    local ok = pcall(function() for k in pairs(v) do keys[#keys + 1] = k end end)
    if not ok or #keys == 0 then
        budget.n = budget.n - 1
        add(prefix .. "=" .. describe(v) .. " meta=" .. describe(getmetatable(v)))
        return
    end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    for _, k in ipairs(keys) do dumpState(prefix .. "." .. tostring(k), v[k], add, depth + 1, budget) end
end

-- #GetState on each object (MPC 3.10 registers it next to Help on every scriptable object); mpc.GetState is
-- the whole tree. The clipMatrix and project states include each track's events (Time, Note, Velocity, Length).
local function probeState(add)
    local targets = {
        {"seqEvents", mpc.currentSequence and mpc.currentSequence.events},
        {"clipEvents", mpc.clipMatrix and mpc.clipMatrix.selectedClip and mpc.clipMatrix.selectedClip.events},
        {"currentSequence", mpc.currentSequence},
        {"clipMatrix", mpc.clipMatrix},
        {"project", mpc.project},
    }
    for _, t in ipairs(targets) do
        local name, obj = t[1], t[2]
        if obj ~= nil then
            local okf, f = pcall(function() return obj["#GetState"] end)
            add(name .. "[#GetState]=" .. tostring(okf) .. " " .. type(f))
            if okf and type(f) == "function" then
                local ok, s = pcall(f)
                if not ok then ok, s = pcall(f, obj) end
                add(name .. " GetState ok=" .. tostring(ok) .. " " .. (ok and describe(s) or tostring(s)))
                if ok then dumpState("state." .. name, s, add, 0, {n = 400}) end
            end
        end
    end
    local okg, g = pcall(function() return mpc.GetState end)
    add("mpc.GetState=" .. tostring(okg) .. " " .. type(g))
end

local function probeEvents()
    local out = {}
    local function add(s) out[#out + 1] = s end
    probeState(add)
    for name, ev in pairs({sequence = mpc.currentSequence and mpc.currentSequence.events,
                           clip = mpc.clipMatrix and mpc.clipMatrix.selectedClip and mpc.clipMatrix.selectedClip.events}) do
        local ok, n = pcall(ev.GetSize)
        add(name .. ".GetSize()=" .. tostring(ok) .. ":" .. tostring(n))
        add(name .. ".#=" .. tostring(pcall(function() return #ev end) and #ev or "err"))
        for _, i in ipairs({0, 1, 2}) do add(name .. "[" .. i .. "]=" .. describe(ev[i])) end
        local okp, first = pcall(function() for k, v in pairs(ev) do if type(k) == "number" then return k .. ":" .. describe(v) end end end)
        add(name .. " first numeric key=" .. tostring(okp) .. " " .. tostring(first))
        -- container-style readers (sol2 containers: get/at/size, 1-based __index, ipairs) and list properties
        local mt = getmetatable(ev)
        add(name .. " metatable=" .. describe(mt))
        if type(mt) == "table" then add(name .. " metatable.__index=" .. describe(mt.__index)) end
        for _, f in ipairs({"get", "at", "Get", "GetEvent", "GetEventAt", "size", "Events", "events", "list", "List"}) do
            local okf, v = pcall(function() return ev[f] end)
            if okf and v ~= nil then
                add(name .. "." .. f .. "=" .. describe(v))
                if type(v) == "function" then
                    for _, arg in ipairs({0, 1}) do
                        local okc, r = pcall(v, arg)
                        add(name .. "." .. f .. "(" .. arg .. ")=" .. tostring(okc) .. " " .. (okc and describe(r) or tostring(r)))
                        local okm, rm = pcall(v, ev, arg)
                        add(name .. ":" .. f .. "(" .. arg .. ")=" .. tostring(okm) .. " " .. (okm and describe(rm) or tostring(rm)))
                    end
                end
            end
        end
        local oki, it = pcall(function() local n = 0; for i, v in ipairs(ev) do n = n + 1; if n == 1 then return i .. ":" .. describe(v) end end return "none" end)
        add(name .. " ipairs=" .. tostring(oki) .. " " .. tostring(it))
    end
    local pads = mpc.controlSurface.pads
    add("LastHitPad=" .. tostring(pads.LastHitPad and select(2, pcall(pads.LastHitPad))))
    add("GetCurrentBank=" .. tostring(pads.GetCurrentBank and select(2, pcall(pads.GetCurrentBank))))
    add("PlayheadPosition=" .. tostring(select(2, pcall(mpc.sequencer.PlayheadPosition))))
    add("GetLoopStart=" .. tostring(select(2, pcall(mpc.sequencer.GetLoopStart))))
    add("GetEndOrLoop=" .. tostring(select(2, pcall(mpc.sequencer.GetEndOrLoop))))
    add("song=" .. describe(mpc.song))
    add("project=" .. describe(mpc.project))
    add("currentTrack.GetType=" .. tostring(select(2, pcall(mpc.tracks.currentTrack.GetType))))
    add("GetTCPeriod=" .. tostring(select(2, pcall(mpc.sequencer.GetTCPeriod))))
    add("currentSequence=" .. describe(mpc.currentSequence))
    add("currentSequence metatable=" .. describe(getmetatable(mpc.currentSequence)))
    local okm, modes = pcall(mpc.engineMode.GetAvailableModes)
    if okm and type(modes) == "table" then
        local m = {}
        for _, v in pairs(modes) do m[#m + 1] = tostring(v) end
        add("modes=" .. table.concat(m, ","))
    end
    add("CurrentMode=" .. tostring(mpc.engineMode.CurrentMode and mpc.engineMode.CurrentMode.value))
    add("stepSequencer.GetCurrentMode=" .. tostring(select(2, pcall(mpc.controlSurface.stepSequencer.GetCurrentMode))))
    for _, line in ipairs(out) do print("ForceLink: probe " .. line) end
    mpc.ui.ShowTUIFeedback("ForceLink: event probe written to the log")
end

local function move(dir)
    if mode == "step" then                                   -- STEP SEQ: LEFT / RIGHT page through bar pairs
        if dir == "left" and pairIndex > 0 then pairIndex = pairIndex - 1 end
        local limit = useSnap() and snapPairs() or math.ceil(numBars() / 2)
        if dir == "right" and pairIndex + 1 < limit then pairIndex = pairIndex + 1 end
        if snap.on then sendView(true) end
        padCache = {}
        paintAll()
        return
    end
    if mode == "note" and noteDrum == false and (dir == "up" or dir == "down") then   -- melodic NOTE: octave
        local nb = keyBase + (dir == "up" and 12 or -12)
        if nb >= 0 and nb + 7 + 35 <= 127 then
            keyBase = nb
            heldPads = {}
            text("notemap keys " .. keyBase)
            paintNote()
            if mpc.ui and mpc.ui.ShowTUIFeedback then mpc.ui.ShowTUIFeedback("Force NOTE: octave " .. (math.floor(keyBase / 12) - 2)) end
        end
        return
    end
    if not aperture then return end
    local n = shift and 8 or 1
    local fn = ({up = "MoveUp", down = "MoveDown", left = "MoveLeft", right = "MoveRight"})[dir]
    local ok, err = pcall(aperture[fn], n)
    print(string.format("ForceLink: %s(%d) %s origin %s,%s", fn, n, ok and "ok" or ("failed: " .. tostring(err)),
                        tostring(aperture.GetOriginX and aperture.GetOriginX()), tostring(aperture.GetOriginY and aperture.GetOriginY())))
    padCache = {}
    paintAll()
end

function ForceLink.HandleMIDINoteOn(channel, note, velocity)
    if channel == 0 and note >= 0x53 and note <= 0x5A then return knobTouch(note - 0x53, velocity > 0) end
    if channel == 15 and note >= PAD0 and note < PAD0 + PADS then        -- melodic NOTE: flsurf's copy of a press
        heldPads[note - PAD0] = velocity > 0 or nil
        if mode == "note" then paintNote() end
        return
    end
    if channel == 9 and note >= PAD0 and note < PAD0 + PADS then
        local i = note - PAD0
        if mode == "step" then
            local echo = echoTick ~= nil and ticks - echoTick <= 2 and (i == echoHit or i == echoHit + 1)
            print(string.format("ForceLink: ch9 note %d (pad %d) velocity %d%s", note, i, velocity, echo and " [echo of our press, ignored]" or ""))
            if velocity > 0 and not echo then stepPadPressed(i, velocity) end
        elseif aperture then
            aperture.TriggerClip(i % 8, math.floor(i / 8))
        end
    elseif channel == 0 then
        if note == BTN.shift then shift = true
        elseif note == BTN.matrix then setMode("matrix")
        elseif note == BTN.note then setMode("note")
        elseif note == BTN.step and shift then probeEvents()
        elseif note == BTN.step then setMode("step")
        elseif note == BTN.knobs then
            if not shift then
                forcePage = 1 - forcePage                     -- KNOBS: Q-link page 1 (banks 1-2) <-> 2 (banks 3-4)
                bindKnobs()
            else
                -- SHIFT + KNOBS: try to open the MPC's Q-link mode menu (its Q-LINK button held); result logged
                local q = mpc.controlSurface.qlink
                local ok, err = false, "no qlink button"
                if q and q.Hold then ok, err = pcall(q.Hold) end
                qlinkHeld = ok
                print("ForceLink: qlink Hold " .. tostring(ok) .. " " .. tostring(err))
            end
        elseif note == BTN.play then mpc.sequencer.TogglePlay()
        elseif note == BTN.rec then mpc.sequencer.Record()
        elseif note == BTN.up then move("up") elseif note == BTN.down then move("down")
        elseif note == BTN.left then move("left") elseif note == BTN.right then move("right")
        elseif note >= LAUNCH0 and note < LAUNCH0 + 8 then if aperture then aperture.TriggerRow(note - LAUNCH0) end
        elseif note >= TRACKSEL0 and note < TRACKSEL0 + 8 then if aperture then aperture.SelectTrack(note - TRACKSEL0) end
        elseif note >= TRACKARM0 and note < TRACKARM0 + 8 then if aperture then aperture.StopClipsOnTrack(note - TRACKARM0) end
        end
    end
end

function ForceLink.HandleMIDINoteOff(channel, note, velocity)
    if channel == 0 and note >= 0x53 and note <= 0x5A then return knobTouch(note - 0x53, false) end
    if channel == 15 and note >= PAD0 and note < PAD0 + PADS then
        heldPads[note - PAD0] = nil
        if mode == "note" then paintNote() end
        return
    end
    if channel == 0 and note == BTN.shift then shift = false end
    if channel == 0 and note == BTN.knobs and qlinkHeld then
        local q = mpc.controlSurface.qlink
        if q and q.Release then pcall(q.Release) end
        qlinkHeld = false
    end
end

function ForceLink.HandleMIDICC(channel, cc, value) end


function ForceLink.HandleTimer()
    blinkOn = not blinkOn
    ticks = ticks + 1
    for i = #pendingRelease, 1, -1 do
        call(mpc.controlSurface.pads.Release, pendingRelease[i])
        table.remove(pendingRelease, i)
    end
    if snap.on then
        if mode == "step" and snapMode then
            followLiveHit()
            sendView(false)
            pollEvents()
        end
    elseif ticks % 16 == 0 then
        sendView(true)               -- the library's hello at MPC's start may have come before we listened: ask again
    end
    if aperture and not clipProps then
        -- window position, by whichever getter this firmware has (reported once in the log)
        for _, n in ipairs({"GetOriginX", "GetColumn", "GetX"}) do
            if aperture[n] then local ok, v = pcall(aperture[n]); if ok and type(v) == "number" then originCol = v; break end end
        end
        for _, n in ipairs({"GetOriginY", "GetRow", "GetY"}) do
            if aperture[n] then local ok, v = pcall(aperture[n]); if ok and type(v) == "number" then originRow = v; break end end
        end
    end
    if ticks % 8 == 0 then padCache = {}; ledCache = {} end -- resend everything every 2 s (the link may have reconnected)
    for k = 0, 7 do                                   -- value view ended: back to the name
        if valueUntil[k] and valueUntil[k] < ticks and not touched[k] then valueUntil[k] = nil; showQLink(knobQ(k)) end
    end
    if ticks % 16 == 0 then for k = 0, 7 do showQLink(knobQ(k)) end end -- and the OLEDs every 4 s
    paintAll()
end

-- one-time report of what the script found, to MPC's log (journalctl -u acvs) and screen
local function report()
    local parts = {"aperture=" .. tostring(aperture), "clipProps=" .. tostring(clipProps),
                   "dev.clipMatrixAperture=" .. tostring(dev.clipMatrixAperture),
                   "mpc.clipMatrix=" .. tostring(mpc.clipMatrix)}
    if mpc.clipMatrix then
        for k, v in pairs(mpc.clipMatrix) do parts[#parts + 1] = "clipMatrix." .. tostring(k) .. "=" .. type(v) end
    end
    if aperture then
        local names = {}
        for k, v in pairs(aperture) do names[#names + 1] = tostring(k) .. "=" .. type(v) end
        table.sort(names)
        parts[#parts + 1] = "aperture{" .. table.concat(names, ",") .. "}"
    end
    for name, obj in pairs({stepSequencer = stepSeq, padMappings = dev.mappings.pads, scriptMappings = dev.mappings.script}) do
        if obj then
            local names = {}
            for k, v in pairs(obj) do names[#names + 1] = tostring(k) .. "=" .. type(v) end
            table.sort(names)
            parts[#parts + 1] = name .. "{" .. table.concat(names, ",") .. "}"
        end
    end
    parts[#parts + 1] = "stepTap=" .. tostring(stepTap ~= nil) .. " stepBright=" .. tostring(stepBright ~= nil)
    -- member lists for the next steps (Q-link modes, per-quadrant colours, 32-step page), one log line each
    local function members(name, obj)
        if type(obj) ~= "table" then print("ForceLink: members " .. name .. " = " .. type(obj)); return end
        local names = {}
        for k, v in pairs(obj) do names[#names + 1] = tostring(k) .. "=" .. type(v) end
        table.sort(names)
        print("ForceLink: members " .. name .. " {" .. table.concat(names, ",") .. "}")
    end
    local cs = mpc.controlSurface
    members("mpc", mpc)
    members("controlSurface", cs)
    members("pads", cs.pads)
    members("pads[0]", cs.pads and cs.pads[0])
    members("qlinks", cs.qlinks)
    members("qlinks[0]", cs.qlinks and cs.qlinks[0])
    members("stepSequencerControl", mpc.stepSequencerControl)
    members("currentSequence", mpc.currentSequence)
    members("selectedClip", mpc.clipMatrix and mpc.clipMatrix.selectedClip)
    members("tracks.currentTrack", mpc.tracks and mpc.tracks.currentTrack)
    members("currentSequence.events", mpc.currentSequence and mpc.currentSequence.events)
    members("selectedClip.events", mpc.clipMatrix and mpc.clipMatrix.selectedClip and mpc.clipMatrix.selectedClip.events)
    members("force", mpc.force)
    members("controlSurface.qlink", cs.qlink)
    members("sequencer", mpc.sequencer)
    members("currentInstrument", mpc.tracks and mpc.tracks.currentTrack and mpc.tracks.currentTrack.currentInstrument)
    for p = 0, 3 do
        local ok, hex = pcall(cs.pads.LightRGBColor, p)
        print("ForceLink: pads.LightRGBColor(" .. p .. ") = " .. tostring(ok) .. " " .. tostring(hex))
    end
    -- how existing events are read: size, then whatever indexing gives
    local ev = mpc.currentSequence and mpc.currentSequence.events
    if ev then
        local ok, n = pcall(ev.GetSize)
        print("ForceLink: currentSequence.events.GetSize() = " .. tostring(ok) .. " " .. tostring(n))
        for _, i in ipairs({0, 1}) do
            local e = ev[i]
            print("ForceLink: events[" .. i .. "] = " .. type(e) .. " " .. tostring(e))
            if type(e) == "table" then members("events[" .. i .. "]", e) end
        end
    end
    local okp, pos = pcall(mpc.sequencer.PlayheadPosition)
    print("ForceLink: PlayheadPosition() = " .. tostring(okp) .. " " .. tostring(pos))
    for _, where in ipairs({"buttons", "button"}) do
        local b = cs[where]
        if type(b) == "table" then
            members("controlSurface." .. where, b)
            if b.GetButtons then
                local ok, list = pcall(b.GetButtons)
                local names = {}
                if ok and type(list) == "table" then for _, v in pairs(list) do names[#names + 1] = tostring(v) end end
                print("ForceLink: button names " .. table.concat(names, ","))
            end
        end
    end
    if mpc.clipMatrix and mpc.clipMatrix.Help then
        local ok, h = pcall(mpc.clipMatrix.Help)
        if ok and h then print("ForceLink: clipMatrix.Help " .. tostring(h)) end
    end
    if aperture and aperture.Help then
        local ok, h = pcall(aperture.Help)
        if ok and h then print("ForceLink: aperture.Help " .. tostring(h)) end
    end
    local p = clipProps and clipProps[0] and clipProps[0][0]
    if p then parts[#parts + 1] = "clip00 colour=" .. tostring(p.Colour.value) .. " state=" .. tostring(p.State.value) end
    print("ForceLink: " .. table.concat(parts, " "))
end

function ForceLink.OnLoad()
    if dev.mappings.script.SetHandleSysex then dev.mappings.script.SetHandleSysex(true) end      -- stepsnap's answers
    -- the step sequencer object and its tap / brightness functions (names checked at run time, logged by report())
    stepSeq = mpc.controlSurface.stepSequencer
    if stepSeq then
        for _, n in ipairs({"Tap", "Press", "PressButton", "TapButton"}) do
            if type(stepSeq[n]) == "function" then stepTap = stepSeq[n]; break end
        end
        for _, n in ipairs({"LightBrightness", "Brightness", "LightLevel"}) do
            if type(stepSeq[n]) == "function" then stepBright = stepSeq[n]; break end
        end
    end
    -- the per-device aperture: where exactly it lives is checked here and reported on the MPC screen
    aperture = dev.clipMatrixAperture or (mpc.clipMatrix and mpc.clipMatrix.clipMatrixAperture)
    -- 3.10.0.49: the per-device clip, scene and track properties hang off the aperture itself
    clipProps = (aperture and aperture.clipProperties) or dev.clipProperties or
                (mpc.clipMatrix and mpc.clipMatrix.clipProperties)
    mpc.ui.ShowTUIFeedback("ForceLink: aperture " .. (aperture and "ok" or "MISSING") ..
                           ", clips " .. (clipProps and "ok" or "MISSING"))
    if aperture then
        aperture.SetColumns(8)
        aperture.SetRows(8)
        aperture.SetLabel("Force")
        if aperture.SetFollowsTUI then pcall(aperture.SetFollowsTUI, false) end -- our own window, not the screen's
        aperture.Show()
    end
    for n = PAD0, PAD0 + PADS - 1 do dev.mappings.script.AddNoteToScript(9, n) end
    -- every button note and the encoder go to the script, so nothing but NOTE-mode pads reaches the track as MIDI
    for n = 0, 127 do dev.mappings.script.AddNoteToScript(0, n) end
    dev.mappings.script.AddCCToScript(0, 0x64)
    for i = 0, 7 do
        dev.mappings.script.AddNoteToScript(0, LAUNCH0 + i)
        dev.mappings.script.AddNoteToScript(0, TRACKSEL0 + i)
        dev.mappings.script.AddNoteToScript(0, TRACKARM0 + i)
    end
    listenGrid()
    listenQLinks()
    led(BTN.play, mpc.sequencer.Playing.value and 3 or 1)
    mpc.sequencer.Playing:listen(function(p) led(BTN.play, p and 3 or 1) end, deviceName)
    led(BTN.matrix, 3); led(BTN.note, 1); led(BTN.step, 1)
    paintAll()
    report()
    dev.timer.Start(250)
end

function ForceLink.OnUnload()
    dev.timer.Stop()
    mpc.sequencer.Playing:stopListening(deviceName)
    for row = 0, 7 do for col = 0, 7 do
        sysex(string.char(0x47, 0x7F, 0x40, 0x65, 0x00, 0x04, row * 8 + col, 0, 0, 0))
    end end
    if aperture then aperture.Hide() end
end
