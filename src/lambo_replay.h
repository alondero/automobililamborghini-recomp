#ifndef LAMBO_REPLAY_H
#define LAMBO_REPLAY_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lambo::replay {

// Controller state observed at one game-dispatch tick. Values deliberately
// retain the N64 controller's native widths; traces still validate every
// field before exposing any frame to playback.
struct InputFrame {
    std::uint16_t buttons{};
    std::int8_t stick_x{};
    std::int8_t stick_y{};
    bool throttle_analog{};
    std::uint16_t throttle{};
    bool brake_analog{};
    std::uint16_t brake{};

    friend bool operator==(const InputFrame&, const InputFrame&) = default;
};

class Trace;
struct LoadResult;

LoadResult load_trace(const std::filesystem::path& path);

// An immutable, run-length encoded trace. frame_at() uses a zero-based frame
// index and returns false when the index is outside the trace.
class Trace {
public:
    Trace() = default;

    [[nodiscard]] bool empty() const noexcept { return total_frames_ == 0; }
    [[nodiscard]] std::uint64_t total_frames() const noexcept { return total_frames_; }
    [[nodiscard]] bool frame_at(std::uint64_t index, InputFrame& output) const noexcept;

private:
    struct Run {
        std::uint64_t end_frame{};
        InputFrame input{};
    };

    explicit Trace(std::vector<Run> runs, std::uint64_t total_frames);

    std::vector<Run> runs_;
    std::uint64_t total_frames_{};

    friend LoadResult load_trace(const std::filesystem::path& path);
};

// Loading is intentionally non-throwing for file, syntax, and schema errors so
// a command-line harness can report a diagnostic without unwinding the game.
struct LoadResult {
    std::optional<Trace> trace;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept { return trace.has_value(); }
};

// Finalization is the publication boundary, so an abandoned capture cannot replace a prior trace.
class Recorder {
public:
    explicit Recorder(std::filesystem::path path);
    ~Recorder();

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;
    Recorder(Recorder&&) = delete;
    Recorder& operator=(Recorder&&) = delete;

    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] bool observe(const InputFrame& input);
    [[nodiscard]] bool finalize();
    [[nodiscard]] std::uint64_t total_frames() const noexcept;
    [[nodiscard]] const std::string& error() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lambo::replay

#endif // LAMBO_REPLAY_H
