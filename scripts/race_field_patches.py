"""USA participant-table relocation inputs; see docs/race-fields.md.

The scanner follows literal LUI bases through indexed ADDU operations. It only
rewrites direct references (large signed displacements), never accesses through
an already materialized pointer. Instruction hooks replace those references;
all other instructions, including delay slots, retain their native semantics.
"""
import hashlib
import struct

ROM_SHA256 = 'cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c'
# Each camera table gets room for indices 0..16. Scalar scratch between the
# two direction tables and position tables is deliberately excluded.
CAMERA = [0x2D10, 0x2D28, 0x2D40, 0x2D58, 0x2D70, 0x2D98,
          0x2DD0, 0x2DF8, 0x2E20, 0x2E48, 0x2E70, 0x2E98,
          0x2EC0, 0x2EE8]
TABLES = [(0x800A0000 + off, 2 if i < 4 else 4, 0x80900000 + i * 0x100)
          for i, off in enumerate(CAMERA)]
TABLES += [(0x800A2F10, 2, 0x80900E00), (0x80098228, 2, 0x80901000), (0x800CE818, 12 * 2, 0x80901100),
           (0x800CE740, 2, 0x80901200), (0x800CE758, 2, 0x80901300),
           (0x800A5EE8, 16 * 0x84, 0x80902000), (0x800B69A8, 0x10C, 0x80910000),
           (0x800B8910, 0x40, 0x80918000), (0x800B9818, 8, 0x8091C000),
           (0x800B9878, 0x200, 0x80920000)]

def delta_for(address):
    for old, width, new in TABLES:
        if old <= address < old + width:
            return new - old
    return None

