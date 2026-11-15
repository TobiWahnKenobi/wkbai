// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <vector>
#include <algorithm>
#include <tuple>
//#include <iostream>

namespace Pathfinding {

    // test runs (timing each part): WKB_PATH_NOPRUNE=1 all 8 neighbours, WKB_PATH_CORNERS=1 diagonal
    // steps across corners allowed, WKB_PATH_NOLATE=1 no leaning on the guess in long searches
    inline const bool g_noPrune = std::getenv("WKB_PATH_NOPRUNE") != nullptr;
    inline const bool g_cutCorners = std::getenv("WKB_PATH_CORNERS") != nullptr;
    inline const bool g_noLate = std::getenv("WKB_PATH_NOLATE") != nullptr;

    struct PFPos {
        int x, z;
        bool operator==(const PFPos& p) const { return x == p.x && z == p.z; }
        bool operator!=(const PFPos& p) const { return !(*this == p); }
        bool operator<(const PFPos& p) const { return (x != p.x) ? (x < p.x) : (z < p.z); }
        // both coordinates in one 64-bit key, mixed by std::hash (x + 3z made whole diagonals of
        // tiles collide, and the searches' hash maps slow)
        size_t hash() const { return std::hash<uint64_t>()(((uint64_t)(uint32_t)x << 32) | (uint32_t)z); }
        struct Hasher {
            size_t operator()(const PFPos& pfp) const noexcept { return pfp.hash(); }
        };
    };

    inline int ManhattanDiagHeuristic(PFPos o, PFPos n) {
        int dx = std::abs(n.x - o.x);
        int dz = std::abs(n.z - o.z);
        int ndiag = std::min(dx, dz);
        int nrect = std::max(dx, dz) - ndiag;
        return ndiag * 141 + nrect * 100;
    };

    inline int EuclideanHeuristic(PFPos o, PFPos n) {
        return (int)(std::sqrt((float)(n.x - o.x) * (n.x - o.x) + (float)(n.z - o.z) * (n.z - o.z)) * 100.0f);
    }

    // A* on a grid of width x height tiles (outside it: blocked). The scores, parents and closed
    // flags are flat arrays, valid when their stamp is the search's: begin() is O(1), and nothing
    // is hashed (hash maps were most of a search's time on the large maps).
    struct AStarPathfinder {
        using score_t = int;
        std::vector<std::pair<PFPos, score_t>> nextTiles;
        std::vector<uint32_t> scoreStamp, closedStamp;
        std::vector<score_t> scores;
        std::vector<int> edges; // the parent tile's index
        uint32_t stamp = 0;
        int width = 0, height = 0;
        PFPos start, end;
        bool finished = false;
        bool nothingFound = false;
        // the visited tile nearest to the end so far (a partial path when the search is cut short)
        PFPos best;
        int bestHeuristic = std::numeric_limits<int>::max();

        int index(PFPos p) const { return p.z * width + p.x; }
        bool inside(PFPos p) const { return p.x >= 0 && p.x < width && p.z >= 0 && p.z < height; }
        PFPos posOf(int i) const { return { i % width, i / width }; }
        bool hasScore(int i) const { return scoreStamp[i] == stamp; }
        bool closed(int i) const { return closedStamp[i] == stamp; }

        void begin(PFPos start, PFPos end, int width, int height) {
            if (width * height > (int)scores.size() || width != this->width) {
                scoreStamp.assign(width * height, 0);
                closedStamp.assign(width * height, 0);
                scores.assign(width * height, 0);
                edges.assign(width * height, -1);
                stamp = 0;
            }
            this->width = width;
            this->height = height;
            if (++stamp == 0) { // wrapped: old stamps could match again
                std::fill(scoreStamp.begin(), scoreStamp.end(), 0);
                std::fill(closedStamp.begin(), closedStamp.end(), 0);
                stamp = 1;
            }
            this->start = start;
            this->end = end;
            finished = false;
            nothingFound = false;
            best = start;
            bestHeuristic = std::numeric_limits<int>::max();
            nextTiles.clear();
            if (!inside(start)) {
                finished = nothingFound = true;
                return;
            }
            scores[index(start)] = 0;
            scoreStamp[index(start)] = stamp;
            nextTiles.push_back({ start, 0 });
        }

