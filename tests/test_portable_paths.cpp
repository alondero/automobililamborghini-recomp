// Portable-mode path resolution (issue #190): an empty portable.txt next to
// the executable (or --portable / LAMBO_PORTABLE) keeps settings and saves
// next to the game instead of %LOCALAPPDATA% / XDG. The executable directory
// wins over the launch working directory.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "lambo_paths.h"

namespace {

void set_env(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    if (value[0] == '\0')
        unsetenv(name);
    else
        setenv(name, value, 1);
#endif
}

struct PathsReset {
    PathsReset() { reset(); }
    ~PathsReset() { reset(); }
    static void reset() {
        lambo::paths::clear_executable_dir();
        lambo::paths::set_portable_forced(false);
        set_env("LAMBO_PORTABLE", "");
    }
};

struct CwdGuard {
    std::filesystem::path saved;
    bool ok = false;
    explicit CwdGuard(const std::filesystem::path& dir) {
        std::error_code ec;
        saved = std::filesystem::current_path(ec);
        if (ec) return;
        std::filesystem::current_path(dir, ec);
        ok = !ec;
    }
    ~CwdGuard() {
        if (ok) {
            std::error_code ec;
            std::filesystem::current_path(saved, ec);
        }
    }
};

bool same_dir(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    if (std::filesystem::equivalent(a, b, ec) && !ec) return true;
    return a == b;
}

void touch(const std::filesystem::path& path) {
    std::ofstream out{path};
    out << "";
}

} // namespace

int main() {
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    };

    const std::filesystem::path base =
        std::filesystem::temp_directory_path() / "lambo-portable-paths-test";
    const std::filesystem::path exe_dir = base / "exe";
    const std::filesystem::path cwd_dir = base / "cwd";
    const std::filesystem::path plain_dir = base / "plain";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(exe_dir, ec);
    std::filesystem::create_directories(cwd_dir, ec);
    std::filesystem::create_directories(plain_dir, ec);
    if (ec) {
        std::fprintf(stderr, "FAIL: cannot create temp dirs: %s\n", ec.message().c_str());
        return 1;
    }
    // Markers for the exe-wins and cwd-fallback cases; plain_dir stays clean
    // for the default-off / forced / env cases.
    touch(exe_dir / "portable.txt");
    touch(cwd_dir / "portable.txt");

    {
        // Executable-directory marker wins over the launch-directory marker.
        PathsReset reset;
        CwdGuard cwd{cwd_dir};
        expect(cwd.ok, "test setup: chdir to marker cwd works");
        lambo::paths::set_executable_dir(exe_dir);
        expect(lambo::paths::portable_mode(), "exe portable.txt enables portable mode");
        expect(same_dir(lambo::paths::portable_root(), exe_dir),
               "exe directory wins over launch directory");
        expect(same_dir(lambo::paths::app_config_dir(), exe_dir),
               "config dir follows the exe directory in portable mode");
        expect(same_dir(lambo::paths::app_state_dir(), exe_dir),
               "state dir follows the exe directory in portable mode");
        expect(std::string(lambo::paths::portable_mode_source()).find("executable") !=
                   std::string::npos,
               "source names the executable directory");
    }

    {
        // Launch-directory marker is the fallback when the exe dir is unknown
        // (developer build-tree runs) or has no marker.
        PathsReset reset;
        CwdGuard cwd{cwd_dir};
        expect(cwd.ok, "test setup: chdir to marker cwd works");
        expect(lambo::paths::portable_mode(), "cwd portable.txt enables portable mode");
        expect(same_dir(lambo::paths::portable_root(), cwd_dir),
               "launch directory is the portable fallback");
        expect(std::string(lambo::paths::portable_mode_source()).find("launch") !=
                   std::string::npos,
               "source names the launch directory fallback");
    }

    {
        // No markers anywhere: system locations, not portable.
        PathsReset reset;
        lambo::paths::set_executable_dir(plain_dir);
        CwdGuard cwd{plain_dir};
        expect(cwd.ok, "test setup: chdir to plain dir works");
        expect(!lambo::paths::portable_mode(), "no marker means portable mode is off");
    }

    {
        // --portable force flag: portable without any marker file, rooted at
        // the executable directory.
        PathsReset reset;
        lambo::paths::set_executable_dir(plain_dir);
        CwdGuard cwd{plain_dir};
        expect(cwd.ok, "test setup: chdir to plain dir works");
        lambo::paths::set_portable_forced(true);
        expect(lambo::paths::portable_mode(), "--portable forces portable mode");
        expect(same_dir(lambo::paths::portable_root(), plain_dir),
               "forced portable mode roots at the executable directory");
        expect(same_dir(lambo::paths::app_config_dir(), plain_dir),
               "forced portable config dir is the executable directory");
        expect(std::string(lambo::paths::portable_mode_source()).find("portable flag") !=
                   std::string::npos,
               "source names the --portable flag");
    }

    {
        // Forced mode without a known executable directory falls back to CWD.
        PathsReset reset;
        CwdGuard cwd{plain_dir};
        expect(cwd.ok, "test setup: chdir to plain dir works");
        lambo::paths::set_portable_forced(true);
        expect(lambo::paths::portable_mode(), "--portable works without exe dir");
        expect(same_dir(lambo::paths::portable_root(), plain_dir),
               "forced portable without exe dir uses the launch directory");
    }

    {
        // LAMBO_PORTABLE env var enables portable mode; falsy values opt out.
        PathsReset reset;
        lambo::paths::set_executable_dir(plain_dir);
        CwdGuard cwd{plain_dir};
        expect(cwd.ok, "test setup: chdir to plain dir works");
        set_env("LAMBO_PORTABLE", "1");
        expect(lambo::paths::portable_mode(), "LAMBO_PORTABLE=1 enables portable mode");
        expect(same_dir(lambo::paths::portable_root(), plain_dir),
               "env portable mode roots at the executable directory");
        expect(std::string(lambo::paths::portable_mode_source()).find("LAMBO_PORTABLE") !=
                   std::string::npos,
               "source names LAMBO_PORTABLE");
        for (const char* off : {"0", "false", "no", "off"}) {
            set_env("LAMBO_PORTABLE", off);
            expect(!lambo::paths::portable_mode(),
                   "falsy LAMBO_PORTABLE values leave portable mode off");
        }
    }

    std::filesystem::remove_all(base, ec);
    if (failures == 0) std::printf("portable paths: all cases passed\n");
    return failures == 0 ? 0 : 1;
}