def references(rom, functions):
    if hashlib.sha256(rom).hexdigest() != ROM_SHA256:
        raise ValueError('Race-field relocation requires the verified USA ROM')
    found = []
    for name, start, size in functions:
        bases = {}
        for pc in range(start, start + size, 4):
            word = struct.unpack_from('>I', rom, pc - 0x80000000 + 0xC00)[0]
            op, rs, rt, rd = word >> 26, (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31
            imm = (word & 0xFFFF) - (0x10000 if word & 0x8000 else 0)
            if op == 15:  # lui
                bases[rt] = (word & 0xFFFF) << 16
                continue
            # Only the upper literal base is tracked. A materialized pointer
            # kills the tag, preventing a second relocation of its field loads.
            base = bases.get(rs)
            delta = delta_for((base + imm) & 0xFFFFFFFF) if base is not None else None
            if op in (9, 32, 33, 35, 36, 37, 40, 41, 43, 49, 57) and delta is not None:
                address = f'ADD32(ctx->r{rs}, {imm + delta})'
                if op == 9:
                    text = f'ctx->r{rt} = {address};'
                elif op in (32, 33, 35, 36, 37):
                    macro = {32: 'MEM_B', 33: 'MEM_H', 35: 'MEM_W', 36: 'MEM_BU', 37: 'MEM_HU'}[op]
                    text = f'ctx->r{rt} = {macro}(0, {address});'
                elif op in (40, 41, 43):
                    macro = {40: 'MEM_B', 41: 'MEM_H', 43: 'MEM_W'}[op]
                    text = f'{macro}(0, {address}) = ctx->r{rt};'
                elif op == 49:
                    text = f'ctx->f{rt}.u32l = MEM_W(0, {address});'
                else:
                    text = f'MEM_W(0, {address}) = ctx->f{rt}.u32l;'
                found.append((name, pc, word, text))
            if op == 0:
                fn = word & 63
                if fn in (33, 37):  # addu/or, exactly one literal base
                    a, b = bases.get(rs), bases.get(rt)
                    bases[rd] = a if b is None else b if a is None else None
                elif fn not in (8, 9, 24, 25, 26, 27):
                    bases.pop(rd, None)
            elif op in (9, 8, 12, 13, 14, 10, 11, 32, 33, 35, 36, 37):
                bases.pop(rt, None)
    return found

def patch_blocks(rom, functions):
    result = ['\n# Expanded race-field table references (scripts/race_field_patches.py).']
    for name, pc, word, text in references(rom, functions):
        result.append(f'\n[[patches.instruction]]\nfunc = "{name}"\nvram = {pc:#x}\nvalue = 0\n'
                      f'\n[[patches.hook]]\nfunc = "{name}"\nbefore_vram = {pc:#x}\ntext = "{text}"\n')
    def hook(name, pc, text):
        result.append(f'\n[[patches.hook]]\nfunc = "{name}"\nbefore_vram = {pc:#x}\ntext = "{{ {text} }}"\n')
    # The minimap uses zero-based entrants via a second-record AI alias and
    # eight-entry RGB palettes. Repeat the palette without reading its neighbours.
    hook('func_80054FFC', 0x800553AC, 'ctx->r24 &= 7;')
    # Large text only has authored digits 1-6. Extend measurement and emission
    # to 0-9, supplying the missing textures from the game's complete small font.
    for pc, mask, value in ((0x8004B664, 0xFC000000, 0x04000000),
                            (0x8004B674, 0xFFFF, 10),
                            (0x8004BCE0, 0xFFFF, 0x30),
                            (0x8004C098, 0xFFFF, 0x3A)):
        word = struct.unpack_from('>I', rom, pc - 0x80000000 + 0xC00)[0]
        # blez t9 -> bltz t9 (REGIMM, rt=0).
        patched = (word & ~mask) | value
        result.append(f'\n[[patches.instruction]]\nfunc = "func_8004C070"\nvram = {pc:#x}\nvalue = {patched:#x}\n')
    hook('func_8004C070', 0x8004BDB4,
         'extern unsigned int lambo_field_digit_texture(uint8_t*, int, unsigned int); ctx->r15 = (gpr)(int32_t)lambo_field_digit_texture(rdram, MEM_HU(0x86, ctx->r29) - 48, (unsigned int)ctx->r15);')
    for pc, immediate in ((0x80010B34, 64), (0x80010BBC, 64), (0x80010BD4, 63 * 0x10C + 6),
                          (0x80010BEC, 128), (0x80010C70, 128), (0x80010C88, 127 * 64 + 6),
                          (0x80010C94, 32), (0x80010CFC, 32), (0x80010D14, 31 * 8 + 4)):
        word = struct.unpack_from('>I', rom, pc - 0x80000000 + 0xC00)[0]
        result.append(f'\n[[patches.instruction]]\nfunc = "func_80011710"\nvram = {pc:#x}\nvalue = {(word & 0xffff0000) | immediate:#x}\n')
    hook('func_80019D20', 0x800191A4,
         'extern int lambo_field_car(uint8_t*, int); if (ctx->r10 >= 13 && lambo_field_car(rdram, MEM_H(0, (gpr)(int32_t)0x80098398))) ctx->r10 = 5;')
    for name, pc, obj in [('func_8002807C', 0x8002751C, 'MEM_H(0, (gpr)(int32_t)0x80098398)'),
                          ('func_800657CC', 0x80064C28, 'MEM_H(0x46, ctx->r29)'),
                          ('func_800657CC', 0x80065A14, 'MEM_H(0x46, ctx->r29)')]:
        hook(name, pc, f'extern int lambo_field_car(uint8_t*, int); if (lambo_field_car(rdram, {obj})) ctx->r1 = 1;')
    for pc, column in ((0x8003B430, 0), (0x8003B4E8, 1), (0x8003B578, 2), (0x8003B5CC, 3),
                       (0x8003B3B8, 4), (0x8003AEDC, 4), (0x8003AF78, 0), (0x8003AFDC, 1),
                       (0x8003B1B8, 2), (0x8003B210, 3)):
        hook('func_80038D6C', pc, f'extern void lambo_field_standings(uint8_t*, recomp_context*, int); lambo_field_standings(rdram, ctx, {column});')
    hook('func_800030F8', 0x800032B0,
         'extern void lambo_field_restart(uint8_t*); lambo_field_restart(rdram);')
    setup = "boot_pad_apply_calibration"
    hook(setup, 0x80005A24, 'extern void lambo_field_prepare(uint8_t*); lambo_field_prepare(rdram);')
    for pc in (0x80005AE0, 0x80005C94, 0x80005D78):
        hook(setup, pc, 'extern void lambo_field_count(uint8_t*); lambo_field_count(rdram);')
    hook('func_8000637C', 0x8000577C, 'extern int lambo_field_roster(uint8_t*); if (lambo_field_roster(rdram)) return;')
    for pc, immediate in ((0x80005DCC, 17), (0x800067BC, 17), (0x80006FAC, 17)):
        word = struct.unpack_from('>I', rom, pc - 0x80000000 + 0xC00)[0]
        result.append(f'\n[[patches.instruction]]\nfunc = "{setup}"\nvram = {pc:#x}\nvalue = {(word & 0xffff0000) | immediate:#x}\n')
    hook(setup, 0x8000700C, 'extern void lambo_field_grid_begin(uint8_t*, recomp_context*); lambo_field_grid_begin(rdram, ctx);')
    for pc in (0x80007048, 0x8000709C):
        hook(setup, pc, 'extern void lambo_field_grid_slot(uint8_t*, recomp_context*); lambo_field_grid_slot(rdram, ctx);')
    hook(setup, 0x80007D8C, 'extern void lambo_field_grid_end(uint8_t*); lambo_field_grid_end(rdram);')
    for name, pc, rank, count in [('func_800583B8', 0x80057830, 'MEM_H(0x1A, ctx->r29)', 1),
                                  ('func_80050860', 0x800508C8, 'ctx->r14', 0),
                                  ('func_80050860', 0x80050A14, 'ctx->r10', 0),
                                  ('func_80038D6C', 0x8003B41C, 'ctx->r8', 0),
                                  ('func_80038D6C', 0x8003AF64, 'MEM_H(0, (gpr)(int32_t)0x800986C8)', 0)]:
        hook(name, pc, f'extern void lambo_field_rank_text(uint8_t*, int, int); lambo_field_rank_text(rdram, {rank}, {count});')
    for pc in (0x800508E8, 0x80050A2C):
        hook('func_80050860', pc,
             'if (MEM_H(0x8A, (gpr)(int32_t)0x80902000) >= 10) ctx->r5 -= 20;')
    for pc in (0x80050954, 0x80050A98):
        hook('func_80050860', pc,
             'if (MEM_H(0x8A, (gpr)(int32_t)0x80902000) >= 10) MEM_H(0, (gpr)(int32_t)0x800986F8) += 20;')
    result.append('\n[[patches.instruction]]\nfunc = "func_8003CD84"\nvram = 0x8003CFC0\nvalue = 0\n')
    hook('func_8003CD84', 0x8003CFC0,
         'int place = MEM_H(0, (gpr)(int32_t)0x800986D8); ctx->r15 = place >= 1 && place <= 6 ? MEM_H(place * 2, (gpr)(int32_t)0x801EA164) : 0;')
    result.append('\n[[patches.instruction]]\nfunc = "func_80038D6C"\nvram = 0x8003AFAC\nvalue = 0\n')
    hook('func_80038D6C', 0x8003AFAC,
         'int place = MEM_H(0, (gpr)(int32_t)0x800986C8); ctx->r12 = place >= 1 && place <= 6 ? MEM_H(place * 2, (gpr)(int32_t)0x801EA164) : 0;')
    return '\n'.join(result)
