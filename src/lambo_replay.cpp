#include "lambo_replay.h"

#include "json/json.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace lambo::replay {
namespace {

using json = nlohmann::json;

constexpr char kFormat[] = "lambo-input-trace";
constexpr char kClock[] = "game-dispatch";
constexpr std::uint64_t kVersion = 1;
constexpr std::uint64_t kPorts = 1;

std::string display_path(const std::filesystem::path& path) {
    const std::string text = path.string();
    return text.empty() ? std::string{"<empty path>"} : text;
}

LoadResult load_failure(const std::filesystem::path& path, std::uint64_t line,
                        const std::string& message) {
    std::ostringstream error;
    error << "trace '" << display_path(path) << "'";
    if (line != 0) error << " line " << line;
    error << ": " << message;
    return LoadResult{std::nullopt, error.str()};
}

bool parse_json_line(const std::string& line, json& result, std::string& error) {
    std::vector<std::unordered_set<std::string>> object_keys;
    std::string duplicate_key;
    const json::parser_callback_t reject_duplicates =
        [&](int, json::parse_event_t event, json& parsed) {
            if (event == json::parse_event_t::object_start) {
                object_keys.emplace_back();
            } else if (event == json::parse_event_t::key && !object_keys.empty()) {
                const std::string key = parsed.get<std::string>();
                if (!object_keys.back().insert(key).second && duplicate_key.empty()) {
                    duplicate_key = key;
                }
            } else if (event == json::parse_event_t::object_end && !object_keys.empty()) {
                object_keys.pop_back();
            }
            return true;
        };

    try {
        result = json::parse(line, reject_duplicates);
    } catch (const json::exception& exception) {
        error = std::string{"invalid JSON: "} + exception.what();
        return false;
    }
    if (!duplicate_key.empty()) {
        error = "duplicate field '" + duplicate_key + "'";
        return false;
    }
    return true;
}

bool has_field(const json& record, std::string_view field) {
    return record.find(std::string{field}) != record.end();
}

bool has_exact_fields(const json& record,
                      std::initializer_list<std::string_view> expected,
                      std::string_view record_name, std::string& error) {
    if (!record.is_object()) {
        error = std::string{record_name} + " record must be a JSON object";
        return false;
    }

    for (const std::string_view field : expected) {
        if (!has_field(record, field)) {
            error = std::string{record_name} + " record is missing field '" +
                    std::string{field} + "'";
            return false;
        }
    }
    for (auto it = record.begin(); it != record.end(); ++it) {
        const bool known = std::find(expected.begin(), expected.end(), it.key()) != expected.end();
        if (!known) {
            error = std::string{record_name} + " record has unknown field '" + it.key() + "'";
            return false;
        }
    }
    return true;
}

bool read_string(const json& record, std::string_view field, std::string_view expected,
                 std::string& error) {
    const json& value = record.at(std::string{field});
    if (!value.is_string()) {
        error = "field '" + std::string{field} + "' must be a string";
        return false;
    }
    const std::string actual = value.get<std::string>();
    if (actual != expected) {
        error = "field '" + std::string{field} + "' must be '" +
                std::string{expected} + "' (got '" + actual + "')";
        return false;
    }
    return true;
}

bool read_unsigned(const json& record, std::string_view field, std::uint64_t minimum,
                   std::uint64_t maximum, std::uint64_t& result, std::string& error) {
    const json& value = record.at(std::string{field});
    if (!value.is_number_unsigned()) {
        error = "field '" + std::string{field} + "' must be an unsigned integer";
        return false;
    }
    result = value.get<std::uint64_t>();
    if (result < minimum || result > maximum) {
        std::ostringstream range_error;
        range_error << "field '" << field << "' must be in [" << minimum << ", "
                    << maximum << "] (got " << result << ')';
        error = range_error.str();
        return false;
    }
    return true;
}

bool read_signed(const json& record, std::string_view field, std::int64_t minimum,
                 std::int64_t maximum, std::int64_t& result, std::string& error) {
    const json& value = record.at(std::string{field});
    if (value.is_number_unsigned()) {
        const std::uint64_t unsigned_value = value.get<std::uint64_t>();
        if (unsigned_value > static_cast<std::uint64_t>(maximum)) {
            std::ostringstream range_error;
            range_error << "field '" << field << "' must be in [" << minimum << ", "
                        << maximum << "] (got " << unsigned_value << ')';
            error = range_error.str();
            return false;
        }
        result = static_cast<std::int64_t>(unsigned_value);
    } else if (value.is_number_integer()) {
        result = value.get<std::int64_t>();
    } else {
        error = "field '" + std::string{field} + "' must be an integer";
        return false;
    }

    if (result < minimum || result > maximum) {
        std::ostringstream range_error;
        range_error << "field '" << field << "' must be in [" << minimum << ", "
                    << maximum << "] (got " << result << ')';
        error = range_error.str();
        return false;
    }
    return true;
}

bool read_bool(const json& record, std::string_view field, bool& result, std::string& error) {
    const json& value = record.at(std::string{field});
    if (!value.is_boolean()) {
        error = "field '" + std::string{field} + "' must be a boolean";
        return false;
    }
    result = value.get<bool>();
    return true;
}

bool validate_header(const json& record, std::string& error) {
    if (!has_exact_fields(record, {"type", "format", "version", "clock", "ports"},
                          "header", error)) {
        return false;
    }
    if (!read_string(record, "type", "header", error) ||
        !read_string(record, "format", kFormat, error) ||
        !read_string(record, "clock", kClock, error)) {
        return false;
    }
    std::uint64_t value = 0;
    if (!read_unsigned(record, "version", kVersion, kVersion, value, error)) return false;
    if (!read_unsigned(record, "ports", kPorts, kPorts, value, error)) return false;
    return true;
}

bool parse_input_run(const json& record, std::uint64_t& frames, InputFrame& input,
                     std::string& error) {
    if (!has_exact_fields(record,
                          {"type", "frames", "buttons", "stick_x", "stick_y",
                           "throttle_analog", "throttle", "brake_analog", "brake"},
                          "run", error)) {
        return false;
    }
    if (!read_string(record, "type", "run", error)) return false;

    std::uint64_t value = 0;
    if (!read_unsigned(record, "frames", 1, std::numeric_limits<std::uint64_t>::max(),
                       frames, error)) {
        return false;
    }
    if (!read_unsigned(record, "buttons", 0, std::numeric_limits<std::uint16_t>::max(),
                       value, error)) {
        return false;
    }
    input.buttons = static_cast<std::uint16_t>(value);

    std::int64_t signed_value = 0;
    if (!read_signed(record, "stick_x", -80, 80, signed_value, error)) return false;
    input.stick_x = static_cast<std::int8_t>(signed_value);
    if (!read_signed(record, "stick_y", -80, 80, signed_value, error)) return false;
    input.stick_y = static_cast<std::int8_t>(signed_value);

    if (!read_bool(record, "throttle_analog", input.throttle_analog, error)) return false;
    if (!read_unsigned(record, "throttle", 0, std::numeric_limits<std::uint16_t>::max(),
                       value, error)) {
        return false;
    }
    input.throttle = static_cast<std::uint16_t>(value);

    if (!read_bool(record, "brake_analog", input.brake_analog, error)) return false;
    if (!read_unsigned(record, "brake", 0, std::numeric_limits<std::uint16_t>::max(),
                       value, error)) {
        return false;
    }
    input.brake = static_cast<std::uint16_t>(value);
    return true;
}

bool parse_end(const json& record, std::uint64_t& frames, std::string& error) {
    if (!has_exact_fields(record, {"type", "frames"}, "end", error)) return false;
    if (!read_string(record, "type", "end", error)) return false;
    return read_unsigned(record, "frames", 1, std::numeric_limits<std::uint64_t>::max(),
                         frames, error);
}

bool input_is_valid(const InputFrame& input, std::string& error) {
    if (input.stick_x < -80 || input.stick_x > 80) {
        error = "stick_x must be in [-80, 80]";
        return false;
    }
    if (input.stick_y < -80 || input.stick_y > 80) {
        error = "stick_y must be in [-80, 80]";
        return false;
    }
    return true;
}

json header_record() {
    return json{{"type", "header"}, {"format", kFormat}, {"version", kVersion},
                {"clock", kClock}, {"ports", kPorts}};
}

json run_record(std::uint64_t frames, const InputFrame& input) {
    return json{{"type", "run"},
                {"frames", frames},
                {"buttons", input.buttons},
                {"stick_x", static_cast<int>(input.stick_x)},
                {"stick_y", static_cast<int>(input.stick_y)},
                {"throttle_analog", input.throttle_analog},
                {"throttle", input.throttle},
                {"brake_analog", input.brake_analog},
                {"brake", input.brake}};
}

bool atomic_replace(const std::filesystem::path& source,
                    const std::filesystem::path& destination, std::string& error) {
#if defined(_WIN32)
    if (MoveFileExW(source.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        return true;
    }
    const std::error_code ec{static_cast<int>(GetLastError()), std::system_category()};
#else
    if (std::rename(source.c_str(), destination.c_str()) == 0) return true;
    const std::error_code ec{errno, std::generic_category()};
#endif
    error = "could not publish temporary trace '" + display_path(source) + "' as '" +
            display_path(destination) + "': " + ec.message();
    return false;
}

} // namespace

Trace::Trace(std::vector<Run> runs, std::uint64_t total_frames)
    : runs_(std::move(runs)), total_frames_(total_frames) {}

const InputFrame& Trace::frame_at(std::uint64_t index) const {
    if (index >= total_frames_) {
        std::ostringstream error;
        error << "replay frame " << index << " is out of range for " << total_frames_
              << "-frame trace";
        throw std::out_of_range(error.str());
    }
    const auto run = std::lower_bound(
        runs_.begin(), runs_.end(), index,
        [](const Run& candidate, std::uint64_t frame) { return candidate.end_frame <= frame; });
    return run->input;
}

LoadResult load_trace(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return load_failure(path, 0, "could not open file");
    }

