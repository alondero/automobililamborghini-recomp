#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <new>
#include <random>
#include <set>
#include <utility>
#include <vector>

#include "src/hle/rt64_game_call_keys.h"

static size_t allocations = 0;
void *operator new(size_t size) {
    allocations++;
    if (void *memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
// Temporary buffers (std::stable_sort) use the nothrow form.
void *operator new(size_t size, const std::nothrow_t &) noexcept {
    allocations++;
    return std::malloc(size ? size : 1);
}
void *operator new[](size_t size) { return operator new(size); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, size_t) noexcept { std::free(memory); }
void operator delete(void *memory, const std::nothrow_t &) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }

using namespace RT64;
typedef std::pair<uint32_t, uint32_t> IndexPair;

// Matrix IDs: 0 is G_EX_ID_IGNORE, 1 is G_EX_ID_AUTO, others are game IDs.
struct Group {
    uint32_t matrixId;
    bool autoOrdering;
};

struct Workload {
    std::vector<Group> worldGroups;
    std::vector<uint32_t> faceLookAts;
};

// The parts of a draw call frame matching reads.
struct Call {
    uint64_t hash;
    uint32_t workloadIndex;
    uint32_t minWorldMatrix;
    uint32_t worldMatrixCount;
    bool doTransformMatching;
    bool doTileInterpolation;
    bool doTileMatching;
    uint32_t tileIndex;
    uint32_t tileCount;
    uint64_t tmem[2];
    bool textureGen;
    uint32_t faceIndicesStart;
};

struct Frame {
    std::vector<Workload> workloads;
    std::vector<Call> calls;
};

struct CheckSets {
    std::vector<IndexPair> transforms, tiles, lookAts;
    std::set<std::pair<uint32_t, uint32_t>> lookAtReads;
    bool recordReads = true;
};

static bool transformsMatch(const Group &cur, const Group &prev) {
    return (cur.matrixId == prev.matrixId) && ((cur.matrixId == 1) || ((cur.matrixId != 0) && cur.autoOrdering));
}

// Upstream RT64: every current call with every previous call of equal hash,
// through std::multimap and std::set.
static void reference(const Frame &cur, const Frame &prev, CheckSets &sets) {
    std::multimap<uint64_t, const Call *> curMap, prevMap;
    for (const Call &call : cur.calls) curMap.emplace(call.hash, &call);
    for (const Call &call : prev.calls) prevMap.emplace(call.hash, &call);
    std::set<IndexPair> transforms, tiles, lookAts;
    for (const auto &curIt : curMap) {
        auto prevRange = prevMap.equal_range(curIt.first);
        for (auto prevIt = prevRange.first; prevIt != prevRange.second; prevIt++) {
            const Call &c = *curIt.second;
            const Call &p = *prevIt->second;
            const Workload &cw = cur.workloads[c.workloadIndex];
            const Workload &pw = prev.workloads[p.workloadIndex];
            if ((c.worldMatrixCount == p.worldMatrixCount) && c.doTransformMatching && p.doTransformMatching) {
                for (uint32_t w = 0; w < c.worldMatrixCount; w++) {
                    if (transformsMatch(cw.worldGroups[c.minWorldMatrix + w], pw.worldGroups[p.minWorldMatrix + w])) {
                        transforms.emplace(c.minWorldMatrix + w, p.minWorldMatrix + w);
                    }
                }
            }

            if ((c.tileCount == p.tileCount) && c.doTileInterpolation && p.doTileInterpolation) {
                for (uint32_t t = 0; t < c.tileCount; t++) {
                    if (c.doTileMatching && p.doTileMatching && (c.tmem[t] != p.tmem[t])) {
                        continue;
                    }

                    tiles.emplace(c.tileIndex + t, p.tileIndex + t);
                }
            }

            if (c.textureGen && p.textureGen) {
                sets.lookAtReads.emplace(c.workloadIndex, c.faceIndicesStart);
                sets.lookAtReads.emplace(100 + p.workloadIndex, p.faceIndicesStart);
                lookAts.emplace(cw.faceLookAts.at(c.faceIndicesStart), pw.faceLookAts.at(p.faceIndicesStart));
            }
        }
    }

    sets.transforms.assign(transforms.begin(), transforms.end());
    sets.tiles.assign(tiles.begin(), tiles.end());
    sets.lookAts.assign(lookAts.begin(), lookAts.end());
}

// What GameFrame::buildCallKeys stores for each call.
static void buildKeys(const Frame &frame, GameCallKeys &keys) {
    keys.clear();
    for (const Call &call : frame.calls) {
        if (call.doTransformMatching) {
            keys.transforms.emplace_back(CallTransformKey{ call.hash, call.worldMatrixCount, call.workloadIndex, call.minWorldMatrix });
        }

        if (call.doTileInterpolation) {
            for (uint32_t t = 0; t < call.tileCount; t++) {
                keys.tiles.emplace_back(CallTileKey{ call.hash, call.tileCount, t, call.doTileMatching, call.tmem[t], call.tileIndex + t });
            }
        }

        if (call.textureGen) {
            keys.lookAts.emplace_back(CallLookAtKey{ call.hash, call.workloadIndex, call.faceIndicesStart });
        }
    }

    keys.sortUnique();
}

static void production(const Frame &cur, const Frame &prev, GameCallKeys &curCallKeys, GameCallKeys &prevCallKeys, CheckSets &sets) {
    buildKeys(cur, curCallKeys);
    buildKeys(prev, prevCallKeys);
    std::vector<IndexPair> &transformCheckSet = sets.transforms;
    std::vector<IndexPair> &tileCheckSet = sets.tiles;
    std::vector<IndexPair> &lookAtCheckSet = sets.lookAts;
    transformCheckSet.clear();
    tileCheckSet.clear();
    lookAtCheckSet.clear();
    const auto addTransformPairs = [&](const CallTransformKey &curKey, const CallTransformKey &prevKey) {
        const Workload &cw = cur.workloads[curKey.workloadIndex];
        const Workload &pw = prev.workloads[prevKey.workloadIndex];
        for (uint32_t w = 0; w < curKey.worldMatrixCount; w++) {
            if (transformsMatch(cw.worldGroups[curKey.minWorldMatrix + w], pw.worldGroups[prevKey.minWorldMatrix + w])) {
                transformCheckSet.emplace_back(curKey.minWorldMatrix + w, prevKey.minWorldMatrix + w);
            }
        }
    };

    // Look ats are read from both frames; the current frame's keys are in curCallKeys.
    const auto lookAtIndex = [&](const CallLookAtKey &key) {
        const bool isCur = (&key >= curCallKeys.lookAts.data()) && (&key < curCallKeys.lookAts.data() + curCallKeys.lookAts.size());
        if (sets.recordReads) {
            sets.lookAtReads.emplace(isCur ? key.workloadIndex : 100 + key.workloadIndex, key.faceIndicesStart);
        }

        const Workload &workload = isCur ? cur.workloads[key.workloadIndex] : prev.workloads[key.workloadIndex];
        return workload.faceLookAts.at(key.faceIndicesStart);
    };

    pairCallKeys(curCallKeys, prevCallKeys, addTransformPairs, lookAtIndex, tileCheckSet, lookAtCheckSet);
    // PRODUCTION_NORMALIZE
}

struct Shape {
    uint32_t calls;
    uint32_t hashes;
    uint32_t matrices;
    uint32_t workloads;
    uint32_t tmems;
};

static Frame makeFrame(std::mt19937 &random, const Shape &shape) {
    Frame frame;
    frame.workloads.resize(shape.workloads);
    for (Workload &workload : frame.workloads) {
        for (uint32_t m = 0; m < shape.matrices + 2; m++) {
            workload.worldGroups.push_back(Group{ random() % 4 ? 1u : uint32_t(random() % 4), (random() % 2) == 0 });
        }

        for (uint32_t f = 0; f < shape.calls; f++) {
            workload.faceLookAts.push_back(random() % 4);
        }
    }

    uint32_t tileIndex = 0;
    for (uint32_t i = 0; i < shape.calls; i++) {
        Call call = {};
        call.hash = random() % shape.hashes;
        call.workloadIndex = random() % shape.workloads;
        call.minWorldMatrix = random() % shape.matrices;
        call.worldMatrixCount = 1 + random() % 3;
        call.doTransformMatching = (random() % 8) != 0;
        call.doTileInterpolation = (random() % 8) != 0;
        call.doTileMatching = call.doTileInterpolation && ((random() % 4) != 0);
        call.tileCount = random() % 3;
        call.tileIndex = tileIndex;
        tileIndex += call.tileCount;
        call.tmem[0] = random() % shape.tmems;
        call.tmem[1] = random() % shape.tmems;
        call.textureGen = (random() % 4) == 0;
        // Rectangle calls have no face indices: an out-of-range start must never be read.
        call.faceIndicesStart = call.textureGen ? (random() % shape.calls) : 0xFFFFFFFFu;
        frame.calls.push_back(call);
    }

    return frame;
}

static int failures = 0;
static void expect(bool condition, const char *label, uint32_t trial) {
    if (!condition) {
        std::fprintf(stderr, "trial %u: %s\n", trial, label);
        failures++;
    }
}

static void compare(const Frame &cur, const Frame &prev, GameCallKeys &curKeys, GameCallKeys &prevKeys, CheckSets &sets, uint32_t trial) {
    CheckSets expected;
    reference(cur, prev, expected);
    sets.lookAtReads.clear();
    production(cur, prev, curKeys, prevKeys, sets);
    expect(sets.transforms == expected.transforms, "transform pairs match the call-by-call std::set", trial);
    expect(sets.tiles == expected.tiles, "tile pairs match the call-by-call std::set", trial);
    expect(sets.lookAts == expected.lookAts, "look at pairs match the call-by-call std::set", trial);
    expect(sets.lookAtReads == expected.lookAtReads, "look ats are read only for calls with a partner", trial);
}

int main() {
    std::mt19937 random(378);
    static GameCallKeys curKeys, prevKeys;
    static CheckSets sets;

    // Repeated scenery: few hashes and few matrices give long runs of equal keys.
    const Shape shapes[] = {
        { 300, 4, 3, 1, 2 },
        { 300, 40, 200, 2, 8 },
        { 120, 1, 1, 3, 1 },
        { 60, 6, 30, 2, 3 },
        { 1, 1, 1, 1, 1 },
    };

    uint32_t trial = 0;
    for (const Shape &shape : shapes) {
        for (uint32_t i = 0; i < 40; i++, trial++) {
            const Frame cur = makeFrame(random, shape);
            const Frame prev = makeFrame(random, shape);
            compare(cur, prev, curKeys, prevKeys, sets, trial);
        }
    }

    // An empty side pairs nothing.
    const Frame empty;
    const Frame some = makeFrame(random, shapes[0]);
    compare(empty, some, curKeys, prevKeys, sets, trial++);
    compare(some, empty, curKeys, prevKeys, sets, trial++);

    // Same calls in another order each frame: after the first frame every
    // container has its size, so any allocation would be per frame.
    Frame cur = makeFrame(random, shapes[0]);
    Frame prev = makeFrame(random, shapes[0]);
    sets.recordReads = false;
    for (int frame = 0; frame < 4; frame++) {
        std::shuffle(cur.calls.begin(), cur.calls.end(), random);
        std::shuffle(prev.calls.begin(), prev.calls.end(), random);
        const size_t before = allocations;
        production(cur, prev, curKeys, prevKeys, sets);
        const size_t frameAllocations = allocations - before;
        if ((frame > 0) && (frameAllocations > 0)) {
            std::fprintf(stderr, "frame %d allocated %zu times after warm-up\n", frame, frameAllocations);
            failures++;
        }
    }

    return failures ? 1 : 0;
}
