#!/usr/bin/env python3
"""Build host generators, cross-compile ARM64, and package a ROM-free Android APK."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "build-android"
NDK_VERSION = "28.2.13676358"
DEPS = {
    "SDL": ("https://github.com/libsdl-org/SDL.git", "5d249570393f7a37e037abf22cd6012a4cc56a71"),
    "freetype": ("https://github.com/freetype/freetype.git", "42608f77f20749dd6ddc9e0536788eaad70ea4b5"),
    "adrenotools": ("https://github.com/bylaws/libadrenotools.git", "8fae8ce254dfc1344527e05301e43f37dea2df80"),
}
PATCHES = {
    "lib/N64ModernRuntime": ["0001", "0007", "0012"],
    "lib/rt64": ["0006", "0009", "0010", "0011", "0013"],
    "lib/rt64/src/contrib/plume": ["0014"],
}
# SDL is fetched rather than a submodule, so CMake never patches it.
SDL_PATCHES = ["0015", "0048"]
# CMake owns application of these patches. Source preparation must also
# recognize their composed states, since they change the script prefix's hunks.
# tools/check_patch_series.py checks this list against the actual CMake order.
CMAKE_PATCHES = {
    "lib/rt64": ["0021", "0022", "0024", "0025", "0026", "0027", "0029", "0030", "0033", "0038", "0039", "0042", "0046"],
}


def run(*args, cwd=ROOT):
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)


def apply_patch_series(directory, patches, following_patches=()):
    # Later patches can change earlier hunks. Validate the composed result,
    # using a temporary index without modifying the user's index or files.
    git = ["git", "-C", str(directory)]
    with tempfile.TemporaryDirectory(prefix="lambo-patches-") as temporary:
        environment = dict(os.environ, GIT_INDEX_FILE=str(Path(temporary) / "index"))
        subprocess.run(git + ["read-tree", "HEAD"], env=environment, check=True)
        trees = [subprocess.check_output(git + ["write-tree"], env=environment).decode().strip()]
        required = len(patches)
        for patch in [*patches, *following_patches]:
            subprocess.run(git + ["apply", "--cached", "--ignore-whitespace", str(patch)], env=environment, check=True)
            trees.append(subprocess.check_output(git + ["write-tree"], env=environment).decode().strip())
        prefix = Path(temporary) / "prefix.patch"
        remaining = Path(temporary) / "remaining.patch"
        command = git + ["apply", "--ignore-whitespace"]
        for index in range(len(trees) - 1, -1, -1):
            prefix.write_bytes(subprocess.check_output(git + ["diff", "--binary", trees[0], trees[index]]))
            prefix_present = index == 0 or subprocess.run(command + ["--reverse", "--check", str(prefix)], capture_output=True).returncode == 0
            if prefix_present:
                # Validate the entire composed prefix, rather than trusting a
                # later patch marker that could hide missing or edited hunks.
                if following_patches and index < len(trees) - 1:
                    # A damaged later patch must not look like an earlier
                    # valid state. Check the missing suffix without applying it.
                    remaining.write_bytes(subprocess.check_output(git + ["diff", "--binary", trees[index], trees[-1]]))
                    if subprocess.run(command + ["--check", str(remaining)], capture_output=True).returncode != 0:
                        continue
                if index >= required:
                    break
                # Leave the shared suffix for CMake on clean/partial builds.
                remaining.write_bytes(subprocess.check_output(git + ["diff", "--binary", trees[index], trees[required]]))
                if subprocess.run(command + ["--check", str(remaining)], capture_output=True).returncode == 0:
                    run(*command, remaining)
                    break
        else:
            raise RuntimeError(f"Patch series conflicts with local changes in {directory}. Use a clean worktree.")


def fetch_dependency(name):
    url, revision = DEPS[name]
    destination = WORK / "deps" / name
    if not destination.exists():
        run("git", "init", destination)
        run("git", "-C", destination, "remote", "add", "origin", url)
        run("git", "-C", destination, "fetch", "--depth", "1", "origin", revision)
        run("git", "-C", destination, "checkout", "--detach", "FETCH_HEAD")
    actual = subprocess.check_output(["git", "-C", str(destination), "rev-parse", "HEAD"], text=True).strip()
    if actual != revision:
        raise RuntimeError(f"Unexpected {name} revision: {actual}")
    run("git", "-C", destination, "submodule", "update", "--init", "--recursive")
    return destination


def resolve_patches(numbers):
    patches = []
    for number in numbers:
        patch, = (ROOT / "patches").glob(f"{number}-*.patch")
        patches.append(patch)
    return patches


def prepare_sources():
    run("git", "submodule", "update", "--init", "--recursive")
    for submodule, numbers in PATCHES.items():
        patches = resolve_patches(numbers + CMAKE_PATCHES.get(submodule, []))
        apply_patch_series(ROOT / submodule, patches[:len(numbers)], patches[len(numbers):])
    for name in DEPS:
        fetch_dependency(name)
    apply_patch_series(WORK / "deps/SDL", resolve_patches(SDL_PATCHES))


# AssetManager hides the APK's outer assets/ prefix. LauncherActivity copies
# its "assets" subdirectory to files/assets, so assets/assets/ in the ZIP is
# intentional. Keep this contract in sync with the launcher and assetcheck.
REQUIRED_RUNTIME_ASSETS = [
    "LatoLatin-Regular.ttf", "LatoLatin-Bold.ttf", "NotoEmoji-Regular.ttf",
    "promptfont/promptfont.ttf", "recomp.rcss",
]


def stage_package_assets(assets, native_assets, root, dep_licenses):
    """Lay out the APK assets tree from the native runtime assets.

    assets is created by this function; native_assets is the
    build-android/native/assets directory produced by the native configure.
    """
    shutil.copytree(native_assets, assets / "assets")
    shutil.copy2(root / "lamborghini.syms.toml", assets)
    shutil.copy2(root / "LICENSE", assets / "LICENSE")
    notices = assets / "licenses"
    notices.mkdir()
    for name, source in dep_licenses.items():
        shutil.copy2(source, notices / name)


def package(ndk, sdk, variant):
    staging = WORK / "package"
    # This directory is exclusively generated by this script. Recreate it so
    # stale files can never become release payload (especially a developer ROM).
    if staging.exists():
        shutil.rmtree(staging)
    libraries = staging / "jniLibs" / "arm64-v8a"
    libraries.mkdir(parents=True)
    native = WORK / "native"
    for source in [native / "libmain.so", native / "SDL" / "libSDL2.so", native / "liblambo_vulkan.so",
                   native / "adrenotools/src/hook/libmain_hook.so", native / "adrenotools/src/hook/libhook_impl.so"]:
        shutil.copy2(source, libraries)
    host = "windows-x86_64" if os.name == "nt" else "linux-x86_64"
    toolchain = ndk / "toolchains" / "llvm" / "prebuilt" / host
    shutil.copy2(toolchain / "sysroot/usr/lib/aarch64-linux-android/libc++_shared.so", libraries)
    strip = toolchain / "bin" / ("llvm-strip.exe" if os.name == "nt" else "llvm-strip")
    for library in libraries.glob("*.so"):
        run(strip, "--strip-unneeded", library)
    stage_package_assets(staging / "assets", native / "assets", ROOT, {
        "SDL.txt": WORK / "deps/SDL/LICENSE.txt",
        "FreeType.txt": WORK / "deps/freetype/docs/FTL.TXT",
        "AdrenoTools.txt": WORK / "deps/adrenotools/LICENSE",
        "linkernsbypass.txt": WORK / "deps/adrenotools/lib/linkernsbypass/LICENSE",
    })
    os.environ["ANDROID_HOME"] = str(sdk)
    wrapper = ROOT / "android" / ("gradlew.bat" if os.name == "nt" else "gradlew")
    command = [wrapper] if os.name == "nt" else ["sh", wrapper]
    run(*command, "--no-daemon", "assemble" + variant.capitalize(), cwd=ROOT / "android")
    apk = ROOT / f"android/app/build/outputs/apk/{variant}/app-{variant}.apk"
    with zipfile.ZipFile(apk) as archive:
        for name in archive.namelist():
            if name.lower().endswith((".z64", ".v64", ".n64", ".mpk", ".pak")):
                raise RuntimeError(f"Forbidden game data in APK: {name}")
        required = ["lib/arm64-v8a/libmain.so", "lib/arm64-v8a/libSDL2.so", "lib/arm64-v8a/libc++_shared.so", "lib/arm64-v8a/liblambo_vulkan.so",
                    "lib/arm64-v8a/libmain_hook.so", "lib/arm64-v8a/libhook_impl.so"]
        required += ["assets/assets/" + name for name in REQUIRED_RUNTIME_ASSETS]
        required += ["assets/lamborghini.syms.toml"]
        for name in required:
            if name not in archive.namelist():
                raise RuntimeError(f"Missing APK payload: {name}")
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    result = dist / f"lamborghini-recomp-android-arm64{'-debug' if variant == 'debug' else ''}.apk"
    shutil.copy2(apk, result)
    print(f"APK: {result}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release", action="store_true", help="Require release signing environment variables")
    parser.add_argument("--package-only", action="store_true", help="Package an existing native build")
    parser.add_argument("--install", action="store_true", help="Install the finished APK on the connected device")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 4, 12))
    parser.add_argument("--sdk", default=os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT"))
    parser.add_argument("--cmake", default="cmake")
    args = parser.parse_args()
    if not args.sdk:
        parser.error("Set ANDROID_HOME or pass --sdk")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.release:
        for key in ["ANDROID_KEYSTORE_PATH", "ANDROID_KEYSTORE_PASSWORD", "ANDROID_KEY_ALIAS", "ANDROID_KEY_PASSWORD"]:
            if not os.environ.get(key):
                parser.error(f"Release signing requires {key}")
    sdk = Path(args.sdk).resolve()
    ndk = sdk / "ndk" / NDK_VERSION
    if not (ndk / "build/cmake/android.toolchain.cmake").is_file():
        parser.error(f"Install Android NDK {NDK_VERSION} with sdkmanager")
    WORK.mkdir(exist_ok=True)
    if not args.package_only:
        if not (ROOT / "Automobili Lamborghini (USA).z64").is_file():
            parser.error("Place your USA ROM at the repository root before recompiling")
        prepare_sources()
        host = WORK / "host"
        host_flags = []
        if os.name == "nt":
            host_flags = ["-DCMAKE_C_COMPILER=gcc.exe", "-DCMAKE_CXX_COMPILER=g++.exe"]
        run(args.cmake, "-S", ROOT / "android/host-tools", "-B", host, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", *host_flags)
        run(args.cmake, "--build", host, "--target", "N64RecompCLI", "RSPRecomp", "file_to_c", "-j", args.jobs)
        suffix = ".exe" if os.name == "nt" else ""
        run(host / "bin" / f"N64Recomp{suffix}", "lamborghini.us.toml")
        run(host / "bin" / f"RSPRecomp{suffix}", "aspMain.us.toml")
        run(args.cmake, "-S", ROOT, "-B", WORK / "native", "-G", "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={ndk.as_posix()}/build/cmake/android.toolchain.cmake",
            "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-28", "-DANDROID_STL=c++_shared",
            "-DCMAKE_BUILD_TYPE=Release", f"-DLAMBO_HOST_TOOLS={host.as_posix()}/bin")
        run(args.cmake, "--build", WORK / "native", "--target", "lamborghini_modern", "-j", args.jobs)
    apk = package(ndk, sdk, "release" if args.release else "debug")
    if args.install:
        run(sdk / "platform-tools" / ("adb.exe" if os.name == "nt" else "adb"), "install", "-r", apk)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