    std::string line;
    std::uint64_t line_number = 1;
    if (!std::getline(input, line)) {
        if (input.bad()) return load_failure(path, 0, "I/O error while reading header");
        return load_failure(path, 0, "file is empty; expected a header record");
    }

    json record;
    std::string validation_error;
    if (!parse_json_line(line, record, validation_error)) {
        return load_failure(path, line_number, validation_error);
    }

    if (!validate_header(record, validation_error)) {
        return load_failure(path, line_number, validation_error);
    }

    std::vector<Trace::Run> runs;
    std::uint64_t total_frames = 0;
    bool found_end = false;

    while (std::getline(input, line)) {
        ++line_number;
        if (!parse_json_line(line, record, validation_error)) {
            return load_failure(path, line_number, validation_error);
        }
        if (!record.is_object()) {
            return load_failure(path, line_number, "record must be a JSON object");
        }
        const auto type_it = record.find("type");
        if (type_it == record.end()) {
            return load_failure(path, line_number, "record is missing field 'type'");
        }
        if (!type_it->is_string()) {
            return load_failure(path, line_number, "field 'type' must be a string");
        }
        if (found_end) {
            return load_failure(path, line_number, "record appears after the end trailer");
        }

        const std::string type = type_it->get<std::string>();
        if (type == "run") {
            std::uint64_t frames = 0;
            InputFrame frame{};
            if (!parse_input_run(record, frames, frame, validation_error)) {
                return load_failure(path, line_number, validation_error);
            }
            if (frames > std::numeric_limits<std::uint64_t>::max() - total_frames) {
                return load_failure(path, line_number,
                                    "cumulative frame count overflows uint64");
            }
            total_frames += frames;
            runs.push_back(Trace::Run{total_frames, frame});
        } else if (type == "end") {
            if (runs.empty()) {
                return load_failure(path, line_number,
                                    "end trailer requires at least one run record");
            }
            std::uint64_t declared_frames = 0;
            if (!parse_end(record, declared_frames, validation_error)) {
                return load_failure(path, line_number, validation_error);
            }
            if (declared_frames != total_frames) {
                std::ostringstream mismatch;
                mismatch << "end frame count " << declared_frames
                         << " does not match run total " << total_frames;
                return load_failure(path, line_number, mismatch.str());
            }
            found_end = true;
        } else {
            return load_failure(path, line_number,
                                "unknown record type '" + type + "'");
        }
    }

