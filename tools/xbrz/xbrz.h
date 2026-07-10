// ****************************************************************************
// * This file is part of the xBRZ project. It is distributed under           *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0         *
// * Â© Zenju (zenju AT gmx DOT de) - All Rights Reserved                      *
// * Â© io mintz <io@mintz.cc> - All Rights Reserved                           *
// * Modified by io mintz 2020-06-12                                          *
// ****************************************************************************

#ifndef XBRZ_HEADER_3847894708239054
#define XBRZ_HEADER_3847894708239054

#include <cstddef> //size_t
#include <cstdint> //uint32_t
#include <limits>
#include "xbrz_config.h"

#define EXPORT
#ifdef _WIN32
	#define EXPORT __declspec(dllexport)
#endif

extern "C"
{
/*
-------------------------------------------------------------------------
| xBRZ: "Scale by rules" - high quality image upscaling filter by Zenju |
-------------------------------------------------------------------------
using a modified approach of xBR:
http://board.byuu.org/viewtopic.php?f=10&t=2248
- new rule set preserving small image features
- highly optimized for performance
- support alpha channel
- support multithreading
- support 64-bit architectures
- support processing image slices
- support scaling up to 6xBRZ
*/

enum class ColorFormat //from high bits -> low bits, 8 bit per channel
{
    RGB = 1,  //8 bit for each red, green, blue, upper 8 bits unused
    RGBA = 2, //including alpha channel, ABGR byte order on little-endian machines
    RGBA_UNBUFFERED = 3, //like RGBA, but without the one-time buffer creation overhead (ca. 100 - 300 ms) at the expense of a slightly slower scaling time
};

const int SCALE_FACTOR_MAX = 6;

/*
-> map source (srcWidth * srcHeight) to target (scale * width x scale * height) image, optionally processing a half-open slice of rows [yFirst, yLast) only
-> if your emulator changes only a few image slices during each cycle (e.g. DOSBox) then there's no need to run xBRZ on the complete image:
   Just make sure you enlarge the source image slice by 2 rows on top and 2 on bottom (this is the additional range the xBRZ algorithm is using during analysis)
   CAVEAT: If there are multiple changed slices, make sure they do not overlap after adding these additional rows in order to avoid a memory race condition
   in the target image data if you are using multiple threads for processing each enlarged slice!

THREAD-SAFETY: - parts of the same image may be scaled by multiple threads as long as the [yFirst, yLast) ranges do not overlap!
               - there is a minor inefficiency for the first row of a slice, so avoid processing single rows only; suggestion: process at least 8-16 rows
*/
void xbrz_scale(
    size_t factor, //valid range: 2 - SCALE_FACTOR_MAX
    const uint32_t* src, uint32_t* trg, int srcWidth, int srcHeight,
    ColorFormat colFmt,
    const xbrz::ScalerCfg& cfg,
    int yFirst, int yLast //slice of source image
);

inline void xbrz_scale_defaults(
    size_t factor, //valid range: 2 - SCALE_FACTOR_MAX
    const uint32_t* src, uint32_t* trg, int srcWidth, int srcHeight,
    ColorFormat colFmt
)
{
	xbrz_scale(factor, src, trg, srcWidth, srcHeight, colFmt, xbrz::ScalerCfg(), 0, std::numeric_limits<int>::max());
}

void xbrz_bilinearScale(const uint32_t* src, int srcWidth, int srcHeight,
                   /**/  uint32_t* trg, int trgWidth, int trgHeight);

void xbrz_nearestNeighborScale(const uint32_t* src, int srcWidth, int srcHeight,
                          /**/  uint32_t* trg, int trgWidth, int trgHeight);

//parameter tuning
bool xbrz_equalColorTest(uint32_t col1, uint32_t col2, ColorFormat colFmt, double luminanceWeight, double equalColorTolerance);
}

#endif
