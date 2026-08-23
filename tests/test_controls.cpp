#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <atomic>

#include "json/json.hpp"

#include "controls/lambo_controls.h"
#include "ultramodern/config.hpp"

namespace ultramodern::renderer {
void set_graphics_config(const GraphicsConfig&) {}
}

namespace {
int failures = 0;
void expect(bool condition, const std::string& message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

std::filesystem::path temporary_path(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}
}

int main() {
    using namespace lambo::controls;
    Profile profile = default_profile();
    RawState raw{};

    const std::array button_cases{
        std::pair{LogicalButton::A, std::uint16_t{0x8000}},
        std::pair{LogicalButton::B, std::uint16_t{0x4000}},
        std::pair{LogicalButton::Start, std::uint16_t{0x1000}},
        std::pair{LogicalButton::LeftShoulder, std::uint16_t{0x0020}},
        std::pair{LogicalButton::RightShoulder, std::uint16_t{0x0010}},
        std::pair{LogicalButton::DpadUp, std::uint16_t{0x0800}},
        std::pair{LogicalButton::DpadDown, std::uint16_t{0x0400}},
        std::pair{LogicalButton::DpadLeft, std::uint16_t{0x0200}},
        std::pair{LogicalButton::DpadRight, std::uint16_t{0x0100}},
        std::pair{LogicalButton::Y, std::uint16_t{0x0008}},
        std::pair{LogicalButton::RightStick, std::uint16_t{0x0004}},
        std::pair{LogicalButton::X, std::uint16_t{0x0002}},
    };
    for (const auto& [button, expected] : button_cases) {
        raw = {};
        raw.buttons[static_cast<std::size_t>(button)] = true;
        expect(evaluate(profile, raw).buttons == expected,
               "default button parity for " + std::string(button_name(button)));
    }

    raw = {};
    raw.axes[static_cast<std::size_t>(LogicalAxis::TriggerLeft)] = 7999;
    expect(evaluate(profile, raw).buttons == 0, "trigger is inactive below 8000");
    raw.axes[static_cast<std::size_t>(LogicalAxis::TriggerLeft)] = 8000;
    expect(evaluate(profile, raw).buttons == 0, "trigger threshold is strict at 8000");
    raw.axes[static_cast<std::size_t>(LogicalAxis::TriggerLeft)] = 8001;
    expect(evaluate(profile, raw).buttons == 0x2000, "left trigger maps to Z above threshold");
    raw = {};
    raw.axes[static_cast<std::size_t>(LogicalAxis::RightX)] = 11999;
    expect(evaluate(profile, raw).buttons == 0, "C axis is inactive below 12000");
    raw.axes[static_cast<std::size_t>(LogicalAxis::RightX)] = 12000;
    expect(evaluate(profile, raw).buttons == 0, "C threshold is strict at 12000");
    raw.axes[static_cast<std::size_t>(LogicalAxis::RightX)] = 12001;
    expect(evaluate(profile, raw).buttons == 0x0001, "right X maps to C Right");
    raw.axes[static_cast<std::size_t>(LogicalAxis::RightX)] = -12001;
    expect(evaluate(profile, raw).buttons == 0x0002, "negative right X maps to C Left");
    raw = {};
    raw.axes[static_cast<std::size_t>(LogicalAxis::RightY)] = 12001;
    expect(evaluate(profile, raw).buttons == 0x0004, "positive right Y maps to C Down");
    raw.axes[static_cast<std::size_t>(LogicalAxis::RightY)] = -12001;
    expect(evaluate(profile, raw).buttons == 0x0008, "negative right Y maps to C Up");
    raw = {};
    raw.axes[static_cast<std::size_t>(LogicalAxis::TriggerRight)] = 8001;
    expect(evaluate(profile, raw).buttons == 0x0010, "right trigger is the second R source");

    raw = {};
    raw.axes[static_cast<std::size_t>(LogicalAxis::LeftX)] = 7999;
    raw.axes[static_cast<std::size_t>(LogicalAxis::LeftY)] = 7999;
    expect(evaluate(profile, raw).stick_x == 0 && evaluate(profile, raw).stick_y == 0,
           "values strictly inside deadzone are neutral");
    raw.axes[static_cast<std::size_t>(LogicalAxis::LeftX)] = 8000;
    expect(evaluate(profile, raw).stick_x == 19, "deadzone boundary is not rescaled");
    raw.axes[static_cast<std::size_t>(LogicalAxis::LeftX)] = 32767;
    raw.axes[static_cast<std::size_t>(LogicalAxis::LeftY)] = 32767;
    expect(evaluate(profile, raw).stick_x == 80 && evaluate(profile, raw).stick_y == -80,
           "full range and Y inversion match legacy evaluator");
    raw.axes[static_cast<std::size_t>(LogicalAxis::LeftX)] = std::numeric_limits<std::int16_t>::min();
    expect(evaluate(profile, raw).stick_x == -80, "-32768 clamps safely to -80");

    raw = {};
    raw.buttons[static_cast<std::size_t>(LogicalButton::RightShoulder)] = true;
    raw.axes[static_cast<std::size_t>(LogicalAxis::TriggerRight)] = 20000;
    expect(evaluate(profile, raw).buttons == 0x0010, "multiple sources OR without duplicating target");

    expect(canonicalize_guid("030000005E0400008E02000014010000") ==
           std::optional<std::string>{"030000005e0400008e02000014010000"},
           "GUID is canonical lowercase");
    expect(!canonicalize_guid("not-a-guid"), "invalid GUID is rejected");

    const auto path = temporary_path("lambo-controls-test.json");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    expect(load_config(path).status == LoadStatus::Missing, "missing file uses in-memory defaults");

    ControlsConfig config{};
    config.preferred_controller_guid = "030000005e0400008e02000014010000";
    profile_for_guid(config, config.preferred_controller_guid).digital[0].clear();
    config.preserved_document = R"({"version":1,"unknown_root":{"keep":true},"profiles":{"030000005e0400008e02000014010000":{"unknown_profile":7,"digital":{"b":[{"type":"button","button":"b","vendor_note":"keep"}]}}}})";
    expect(save_config(config, path).saved, "controls config saves atomically");
    auto loaded = load_config(path);
    expect(loaded.status == LoadStatus::Loaded, "saved config reloads");
    expect(profile_for_guid(loaded.config, config.preferred_controller_guid).digital[0].empty(),
           "explicit empty binding remains unbound");
    std::ifstream saved_file(path);
    nlohmann::json saved; saved_file >> saved;
    expect(saved["unknown_root"]["keep"] == true, "unknown root fields survive rewrite");
    expect(saved["profiles"][config.preferred_controller_guid]["unknown_profile"] == 7,
           "unknown profile fields survive rewrite");
    expect(saved["profiles"][config.preferred_controller_guid]["digital"]["b"][0]["vendor_note"] == "keep",
           "unknown binding fields survive rewrite");

    const std::string guid = config.preferred_controller_guid;
    { std::ofstream partial(path, std::ios::trunc); partial <<
        R"({"version":1,"profiles":{"030000005e0400008e02000014010000":{"digital":{"a":[{"type":"button","button":"b"},{"type":"broken"}],"z":"invalid","c_right":[]},"analog":{"stick_x":{"axis":"missing","invert":false,"deadzone":8000}}}}})"; }
    auto partial = load_config(path);
    const Profile& partial_profile = profile_for_guid(partial.config, guid);
    expect(partial_profile.digital[0] == DigitalBindings{ButtonSource{LogicalButton::B}},
           "invalid individual binding is rejected without losing valid siblings");
    expect(partial_profile.digital[2] == default_profile().digital[2],
           "invalid target container falls back only that target");
    expect(partial_profile.digital[13].empty(), "explicit empty partial target remains unbound");
    expect(partial_profile.analog[0] == default_profile().analog[0],
           "invalid analog binding falls back only that stick");
    expect(!partial.warnings.empty(), "invalid and cross-target bindings produce warnings");

    ControlsConfig shared{};
    Profile& first_identical = profile_for_guid(shared, guid);
    first_identical.digital[0].clear();
    expect(profile_for_guid(shared, guid).digital[0].empty(),
           "identical GUID controllers intentionally share one profile");

    const auto blocked = temporary_path("lambo-controls-save-blocked");
    std::filesystem::remove_all(blocked, ec);
    std::filesystem::create_directory(blocked, ec);
    expect(!save_config(config, blocked).saved, "atomic replacement failure is reported truthfully");
    expect(std::filesystem::is_directory(blocked), "failed save leaves the original destination intact");
    std::filesystem::remove_all(blocked, ec);

    { std::ofstream malformed(path, std::ios::trunc); malformed << "{broken"; }
    const std::string malformed_bytes = "{broken";
    auto malformed = load_config(path);
    expect(malformed.status == LoadStatus::Malformed && malformed.config.read_only,
           "malformed document becomes read-only defaults");
    expect(!save_config(malformed.config, path).saved, "read-only malformed config is untouched");
    std::ifstream unchanged(path); std::string unchanged_bytes((std::istreambuf_iterator<char>(unchanged)), {});
    expect(unchanged_bytes == malformed_bytes, "malformed bytes remain unchanged");

    { std::ofstream future(path, std::ios::trunc); future << R"({"version":2,"future":true})"; }
    auto future = load_config(path);
    expect(future.status == LoadStatus::FutureVersion && future.config.read_only,
           "future document becomes read-only defaults");

    { std::ofstream oversized(path, std::ios::trunc); oversized <<
        R"({"version":1,"profiles":{"030000005e0400008e02000014010000":{"digital":{"z":[{"type":"axis","axis":"trigger_left","direction":"positive","threshold":999999999999999999999999}]}}}})"; }
    auto oversized = load_config(path);
    expect(oversized.status == LoadStatus::Loaded &&
           profile_for_guid(oversized.config, guid).digital[2].empty(),
           "out-of-range binding integers are rejected without aborting the load");

    std::atomic<bool> publishing{true};
    std::thread publisher([&] {
        for (std::uint64_t revision = 1; revision <= 1000; ++revision) {
            UiSnapshot snapshot{};
            snapshot.config_revision = revision;
            snapshot.sample_revision = revision;
            publish_ui_snapshot(std::move(snapshot));
        }
        publishing.store(false);
    });
    std::thread reader([&] {
        while (publishing.load()) {
            const auto snapshot = ui_snapshot();
            expect(snapshot.profile.digital.size() == digital_target_count,
                   "UI snapshot copy remains complete under concurrency");
        }
    });
    std::thread commands_a([] {
        for (int i = 0; i < 100; ++i) enqueue_command({CommandKind::ResetProfile});
    });
    std::thread commands_b([] {
        for (int i = 0; i < 100; ++i) enqueue_command({CommandKind::CaptureCancel});
    });
    publisher.join(); reader.join(); commands_a.join(); commands_b.join();
    expect(take_commands().size() == 200, "command queue is lossless under concurrent writers");
    std::filesystem::remove(path, ec);
    return failures == 0 ? 0 : 1;
}