        struct NoCost { int operator()(PFPos) const { return 0; } };
        template<typename Predicate, typename Heuristic, typename Cost = NoCost>
        bool next(Predicate pred, Heuristic heuristic, Cost extra = Cost()) {
            if (finished) return true;

            static auto heapcmp = [](const auto& a, const auto& b) {return a.second > b.second; };

            // find tile with min score
            PFPos mt;
            score_t mscore = std::numeric_limits<score_t>::max();
            while (!nextTiles.empty()) {
                std::tie(mt, mscore) = nextTiles.front();
                std::pop_heap(nextTiles.begin(), nextTiles.end(), heapcmp);
                nextTiles.pop_back();
                if (!closed(index(mt)))
                    break;
                mscore = std::numeric_limits<score_t>::max();
            }
            if (mscore == std::numeric_limits<score_t>::max()) {
                finished = true;
                nothingFound = true;
                return true;
            }
            if (const int h = heuristic(mt, end); h < bestHeuristic) {
                bestHeuristic = h;
                best = mt;
            }
            if (mt == end) {
                finished = true;
                nothingFound = false;
                return true;
            }

            // update neighbours
            const int mi = index(mt);
            const score_t mtScore = scores[mi];
            // The way it came: the 3 neighbours behind aren't looked at (WKB.exe 0x6c5df0: 5 of
            // the 8). Each is a neighbour of the parent no dearer from there, so the paths stay the
            // same, with 3/8 fewer tests.
            int idx = 0, idz = 0;
            if (mt != start) {
                const PFPos parent = posOf(edges[mi]);
                idx = mt.x - parent.x; idz = mt.z - parent.z;
            }
            // the four straight neighbours' blocked test, each made once (-1 not yet)
            int straightBlocked[4] = { -1, -1, -1, -1 }; // +x, -x, +z, -z
            auto blockedStraight = [&](int k, int dx, int dz) {
                if (straightBlocked[k] < 0) {
                    const PFPos p{ mt.x + dx, mt.z + dz };
                    straightBlocked[k] = (!inside(p) || pred(p)) ? 1 : 0;
                }
                return straightBlocked[k] == 1;
            };
            auto updateNeighbour = [&](int dx, int dz, score_t cost) {
                if (dx * idx + dz * idz < 0 && !g_noPrune)
                    return;
                PFPos pfp{ mt.x + dx, mt.z + dz };
                if (dx != 0 && dz != 0 && g_cutCorners) {
                    if (!inside(pfp) || pred(pfp))
                        return;
                }
                else if (dx != 0 && dz != 0) {
                    // no cutting across a blocked tile's corner: both straight tiles beside the
                    // diagonal step are free (units clipped building corners; a diagonal wall
                    // let them through)
                    if (blockedStraight(dx > 0 ? 0 : 1, dx, 0) || blockedStraight(dz > 0 ? 2 : 3, 0, dz))
                        return;
                    if (!inside(pfp) || pred(pfp))
                        return;
                }
                else if (blockedStraight(dx > 0 ? 0 : dx < 0 ? 1 : dz > 0 ? 2 : 3, dx, dz))
                    return;
                const int pi = index(pfp);
                if (closed(pi))
                    return;
                score_t newscore = mtScore + cost + extra(pfp);
                if (!hasScore(pi) || newscore < scores[pi]) {
                    scores[pi] = newscore;
                    scoreStamp[pi] = stamp;
                    edges[pi] = mi;
                    nextTiles.emplace_back(pfp, newscore + heuristic(pfp, end));
                    std::push_heap(nextTiles.begin(), nextTiles.end(), heapcmp);
                }
            };
            updateNeighbour(1, 0, 100);
            updateNeighbour(-1, 0, 100);
            updateNeighbour(0, 1, 100);
            updateNeighbour(0, -1, 100);
            updateNeighbour(1, 1, 141);
            updateNeighbour(-1, 1, 141);
            updateNeighbour(1, -1, 141);
            updateNeighbour(-1, -1, 141);
            closedStamp[mi] = stamp;
            return false;
        }

