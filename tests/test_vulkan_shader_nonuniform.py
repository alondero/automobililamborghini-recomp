"""Shader capabilities match the Vulkan device's descriptor indexing (issue #350).

No ROM or GPU needed. RT64's texture sampler marks its texture-table index
NonUniformResourceIndex, so every raster shader declares ShaderNonUniform,
SampledImageArrayNonUniformIndexing and SPV_EXT_descriptor_indexing. Drivers
without descriptor indexing must not receive those declarations
(VUID-VkShaderModuleCreateInfo-pCode-08740/-08742). This game never enables
RDP texture LOD, so each draw indexes the table uniformly; see
docs/no_lod_audit.md, Axis D. Compile Plume's production shader-module
constructor against a recording Vulkan boundary.
"""
import os
import platform
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.patched_source import PLUME, ROOT, patched_plume_vulkan

MAGIC = 0x07230203
OP_EXTENSION, OP_MEMORY_MODEL, OP_CAPABILITY = 10, 14, 17
OP_DECORATE, OP_MEMBER_DECORATE = 71, 72
DECORATION_BINDING, DECORATION_OFFSET, DECORATION_NON_UNIFORM = 33, 35, 5300
CAPABILITY_SHADER, CAPABILITY_RUNTIME_DESCRIPTOR_ARRAY = 1, 5302
NON_UNIFORM_CAPABILITIES = {5301, *range(5306, 5313)}
EXTENSION = "SPV_EXT_descriptor_indexing"
DXC_ROOT = ROOT / "lib/rt64/src/contrib/dxc"
# Mirrors how lib/rt64/CMakeLists.txt runs its bundled x64 compiler.
if os.name == "nt":
    DXC, DXC_ENV = DXC_ROOT / "bin/x64/dxc.exe", None
elif platform.system() == "Linux" and platform.machine() == "x86_64":
    DXC = DXC_ROOT / "bin/x64/dxc-linux"
    DXC_ENV = dict(os.environ, LD_LIBRARY_PATH=str(DXC_ROOT / "lib/x64"))
else:
    DXC, DXC_ENV = None, None


def literal(text):
    data = text.encode("ascii") + b"\0"
    data += b"\0" * (-len(data) % 4)
    return list(struct.unpack(f"<{len(data) // 4}I", data))


def instruction(opcode, *operands):
    return [((len(operands) + 1) << 16) | opcode, *operands]


def module(*instructions):
    words = [MAGIC, 0x00010000, 0, 100, 0]
    for words_of_instruction in instructions:
        words += words_of_instruction
    return struct.pack(f"<{len(words)}I", *words)


def instructions(code):
    words = struct.unpack(f"<{len(code) // 4}I", code)
    index, result = 5, []
    while index < len(words):
        count = words[index] >> 16
        result.append(tuple(words[index:index + count]))
        index += count
    return words[:5], result


def non_uniform(entry):
    opcode = entry[0] & 0xFFFF
    return ((opcode == OP_CAPABILITY and entry[1] in NON_UNIFORM_CAPABILITIES)
            or (opcode == OP_DECORATE and entry[2] == DECORATION_NON_UNIFORM)
            or (opcode == OP_MEMBER_DECORATE and entry[3] == DECORATION_NON_UNIFORM))


def extension(entry):
    return entry[0] & 0xFFFF == OP_EXTENSION and list(entry[1:]) == literal(EXTENSION)


SAMPLER = module(
    instruction(OP_CAPABILITY, CAPABILITY_SHADER),
    instruction(OP_CAPABILITY, 5301),
    instruction(OP_CAPABILITY, 5307),
    instruction(OP_EXTENSION, *literal(EXTENSION)),
    instruction(OP_MEMORY_MODEL, 0, 1),
    instruction(OP_DECORATE, 10, DECORATION_BINDING, 0),
    instruction(OP_DECORATE, 12, DECORATION_NON_UNIFORM),
    instruction(OP_MEMBER_DECORATE, 11, 0, DECORATION_NON_UNIFORM),
    instruction(OP_MEMBER_DECORATE, 11, 0, DECORATION_OFFSET, 0),
)


