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

struct GameCallMap {
    uint32_t sceneProjIndex : 10;
    uint32_t callIndex : 20;
    uint32_t doTransformMatching : 1;
    uint32_t doTileInterpolation : 1;
    uint32_t doTileMatching : 1;
};
typedef std::pair<uint32_t, uint32_t> IndexPair;
// PRODUCTION_TYPES

static int failures = 0;
static void expect(bool condition, const char *label) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", label);
        failures++;
    }
}

// One frame of production container handling: sort the call hashes, pair every
// current call with the previous calls of equal hash, then normalize the sets.
static void production(GameCallHashMap &curCallHashMap, GameCallHashMap &prevCallHashMap, std::vector<IndexPair> &transformCheckSet,
    std::vector<IndexPair> &tileCheckSet, std::vector<IndexPair> &lookAtCheckSet, std::vector<IndexPair> &pairs) {
    // PRODUCTION_SORT
    pairs.clear();
    for (const std::pair<uint64_t, GameCallMap> &curIt : curCallHashMap) {
        // PRODUCTION_RANGE
        for (auto prevIt = prevRange.first; prevIt != prevRange.second; prevIt++) {
            pairs.emplace_back(uint32_t(curIt.second.callIndex), uint32_t(prevIt->second.callIndex));
            transformCheckSet.emplace_back(curIt.second.callIndex % 7, prevIt->second.callIndex % 5);
            tileCheckSet.emplace_back(curIt.second.callIndex % 3, prevIt->second.callIndex % 3);
            lookAtCheckSet.emplace_back(0, prevIt->second.callIndex % 2);
        }
    }
    // PRODUCTION_NORMALIZE
}

int main() {
    std::mt19937 random(374);
    static GameCallHashMap curCallHashMap, prevCallHashMap;
    static std::vector<IndexPair> transformCheckSet, tileCheckSet, lookAtCheckSet, pairs;
    // Few distinct hashes: identical calls (repeated scenery) give long equal
    // ranges. Shuffling keeps every container's size equal between frames, so
    // any allocation after the first frame is the containers' own.
    std::vector<uint64_t> curHashes, prevHashes;
    for (uint32_t i = 0; i < 400; i++) curHashes.push_back(random() % 12);
    for (uint32_t i = 0; i < 380; i++) prevHashes.push_back(random() % 12);
    // Production appends calls in projection order, then call order.
    auto call = [](uint32_t i) { return GameCallMap{ i / 100, i, 1, 1, 1 }; };
    for (int frame = 0; frame < 4; frame++) {
        std::shuffle(curHashes.begin(), curHashes.end(), random);
        std::shuffle(prevHashes.begin(), prevHashes.end(), random);

        std::multimap<uint64_t, GameCallMap> curReference, prevReference;
        std::set<IndexPair> transformReference, tileReference, lookAtReference;
        std::vector<IndexPair> pairsReference;
        for (uint32_t i = 0; i < curHashes.size(); i++) curReference.emplace(curHashes[i], call(i));
        for (uint32_t i = 0; i < prevHashes.size(); i++) prevReference.emplace(prevHashes[i], call(i));
        for (const auto &curIt : curReference) {
            auto prevRange = prevReference.equal_range(curIt.first);
            for (auto prevIt = prevRange.first; prevIt != prevRange.second; prevIt++) {
                pairsReference.emplace_back(uint32_t(curIt.second.callIndex), uint32_t(prevIt->second.callIndex));
                transformReference.emplace(curIt.second.callIndex % 7, prevIt->second.callIndex % 5);
                tileReference.emplace(curIt.second.callIndex % 3, prevIt->second.callIndex % 3);
                lookAtReference.emplace(0, prevIt->second.callIndex % 2);
            }
        }

        const size_t before = allocations;
        curCallHashMap.clear();
        prevCallHashMap.clear();
        transformCheckSet.clear();
        tileCheckSet.clear();
        lookAtCheckSet.clear();
        for (uint32_t i = 0; i < curHashes.size(); i++) curCallHashMap.emplace_back(curHashes[i], call(i));
        for (uint32_t i = 0; i < prevHashes.size(); i++) prevCallHashMap.emplace_back(prevHashes[i], call(i));
        production(curCallHashMap, prevCallHashMap, transformCheckSet, tileCheckSet, lookAtCheckSet, pairs);
        const size_t frameAllocations = allocations - before;

        std::vector<uint32_t> curOrder, curReferenceOrder;
        for (const auto &it : curCallHashMap) curOrder.push_back(it.second.callIndex);
        for (const auto &it : curReference) curReferenceOrder.push_back(it.second.callIndex);
        expect(curOrder == curReferenceOrder, "call hashes iterate in multimap order");
        expect(pairs == pairsReference, "equal ranges match multimap::equal_range");
        expect(transformCheckSet == std::vector<IndexPair>(transformReference.begin(), transformReference.end()), "transform pairs match std::set");
        expect(tileCheckSet == std::vector<IndexPair>(tileReference.begin(), tileReference.end()), "tile pairs match std::set");
        expect(lookAtCheckSet == std::vector<IndexPair>(lookAtReference.begin(), lookAtReference.end()), "look-at pairs match std::set");
        if ((frame > 0) && (frameAllocations > 0)) {
            std::fprintf(stderr, "frame %d allocated %zu times after warm-up\n", frame, frameAllocations);
            failures++;
        }
    }
    return failures ? 1 : 0;
}