        std::vector<PFPos> get() {
            if (nothingFound)
                return {};
            return getTo(end);
        }

        // The path to a visited tile (end first, as get())
        std::vector<PFPos> getTo(PFPos target) {
            std::vector<PFPos> vec;
            for (int i = index(target); i != index(start); i = edges[i])
                vec.push_back(posOf(i));
            vec.push_back(start);
            return vec;
        }
    };

    // Tiles each direction of the search may visit: a long or impossible path stopped there took
    // up to 0.4 s on the large maps; the unit then goes to the tile nearest to the end so far
    constexpr int MAX_SEARCH_STEPS = 20000;
    // the last DoPathfinding of this thread stopped at MAX_SEARCH_STEPS (its path ends short of the end)
    inline thread_local bool g_lastSearchCut = false;
    // the tiles the last DoPathfinding of this thread took from its open lists (both directions; profiling)
    inline thread_local int g_lastSearchSteps = 0;

    template<typename Predicate, typename Heuristic, typename Cost = AStarPathfinder::NoCost>
    std::vector<PFPos> DoPathfinding(PFPos start, PFPos end, Predicate pred, Heuristic heuristic, int width = 1024, int height = 1024, Cost extra = Cost()) {
        //AStarPathfinder astar;
        //astar.begin(start, end);

        //while (!astar.next(pred, heuristic));

        //return astar.get();

        // kept between searches: their arrays are allocated once per thread (the server's
        // searches run on worker threads: MovementController::runPathSearches)
        static thread_local AStarPathfinder astar1, astar2;
        astar1.begin(start, end, width, height);
        astar2.begin(end, start, width, height);

        g_lastSearchCut = false;
        // A long search (over 2000 steps a side) leans on its guess by 1.2 from there: 2-4 times
        // fewer tiles on the long ways round, paths a few percent longer at most
        bool late = false;
        auto h = [&](PFPos a, PFPos b) { const int v = heuristic(a, b); return late ? v + v / 5 : v; };
        for (int steps = 0;; steps++) {
            g_lastSearchSteps = steps * 2;
            if (steps >= MAX_SEARCH_STEPS) {
                g_lastSearchCut = true;
                return astar1.getTo(astar1.best);
            }
            if (steps == 2000 && !g_noLate)
                late = true;
            if (astar1.next(pred, h, extra)) return astar1.get();
            if (astar2.next(pred, h, extra)) {
                auto vec = astar2.get();
                std::reverse(vec.begin(), vec.end());
                return vec;
            }
        }
    }

    template<typename Predicate> std::optional<PFPos> SegmentTraversal(float start_x, float start_z, float end_x, float end_z, Predicate pred);

    // A path made straight (end first, as DoPathfinding's): from the start, on to the farthest tile
    // the straight line to which is free, and so on. The legs go at any angle instead of the
    // grid's 45-degree zig-zag, with far fewer nodes (movement messages). The farthest free tile is
    // found by doubling and halving, so a leg costs a few line tests.
    template<typename Blocked> void StraightenPath(std::vector<PFPos>& tiles, Blocked blocked) {
        if (tiles.size() < 3)
            return;
        auto freeLine = [&](size_t a, size_t b) {
            return !SegmentTraversal(tiles[a].x + 0.5f, tiles[a].z + 0.5f, tiles[b].x + 0.5f, tiles[b].z + 0.5f, blocked);
        };
        std::vector<PFPos> out;
        size_t a = tiles.size() - 1; // the start
        out.push_back(tiles[a]);
        while (a > 0) {
            // the farthest free one (toward index 0): a-1 is the next tile, always reachable
            size_t good = a - 1, step = 2;
            while (true) {
                const size_t k = a >= step ? a - step : 0;
                if (k >= good || !freeLine(a, k))
                    break;
                good = k;
                if (k == 0) break;
                step *= 2;
            }
            // between good and the first failure, halving
            size_t bad = (a >= step ? a - step : 0);
            if (good > 0 && bad < good) {
                size_t lo = bad, hi = good; // hi free, lo not (or 0 untested)
                if (lo == 0 && freeLine(a, 0)) good = 0;
                else {
                    while (hi - lo > 1) {
                        const size_t mid = (lo + hi) / 2;
                        if (freeLine(a, mid)) hi = mid; else lo = mid;
                    }
                    good = hi;
                }
            }
            out.push_back(tiles[good]);
            a = good;
        }
        tiles.assign(out.rbegin(), out.rend());
    }