@unittest.skipUnless((PLUME / ".git").exists(), "Plume submodule not initialized")
@unittest.skipUnless(shutil.which("g++"), "g++ needed for Vulkan shader fixture")
class ShaderNonUniformTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="lambo-shader-nonuniform-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.tree = Path(cls.temporary.name)
        source = patched_plume_vulkan(cls.tree)
        start = source.index("\n    // VulkanShader\n")
        end = source.index("    VulkanShader::~VulkanShader()")
        fixture = (ROOT / "tests/fixtures/vulkan_shader_nonuniform.cpp").read_text(encoding="utf-8")
        cpp = cls.tree / "fixture.cpp"
        cpp.write_text(fixture + source[start:end], encoding="utf-8")
        cls.exe = cls.tree / "fixture.exe"
        result = subprocess.run(["g++", "-std=c++17", str(cpp), "-o", str(cls.exe)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def received(self, code, device):
        source, received = self.tree / "input.spv", self.tree / "received.spv"
        source.write_bytes(code)
        result = subprocess.run([str(self.exe), str(source), str(received), device],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        return received.read_bytes()

    def assert_only_non_uniform_removed(self, original, received, keep_extension=False):
        header, before = instructions(original)
        received_header, after = instructions(received)
        self.assertEqual(received_header, header)
        expected = [entry for entry in before
                    if not non_uniform(entry) and (keep_extension or not extension(entry))]
        self.assertEqual(after, expected)

    def test_driver_without_descriptor_indexing_gets_no_non_uniform_declarations(self):
        self.assert_only_non_uniform_removed(SAMPLER, self.received(SAMPLER, "no-indexing"))

    def test_driver_with_descriptor_indexing_gets_unchanged_shader(self):
        self.assertEqual(self.received(SAMPLER, "indexing"), SAMPLER)

    def test_extension_stays_while_another_indexing_capability_needs_it(self):
        code = module(
            instruction(OP_CAPABILITY, CAPABILITY_SHADER),
            instruction(OP_CAPABILITY, CAPABILITY_RUNTIME_DESCRIPTOR_ARRAY),
            instruction(OP_CAPABILITY, 5301),
            instruction(OP_EXTENSION, *literal(EXTENSION)),
            instruction(OP_DECORATE, 12, DECORATION_NON_UNIFORM),
        )
        self.assert_only_non_uniform_removed(code, self.received(code, "no-indexing"),
                                             keep_extension=True)

    def test_malformed_code_is_passed_through(self):
        truncated = SAMPLER[:-4]
        self.assertEqual(self.received(truncated, "no-indexing"), truncated)
        foreign = struct.pack("<I", 0x03022307) + SAMPLER[4:]
        self.assertEqual(self.received(foreign, "no-indexing"), foreign)

    @unittest.skipUnless(DXC and DXC.exists(), "RT64's bundled x64 DXC is needed for the raster shader")
    def test_rt64_raster_shader_loses_only_non_uniform_declarations(self):
        spv = self.tree / "RasterPSDynamic.spv"
        # Mirrors RT64's RasterPSDynamic build options in lib/rt64/CMakeLists.txt.
        subprocess.run([str(DXC), "-spirv", "-fspv-target-env=vulkan1.0", "-fvk-use-dx-layout",
                        f"-I{ROOT / 'lib/rt64/src'}", "-E", "PSMain", "-T", "ps_6_3",
                        "-D", "DYNAMIC_RENDER_PARAMS",
                        str(ROOT / "lib/rt64/src/shaders/RasterPS.hlsl"), "-Fo", str(spv)],
                       check=True, capture_output=True, env=DXC_ENV)
        original = spv.read_bytes()
        _, before = instructions(original)
        self.assertTrue(any(non_uniform(entry) for entry in before))
        self.assert_only_non_uniform_removed(original, self.received(original, "no-indexing"))


if __name__ == "__main__":
    unittest.main()
