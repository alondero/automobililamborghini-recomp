// Minimal stdin/stdout CLI around xBRZ (GPLv3, (c) Zenju).
//   xbrz_cli <factor 2-6> <width> <height>
// Reads width*height raw pixels (4 bytes each, R,G,B,A byte order) from stdin,
// writes factor^2 * that many scaled pixels in the same byte order to stdout.
#include "xbrz.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cstdint>
#include <limits>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

int main(int argc, char** argv)
{
    if (argc != 4) { std::fprintf(stderr, "usage: xbrz_cli <factor 2-6> <width> <height>\n"); return 1; }
    const int factor = std::atoi(argv[1]);
    const int w = std::atoi(argv[2]);
    const int h = std::atoi(argv[3]);
    if (factor < 2 || factor > SCALE_FACTOR_MAX || w <= 0 || h <= 0) { std::fprintf(stderr, "bad args\n"); return 1; }
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::vector<uint32_t> src(static_cast<size_t>(w) * h);
    if (std::fread(src.data(), 4, src.size(), stdin) != src.size()) { std::fprintf(stderr, "short read\n"); return 1; }
    // bytes R,G,B,A (little-endian uint32 0xAABBGGRR) -> xBRZ wants A<<24|R<<16|G<<8|B
    // (its getAlpha/getRed accessors in xbrz_tools.h read bytes 3/2/1/0)
    for (auto& p : src) {
        const uint32_t r = p & 0xff, g = (p >> 8) & 0xff, b = (p >> 16) & 0xff, a = p >> 24;
        p = (a << 24) | (r << 16) | (g << 8) | b;
    }
    std::vector<uint32_t> dst(src.size() * factor * factor);
    xbrz_scale(factor, src.data(), dst.data(), w, h, ColorFormat::RGBA,
               xbrz::ScalerCfg(), 0, std::numeric_limits<int>::max());
    for (auto& p : dst) {
        const uint32_t a = p >> 24, r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
        p = (a << 24) | (b << 16) | (g << 8) | r;
    }
    if (std::fwrite(dst.data(), 4, dst.size(), stdout) != dst.size()) { std::fprintf(stderr, "short write\n"); return 1; }
    return 0;
}
