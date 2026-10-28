// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <cmath>
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

        template<typename Predicate, typename Heuristic>
        bool next(Predicate pred, Heuristic heuristic) {
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
            auto updateNeighbour = [&](int dx, int dz, score_t cost) {
                PFPos pfp{ mt.x + dx, mt.z + dz };
                if (!inside(pfp) || pred(pfp))
                    return;
                const int pi = index(pfp);
                if (closed(pi))
                    return;
                score_t newscore = mtScore + cost;
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

    template<typename Predicate, typename Heuristic>
    std::vector<PFPos> DoPathfinding(PFPos start, PFPos end, Predicate pred, Heuristic heuristic, int width = 1024, int height = 1024) {
        //AStarPathfinder astar;
        //astar.begin(start, end);

        //while (!astar.next(pred, heuristic));

        //return astar.get();

        // kept between searches: their arrays are allocated once
        static AStarPathfinder astar1, astar2;
        astar1.begin(start, end, width, height);
        astar2.begin(end, start, width, height);

        for (int steps = 0;; steps++) {
            if (steps >= MAX_SEARCH_STEPS)
                return astar1.getTo(astar1.best);
            if (astar1.next(pred, heuristic)) return astar1.get();
            if (astar2.next(pred, heuristic)) {
                auto vec = astar2.get();
                std::reverse(vec.begin(), vec.end());
                return vec;
            }
        }
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