-- input_auto.lua
-- Headless input automation: drives gameplay from boot through menu and race
-- Usage: --lua input_auto.lua --frames N [--phase boot|menu|race]
-- Phases:
--   boot (default): boot → press start (frame 30-60)
--   menu: press start → select race (frame 60-90)
--   race: select race → drive (frame 90+)

local FRAME_LIMIT = 300
local PHASE = "boot"
local ROM_PATH = ""

-- Parse CLI args
for i = 1, #arg do
    if arg[i] == "--frames" and arg[i+1] then
        FRAME_LIMIT = tonumber(arg[i+1])
    elseif arg[i] == "--phase" and arg[i+1] then
        PHASE = arg[i+1]
    elseif arg[i]:match("%.z64$") or arg[i]:match("%.v64$") or arg[i]:match("%.n64$") then
        ROM_PATH = arg[i]
    end
end

-- Timing constants (in frames at 60fps)
local FRAME_BOOT_WAIT = 30
local FRAME_PRESS_START = 60
local FRAME_SELECT_RACE = 90
local FRAME_RACE_START = 120
local FRAME_DRIVE = 180

emu.pause()

-- Clear all inputs
local function clear_input()
    joypad.set(1, {
        A = false, B = false, Z = false,
        Start = false, Up = false, Down = false, Left = false, Right = false,
        L = false, R = false,
        CUp = false, CDown = false, CLeft = false, CRight = false
    })
end

clear_input()

-- Main loop
for frame = 0, FRAME_LIMIT - 1 do
    local buttons = {}

    if PHASE == "boot" or PHASE == "all" then
        if frame >= FRAME_BOOT_WAIT and frame < FRAME_PRESS_START then
            -- Hold Start to skip boot/start screen
            buttons.Start = true
        end
    elseif PHASE == "menu" then
        if frame >= FRAME_PRESS_START and frame < FRAME_SELECT_RACE then
            -- Confirm "Press Start" / select
            buttons.A = true
        elseif frame >= FRAME_SELECT_RACE and frame < FRAME_RACE_START then
            -- Navigate race select (Up to move up, A to select)
            buttons.Up = true
            buttons.A = true
        end
    elseif PHASE == "race" then
        if frame >= FRAME_RACE_START and frame < FRAME_DRIVE then
            -- Hold A to accelerate
            buttons.A = true
            buttons.R = true  -- max speed
        elseif frame >= FRAME_DRIVE then
            -- Drive: slight steering
            buttons.A = true
            buttons.R = true
            -- alternate steering left/right
            if (frame % 60) < 30 then
                buttons.Left = true
            else
                buttons.Right = true
            end
        end
    end

    if frame < FRAME_BOOT_WAIT then
        -- Wait for boot: no input
    elseif frame < FRAME_PRESS_START then
        buttons = {Start = true}
    elseif frame < FRAME_SELECT_RACE then
        buttons = {A = true}
    elseif frame < FRAME_RACE_START then
        buttons = {Up = true, A = true}
    else
        buttons = {A = true, R = true}
        if (frame % 60) < 30 then
            buttons.Right = true
        else
            buttons.Left = true
        end
    end

    joypad.set(1, buttons)
    emu.frameadvance()
end

clear_input()
emu.message("Input automation complete (" .. FRAME_LIMIT .. " frames, phase: " .. PHASE .. ")")
emu.pause()
