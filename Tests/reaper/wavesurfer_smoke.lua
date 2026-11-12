-- REAPER smoke test for WaveSurfer: load VST3 + AU, open the windows, render audio through them.
-- Run:  REAPER -nosplash -new wavesurfer_smoke.lua      (writes ~/wavesurfer_reaper.txt)
local out = io.open(os.getenv("HOME") .. "/wavesurfer_reaper.txt", "w")
local function log(s) out:write(s .. "\n"); out:flush(); reaper.ShowConsoleMsg(s .. "\n") end

log("REAPER " .. reaper.GetAppVersion())
reaper.InsertTrackAtIndex(0, true)
local tr = reaper.GetTrack(0, 0)

-- a test signal: stock JSFX tone generator, then WaveSurfer
local tone = reaper.TrackFX_AddByName(tr, "JS:Utility/tonegenerator", false, -1)
log("tone generator index " .. tostring(tone))

local loaded = {}
for _, name in ipairs({ "VST3:WaveSurfer", "AU:WaveSurfer" }) do
  local fx = reaper.TrackFX_AddByName(tr, name, false, -1)
  log(name .. " -> fx index " .. tostring(fx))
  if fx >= 0 then
    local _, fxname = reaper.TrackFX_GetFXName(tr, fx, "")
    log("  name: " .. fxname .. ", params: " .. reaper.TrackFX_GetNumParams(tr, fx))
    -- push every parameter to an extreme, then something musical
    for p = 0, reaper.TrackFX_GetNumParams(tr, fx) - 1 do reaper.TrackFX_SetParamNormalized(tr, fx, p, (p % 2 == 0) and 1.0 or 0.0) end
    for p = 0, reaper.TrackFX_GetNumParams(tr, fx) - 1 do reaper.TrackFX_SetParamNormalized(tr, fx, p, 0.5) end
    reaper.TrackFX_Show(tr, fx, 3)   -- floating window: this is "opening" the plug-in
    loaded[#loaded + 1] = fx
  end
end

-- offline render of 8 s through the chain
reaper.GetSet_LoopTimeRange(true, false, 0, 8, false)
reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 2, true)
reaper.GetSetProjectInfo(0, "RENDER_SETTINGS", 0, true)
reaper.GetSetProjectInfo_String(0, "RENDER_FILE", os.getenv("HOME"), true)
reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", "wavesurfer_render", true)
log("rendering...")
reaper.Main_OnCommand(42230, 0)   -- render using most recent settings, auto-close dialog
log("render done")

-- keep the windows open for a few seconds of UI timers, then finish
local frames = 0
local function tick()
  frames = frames + 1
  if frames < 180 then reaper.defer(tick) else
    for _, fx in ipairs(loaded) do reaper.TrackFX_Show(tr, fx, 2) end
    log("windows closed, loaded " .. #loaded .. " instance(s)")
    log("SMOKE TEST DONE")
    out:close()
  end
end
reaper.defer(tick)