    if (input.bad()) {
        return load_failure(path, line_number, "I/O error while reading trace");
    }
    if (!found_end) {
        return load_failure(path, line_number, "missing end trailer");
    }
    return LoadResult{Trace{std::move(runs), total_frames}, {}};
}

struct Recorder::Impl {
    enum class State { Failed, Writing, Finalized };

    explicit Impl(std::filesystem::path path)
        : destination(std::move(path)), temporary(destination) {
        if (destination.empty()) {
            error_message = "trace destination path is empty";
            return;
        }
        temporary += ".tmp";

        const std::filesystem::path parent = destination.parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                error_message = "could not create trace directory '" + display_path(parent) +
                                "': " + ec.message();
                return;
            }
        }

        output.open(temporary, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output.is_open()) {
            error_message = "could not open temporary trace '" + display_path(temporary) + "'";
            return;
        }
        owns_temporary = true;
        output << header_record().dump() << '\n';
        if (!output.good()) {
            fail("could not write header to temporary trace '" + display_path(temporary) + "'");
            return;
        }
        state = State::Writing;
    }

    ~Impl() {
        if (state != State::Finalized) discard_temporary();
    }

    bool fail(std::string message) {
        if (error_message.empty()) error_message = std::move(message);
        state = State::Failed;
        discard_temporary();
        return false;
    }

    void discard_temporary() noexcept {
        if (output.is_open()) output.close();
        if (owns_temporary && !temporary.empty()) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            owns_temporary = false;
        }
    }

    bool flush_pending() {
        if (!pending.has_value()) return true;
        output << run_record(pending_frames, *pending).dump() << '\n';
        if (!output.good()) {
            return fail("could not write run to temporary trace '" +
                        display_path(temporary) + "'");
        }
        pending.reset();
        pending_frames = 0;
        return true;
    }

    std::filesystem::path destination;
    std::filesystem::path temporary;
    std::ofstream output;
    std::optional<InputFrame> pending;
    std::uint64_t pending_frames{};
    std::uint64_t observed_frames{};
    State state{State::Failed};
    bool owns_temporary{};
    std::string error_message;
};

