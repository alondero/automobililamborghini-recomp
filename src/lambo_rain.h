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
bool road_mesh(const uint8_t* ram, uint32_t list, std::vector<Triangle>& triangles);
bool floor(const Triangle& triangle);
bool covered(Point p, const std::vector<Triangle>& roof);
struct Vertex { Point p; uint32_t rgba; };
// Non-indexed triangles. Geometry stays strictly inside each source road face.
std::vector<Vertex> puddles(const std::vector<Triangle>& road,
    const std::vector<Triangle>& roof, bool modern, float seconds, float intensity);
uint16_t cloud_pixel(int x, int y, bool modern);
uint32_t hash(uint32_t value);
}

extern "C" void lambo_rain_frame(uint8_t* ram);
extern "C" void lambo_rain_publish(uint8_t* ram, uint32_t root, uint32_t bytes);
extern "C" void lambo_rain_road(uint8_t* ram, uint32_t record);
extern "C" void lambo_rain_camera(uint8_t* ram);
extern "C" unsigned lambo_rain_active(uint8_t* ram);
// Weather selection is independent of style Off and diagnostic rendering.
extern "C" unsigned lambo_rain_wet(uint8_t* ram);
extern "C" uint32_t lambo_rain_sky(uint8_t* ram, int tile, int row, uint32_t original);
