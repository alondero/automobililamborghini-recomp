-- rdram_dump.lua
-- Dumps specified RDRAM address ranges to binary files
-- Usage: run with --rdram-ranges addr,size addr,size ... --frames N --output meta.json
-- Example: --rdram-ranges 0x80000000,0x00100000 0x7F388000,0x00010000 --frames 120

local FRAME_LIMIT = 120
local OUTPUT_DIR = "rdram_dumps"
local RANGES = {
    {addr = 0x80000000, size = 0x00100000},  -- 1MB from RDRAM base
    {addr = 0x7F388000, size = 0x00010000},  -- Segment 10 region (CI8 textures)
}
local ROM_PATH = ""

-- Parse CLI args
for i = 1, #arg do
    if arg[i] == "--frames" and arg[i+1] then
        FRAME_LIMIT = tonumber(arg[i+1])
    elseif arg[i] == "--output" and arg[i+1] then
        OUTPUT_DIR = arg[i+1]
    elseif arg[i] == "--rdram-ranges" then
        RANGES = {}
        local idx = i + 1
        while idx <= #arg and arg[idx] and not arg[idx]:find("^%-%-") do
            local addr, size = arg[idx]:match("^(0x[0-9A-Fa-f]+),?(0x[0-9A-Fa-f]+)$")
            if addr and size then
                table.insert(RANGES, {addr = tonumber(addr), size = tonumber(size)})
            end
            idx = idx + 1
        end
    elseif arg[i]:match("%.z64$") or arg[i]:match("%.v64$") or arg[i]:match("%.n64$") then
        ROM_PATH = arg[i]
    end
end

-- Create output directory
os.execute("mkdir -p \"" .. OUTPUT_DIR .. "\"")

emu.pause()

-- Helper: read a byte from RDRAM
local function readbyte(addr)
    emu.memory.setmemorydomain("RDRAM")
    return emu.memory.readbyte(addr)
end

-- Helper: compute SHA-256 of a memory region (simple, in Lua)
local function sha256_region(addr, size)
    -- Mupen64Plus Lua doesn't have a native SHA256.
    -- We'll compute a simple XOR checksum instead for quick comparison.
    emu.memory.setmemorydomain("RDRAM")
    local checksum = 0
    for i = 0, size - 1, 4 do
        local word = emu.memory.readword(addr + i)
        checksum = checksum ~ word
    end
    return string.format("%08X", checksum & 0xFFFFFFFF)
end

-- Advance to target frame
emu.message("Advancing to frame " .. FRAME_LIMIT .. "...")
for frame = 0, FRAME_LIMIT - 1 do
    emu.frameadvance()
end

-- Capture RDRAM ranges
emu.message("Capturing " .. #RANGES .. " RDRAM ranges...")
local dump_meta = {
    frame = FRAME_LIMIT,
    ranges = {},
    rom_path = ROM_PATH
}

for i, range in ipairs(RANGES) do
    local filename = string.format("%s/rdram_f%04d_0x%08X.bin", OUTPUT_DIR, FRAME_LIMIT, range.addr)
    local f = io.open(filename, "wb")
    if f then
        emu.memory.setmemorydomain("RDRAM")
        for offset = 0, range.size - 1 do
            local byte = emu.memory.readbyte(range.addr + offset)
            f:write(string.char(byte))
        end
        f:close()

        local checksum = sha256_region(range.addr, range.size)
        table.insert(dump_meta.ranges, {
            addr = string.format("0x%08X", range.addr),
            size = range.size,
            file = filename,
            checksum = checksum
        })
        emu.message(string.format("  Dumped 0x%08X + 0x%X -> %s (checksum: %s)",
            range.addr, range.size, filename, checksum))
    else
        emu.message("ERROR: Could not write " .. filename)
    end
end

-- Write metadata JSON
local meta_file = OUTPUT_DIR .. "/rdram_meta.json"
local meta_f = io.open(meta_file, "w")
if meta_f then
    -- Simple JSON (same serializer as rdp_trace.lua, but inline here)
    local lines = {"{"}
    table.insert(lines, "  \"frame\": " .. FRAME_LIMIT .. ",")
    table.insert(lines, "  \"rom_path\": \"" .. ROM_PATH:gsub("\\", "\\\\"):gsub("\"", "\\\"") .. "\",")
    table.insert(lines, "  \"ranges\": [")
    for i, r in ipairs(dump_meta.ranges) do
        local comma = i < #dump_meta.ranges and "," or ""
        table.insert(lines, string.format(
            "    {\"addr\": \"%s\", \"size\": %d, \"file\": \"%s\", \"checksum\": \"%s\"}%s",
            r.addr, r.size, r.file, r.checksum, comma))
    end
    table.insert(lines, "  ]")
    table.insert(lines, "}")
    meta_f:write(table.concat(lines, "\n"))
    meta_f:close()
    emu.message("Metadata written to " .. meta_file)
else
    emu.message("ERROR: Could not write metadata file")
end

emu.message("RD RAM dump complete at frame " .. FRAME_LIMIT)
emu.pause()
