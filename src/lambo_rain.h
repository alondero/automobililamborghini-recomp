#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace lambo::rain {
struct Point {
    float x = 0, y = 0, z = 0;
    Point operator+(Point b) const { return {x + b.x, y + b.y, z + b.z}; }
    Point operator-(Point b) const { return {x - b.x, y - b.y, z - b.z}; }
    Point operator*(float s) const { return {x * s, y * s, z * s}; }
};
struct Triangle { std::array<Point, 3> p; };
// Decode only bounded native F3DEX geometry with absolute/physical addresses.
// Matrices, segmented targets and dynamic vertex modifications fail closed.
// Input is word-swapped native 8 MiB RAM; no memory or asset pointer is retained.
// Output storage is reused; it is empty after a failure.
bool road_mesh(const uint8_t* ram, uint32_t list, std::vector<Triangle>& triangles);
bool floor(const Triangle& triangle);
// Coverage terms of one floor triangle, computed once per decoded roof.
struct RoofPlane { Point origin, a, b; float det = 0; };
// Appends planes for floor triangles; walls and vertical slivers cannot roof.
void roof_planes(const std::vector<Triangle>& triangles, std::vector<RoofPlane>& planes);
bool covered(Point p, const std::vector<RoofPlane>& roof);
struct Vertex { Point p; uint32_t rgba; };

constexpr int ripple_sides = 12;
// One puddle's ring frame. Static rings and animated ripples are both placed
// by point(), so rewriting a ripple reproduces a full regeneration exactly.
struct Puddle {
    Point center;
    float radius = 0, intensity = 0;
    uint32_t seed = 0, first = 0; // first: index of the puddle's first ring vertex
    int sides = 0;
    std::array<Point, ripple_sides> direction{};
    std::array<float, ripple_sides> shape{};
    Point point(int j, float r) const;
};
struct PuddleMesh {
    std::vector<Vertex> vertices; // non-indexed triangles, strictly inside each road face
    std::vector<Puddle> ripples;  // Modern only; Retro output has no time dependence
};
// Ripple vertices are placed for `seconds`; ripple() moves them to another time.
void puddle_mesh(const std::vector<Triangle>& road, const std::vector<RoofPlane>& roof,
    bool modern, float seconds, float intensity, PuddleMesh& mesh);
uint32_t ripple_vertex(const Puddle& puddle, int side);
float ripple_phase(const Puddle& puddle, float seconds);
// Writes the six vertices at ripple_vertex(puddle, side) for a ripple_phase().
void ripple(const Puddle& puddle, int side, float phase, Vertex* out);
std::vector<Vertex> puddles(const std::vector<Triangle>& road,
    const std::vector<Triangle>& roof, bool modern, float seconds, float intensity);
// Guest s16 vertex coordinate: int16_t(std::lround(f)) without a libm call.
// Exact for |f| < 2^31, which bounds every puddle coordinate.
inline int16_t coordinate(float f) {
    int32_t i = int32_t(f);
    float d = f - float(i);
    return int16_t(i + (d >= 0.5f) - (d <= -0.5f));
}
// Modern camera rain for one view, in native pixels from the top-left of the
// view's widened area (width x height). RT64 snaps rectangle edges to whole
// native pixels, so this art is triangles that it rasterizes at output
// resolution; see docs/rain.md, "Camera rain".
struct ScreenVertex { float x = 0, y = 0; uint32_t rgba = 0; };
// A shape's triangles index its own vertices, so one vertex load can hold it.
struct ScreenShape { uint32_t first = 0, count = 0, first_index = 0, index_count = 0; };
constexpr uint32_t screen_shape_vertices = 32; // most vertices in one shape
struct ScreenMesh {
    std::vector<ScreenVertex> vertices;
    std::vector<uint8_t> indices; // triangle corners, relative to the shape's first vertex
    std::vector<ScreenShape> shapes;
};
void screen_rain(float width, float height, int player, float seconds, float intensity,
    bool droplets, ScreenMesh& mesh);
uint16_t cloud_pixel(int x, int y, bool modern);
uint32_t hash(uint32_t value);

// Producer instrumentation (game thread). docs/rain.md, "Cache lifecycle".
struct Counters {
    uint64_t decodes = 0;       // road_mesh calls
    uint64_t builds = 0;        // segment geometry generations
    uint64_t full_writes = 0;   // static geometry copied into a task buffer
    uint64_t ripple_writes = 0; // Modern ripple-only buffer updates
    // Guest heap held by the rain producers, in requested bytes (the heap may round up).
    uint64_t heap_bytes = 0, peak_heap_bytes = 0; // every rain block
    uint64_t road_bytes = 0, peak_road_bytes = 0; // road buffers only
    std::array<uint32_t, 2> road_buffers{};       // live road buffers per task slot
};
const Counters& counters();
float animation_seconds();
}

extern "C" void lambo_rain_frame(uint8_t* ram);
extern "C" void lambo_rain_publish(uint8_t* ram, uint32_t root, uint32_t bytes);
extern "C" void lambo_rain_road(uint8_t* ram, uint32_t record);
extern "C" void lambo_rain_camera(uint8_t* ram);
// Track load and save-state load: drop cached geometry before the next frame.
extern "C" void lambo_rain_invalidate(void);
extern "C" unsigned lambo_rain_active(uint8_t* ram);
// Weather selection is independent of style Off and diagnostic rendering.
extern "C" unsigned lambo_rain_wet(uint8_t* ram);
extern "C" uint32_t lambo_rain_sky(uint8_t* ram, int tile, int row, uint32_t original);
