#include "lambo_replay.h"
#include "lambo_input_quantize.h"

#include "json/json.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using json = nlohmann::json;
using lambo::replay::InputFrame;

int failures = 0;
std::filesystem::path test_directory;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

json header() {
    return json{{"type", "header"},
                {"format", "lambo-input-trace"},
                {"version", 1},
                {"clock", "game-dispatch"},
                {"ports", 1}};
}

json run(std::uint64_t frames = 1) {
    return json{{"type", "run"},
                {"frames", frames},
                {"buttons", 0},
                {"stick_x", 0},
                {"stick_y", 0},
                {"throttle_analog", false},
                {"throttle", 0},
                {"brake_analog", false},
                {"brake", 0}};
}

json end(std::uint64_t frames) {
    return json{{"type", "end"}, {"frames", frames}};
}

std::string lines(const std::vector<json>& records, bool trailing_newline = true) {
    std::ostringstream text;
    for (std::size_t index = 0; index < records.size(); ++index) {
        text << records[index].dump();
        if (trailing_newline || index + 1 < records.size()) text << '\n';
    }
    return text.str();
}

void write_text(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << contents;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

void expect_rejected(const std::string& name, const std::string& contents,
                     const std::string& diagnostic) {
    const auto path = test_directory / (name + ".jsonl");
    write_text(path, contents);
    const auto loaded = lambo::replay::load_trace(path);
    expect(!loaded, name + " is rejected");
    expect(!loaded.trace.has_value(), name + " does not expose a partial trace");
    expect(loaded.error.find(diagnostic) != std::string::npos,
           name + " reports '" + diagnostic + "' (actual: " + loaded.error + ")");
}

void test_valid_boundaries_and_lookup() {
    json first = run(1);
    first["buttons"] = 0;
    first["stick_x"] = -80;
    first["stick_y"] = 80;
    first["throttle_analog"] = false;
    first["throttle"] = 0;
    first["brake_analog"] = true;
    first["brake"] = 65535;

    json second = run(2);
    second["buttons"] = 65535;
    second["stick_x"] = 80;
    second["stick_y"] = -80;
    second["throttle_analog"] = true;
    second["throttle"] = 65535;
    second["brake_analog"] = false;
    second["brake"] = 0;

    const auto path = test_directory / "valid-boundaries.jsonl";
    // A complete final JSON record does not require a trailing newline.
    write_text(path, lines({header(), first, second, end(3)}, false));
    const auto loaded = lambo::replay::load_trace(path);
    expect(static_cast<bool>(loaded), "valid boundary trace loads");
    expect(loaded.error.empty(), "successful load has no error");
    if (!loaded) return;

    expect(!loaded.trace->empty(), "loaded trace is not empty");
    expect(loaded.trace->total_frames() == 3, "run lengths produce total frame count");
    InputFrame actual{};
    expect(loaded.trace->frame_at(0, actual) &&
               actual == InputFrame{0, -80, 80, false, 0, true, 65535},
           "first run maps to frame zero");
    const InputFrame expected_second{65535, 80, -80, true, 65535, false, 0};
    expect(loaded.trace->frame_at(1, actual) && actual == expected_second,
           "second run maps to its first frame");
    expect(loaded.trace->frame_at(2, actual) && actual == expected_second,
           "second run maps to its last frame");
    expect(!loaded.trace->frame_at(3, actual),
           "frame_at(total_frames) returns false without throwing");

    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    const auto maximum_path = test_directory / "maximum-run.jsonl";
    write_text(maximum_path, lines({header(), run(maximum), end(maximum)}));
    const auto maximum_loaded = lambo::replay::load_trace(maximum_path);
    expect(static_cast<bool>(maximum_loaded), "UINT64_MAX frame run is accepted");
    if (maximum_loaded) {
        expect(maximum_loaded.trace->total_frames() == maximum,
               "UINT64_MAX total is represented exactly");
        expect(maximum_loaded.trace->frame_at(maximum - 1, actual) && actual == InputFrame{},
               "last addressable frame of maximum run is available");
    }
}

void test_invalid_headers_and_sequences() {
    expect_rejected("empty", "", "file is empty");
    expect_rejected("malformed-header", "{not json}\n", "invalid JSON");
    expect_rejected("scalar-header", "42\n", "header record must be a JSON object");

    json wrong = header();
    wrong["format"] = "something-else";
    expect_rejected("wrong-format", lines({wrong, run(), end(1)}), "lambo-input-trace");
    wrong = header();
    wrong["version"] = 2;
    expect_rejected("wrong-version", lines({wrong, run(), end(1)}), "[1, 1]");
    wrong = header();
    wrong["version"] = 1.0;
    expect_rejected("float-version", lines({wrong, run(), end(1)}), "unsigned integer");
    wrong = header();
    wrong["clock"] = "video-interface";
    expect_rejected("wrong-clock", lines({wrong, run(), end(1)}), "game-dispatch");
    wrong = header();
    wrong["ports"] = 2;
    expect_rejected("wrong-ports", lines({wrong, run(), end(1)}), "[1, 1]");
    wrong = header();
    wrong["extra"] = true;
    expect_rejected("extra-header-field", lines({wrong, run(), end(1)}), "unknown field 'extra'");
    wrong = header();
    wrong.erase("format");
    expect_rejected("missing-header-field", lines({wrong, run(), end(1)}), "missing field 'format'");

    expect_rejected("run-first", lines({run(), end(1)}), "header record");
    expect_rejected("end-before-run", lines({header(), end(1)}), "at least one run");
    expect_rejected("missing-end", lines({header(), run()}), "missing end trailer");
    expect_rejected("mismatched-end", lines({header(), run(2), end(1)}),
                    "does not match run total 2");
    expect_rejected("unknown-record", lines({header(), json{{"type", "metadata"}}}),
                    "unknown record type 'metadata'");
    expect_rejected("duplicate-header", lines({header(), run(), header(), end(1)}),
                    "unknown record type 'header'");
    expect_rejected("record-after-end", lines({header(), run(), end(1), run()}),
                    "after the end trailer");
    expect_rejected("blank-record", lines({header()}, false) + "\n\n" +
                                        lines({run(), end(1)}),
                    "invalid JSON");
    expect_rejected("scalar-record", lines({header()}, false) + "\n[]\n", "JSON object");

    const std::string duplicate =
        header().dump() + "\n" +
        "{\"type\":\"run\",\"frames\":1,\"frames\":2,\"buttons\":0,"
        "\"stick_x\":0,\"stick_y\":0,\"throttle_analog\":false,"
        "\"throttle\":0,\"brake_analog\":false,\"brake\":0}\n" +
        end(2).dump() + "\n";
    expect_rejected("duplicate-field", duplicate, "duplicate field 'frames'");
}

void test_invalid_run_fields() {
    struct Case {
        std::string name;
        std::string diagnostic;
        json record;
    };
    std::vector<Case> cases;

    auto add = [&](std::string name, std::string field, json value,
                   std::string diagnostic) {
        json record = run();
        record[std::move(field)] = std::move(value);
        cases.push_back(Case{std::move(name), std::move(diagnostic), std::move(record)});
    };

    add("zero-frames", "frames", 0, "[1,");
    add("negative-frames", "frames", -1, "unsigned integer");
    add("float-frames", "frames", 1.0, "unsigned integer");
    add("string-frames", "frames", "1", "unsigned integer");
    add("negative-buttons", "buttons", -1, "unsigned integer");
    add("large-buttons", "buttons", 65536, "[0, 65535]");
    add("float-buttons", "buttons", 1.0, "unsigned integer");
    add("low-stick-x", "stick_x", -81, "[-80, 80]");
    add("high-stick-y", "stick_y", 81, "[-80, 80]");
    add("float-stick", "stick_x", 1.0, "must be an integer");
    add("numeric-throttle-flag", "throttle_analog", 1, "must be a boolean");
    add("negative-throttle", "throttle", -1, "unsigned integer");
    add("large-throttle", "throttle", 65536, "[0, 65535]");
    add("string-brake-flag", "brake_analog", "false", "must be a boolean");
    add("negative-brake", "brake", -1, "unsigned integer");
    add("large-brake", "brake", 65536, "[0, 65535]");

    json missing = run();
    missing.erase("brake");
    cases.push_back({"missing-run-field", "missing field 'brake'", std::move(missing)});
    json extra = run();
    extra["player"] = 1;
    cases.push_back({"extra-run-field", "unknown field 'player'", std::move(extra)});

    for (const Case& test : cases) {
        expect_rejected(test.name, lines({header(), test.record, end(1)}), test.diagnostic);
    }

    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    expect_rejected("frame-total-overflow",
                    lines({header(), run(maximum), run(1), end(maximum)}),
                    "overflows uint64");

    json bad_end = end(1);
    bad_end["frames"] = 1.0;
    expect_rejected("float-end", lines({header(), run(), bad_end}), "unsigned integer");
    bad_end = end(1);
    bad_end["extra"] = 0;
    expect_rejected("extra-end-field", lines({header(), run(), bad_end}),
                    "unknown field 'extra'");
}

void test_recorder_round_trip_and_atomic_publish() {
    const auto path = test_directory / "recorded.jsonl";
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    write_text(path, "old trace stays visible\n");

    const InputFrame first{0x8000, -80, 80, true, 65535, false, 0};
    const InputFrame second{0x0001, 12, -34, false, 7, true, 4096};
    {
        lambo::replay::Recorder recorder(path);
        expect(recorder.ready(), "recorder accepts a destination before recording");
        expect(!std::filesystem::exists(temporary),
               "recorder does not touch disk before finalize");
        expect(read_text(path) == "old trace stays visible\n",
               "existing destination stays untouched while recording");
        expect(recorder.observe(first), "recorder accepts first observation");
        expect(recorder.observe(first), "recorder accepts identical observation");
        expect(recorder.observe(second), "recorder accepts changed observation");
        expect(recorder.observe(second), "recorder coalesces second observation");
        expect(recorder.observe(second), "recorder coalesces third observation");
        expect(recorder.total_frames() == 5, "recorder counts dispatcher observations");
        expect(recorder.finalize(), "recorder atomically publishes on finalize");
        expect(!recorder.ready(), "finalized recorder no longer accepts observations");
        expect(recorder.error().empty(), "successful recorder has no error");
        expect(recorder.finalize(), "finalize is idempotent");
    }
    expect(!std::filesystem::exists(temporary), "published temporary file is gone");
    expect(read_text(path) != "old trace stays visible\n", "finalize replaces old destination");

    const auto loaded = lambo::replay::load_trace(path);
    expect(static_cast<bool>(loaded), "recorded trace loads again");
    if (loaded) {
        expect(loaded.trace->total_frames() == 5, "round trip preserves frame total");
        InputFrame actual{};
        expect(loaded.trace->frame_at(0, actual) && actual == first &&
                   loaded.trace->frame_at(1, actual) && actual == first,
               "round trip preserves first RLE run");
        expect(loaded.trace->frame_at(2, actual) && actual == second &&
                   loaded.trace->frame_at(4, actual) && actual == second,
               "round trip preserves second RLE run");
    }

    std::ifstream recorded(path, std::ios::binary);
    std::string line;
    std::vector<std::uint64_t> run_lengths;
    while (std::getline(recorded, line)) {
        const json record = json::parse(line);
        if (record.at("type") == "run") run_lengths.push_back(record.at("frames"));
    }
    expect(run_lengths == std::vector<std::uint64_t>{2, 3},
           "recorder writes exactly two coalesced runs");
}

void test_recorder_failure_and_abandon_paths() {
    const auto abandoned = test_directory / "abandoned.jsonl";
    std::filesystem::path abandoned_temporary = abandoned;
    abandoned_temporary += ".tmp";
    {
        lambo::replay::Recorder recorder(abandoned);
        expect(recorder.ready() && recorder.observe(InputFrame{}),
               "abandon test records a frame");
    }
    expect(!std::filesystem::exists(abandoned), "destructor does not publish implicitly");
    expect(!std::filesystem::exists(abandoned_temporary),
           "destructor leaves no unpublished temporary file");

    const auto empty = test_directory / "empty-recording.jsonl";
    std::filesystem::path empty_temporary = empty;
    empty_temporary += ".tmp";
    write_text(empty, "existing destination\n");
    {
        lambo::replay::Recorder recorder(empty);
        expect(!recorder.finalize(), "zero-frame recording cannot create an invalid trace");
        expect(recorder.error().find("no input frames") != std::string::npos,
               "zero-frame finalize has a detailed error");
    }
    expect(read_text(empty) == "existing destination\n",
           "failed empty finalize preserves existing destination");
    expect(!std::filesystem::exists(empty_temporary),
           "failed empty finalize removes temporary file");

    const auto invalid = test_directory / "invalid-observation.jsonl";
    std::filesystem::path invalid_temporary = invalid;
    invalid_temporary += ".tmp";
    lambo::replay::Recorder invalid_recorder(invalid);
    InputFrame invalid_frame{};
    invalid_frame.stick_x = 81;
    expect(!invalid_recorder.observe(invalid_frame), "out-of-range observation is rejected");
    expect(!invalid_recorder.ready(), "invalid observation fails recorder");
    expect(invalid_recorder.error().find("stick_x") != std::string::npos,
           "invalid observation identifies its field");
    expect(!std::filesystem::exists(invalid_temporary),
           "invalid observation has not created a temporary file");
    expect(!invalid_recorder.finalize(), "failed recorder cannot publish");

    lambo::replay::Recorder empty_path(std::filesystem::path{});
    expect(!empty_path.ready(), "empty recorder destination is rejected");
    expect(empty_path.error().find("empty") != std::string::npos,
           "empty destination has a useful error");

    const auto blocker = test_directory / "not-a-directory";
    write_text(blocker, "file");
    lambo::replay::Recorder blocked(blocker / "trace.jsonl");
    expect(!blocked.ready(), "recorder reports an unusable parent directory");
    expect(!blocked.error().empty(), "unusable parent directory has an error");

    const auto occupied_temporary_destination = test_directory / "occupied-temp.jsonl";
    std::filesystem::path occupied_temporary = occupied_temporary_destination;
    occupied_temporary += ".tmp";
    std::filesystem::create_directory(occupied_temporary);
    {
        lambo::replay::Recorder occupied(occupied_temporary_destination);
        expect(occupied.ready() && occupied.observe(InputFrame{}),
               "recording remains in memory despite an occupied temporary path");
        expect(!occupied.finalize(), "occupied temporary path fails only at finalize");
    }
    expect(std::filesystem::is_directory(occupied_temporary),
           "recorder does not remove a temporary path it never opened");

    const auto directory_destination = test_directory / "directory-destination";
    std::filesystem::create_directory(directory_destination);
    std::filesystem::path publish_temporary = directory_destination;
    publish_temporary += ".tmp";
    lambo::replay::Recorder publish_failure(directory_destination);
    expect(publish_failure.ready() && publish_failure.observe(InputFrame{}),
           "publish-failure recorder reaches finalize");
    expect(!publish_failure.finalize(), "failed atomic rename is reported");
    expect(publish_failure.error().find("could not publish") != std::string::npos,
           "publish failure includes operation context");
    expect(std::filesystem::is_directory(directory_destination),
           "publish failure preserves existing destination");
    expect(!std::filesystem::exists(publish_temporary),
           "publish failure removes temporary file");
}

void test_quantize_normalized_is_finite_and_bounded() {
    expect(lambo::input::quantize_normalized(-1.0f) == 0,
           "negative normalized input clamps to zero");
    expect(lambo::input::quantize_normalized(0.5f) == 32768,
           "midpoint normalized input rounds to the nearest u16");
    expect(lambo::input::quantize_normalized(2.0f) == UINT16_MAX,
           "normalized input above one clamps to UINT16_MAX");
    expect(lambo::input::quantize_normalized(std::numeric_limits<float>::quiet_NaN()) == 0,
           "NaN normalized input safely clamps to zero");
    expect(lambo::input::quantize_normalized(std::numeric_limits<float>::infinity()) == 0,
           "positive infinity normalized input safely clamps to zero");
}

} // namespace

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    test_directory = std::filesystem::temp_directory_path() /
                     ("lambo-replay-test-" + std::to_string(unique));
    std::filesystem::create_directories(test_directory);

    test_valid_boundaries_and_lookup();
    test_invalid_headers_and_sequences();
    test_invalid_run_fields();
    test_recorder_round_trip_and_atomic_publish();
    test_recorder_failure_and_abandon_paths();
    test_quantize_normalized_is_finite_and_bounded();

    std::error_code cleanup_error;
    std::filesystem::remove_all(test_directory, cleanup_error);
    if (cleanup_error) {
        std::cerr << "FAIL: could not remove test directory: " << cleanup_error.message() << '\n';
        ++failures;
    }
    return failures == 0 ? 0 : 1;
}
