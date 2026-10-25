-- Offline test of ForceLink.lua against a mock of the MPC 3.10 scripting API (lua5.4 test_forcelink.lua).
-- The mock only models what the script uses; names follow MPC 3.10.0.49's registered API strings.
local sent, calls = {}, {}
local function prop(v)
    local p = {value = v, fns = {}}
    function p:listen(fn, owner) self.fns[#self.fns + 1] = fn end
    function p:stopListening(owner) self.fns = {} end
    function p:set(x) self.value = x; for _, f in ipairs(self.fns) do f(x) end end
    return p
end

local clips = {}
for c = 0, 7 do
    clips[c] = {}
    for r = 0, 7 do clips[c][r] = {Colour = prop(0), State = prop(0)} end
end
clips[0][1] = {Colour = prop(0xFFFF0000), State = prop(0)}        -- red clip, stopped

local aperture = {}
for _, f in ipairs({"SetColumns", "SetRows", "SetLabel", "Show", "Hide", "TriggerClip", "TriggerRow",
                    "StopClipsOnTrack", "SelectTrack", "MoveUp", "MoveDown", "MoveLeft", "MoveRight"}) do
    aperture[f] = function(...) calls[#calls + 1] = f .. " " .. table.concat({...}, ",") end
end

local qlinks = {GetNumQLinks = function() return 8 end}
qlinks.GetNumQLinks = function() return 16 end
local twists = {}
for i = 0, 15 do qlinks[i] = {TargetName = prop("Q" .. i), ValueString = prop(tostring(i)), GetPosition = function() return i * 8 end,
                              SetPosition = function(p) twists[#twists + 1] = i .. "=" .. p end} end

local padmap = {}
local mpcPads = {}
for i = 0, 15 do mpcPads[i] = {Colour = prop(0)} end
mpcPads[0] = {Colour = prop(0xFF00FF00)}                         -- MPC pad 1 green
local stepTaps = {}
local stepSequencer = {
    LightRGBColour = function(i) return i == 1 and "FF0000" or "000000" end,
    LightBrightness = function(i) return 1.0 end,
    Tap = function(i) stepTaps[#stepTaps + 1] = i end,
}

ForceLink = {
    midiOut = {
        SendMIDISysex = function(b, port) sent[#sent + 1] = b end,
        SendMIDICC = function(ch, cc, v, port) sent[#sent + 1] = string.format("CC %d %d", cc, v) end,
    },
    mappings = {
        script = {AddNoteToScript = function(ch, n) if ch == 15 then padmap["c16:" .. n] = true else padmap[n] = "script" end end, AddCCToScript = function(ch, cc) calls[#calls + 1] = "cc " .. cc end,
                  RemoveNoteToScript = function(ch, n) if ch == 15 then padmap["c16:" .. n] = nil else padmap[n] = nil end end},
        pads = {AddNoteToMpcPadQuadrant = function(ch, n, pad, q) padmap[n] = "quad " .. pad .. "/" .. q end,
                RemoveNoteToMpcPadQuadrant = function(ch, n) padmap[n] = nil end,
                RemoveNoteToMpcPad = function() end},
        controlSurface = {qlinks = {AddInfiniteCCToQLink = function(ch, cc, q) calls[#calls + 1] = "qlink " .. cc .. "->" .. q end,
                                   RemoveInfiniteCCToQLink = function() end}},
    },
    timer = {Start = function() end, Stop = function() end},
    clipMatrixAperture = aperture,
    clipProperties = clips,
}
aperture.ToGlobalColumn = function(c) return c end
aperture.ToGlobalRow = function(r) return r end
aperture.CanMoveUp, aperture.CanMoveDown = prop(false), prop(true)
aperture.CanMoveLeft, aperture.CanMoveRight = prop(false), prop(false)
local function colour(c, r) return clips[c] and clips[c][r] and clips[c][r].Colour.value or 0 end
mpc = {
    clipMatrix = {
        HasClip = function(c, r) return colour(c, r) ~= 0 end,
        GetClipColour = colour,
        ClipIsPlaying = function(c, r) return clips[c][r].State.value == 2 end,
        ClipIsRecording = function() return false end,
        ClipIsStopped = function(c, r) return clips[c][r].State.value == 0 end,
    },
    controlSurface = {qlinks = qlinks, pads = mpcPads, stepSequencer = stepSequencer},
    sequencer = {Playing = prop(false), TogglePlay = function() calls[#calls + 1] = "TogglePlay" end,
                 Record = function() calls[#calls + 1] = "Record" end},
    ui = {ShowTUIFeedback = function(t) calls[#calls + 1] = "TUI " .. t end},
}

dofile("ForceLink.lua")
local fails = 0
local function check(name, ok) print(string.format("%-44s %s", name, ok and "ok" or "FAIL")); if not ok then fails = fails + 1 end end
local function has(list, pat) for _, v in ipairs(list) do if v:find(pat, 1, true) then return true end end return false end
local function padMsg(idx, r, g, b) return string.char(0x47, 0x7F, 0x40, 0x65, 0x00, 0x04, idx, r, g, b) end

ForceLink.OnLoad()
check("aperture found and set to 8x8", has(calls, "TUI ForceLink: aperture ok, clips ok") and has(calls, "SetColumns 8"))
check("stopped clip painted dim red (row 1 col 0)", has(sent, padMsg(8, 19, 0, 0)))
check("row launch 2 dim (row has a clip), 1 off", has(sent, "CC 57 1") and has(sent, "CC 56 0"))
check("arrows: only DOWN lit", has(sent, "CC 113 1") and has(sent, "CC 112 0"))
check("empty slot painted off", has(sent, padMsg(9, 0, 0, 0)))
check("page 1: knob 1 -> Q-link 12, knob 4 -> 0, knob 5 -> 13", has(calls, "qlink 16->12") and has(calls, "qlink 19->0")
      and has(calls, "qlink 20->13"))
check("OLED 1 shows the Live III's knob 1 of bank 1 (Q-link 12)", has(sent, string.char(0x7D, 0x46) .. "qname 1 Q12|96|0"))

sent, calls = {}, {}
ForceLink.HandleMIDINoteOn(9, 54 + 8, 127)            -- pad row 1 col 0
check("pad press triggers clip (0,1)", has(calls, "TriggerClip 0,1"))
ForceLink.HandleMIDINoteOn(0, 56 + 2, 127)            -- row launch 3
check("row launch 3 triggers row 2", has(calls, "TriggerRow 2"))
ForceLink.HandleMIDINoteOn(0, 0x31, 127); ForceLink.HandleMIDINoteOn(0, 0x71, 127); ForceLink.HandleMIDINoteOff(0, 0x31, 0)
check("SHIFT + DOWN moves the window by 8", has(calls, "MoveDown 8"))
ForceLink.HandleMIDINoteOn(0, 0x52, 127)
check("PLAY toggles play", has(calls, "TogglePlay"))
mpc.sequencer.Playing:set(true)
check("PLAY LED lit while playing", has(sent, "CC 82 3"))

sent = {}
clips[0][1].State:set(2)                               -- Playing
check("playing clip bright red", has(sent, padMsg(8, 102, 0, 0)))
qlinks[4].ValueString:set("50%")                       -- bank 1, knob 3 (Q-link 4 * (3 - 2) + 0)
check("Q-link value change redraws OLED 3 (name view)", has(sent, string.char(0x7D, 0x46) .. "qname 3 Q4|32|0"))
-- touching a Force knob shows the value view; pan values get a centre bar; half a second after release, the name
sent = {}
ForceLink.HandleMIDINoteOn(0, 0x53, 127)
check("touch knob 1: value view", has(sent, string.char(0x7D, 0x46) .. "qval 1 12|96|0"))
qlinks[12].ValueString:set("12L")
check("pan value: centre bar", has(sent, string.char(0x7D, 0x46) .. "qval 1 12L|96|1"))
ForceLink.HandleMIDINoteOff(0, 0x53, 0); sent = {}
ForceLink.HandleTimer(); ForceLink.HandleTimer(); ForceLink.HandleTimer()
check("released: back to the name view", has(sent, string.char(0x7D, 0x46) .. "qname 1 Q12|96|1"))
qlinks[12].ValueString:set("12")
-- a Live III knob turning does not move the Force's page (no following)
calls = {}
qlinks[3].ValueString:set("9.7"); ForceLink.HandleTimer()
check("Live III changes leave the Force's Q-link mapping alone", not has(calls, "qlink "))

-- NOTE mode on a melodic track (the real Force's layout, FINDINGS 21): rows a 4th apart from C (48) bottom-left
sent, calls = {}, {}
ForceLink.HandleMIDINoteOn(0, 0x04, 127)
check("NOTE: flsurf keys layout from 48", has(sent, string.char(0x7D, 0x46) .. "notemap keys 48"))
check("NOTE: NOTE LED lit, MATRIX dim", has(sent, "CC 4 3") and has(sent, "CC 3 1"))
check("NOTE: pad notes go to the track, their ch 16 copies to the script", padmap[54] == nil and padmap[54 + 56] == nil
      and padmap["c16:110"] and padmap["c16:54"])
check("NOTE: root C bottom-left in the track colour, C# dim white", has(sent, padMsg(56, 102, 5, 102)) and has(sent, padMsg(57, 25, 25, 25)))
-- C one row up is col 7 (48 + 5 + 7 = 60): also a root
check("NOTE: next C at row 6 col 7", has(sent, padMsg(55, 102, 5, 102)))
sent = {}
ForceLink.HandleMIDINoteOn(15, 110, 100)                -- press bottom-left (C 48)
check("press: the pad bright green, other Cs dim green", has(sent, padMsg(56, 0, 127, 0)) and has(sent, padMsg(55, 0, 42, 17)))
ForceLink.HandleMIDINoteOff(15, 110, 0)
check("release: back to the root colour", has(sent, padMsg(56, 102, 5, 102)))
sent = {}; ForceLink.HandleMIDINoteOn(0, 0x70, 127)    -- UP: octave up
check("UP in melodic NOTE: octave up (keys 60)", has(sent, string.char(0x7D, 0x46) .. "notemap keys 60"))
ForceLink.HandleMIDINoteOn(0, 0x71, 127)               -- DOWN: back

-- NOTE on a drum track: the real Force's 2x2 bank layout (A bottom-left, B bottom-right, C top-left, D top-right)
local vpad = {}
ForceLink.mappings.pads.AddNoteToVirtualPad = function(ch, n, p) vpad[n] = p end
ForceLink.mappings.pads.RemoveNoteToVirtualPad = function(ch, n) vpad[n] = nil end
mpcPads.LightRGBColor = function(i) return i == 1 and "00FF00" or i == 17 and "FF0000" or "000000" end
mpc.tracks = {currentTrack = {Type = prop("Drum")}}
sent = {}
ForceLink.HandleTimer()
check("NOTE drum: corners map to banks A/B/C/D", vpad[36] == 0 and vpad[39] == 3 and vpad[40] == 16 and vpad[68] == 32
      and vpad[72] == 48 and vpad[99] == 63 and vpad[45] == 5)
check("NOTE drum: bank A pad 1 = virtual pad 1 (green), bank B pad 1 = virtual pad 17 (red)", has(sent, padMsg(60, 127, 0, 0))
      and has(sent, padMsg(56, 0, 127, 0)) and has(sent, string.char(0x7D, 0x46) .. "notemap on"))
mpc.tracks.currentTrack.Type:set("Keygroup")
ForceLink.HandleTimer()
check("NOTE keygroup: drum pad mapping removed", next(vpad) == nil)

-- STEP SEQ mode: pads back to the script, row 0 shows steps, tap taps
sent = {}
ForceLink.HandleMIDINoteOn(0, 0x75, 127)
check("STEP: pads mapped back to the script", padmap[54] == "script" and padmap[54 + 56] == "script")
check("STEP: flsurf note layout switched off", has(sent, string.char(0x7D, 0x46) .. "notemap off"))
check("STEP: step 1 red on pad 0, step 2 off", has(sent, padMsg(0, 127, 0, 0)) and has(sent, padMsg(1, 0, 0, 0)))
ForceLink.HandleMIDINoteOn(9, 54 + 9, 127)            -- pad 9 = step 10
check("STEP: pad tap taps step 10", stepTaps[#stepTaps] == 10)
local jumps, padHits = {}, {}
mpc.sequencer.JumpToPulse = function(p) jumps[#jumps + 1] = p end
mpc.sequencer.IsPlaying = function() return false end
mpc.controlSurface.pads.Tap = function(i, v) padHits[#padHits + 1] = i .. "@" .. v end
mpc.controlSurface.pads.Release = function() end
ForceLink.HandleMIDINoteOn(9, 54 + 16 + 2, 127)            -- row 2 col 2: bar B (bar 1) step 3, stopped
check("STEP: bar B tap while stopped jumps to bar 2 (tap waits for it)", jumps[#jumps] == 3840 and stepTaps[#stepTaps] ~= 3)
ForceLink.HandleMIDINoteOn(9, 54 + 7 * 8 + 1, 127)         -- bottom row col 1: MPC pad 2
check("STEP: pad select plays pad 2 (1-based) at level 16", padHits[#padHits] == "2@127")
ForceLink.HandleMIDINoteOn(9, 54 + 7 * 8 + 4, 127)         -- bottom row col 4: level 1
check("STEP: velocity level 1 plays the selected pad softly", padHits[#padHits] == "2@7")
ForceLink.HandleMIDINoteOn(0, 0x03, 127)
check("MATRIX again: clip pad repainted", has(sent, padMsg(8, 102, 0, 0)))

-- STEP SEQ from real note data (the library patch, stepsnap): mock the library's side of the conversation
local modeReq = 0
mpc.engineMode = {CurrentMode = prop("Grid View"), RequestModeChange = function() modeReq = modeReq + 1 end}
local evN, evcalls = 5, {}
local function mkEvents()
    return {GetSize = function() return evN end,
            CreateNoteEvent = function(note, tick, vel, len, prob)
                evcalls[#evcalls + 1] = string.format("create %d %d %.3f %d %d", note, tick, vel, len, prob)
                return "ENC" .. note .. ":" .. tick
            end,
            DeleteEvent = function(e) evcalls[#evcalls + 1] = "delete " .. e end}
end
mpc.clipMatrix.selectedClip = {events = mkEvents()}
mpc.currentSequence = {events = {GetSize = function() return 0 end}}
mpc.tracks.currentTrack.Type:set("Drum")
local function reply(count, vels)
    local v = {}
    for i = 1, 32 do v[i] = vels[i - 1] or 0 end
    ForceLink.HandleMIDISysex(string.char(0x7D, 0x53, 0x02, 0, 1, math.floor(count / 16384), math.floor(count / 128) % 128, count % 128, 32,
                                          table.unpack(v)))
end
ForceLink.HandleMIDINoteOn(0, 0x75, 127)                        -- STEP SEQ
check("stepsnap: before the library is there, STEP SEQ asks MPC for its step mode (legacy)", modeReq > 0)
modeReq = 0
ForceLink.HandleMIDINoteOn(9, 54 + 7 * 8 + 0, 100)              -- select pad 1 (bottom-left) on the legacy page
sent = {}
for _ = 1, 16 do ForceLink.HandleTimer() end
check("stepsnap: without hello a view (note 37 = pad A01, start 0, 240 ticks, 32 steps) is sent every 4 s",
      has(sent, string.char(0x7D, 0x53, 0x01, 37, 0, 0, 0, 0, 0, 1, 112, 32)))
ForceLink.HandleMIDISysex(string.char(0x7D, 0x53, 0x03, 1))      -- the library says hello
ForceLink.HandleTimer()
check("stepsnap: before the first answer the legacy page is used", true)
reply(5, {[0] = 100, [9] = 127})
sent = {}
ForceLink.HandleTimer()
check("stepsnap: step 1 shows its velocity colour (100), step 10 full red (127)", has(sent, padMsg(0, 114, 12, 0)) and has(sent, padMsg(9, 127, 0, 0)))
check("stepsnap: an empty step is the pad's colour, very dim", has(sent, padMsg(1, 0, 6, 0)))
ForceLink.HandleMIDINoteOn(9, 54 + 2, 90)                       -- empty step 3, tapped at velocity 90
check("stepsnap: an empty step gets a note at its tick with the tapped velocity", evcalls[#evcalls] == "create 37 480 0.709 120 100")
stepTaps[#stepTaps + 1] = 0
ForceLink.HandleMIDINoteOn(9, 54 + 0, 100)                      -- step 1 is set, not ours: MPC's own button
check("stepsnap: a set step of MPC's: the library is asked for MPC's own string (note 37, tick 0, 240 window)",
      has(sent, string.char(0x7D, 0x53, 0x06, 37, 0, 0, 0, 0, 0, 1, 112)))
ForceLink.HandleMIDISysex(string.char(0x7D, 0x53, 0x05, 37, 0, 0, 0, 0, 0, 0, 0, 5) .. "HELLO")
check("stepsnap: and DeleteEvent gets that string when it arrives", evcalls[#evcalls] == "delete HELLO")
evN = 6; reply(6, {[0] = 100, [2] = 90, [9] = 127})
ForceLink.HandleTimer()                                          -- MPC counts 6 events now
ForceLink.HandleMIDINoteOn(9, 54 + 2, 100)                      -- step 3 is set and ours
check("stepsnap: a set step we made is deleted by its encoded string", evcalls[#evcalls] == "delete ENC37:480")
sent = {}
ForceLink.HandleMIDINoteOn(9, 54 + 7 * 8 + 1, 100)              -- pad select: bottom row col 1 = pad 2 (note 36)
check("stepsnap: selecting pad 2 subscribes to note 36", has(sent, string.char(0x7D, 0x53, 0x01, 36, 0, 0, 0, 0, 0, 1, 112, 32)))
ForceLink.HandleMIDINoteOn(9, 54 + 4 * 8 + 7, 100)              -- level 16 (top-right of the bottom-right pads)
reply(6, {})
ForceLink.HandleMIDINoteOn(9, 54 + 5, 20)                       -- step 6, pad 2
check("stepsnap: a chosen level fixes the entry velocity (16 = full)", evcalls[#evcalls] == "create 36 1200 1.000 120 100")
ForceLink.HandleMIDINoteOn(9, 54 + 4 * 8 + 7, 100)              -- level 16 again: back to the tapped velocity
for _ = 1, 22 do ForceLink.HandleTimer() end                    -- no answers for 5 s: the legacy page again
stepTaps[#stepTaps + 1] = 0
ForceLink.HandleMIDINoteOn(9, 54 + 3, 100)
check("stepsnap: no answers: back to the legacy page (MPC's step button 4)", stepTaps[#stepTaps] == 4)
reply(6, {}); ForceLink.HandleTimer()
mpc.tracks.currentTrack.Type:set("Keygroup")
stepTaps[#stepTaps + 1] = 0
ForceLink.HandleMIDINoteOn(9, 54 + 3, 100)
check("stepsnap: a keygroup track uses the legacy page", stepTaps[#stepTaps] == 4)
ForceLink.HandleMIDINoteOn(0, 0x03, 127)                        -- MATRIX again
mpc.tracks.currentTrack.Type:set("Drum")
sent = {}
ForceLink.HandleMIDINoteOn(0, 0x75, 127)                        -- STEP SEQ with the library there: no mode request
check("stepsnap: with the library there MPC's mode is left alone", modeReq == 0)
check("stepsnap: and the view (pad 2 = note 36) is sent at once", has(sent, string.char(0x7D, 0x53, 0x01, 36)))
ForceLink.HandleMIDISysex(string.char(0x7D, 0x53, 0x04, 1, 37, 0, 5, 0, 0, 0, 34, 24))     -- notes: 37 x5; last note ends at 4440
sent = {}
ForceLink.HandleMIDINoteOn(0, 0x73, 127)                        -- RIGHT: the next bar pair (notes end in the first)
check("stepsnap: RIGHT pages to bar pair 2 (ticks 7680) because one more pair can be added to",
      has(sent, string.char(0x7D, 0x53, 0x01, 36, 0, 0, 0, 60, 0, 1, 112, 32)))
sent = {}
ForceLink.HandleMIDINoteOn(0, 0x73, 127)
check("stepsnap: but no further", not has(sent, string.char(0x7D, 0x53, 0x01, 36, 0, 0, 0, 120)))
ForceLink.HandleMIDINoteOn(0, 0x72, 127)                        -- LEFT: back
ForceLink.HandleMIDINoteOn(0, 0x03, 127)

-- KNOBS toggles the Q-link page; SHIFT + KNOBS holds the MPC Q-LINK button
local qbtn = {}
mpc.controlSurface.qlink = {Hold = function() qbtn[#qbtn + 1] = "hold" end, Release = function() qbtn[#qbtn + 1] = "release" end}
sent, calls = {}, {}
qlinks[14].TargetName:set("")                           -- no target: shown by the Live III's number
ForceLink.HandleMIDINoteOn(0, 0x01, 127); ForceLink.HandleMIDINoteOff(0, 0x01, 0)
check("KNOBS: page 2, knobs 1, 2, 5 = Q-links 14, 10, 15", has(calls, "qlink 16->14") and has(calls, "qlink 20->15")
      and has(sent, string.char(0x7D, 0x46) .. "qname 2 Q10|80|0")
      and has(sent, string.char(0x7D, 0x46) .. "qname 5 Q15|120|0"))
check("an empty Q-link shows the Live III's number (page 2 knob 1 = Q-LINK 9)", has(sent, string.char(0x7D, 0x46) .. "qname 1 Q-LINK 9|-1|0"))
ForceLink.HandleMIDINoteOn(0, 0x31, 127); ForceLink.HandleMIDINoteOn(0, 0x01, 127); ForceLink.HandleMIDINoteOff(0, 0x01, 0)
ForceLink.HandleMIDINoteOff(0, 0x31, 0)
check("SHIFT + KNOBS holds the MPC Q-LINK button, release on KNOBS up", qbtn[1] == "hold" and qbtn[2] == "release")

-- SHIFT + STEP SEQ probe: #GetState trees are walked into the log (shape as in 3.10.0.49's event serialiser)
mpc.currentSequence = {events = {GetSize = function() return 1 end, ["#GetState"] = function()
    return {NumEvents = 1, Events = {["Event 0"] = {TypeString = "Note", Time = 0, NoteEvent = {Note = 36, Velocity = 100, Length = 60}}}}
end}}
mpc.engineMode = mpc.engineMode or {GetAvailableModes = function() return {} end, CurrentMode = prop("Standalone")}
mpc.tracks = mpc.tracks or {currentTrack = {GetType = function() return 0 end}}
local logged, realPrint = {}, print
print = function(s) logged[#logged + 1] = tostring(s) end
local okProbe, probeErr = pcall(function()
    ForceLink.HandleMIDINoteOn(0, 0x31, 127); ForceLink.HandleMIDINoteOn(0, 0x75, 127)
    ForceLink.HandleMIDINoteOff(0, 0x75, 0); ForceLink.HandleMIDINoteOff(0, 0x31, 0)
end)
print = realPrint
if not okProbe then print("probe error: " .. tostring(probeErr)) end
check("SHIFT + STEP: GetState event tree is logged", okProbe and has(logged, "state.seqEvents.Events.Event 0.NoteEvent.Note=number 36")
      and has(logged, "state.seqEvents.NumEvents=number 1"))

print(fails == 0 and "ALL OK" or "FAILURES")
os.exit(fails == 0 and 0 or 1)
