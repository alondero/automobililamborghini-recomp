"""Packaging command paths stay literal and custom outputs select the right ELF."""
import importlib.util
from pathlib import Path
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("build_weather", ROOT / "scripts/build_jp_weather_mod.py")
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)


class WeatherBuildTests(unittest.TestCase):
    def test_explicit_host_tools_resolve_before_child_cwd_changes(self):
        relative = "tool dir/clang"
        self.assertEqual(build.host_tool(relative), str(Path(relative).resolve()))
        self.assertEqual(build.host_tool("../tools/ld.lld"), str(Path("../tools/ld.lld").resolve()))
        self.assertEqual(build.host_tool("clang"), "clang")
        absolute = str((ROOT / "tool dir/clang").resolve())
        self.assertEqual(build.host_tool(absolute), absolute)

    def test_native_paths_are_individual_arguments(self):
        command = build.compiler_command("tool dir/clang", Path("source dir/a.c"), Path("out dir/a.o"))
        self.assertEqual(command[0], "tool dir/clang")
        self.assertEqual(command[-4:], ["-c", str(Path("source dir/a.c")), "-o", str(Path("out dir/a.o"))])

    def test_wsl_uses_argument_transport_without_shell_interpolation(self):
        command = build.compiler_command("/usr/bin/clang", Path("a $(echo x).c"), Path("a.o"),
                                         "Ubuntu", lambda path: "/mnt/f/" + str(path))
        self.assertEqual(command[:5], ["wsl", "-d", "Ubuntu", "--exec", "/usr/bin/clang"])
        self.assertEqual(command[-4:], ["-c", "/mnt/f/a $(echo x).c", "-o", "/mnt/f/a.o"])

    def test_derived_manifest_points_at_custom_output_and_original_symbols(self):
        text = '[inputs]\nelf_path = "old"\nfunc_reference_syms_file = "old"\n'
        result = tomllib.loads(build.authoring_manifest(text, Path('/custom dir/weather.elf'), Path('/repo/syms.toml')))
        self.assertEqual(result['inputs']['elf_path'], '/custom dir/weather.elf')
        self.assertEqual(result['inputs']['func_reference_syms_file'], '/repo/syms.toml')
        with self.assertRaises(ValueError):
            build.authoring_manifest('[inputs]\n', Path('/a'), Path('/b'))


if __name__ == '__main__':
    unittest.main()
