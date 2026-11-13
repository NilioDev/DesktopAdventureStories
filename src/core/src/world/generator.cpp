// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/world/generator.hpp"

#include "indy_generator.hpp"
#include "yoda_generator.hpp"

#include <algorithm>
#include <set>

namespace das::world {

namespace {
constexpr std::uint32_t kDoorIn = 9;
} // namespace

std::vector<int> zoneCluster(const data::GameData& data, int zone) {
    std::vector<int> out;
    std::vector<int> todo{zone};
    while (!todo.empty()) {
        const int z = todo.back();
        todo.pop_back();
        if (z < 0 || static_cast<std::size_t>(z) >= data.zones.size() ||
            std::find(out.begin(), out.end(), z) != out.end()) {
            continue;
        }
        out.push_back(z);
        const auto& hotspots = data.zones[static_cast<std::size_t>(z)].hotspots;
        if (!hotspots) {
            continue;
        }
        for (auto it = hotspots->rbegin(); it != hotspots->rend(); ++it) {
            if (it->type == kDoorIn) {
                todo.push_back(it->argument);
            }
        }
    }
    return out;
}

bool WorldMap::locate(int zone, const Map*& map, int& x, int& y) const {
    for (const Map* m : {&world, &dagobah}) {
        for (int cy = 0; cy < m->height; ++cy) {
            for (int cx = 0; cx < m->width; ++cx) {
                if (m->at(cx, cy).zone == zone) {
                    map = m;
                    x = cx;
                    y = cy;
                    return true;
                }
            }
        }
    }
    return false;
}

int nextPlanet(int last, int gamesWon, bool coin) {
    if (gamesWon == 5 || gamesWon == 10 || gamesWon == 15) {
        switch (last) {
        case 1: return 3;
        case 2: return coin ? 1 : 3;
        default: return 1;
        }
    }
    switch (last) {
    case 1: return coin ? 2 : 3;
    case 2: return coin ? 1 : 3;
    default: return coin ? 2 : 1;
    }
}

WorldMap generateWorld(const data::GameData& data, const GeneratorOptions& options) {
    if (data.layout() == data::Layout::Yoda) {
        return yoda::generate(data, options);
    }
    return indy::generate(data, options);
}

} // namespace das::world
