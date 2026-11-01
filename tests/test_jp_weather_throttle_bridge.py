"""Compile the emitted throttle hooks and verify their bounded register effects."""
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
CXX = shutil.which('g++') or shutil.which('clang++')
ROMS = {
    'usa': ('Automobili Lamborghini (USA).z64', 'cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c'),
    'japan': ('Super Speed Race 64 (Japan).z64', '06c9c992014da5982e747117c690c1590a011c0c6756b0522df126366bf8b9a1'),
}


def hooks():
    data = tomllib.loads((ROOT / 'lamborghini.us.toml').read_text())
    result = {}
    for address in (0x8002FEFC, 0x8002FF14):
        matches = [hook for hook in data['patches']['hook'] if hook.get('before_vram') == address]
        if len(matches) != 1 or matches[0]['func'] != 'func_8002D37C':
            raise AssertionError(f'Expected one scoped throttle hook at {address:#x}')
        result[address] = matches[0]['text']
    return result


class ThrottleBridgeTests(unittest.TestCase):
    @unittest.skipUnless(CXX, 'a host C++ compiler is required')
    def test_emitted_hooks_reproduce_signed_cap_edges_and_preserve_registers(self):
        snippets = hooks()
        registers = ', '.join(f'r{i}' for i in range(32))
        source = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#define SIGNED(value) static_cast<int64_t>(value)
struct Context { uint64_t REGISTERS; };
static unsigned cap;
unsigned lambo_ai_maneuver_cap(uint8_t*) { return cap; }
static void compare(Context* ctx, uint8_t* rdram) { COMPARE }
static void store(Context* ctx, uint8_t* rdram) { STORE }
static int16_t half(int value) {
    const unsigned bits = static_cast<unsigned>(value) & 65535u;
    return static_cast<int16_t>(bits < 32768 ? static_cast<int>(bits) : static_cast<int>(bits) - 65536);
}
static int reference(int speed, int initial, unsigned limit) {
    const int next = half(initial + (speed < 60 ? 10 : -10));
    return speed < 60 ? (next > static_cast<int>(limit) ? limit : next) : (next < 0 ? 0 : next);
}
static int execute(int speed, int initial, bool bridge) {
    if (speed >= 60) {
        const int next = half(initial - 10);
        return next < 0 ? 0 : next;
    }
    Context context{};
    Context* ctx = &context;
    uint8_t* rdram = nullptr;
    int result = half(initial + 10); // SH then signed LH, before the original SLTI.
    ctx->r24 = static_cast<int64_t>(result);
    ctx->r1 = SIGNED(ctx->r24) < 101;
    if (bridge) compare(ctx, rdram);
    if (ctx->r1 == 0) {
        ctx->r8 = 100;
        if (bridge) store(ctx, rdram);
        result = half(static_cast<int>(ctx->r8));
    }
    return result;
}
int main() {
    const int speeds[] = {-1, 0, 59, 60, 61, 200};
    const int values[] = {-32768, -20, -10, -1, 0, 9, 10, 49, 50, 51, 59, 60, 61, 90, 91, 100, 101, 32757, 32758, 32760, 32767};
    for (cap = 0; cap <= 100; ++cap) {
        for (int speed : speeds) for (int initial : values)
            assert(execute(speed, initial, true) == reference(speed, initial, cap));
        for (int next : values) {
            Context context{};
            uint64_t before[32], after[32];
            for (unsigned i = 0; i < 32; ++i) before[i] = 0x1234567800000000ULL + i;
            std::memcpy(&context, before, sizeof(context));
            context.r24 = static_cast<int64_t>(next);
            std::memcpy(before, &context, sizeof(context));
            compare(&context, nullptr);
            std::memcpy(after, &context, sizeof(context));
            for (unsigned i = 0; i < 32; ++i)
                assert(after[i] == (i == 1 ? uint64_t(next < int(cap + 1)) : before[i]));
            std::memcpy(before, &context, sizeof(context));
            store(&context, nullptr);
            std::memcpy(after, &context, sizeof(context));
            for (unsigned i = 0; i < 32; ++i)
                assert(after[i] == (i == 8 ? cap : before[i]));
        }
    }
    cap = 100;
    for (int speed : speeds) for (int initial = -32768; initial <= 32767; ++initial)
        assert(execute(speed, initial, true) == execute(speed, initial, false));
}
'''.replace('REGISTERS', registers).replace('COMPARE', snippets[0x8002FEFC]).replace('STORE', snippets[0x8002FF14])
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'bridge.cpp'
            exe = Path(directory) / 'bridge.exe'
            cpp.write_text(source)
            build = subprocess.run([CXX, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_verified_roms_have_matching_speed_gate_and_only_cap_difference(self):
        if any(not (ROOT / name).exists() for name, _ in ROMS.values()):
            self.skipTest('local USA and Japanese cartridges are required')
        data = {}
        for region, (name, expected_hash) in ROMS.items():
            data[region] = (ROOT / name).read_bytes()
            self.assertEqual(hashlib.sha256(data[region]).hexdigest(), expected_hash)
        def instruction(region, pc, opcode, rs, rt, immediate):
            word, = struct.unpack_from('>I', data[region], pc - 0x7FFFF400)
            self.assertEqual((word >> 26, (word >> 21) & 31, (word >> 16) & 31, word & 65535),
                             (opcode, rs, rt, immediate & 65535), f'{region} {pc:#x}')
        # Identical signed speed<60 gates, dry/US cap100 versus wet cap60.
        for region, pc, register in [('usa', 0x8002FE90, 8), ('japan', 0x80035BA4, 25), ('japan', 0x80035D08, 15)]:
            instruction(region, pc, 10, register, 1, 60)
        instruction('japan', 0x80035B6C, 33, 15, 15, 0x503C)
        instruction('japan', 0x80035B70, 9, 0, 1, 1)
        instruction('japan', 0x80035B74, 4, 15, 1, (0x80035CE0 - 0x80035B78) // 4)
        for region, compare_pc, register, cap_pc, target, limit in [
                ('usa', 0x8002FEF8, 24, 0x8002FF10, 8, 100),
                ('japan', 0x80035C0C, 12, 0x80035C24, 25, 100),
                ('japan', 0x80035D70, 11, 0x80035D88, 15, 60)]:
            instruction(region, compare_pc, 10, register, 1, limit + 1)
            instruction(region, cap_pc, 9, 0, target, limit)
        # Both acceleration and deceleration retain their signed-halfword writes.
        for region, pc, rs, rt, amount in [('usa', 0x8002FED0, 25, 9, 10), ('japan', 0x80035D48, 25, 9, 10),
                                         ('usa', 0x8002FF6C, 14, 12, -10), ('japan', 0x80035DE4, 10, 24, -10)]:
            instruction(region, pc, 9, rs, rt, amount)
        instruction('usa', 0x8002FEF4, 33, 24, 24, 0x6A52)
        instruction('japan', 0x80035D6C, 33, 11, 11, -0x5D76)
        for region, pc, base, value, displacement in [
                ('usa', 0x8002FED4, 15, 9, 0xAA), ('japan', 0x80035D4C, 14, 9, 0xAA),
                ('usa', 0x8002FF2C, 1, 8, 0x6A52), ('japan', 0x80035DA4, 1, 15, -0x5D76),
                ('usa', 0x8002FF70, 15, 12, 0xAA), ('japan', 0x80035DE8, 14, 24, 0xAA)]:
            instruction(region, pc, 41, base, value, displacement)


if __name__ == '__main__':
    unittest.main()
