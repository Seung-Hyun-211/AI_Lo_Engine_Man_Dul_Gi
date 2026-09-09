// Standalone, throwaway benchmark backing docs/entity-lifecycle-design.md's
// memory-allocation rule ("same type -> one contiguous array"). Not part of
// the engine build (CppWindowGame.vcxproj does not include this) - same
// status as tools/fbx_probe.cpp: a probe you build and run by hand to check
// a claim, not shipped code.
//
// Question: does packing same-type entity data into one contiguous
// std::vector<T> actually beat storing each instance behind its own heap
// allocation (what a naive vector<unique_ptr<Entity>> - or any node-based
// container - gives you), and does a *realistic* (fragmented, out-of-order)
// scattered layout make it worse still?
//
// Build (Windows/MSVC): tools\build_entity_memory_bench.bat
// Build (anything with g++/clang): g++ -O2 -std=c++20 entity_memory_bench.cpp -o entity_memory_bench
// Run: entity_memory_bench [entityCount]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <vector>

// A plausible per-entity component: position + velocity, 24 bytes - similar
// in spirit to this engine's game::Particle{x,y,vx,vy} (16 bytes) but with a
// 3rd axis so its size isn't a suspiciously tidy multiple of the cache line.
struct Transform
{
    float px, py, pz;
    float vx, vy, vz;
};

constexpr float kDt = 1.0f / 60.0f;
constexpr int kPasses = 40;    // repeat the "physics step" this many times per timing sample
constexpr int kSamples = 7;    // timing samples per layout; report the median (less noise than the mean)

// The actual per-entity work, identical across every layout so the only
// variable under test is memory layout, never the math.
inline void Step(Transform& t)
{
    t.px += t.vx * kDt;
    t.py += t.vy * kDt;
    t.pz += t.vz * kDt;
}

using Clock = std::chrono::steady_clock;

double Median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// ---- Layout A: dense, contiguous std::vector<Transform> ----
// What the sparse-set component table in docs/entity-lifecycle-design.md §5
// produces: one flat array per type, iterated straight through.
double BenchDense(std::size_t n)
{
    std::vector<Transform> dense(n);
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (auto& t : dense) t = { 0, 0, 0, dist(rng), dist(rng), dist(rng) };

    std::vector<double> samples;
    for (int s = 0; s < kSamples; ++s)
    {
        const auto start = Clock::now();
        for (int pass = 0; pass < kPasses; ++pass)
            for (auto& t : dense) Step(t);
        const auto end = Clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    // Checksum so the loop can't be optimized away; printed, never compared
    // to a hardcoded value (only cross-checked against the other layouts).
    double checksum = 0;
    for (auto& t : dense) checksum += t.px + t.py + t.pz;
    std::fprintf(stderr, "  (dense checksum %.3f)\n", checksum);
    return Median(std::move(samples));
}

// ---- Layout B/C: scattered, individually new'd Transform, accessed through
// a pointer vector - what a vector<unique_ptr<Entity>> or any node-based
// container gives you, with no attempt to keep same-type instances adjacent.
// `fragment=true` interleaves unrelated allocations and shuffles the pointer
// order first, simulating a long-running program where many entity types
// were created/destroyed over time - the realistic case, not the best case
// for `new`.
double BenchScattered(std::size_t n, bool fragment)
{
    std::vector<std::unique_ptr<Transform>> owners;
    std::vector<Transform*> ptrs;
    owners.reserve(n);
    ptrs.reserve(n);
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    // Separate RNG for noise/shuffle so the velocities drawn below are the
    // exact same sequence regardless of `fragment` - all three layouts then
    // do bit-identical math over the same data, and matching checksums at
    // the end are a correctness check, not just a timing comparison.
    std::mt19937 churnRng(5678);

    std::vector<std::unique_ptr<char[]>> noise;   // unrelated heap traffic, freed before timing
    for (std::size_t i = 0; i < n; ++i)
    {
        if (fragment)
        {
            // Other systems allocating between entity spawns (strings,
            // containers growing, other entity types, ...).
            noise.push_back(std::make_unique<char[]>(16 + (i % 200)));
            if (i % 3 == 0 && !noise.empty())
            {
                // O(1) removal (swap-and-pop): just churn to interleave frees
                // with the entity allocations, order doesn't matter.
                std::swap(noise[churnRng() % noise.size()], noise.back());
                noise.pop_back();
            }
        }
        auto t = std::make_unique<Transform>(Transform{ 0, 0, 0, dist(rng), dist(rng), dist(rng) });
        ptrs.push_back(t.get());
        owners.push_back(std::move(t));
    }
    noise.clear();

    if (fragment) std::shuffle(ptrs.begin(), ptrs.end(), churnRng);   // iteration order != allocation order

    std::vector<double> samples;
    for (int s = 0; s < kSamples; ++s)
    {
        const auto start = Clock::now();
        for (int pass = 0; pass < kPasses; ++pass)
            for (Transform* t : ptrs) Step(*t);
        const auto end = Clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    double checksum = 0;
    for (Transform* t : ptrs) checksum += t->px + t->py + t->pz;
    std::fprintf(stderr, "  (scattered%s checksum %.3f)\n", fragment ? "+fragmented" : "", checksum);
    return Median(std::move(samples));
}

int main(int argc, char** argv)
{
    const std::size_t n = argc > 1 ? static_cast<std::size_t>(std::atoll(argv[1])) : 500'000;
    std::printf("N = %zu entities, %d passes x %d samples (median reported)\n\n", n, kPasses, kSamples);

    const double dense = BenchDense(n);
    const double scatteredFresh = BenchScattered(n, /*fragment=*/false);
    const double scatteredFrag = BenchScattered(n, /*fragment=*/true);

    std::printf("\n%-28s %10s %12s\n", "layout", "ms", "vs dense");
    std::printf("%-28s %10.2f %11s\n", "A: dense vector<Transform>", dense, "1.00x");
    std::printf("%-28s %10.2f %10.2fx\n", "B: scattered (fresh new)", scatteredFresh, scatteredFresh / dense);
    std::printf("%-28s %10.2f %10.2fx\n", "C: scattered (fragmented)", scatteredFrag, scatteredFrag / dense);
    return 0;
}