Recorder::Recorder(std::filesystem::path path)
    : impl_(std::make_unique<Impl>(std::move(path))) {}

Recorder::~Recorder() = default;

bool Recorder::ready() const noexcept {
    return impl_->state == Impl::State::Writing;
}

bool Recorder::observe(const InputFrame& input) {
    if (impl_->state == Impl::State::Finalized) {
        impl_->error_message = "cannot observe input after recorder finalization";
        return false;
    }
    if (impl_->state != Impl::State::Writing) return false;

    std::string validation_error;
    if (!input_is_valid(input, validation_error)) {
        return impl_->fail("invalid input observation: " + validation_error);
    }
    if (impl_->observed_frames == std::numeric_limits<std::uint64_t>::max()) {
        return impl_->fail("recorded frame count overflows uint64");
    }

    if (!impl_->pending.has_value()) {
        impl_->pending = input;
        impl_->pending_frames = 1;
    } else if (*impl_->pending == input) {
        ++impl_->pending_frames;
    } else {
        if (!impl_->flush_pending()) return false;
        impl_->pending = input;
        impl_->pending_frames = 1;
    }
    ++impl_->observed_frames;
    return true;
}

bool Recorder::finalize() {
    if (impl_->state == Impl::State::Finalized) return true;
    if (impl_->state != Impl::State::Writing) return false;
    if (impl_->observed_frames == 0) {
        return impl_->fail("cannot finalize a trace with no input frames");
    }
    if (!impl_->flush_pending()) return false;

    impl_->output << json{{"type", "end"}, {"frames", impl_->observed_frames}}.dump()
                  << '\n';
    impl_->output.flush();
    if (!impl_->output.good()) {
        return impl_->fail("could not finish temporary trace '" +
                           display_path(impl_->temporary) + "'");
    }
    impl_->output.close();
    if (impl_->output.fail()) {
        return impl_->fail("could not close temporary trace '" +
                           display_path(impl_->temporary) + "'");
    }

    std::string publish_error;
    if (!atomic_replace(impl_->temporary, impl_->destination, publish_error)) {
        return impl_->fail(std::move(publish_error));
    }
    impl_->owns_temporary = false;
    impl_->state = Impl::State::Finalized;
    return true;
}

std::uint64_t Recorder::total_frames() const noexcept {
    return impl_->observed_frames;
}

const std::string& Recorder::error() const noexcept {
    return impl_->error_message;
}

} // namespace lambo::replay
