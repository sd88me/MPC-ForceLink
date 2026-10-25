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
                              Twist = function(d, fine) twists[#twists + 1] = i .. ":" .. d end} end

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
        script = {AddNoteToScript = function(ch, n) padmap[n] = "script" end, AddCCToScript = function(ch, cc) calls[#calls + 1] = "cc " .. cc end,
                  RemoveNoteToScript = function(ch, n) padmap[n] = nil end},
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
check("Force knobs CC 0x10..0x17 go to the script", has(calls, "cc 16") and has(calls, "cc 23"))
check("OLED text sent for Q-link 1", has(sent, string.char(0x7D, 0x46) .. "qlink 1 Q0|0|0"))

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
qlinks[4].ValueString:set("50%")                       -- bank 1, knob 2 (column-major numbering)
check("Q-link value change redraws only the bar of OLED 2", has(sent, string.char(0x7D, 0x46) .. "bar 2 Q4 32"))
-- Force knob turns: capped steps on the knob's Q-link; knobs 5-8 are the next bank (Q-links 4-7 from bank 0)
ForceLink.HandleMIDICC(0, 0x10, 1); ForceLink.HandleMIDICC(0, 0x14, 0x7F); ForceLink.HandleMIDICC(0, 0x10, 40)
check("knob 1 twists Q-link 0 by +1, knob 5 Q-link 1 (next bank) by -1, fast turn capped at 3", twists[1] == "0:1" and twists[2] == "1:-1" and twists[3] == "0:3")
-- a Live III knob in bank 4 changes (knob 1 there = Q-link 3): the Force follows, knob 1 = Q-link 3, knob 2 = 7,
-- knobs 5-8 = bank 1 (Q-links 0, 4, 8, 12)
sent = {}
qlinks[3].ValueString:set("9.7")
check("Live III knob in bank 4: Force knobs 1-4 = its knobs", has(sent, string.char(0x7D, 0x46) .. "qlink 1 Q3|9.7|24")
      and has(sent, string.char(0x7D, 0x46) .. "qlink 2 Q7|7|56") and has(sent, string.char(0x7D, 0x46) .. "qlink 5 Q0|0|0"))
twists = {}; ForceLink.HandleMIDICC(0, 0x10, 1)
check("then knob 1 turns Q-link 3", twists[1] == "3:1")

-- NOTE mode: pads go to MPC pad quadrants; bottom-left Force pad (row 7 col 0) = MPC pad 0, bottom-left quadrant
sent, calls = {}, {}
ForceLink.HandleMIDINoteOn(0, 0x04, 127)
check("NOTE: flsurf note layout switched on", has(sent, string.char(0x7D, 0x46) .. "notemap on"))
check("NOTE: pad notes no longer go to the script", padmap[54] == nil and padmap[54 + 56] == nil and padmap[40] == nil)
check("NOTE: bank A pad 1 green bottom-left, bank B dimmer", has(sent, padMsg(56, 0, 127, 0)) and has(sent, padMsg(60, 0, 44, 0)))
check("NOTE: NOTE LED lit, MATRIX dim", has(sent, "CC 4 3") and has(sent, "CC 3 1"))

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
check("NOTE drum: bank B pad 1 shows virtual pad 17 (red), A1 stays green", has(sent, padMsg(60, 127, 0, 0))
      and not has(sent, string.char(0x47, 0x7F, 0x40, 0x65, 0x00, 0x04, 56)))
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

-- KNOBS steps the Q-link bank; SHIFT + KNOBS holds the MPC Q-LINK button
local qbtn = {}
mpc.controlSurface.qlink = {Hold = function() qbtn[#qbtn + 1] = "hold" end, Release = function() qbtn[#qbtn + 1] = "release" end}
sent, calls = {}, {}
ForceLink.HandleMIDINoteOn(0, 0x01, 127); ForceLink.HandleMIDINoteOff(0, 0x01, 0)
check("KNOBS steps the bank (4 -> 1): knob 1 = Q-link 0, knob 2 = Q-link 4, knob 5 = Q-link 1", has(sent, string.char(0x7D, 0x46) .. "qlink 1 Q0|0|0")
      and has(sent, string.char(0x7D, 0x46) .. "qlink 2 Q4|50%|32") and has(sent, string.char(0x7D, 0x46) .. "qlink 5 Q1|1|8"))
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
