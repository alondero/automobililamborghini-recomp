// Static shadow-caster cache: what is remembered across views and what is not.
#include "render/rt64_sun_shadow_cache.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using RT64::SunShadowStaticCache;
using RT64::SunShadowStaticCacheSettings;

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

SunShadowStaticCache::Triangle at(float x, float z = 0) {
    return {x, 0, z, x + 1, 0, z, x, 1, z};
}

const float origin[3] = {0, 0, 0};

SunShadowStaticCacheSettings immediate() {
    SunShadowStaticCacheSettings settings;
    settings.pruneInterval = 1;
    return settings;
}
}

int main() {
    {
        // Scenery repeats its exact positions while in view: promoted on the
        // second sighting. The current view traces what it presents, so only
        // scenery it no longer presents is published.
        SunShadowStaticCache cache(immediate());
        cache.observe(1, 10, {at(0), at(5)}, origin);
        require(cache.promoted() == 0 && cache.version() == 0, "a single sighting was promoted");
        cache.observe(1, 11, {at(0), at(5)}, origin);
        require(cache.promoted() == 2 && cache.published().empty(), "presented scenery was published twice");
        cache.observe(1, 12, {at(5)}, origin);
        require(cache.published().size() == 1 && cache.published()[0][0] == 0, "scenery out of view was not published");
        const uint64_t version = cache.version();
        require(version != 0, "a published snapshot has no identity");
        cache.observe(1, 13, {at(5)}, origin);
        require(cache.version() == version, "an unchanged view republished its snapshot");
        cache.observe(1, 14, {at(0), at(5)}, origin);
        require(cache.published().empty() && cache.version() != version, "scenery back in view stayed published");
    }
    {
        // Moving geometry never repeats its bits, so it never becomes a caster.
        SunShadowStaticCache cache(immediate());
        for (uint64_t task = 1; task < 20; ++task) cache.observe(1, task, {at(float(task) * 0.01f)}, origin);
        require(cache.promoted() == 0, "moving geometry was promoted");
        require(cache.candidates() <= 3, "unpromoted candidates are not pruned");
    }
    {
        // An animation that returns to a pose much later is not static.
        SunShadowStaticCache cache(immediate());
        cache.observe(1, 1, {at(0)}, origin);
        cache.observe(1, 9, {at(0)}, origin);
        require(cache.promoted() == 0, "a pose revisited after a gap was promoted");
        cache.observe(1, 10, {at(0)}, origin);
        require(cache.promoted() == 1, "consecutive sightings after a gap were not promoted");
    }
    {
        // A new scene epoch (circuit, mode or player change) starts empty,
        // and its snapshots never reuse an old identity.
        SunShadowStaticCache cache(immediate());
        cache.observe(1, 1, {at(0)}, origin);
        cache.observe(1, 2, {at(0)}, origin);
        cache.observe(1, 3, {}, origin);
        const uint64_t first = cache.version();
        require(cache.published().size() == 1, "setup did not publish");
        cache.observe(2, 4, {}, origin);
        require(cache.promoted() == 0 && cache.published().empty(), "a new epoch kept old scenery");
        require(cache.version() != first, "emptying the cache kept the old snapshot identity");
        cache.observe(2, 5, {at(7)}, origin);
        cache.observe(2, 6, {at(7)}, origin);
        cache.observe(2, 7, {}, origin);
        require(cache.version() > first + 1, "a new epoch reused an old snapshot identity");
    }
    {
        // Far and long unseen scenery is evicted; nearby scenery stays.
        SunShadowStaticCacheSettings settings = immediate();
        settings.keepTasks = 5;
        settings.radius = 50;
        SunShadowStaticCache cache(settings);
        cache.observe(1, 1, {at(0), at(100)}, origin);
        cache.observe(1, 2, {at(0), at(100)}, origin);
        require(cache.promoted() == 2, "setup did not promote");
        cache.observe(1, 4, {}, origin);
        require(cache.promoted() == 2 && cache.published().size() == 2, "recently seen far scenery was evicted early");
        for (uint64_t task = 5; task < 70; ++task) cache.observe(1, task, {}, origin);
        require(cache.promoted() == 1 && cache.published().size() == 1 && cache.published()[0][0] == 0,
            "far unseen scenery was kept or near scenery evicted");
    }
    {
        // The triangle cap evicts the farthest scenery first.
        SunShadowStaticCacheSettings settings = immediate();
        settings.maxTriangles = 2;
        SunShadowStaticCache cache(settings);
        cache.observe(1, 1, {at(0), at(10), at(300)}, origin);
        cache.observe(1, 2, {at(0), at(10), at(300)}, origin);
        require(cache.promoted() == 2, "the cap was not enforced");
        cache.observe(1, 3, {}, origin);
        bool far = false;
        for (const auto& triangle : cache.published()) far |= triangle[0] == 300;
        require(cache.published().size() == 2 && !far, "the cap kept the farthest triangle");
    }
    {
        // Diagnostic coverage mode publishes every promoted triangle, so the
        // remembered scenery alone can stand in for the presented world.
        SunShadowStaticCacheSettings settings = immediate();
        settings.includePresented = true;
        SunShadowStaticCache cache(settings);
        cache.observe(1, 1, {at(0)}, origin);
        cache.observe(1, 2, {at(0)}, origin);
        require(cache.published().size() == 1, "coverage mode left out presented scenery");
    }
    std::puts("PASS: static shadow-caster cache");
    return 0;
}
