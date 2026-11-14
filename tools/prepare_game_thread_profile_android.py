"""Prepare an ignored debug Java shell for the Android profiling experiment.

Run scripts/build_android.py first. This does not install or launch an app.
The application ID is fixed to profile426 to preserve the player application.
"""
import argparse
import os
from pathlib import Path, PurePosixPath
import shlex
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk', type=Path, default=os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT'))
parser.add_argument('--stage', action='store_true', help='Refresh profiling fixtures after installing this shell')
parser.add_argument('--graphics', type=Path, help='Graphics JSON to stage; required with --stage')
args = parser.parse_args()
if not args.sdk:
    parser.error('Pass --sdk or set ANDROID_HOME')
if args.stage and (not args.graphics or not args.graphics.is_file()):
    parser.error('--stage requires --graphics with the experiment settings')
if not (root / 'build-android/package/jniLibs/arm64-v8a/libmain.so').is_file():
    parser.error('Build and package the native library with scripts/build_android.py first')
shell = root / 'scratch/gh426/shell'
shutil.copytree(root / 'android', shell, ignore=shutil.ignore_patterns('build', '.gradle', 'local.properties'), dirs_exist_ok=True)
gradle = shell / 'app/build.gradle'
source = gradle.read_text()
source = source.replace("def repoRoot = rootProject.projectDir.parentFile", "def repoRoot = new File('" + root.as_posix().replace("'", "\\'") + "')")
source = source.replace("applicationId 'io.github.alondero.lamborghinirecomp'", "applicationId 'io.github.alondero.lamborghinirecomp.profile426'")
gradle.write_text(source)
manifest = shell / 'app/src/main/AndroidManifest.xml'
source = manifest.read_text().replace('android:name=".GameActivity" android:exported="false"', 'android:name="io.github.alondero.lamborghinirecomp.GameActivity" android:exported="true"')
source = source.replace('android:name=".LauncherActivity"', 'android:name="io.github.alondero.lamborghinirecomp.LauncherActivity"')
manifest.write_text(source)
activity = shell / 'app/src/main/java/io/github/alondero/lamborghinirecomp/GameActivity.java'
source = activity.read_text()
source = source.replace('    @Override public void loadLibraries() {', '''    @Override protected String[] getArguments() { return new String[]{"--verbose"}; }

    @Override public void loadLibraries() {''')
source = source.replace('            DriverImport.configure(this);', '''            String files = getFilesDir().getAbsolutePath();
            Os.setenv("RT64_RENDER_COST_REPORT", "1", true);
            Os.setenv("LAMBO_WARP", getIntent().getStringExtra("warp"), true);
            Os.setenv("LAMBO_WARP_MODE", getIntent().getStringExtra("mode"), true);
            Os.setenv("LAMBO_INPUT_REPLAY", files + "/replay.jsonl", true);
            Os.setenv("LAMBO_INPUT_START_STATE", "8", true);
            Os.setenv("LAMBO_INPUT_START_DELAY", "0", true);
            Os.setenv("LAMBO_INPUT_EXIT_ON_END", "1", true);
            Os.setenv("LAMBO_MODERN_MAX_VIS", "30000", true);
            Os.setenv("LAMBO_GRAPHICS_CONFIG", files + "/profile/graphics.json", true);
            Os.setenv("LAMBO_CONTROLLER_PAK_FILE", files + "/profile/controller.mpk", true);
            Os.setenv("LAMBO_HARNESS_RESULT", files + "/profile/result.json", true);
            DriverImport.configure(this);''')
activity.write_text(source)
(shell / 'local.properties').write_text('sdk.dir=' + args.sdk.resolve().as_posix() + '\n')
print(shell)

if args.stage:
    package = 'io.github.alondero.lamborghinirecomp.profile426'

    def adb(*arguments):
        return subprocess.check_output(['adb', *map(str, arguments)], text=True, timeout=60).strip()

    def app(command):
        return adb('shell', 'run-as', package, 'sh', '-c', shlex.quote(command))

    def stage(source, destination):
        temporary = '/data/local/tmp/gh426-stage-' + str(os.getpid())
        adb('push', source, temporary)
        try:
            app('mkdir -p ' + shlex.quote(str(PurePosixPath(destination).parent)))
            app('cp ' + shlex.quote(temporary) + ' ' + shlex.quote(destination))
        finally:
            adb('shell', 'rm', temporary)

    processes = adb('shell', 'ps', '-A')
    if any(package in line or 'simpleperf' in line for line in processes.splitlines()):
        parser.error('Stop the profiling shell and recorder before staging')
    installed = adb('shell', 'pm', 'path', package).removeprefix('package:')
    if not installed.startswith('/data/app/') or '\n' in installed:
        parser.error('Expected a single installed profiling APK under /data/app')
    library = str(PurePosixPath(installed).parent / 'lib/arm64/libmain.so')
    stage(root / 'build-android/native/libmain.so', 'files/symbols' + library)
    stage(root / 'lamborghini.syms.toml', 'files/lamborghini.syms.toml')
    stage(root / 'recordings/circuit-1-time-trial-car-0.jsonl', 'files/replay.jsonl')
    stage(root / 'Automobili Lamborghini (USA).z64', 'files/Automobili Lamborghini (USA).z64')
    assets = root / 'build-android/package/assets/assets'
    for source in sorted(assets.rglob('*')):
        if source.is_file():
            stage(source, 'files/assets/' + source.relative_to(assets).as_posix())
    stage(args.graphics.resolve(), 'files/profile/graphics.json')
    stage(args.sdk / 'ndk/28.2.13676358/simpleperf/bin/android/arm64/simpleperf', 'files/simpleperf')
    app('chmod 700 files/simpleperf')
