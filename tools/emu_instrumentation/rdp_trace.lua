-- rdp_trace.lua
-- Captures RDP registers + VI registers per frame, outputs JSON
-- Usage: mupen64plus-ui-console.exe --lua rdp_trace.lua --noosd --nospeedlimit --audio dummy --gfx dummy --input dummy rom.z64
-- Or via run_instrumented.py: python run_instrumented.py --lua rdp_trace.lua --frames N --output trace.json rom.z64

local FRAME_LIMIT = 60
local OUTPUT_FILE = "emu_rdp_trace.json"
local RDRAM_BASE = 0x80000000

-- Parse CLI args: --frames N --output file.z64
for i = 1, #arg do
    if arg[i] == "--frames" and arg[i+1] then
        FRAME_LIMIT = tonumber(arg[i+1])
    elseif arg[i] == "--output" and arg[i+1] then
        OUTPUT_FILE = arg[i+1]
    end
end

emu.pause()

-- Helper: read a 32-bit word from RDRAM
local function readword(addr)
    emu.memory.setmemorydomain("RDRAM")
    return emu.memory.readword(addr)
end

-- Helper: read a 16-bit half
local function readhalf(addr)
    emu.memory.setmemorydomain("RDRAM")
    return emu.memory.readword(addr) & 0xFFFF
end

-- Capture RDP and VI state for current frame
local function capture_frame(frame_num)
    emu.memory.setmemorydomain("RDRAM")

    -- RDP (DPC) registers at 0xA3C0 region
    local dpc_start = readword(0xA3C0)
    local dpc_end = readword(0xA3C4)
    local dpc_current = readword(0xA3C8)
    local dpc_status = readword(0xA3CC)
    local dpc_clock = readword(0xA3D0)
    local dpc_bufbusy = readword(0xA3D4)
    local dpc_pipebusy = readword(0xA3D8)
    local dpc_tmem = readword(0xA3DC)

    -- VI registers at 0xA430 region
    local vi_dram = readword(0xA430)
    local vi_width = readword(0xA434)
    local vi_intr = readword(0xA438)
    local vi_current = readword(0xA43C)
    local vi_sync = readword(0xA440)
    local vi_hsync = readword(0xA450)
    local vi_vstart = readword(0xA458)

    -- If there's an active display list, capture first few commands
    local dl_cmds = {}
    if dpc_start ~= 0 and dpc_end > dpc_start and dpc_end < RDRAM_BASE + 0x800000 then
        local addr = dpc_start
        local max_cmds = 16  -- limit per frame to keep JSON manageable
        local count = 0
        while addr < dpc_end and count < max_cmds do
            local cmd_lo = readword(addr)
            local cmd_hi = readword(addr + 4)
            table.insert(dl_cmds, {
                addr = string.format("0x%08X", addr),
                lo = string.format("0x%08X", cmd_lo),
                hi = string.format("0x%08X", cmd_hi),
                opcode = cmd_lo & 0xFF
            })
            addr = addr + 8
            count = count + 1
        end
    end

    return {
        frame = frame_num,
        vi_current = vi_current,
        vi_sync = vi_sync,
        vi_width = vi_width,
        vi_intr = vi_intr,
        dpc_start = string.format("0x%08X", dpc_start),
        dpc_end = string.format("0x%08X", dpc_end),
        dpc_current = string.format("0x%08X", dpc_current),
        dpc_status = string.format("0x%08X", dpc_status),
        dpc_clock = dpc_clock,
        dpc_bufbusy = dpc_bufbusy,
        dpc_pipebusy = dpc_pipebusy,
        dpc_tmem = string.format("0x%08X", dpc_tmem),
        dl_cmd_count = #dl_cmds,
        dl_commands = dl_cmds
    }
end

-- JSON escape for strings
local function json_escape(s)
    return (s:gsub("\\", "\\\\"):gsub("\"", "\\\""):gsub("\n", "\\n"):gsub("\r", "\\r"):gsub("\t", "\\t"))
end

-- Simple JSON serializer (no external deps)
local function to_json(data)
    local function stringify(val, indent, key)
        indent = indent or ""
        key = key or ""
        local prefix = key == "" and "" or "\"" .. key .. "\": "
        if type(val) == "nil" then
            return prefix .. "null"
        elseif type(val) == "number" then
            return prefix .. tostring(val)
        elseif type(val) == "boolean" then
            return prefix .. (val and "true" or "false")
        elseif type(val) == "string" then
            return prefix .. "\"" .. json_escape(val) .. "\""
        elseif type(val) == "table" then
            local is_array = (#val > 0)
            local lines = {}
            local sub_indent = indent .. "  "
            if is_array then
                for i = 1, #val do
                    table.insert(lines, sub_indent .. stringify(val[i], sub_indent, i-1))
                end
                return prefix .. "[\n" .. table.concat(lines, ",\n") .. "\n" .. indent .. "]"
            else
                local keys = {}
                for k in pairs(val) do table.insert(keys, k) end
                table.sort(keys)
                for _, k in ipairs(keys) do
                    table.insert(lines, sub_indent .. stringify(val[k], sub_indent, k))
                end
                return prefix .. "{\n" .. table.concat(lines, ",\n") .. "\n" .. indent .. "}"
            end
        else
            return prefix .. "\"" .. json_escape(tostring(val)) .. "\""
        end
    end
    return "{\n" .. stringify(data, "", "") .. "\n}"
end

-- Main loop
local frames = {}
for frame = 0, FRAME_LIMIT - 1 do
    local data = capture_frame(frame)
    table.insert(frames, data)
    emu.frameadvance()
end

-- Write JSON output
local f = io.open(OUTPUT_FILE, "w")
if f then
    f:write(to_json(frames))
    f:close()
    emu.message("RDP trace captured: " .. #frames .. " frames -> " .. OUTPUT_FILE)
else
    emu.message("ERROR: Could not open " .. OUTPUT_FILE .. " for writing")
end

emu.pause()