    // Walks through tiles from a starting point in a direction
    // NOTE: Here, float values are in tile unit: 1 tile length = 1.0 (not 5.0)
    class TileWalker
    {
    private:
        const float start_x, start_z;
        const float dir_x, dir_z;

        float cur_x, cur_z;
        int tile_x, tile_z;

    public:
        TileWalker(float start_x, float start_z, float dir_x, float dir_z)
            : start_x(start_x), start_z(start_z), dir_x(dir_x), dir_z(dir_z), cur_x(start_x), cur_z(start_z), tile_x((int)start_x), tile_z((int)start_z)
        {
            if (cur_x == (float)tile_x)
                cur_x = std::nextafter(cur_x, cur_x + 1.0f);
            if (cur_z == (float)tile_z)
                cur_z = std::nextafter(cur_z, cur_z + 1.0f);
        }

        PFPos next()
        {
            constexpr float finf = std::numeric_limits<float>::infinity();
            const bool right = dir_x >= 0, down = dir_z >= 0;
            
            float nx1 = finf, nz1 = finf, nx2 = finf, nz2 = finf;
            int tx1, tz1, tx2, tz2;
            if (dir_x != 0.0f) {
                nx1 = right ? std::floor(cur_x + 1.0f) : std::ceil(cur_x - 1.0f);
                nz1 = start_z + (nx1 - start_x) * dir_z / dir_x;
                tx1 = (int)nx1 - (right ? 0 : 1);
                tz1 = (int)nz1;
            }
            if (dir_z != 0.0f) {
                nz2 = down ? std::floor(cur_z + 1.0f) : std::ceil(cur_z - 1.0f);
                nx2 = start_x + (nz2 - start_z) * dir_x / dir_z;
                tx2 = (int)nx2;
                tz2 = (int)nz2 - (down ? 0 : 1);
            }

            if (std::abs(nx1 - start_x) + std::abs(nz1 - start_z) < std::abs(nx2 - start_x) + std::abs(nz2 - start_z)) {
                cur_x = nx1;
                cur_z = nz1;
                tile_x = tx1;
                tile_z = tz1;
            }
            else {
                cur_x = nx2;
                cur_z = nz2;
                tile_x = tx2;
                tile_z = tz2;
            }

            return { tile_x, tile_z };
        }
    };

    // Checks if the segment from "start" to "end" intersects with a blocked tile.
    // Returns the first blocked tile that intersects, or nullopt if none found.
    // NOTE: Here, float values are in tile unit: 1 tile length = 1.0 (not 5.0)
    template<typename Predicate> std::optional<PFPos> SegmentTraversal(float start_x, float start_z, float end_x, float end_z, Predicate pred) {
        const PFPos endTile = PFPos{ (int)end_x, (int)end_z };
        const float dir_x = end_x - start_x, dir_z = end_z - start_z;

        PFPos currentTile = PFPos{ (int)start_x, (int)start_z };
        if (pred(currentTile))
            return currentTile;

        TileWalker walker(start_x, start_z, dir_x, dir_z);

        while (currentTile != endTile) {
            currentTile = walker.next();
            if (pred(currentTile))
                return currentTile;
        }
        return std::nullopt;
    }
};