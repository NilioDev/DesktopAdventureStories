// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/world/generator.hpp"

#include "das/script/opcodes.hpp"

#include "yoda_generator.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <random>
#include <set>

namespace das::world {

namespace {

constexpr int kMapSize = 10;
constexpr int kDagobahSize = 2;
constexpr int kOutdoorWidth = 18;
constexpr std::uint16_t kDagobahPlanet = 5;
constexpr std::uint32_t kDoorIn = 9;
constexpr std::uint32_t kSpawnSpot = 1;
constexpr std::uint32_t kItemSpot = 0;
// Bounds the search for a puzzle chain before we try a shorter one.
constexpr int kSearchBudget = 20000;

using Rng = std::mt19937;

bool contains(const std::vector<std::uint16_t>& list, std::uint16_t item) {
    return std::find(list.begin(), list.end(), item) != list.end();
}

template <typename T>
void shuffle(std::vector<T>& v, Rng& rng) {
    std::shuffle(v.begin(), v.end(), rng);
}

int pick(int count, Rng& rng) { return std::uniform_int_distribution<int>(0, count - 1)(rng); }

struct Candidate {
    int zone = -1;
    ZoneType type = ZoneType::Empty;
    std::vector<int> zones;
    std::vector<std::uint16_t> required;
    std::vector<std::uint16_t> goalItems;
    std::vector<std::uint16_t> provided;
    bool scriptHandsOut = false; // a script drops or adds "the find item"
};

// Whether a zone's scripts hand out the zone's find item themselves.
bool handsOutFindItem(const data::Zone& zone, data::Layout layout) {
    if (!zone.actions) {
        return false;
    }
    for (const auto& action : *zone.actions) {
        for (const auto& line : action.instructions) {
            const auto op = script::instruction(layout, line.opcode);
            if ((op == script::Instruction::DropItem || op == script::Instruction::AddItem) && line.args[0] == -1) {
                return true;
            }
        }
    }
    return false;
}

// Dagobah, where a Yoda Stories game starts: the landing zone and the
// empty zones around it.
void buildDagobah(const data::GameData& data_, WorldMap& world) {
    Map& map = world.dagobah;
    map.width = kDagobahSize;
    map.height = kDagobahSize;
    map.sites.assign(kDagobahSize * kDagobahSize, Site{});
    std::vector<int> empties;
    int landing = -1;
    for (std::size_t i = 0; i < data_.zones.size(); ++i) {
        const auto& z = data_.zones[i];
        if (z.planet != kDagobahPlanet || z.width != kOutdoorWidth) {
            continue;
        }
        if (z.type == static_cast<std::uint32_t>(ZoneType::TravelStart)) {
            landing = static_cast<int>(i);
        } else if (z.type == static_cast<std::uint32_t>(ZoneType::Empty)) {
            empties.push_back(static_cast<int>(i));
        }
    }
    if (landing < 0) {
        return;
    }
    // The landing zone sits bottom right, the others around it.
    std::vector<int> cells{landing};
    cells.insert(cells.end(), empties.begin(), empties.end());
    const std::pair<int, int> spots[] = {{1, 1}, {0, 0}, {1, 0}, {0, 1}};
    for (std::size_t i = 0; i < cells.size() && i < 4; ++i) {
        Site s;
        s.kind = SiteKind::Dagobah;
        s.zone = cells[i];
        s.zones = zoneCluster(data_, cells[i]);
        map.at(spots[i].first, spots[i].second) = std::move(s);
    }
    world.startZone = landing;
}

class Generator {
public:
    Generator(const data::GameData& data, const GeneratorOptions& options)
        : data_(data), options_(options), rng_(options.seed), puzzles_(data::parsePuzzles(data)) {
        for (std::size_t i = 0; i < puzzles_.size(); ++i) {
            const auto& p = puzzles_[i];
            const int index = static_cast<int>(i);
            if (p.item1 == data::kNoTile) {
                continue;
            }
            if (p.kind() == data::PuzzleType::Trade && !p.text(data::PuzzleText::Request).empty()) {
                requests_[p.item1].push_back(index);
            }
            if ((p.kind() == data::PuzzleType::Trade || p.kind() == data::PuzzleType::Offer ||
                 p.kind() == data::PuzzleType::Final) &&
                !p.text(data::PuzzleText::Offer).empty()) {
                offers_[p.item1].push_back(index);
            }
        }
    }

    WorldMap run() {
        const bool yoda = data_.layout() == data::Layout::Yoda;
        std::vector<int> planets;
        if (!yoda) {
            planets = {0};
        } else if (options_.planet >= 0) {
            planets = {options_.planet};
        } else {
            planets = {1, 2, 3};
            shuffle(planets, rng_);
        }

        int length = puzzleCount();
        for (; length >= 1; --length) {
            for (const int planet : planets) {
                if (auto map = tryPlanet(planet, length)) {
                    return *map;
                }
            }
        }
        throw GeneratorError("could not build a world from this game data");
    }

private:
    int puzzleCount() const {
        switch (options_.size) {
        case WorldSize::Small: return 5;
        case WorldSize::Medium: return 8;
        case WorldSize::Large: return 12;
        }
        return 8;
    }

    int fillerCount() const {
        switch (options_.size) {
        case WorldSize::Small: return 6;
        case WorldSize::Medium: return 10;
        case WorldSize::Large: return 16;
        }
        return 10;
    }

    std::vector<Candidate> candidates(int planet) const {
        std::vector<Candidate> out;
        for (std::size_t i = 0; i < data_.zones.size(); ++i) {
            const auto& z = data_.zones[i];
            if (z.planet != planet || z.width != kOutdoorWidth) {
                continue;
            }
            const auto type = static_cast<ZoneType>(z.type);
            switch (type) {
            case ZoneType::Empty:
            case ZoneType::Town:
            case ZoneType::Goal:
            case ZoneType::Use:
            case ZoneType::Trade:
            case ZoneType::Find: break;
            default: continue;
            }
            Candidate c;
            c.zone = static_cast<int>(i);
            c.type = type;
            c.zones = zoneCluster(data_, c.zone);
            for (const int member : c.zones) {
                const auto& m = data_.zones[static_cast<std::size_t>(member)];
                c.required.insert(c.required.end(), m.requiredItems.begin(), m.requiredItems.end());
                c.goalItems.insert(c.goalItems.end(), m.goalItems.begin(), m.goalItems.end());
                c.provided.insert(c.provided.end(), m.providedItems.begin(), m.providedItems.end());
                c.scriptHandsOut = c.scriptHandsOut || handsOutFindItem(m, data_.layout());
            }
            out.push_back(std::move(c));
        }
        return out;
    }

    bool free(const Candidate& c) const {
        return std::none_of(c.zones.begin(), c.zones.end(), [&](int z) { return used_.contains(z); });
    }

    void take(const Candidate& c) { used_.insert(c.zones.begin(), c.zones.end()); }
    void release(const Candidate& c) {
        for (const int z : c.zones) {
            used_.erase(z);
        }
    }

    bool hasOffer(std::uint16_t item) const { return offers_.contains(item); }
    bool hasRequest(std::uint16_t item) const { return requests_.contains(item); }

    int offerFor(std::uint16_t item) {
        const auto& list = offers_.at(item);
        // A Final puzzle's offer fits best right before the goal; any will do.
        return list[static_cast<std::size_t>(pick(static_cast<int>(list.size()), rng_))];
    }
    int requestFor(std::uint16_t item) {
        const auto& list = requests_.at(item);
        return list[static_cast<std::size_t>(pick(static_cast<int>(list.size()), rng_))];
    }

    // Builds a chain of `remaining` sites that ends by handing out `item`.
    // The sites are appended last-first.
    bool chain(std::uint16_t item, int remaining, std::vector<Site>& out) {
        if (--budget_ < 0) {
            return false;
        }
        std::vector<std::size_t> order;
        for (std::size_t i = 0; i < pool_.size(); ++i) {
            const auto& c = pool_[i];
            const bool kindFits = remaining == 1 ? c.type == ZoneType::Find
                                                 : (c.type == ZoneType::Use || c.type == ZoneType::Trade);
            if (kindFits && contains(c.provided, item) && free(c)) {
                order.push_back(i);
            }
        }
        shuffle(order, rng_);
        for (const std::size_t i : order) {
            const Candidate& c = pool_[i];
            if (remaining == 1) {
                take(c);
                out.push_back(makeSite(c, data::kNoTile, item));
                return true;
            }
            std::vector<std::uint16_t> wants;
            for (const auto want : c.required) {
                if (want != item && hasRequest(want) && hasOffer(want) && !items_.contains(want) &&
                    !contains(wants, want)) {
                    wants.push_back(want);
                }
            }
            shuffle(wants, rng_);
            take(c);
            for (const auto want : wants) {
                items_.insert(want);
                const std::size_t mark = out.size();
                out.push_back(makeSite(c, want, item));
                if (chain(want, remaining - 1, out)) {
                    return true;
                }
                out.resize(mark);
                items_.erase(want);
                if (budget_ < 0) {
                    break;
                }
            }
            release(c);
            if (budget_ < 0) {
                return false;
            }
        }
        return false;
    }

    Site makeSite(const Candidate& c, std::uint16_t required, std::uint16_t find) {
        Site s;
        s.kind = SiteKind::Puzzle;
        s.zone = c.zone;
        s.zones = c.zones;
        s.requiredItem = required;
        s.findItem = find;
        if (required != data::kNoTile) {
            s.requestPuzzle = requestFor(required);
        }
        if (find != data::kNoTile) {
            s.offerPuzzle = offerFor(find);
        }
        if (c.type == ZoneType::Trade) {
            placeNpc(s);
        }
        if (s.npc == data::kNoTile && find != data::kNoTile && !c.scriptHandsOut) {
            placeItem(s);
        }
        return s;
    }

    static int spotIndex(const data::Zone& z, std::uint32_t type) {
        if (!z.hotspots) {
            return -1;
        }
        for (std::size_t i = 0; i < z.hotspots->size(); ++i) {
            if ((*z.hotspots)[i].type == type) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void placeNpc(Site& s) {
        for (const int member : s.zones) {
            const auto& z = data_.zones[static_cast<std::size_t>(member)];
            const int spot = spotIndex(z, kSpawnSpot);
            if (!z.npcs.empty() && spot >= 0) {
                s.npc = z.npcs[static_cast<std::size_t>(pick(static_cast<int>(z.npcs.size()), rng_))];
                s.npcZone = member;
                s.npcSpot = spot;
                return;
            }
        }
    }

    void placeItem(Site& s) {
        // Prefer the zone that lists the item.
        for (const bool listsItem : {true, false}) {
            for (const int member : s.zones) {
                const auto& z = data_.zones[static_cast<std::size_t>(member)];
                const int spot = spotIndex(z, kItemSpot);
                if (spot >= 0 && (!listsItem || contains(z.providedItems, s.findItem))) {
                    s.itemZone = member;
                    s.itemSpot = spot;
                    return;
                }
            }
        }
    }

    std::optional<WorldMap> tryPlanet(int planet, int length) {
        pool_ = candidates(planet);
        std::vector<std::pair<int, std::size_t>> goals; // puzzle, candidate
        for (std::size_t i = 0; i < puzzles_.size(); ++i) {
            const auto& p = puzzles_[i];
            if (p.kind() != data::PuzzleType::Goal || (options_.goal >= 0 && options_.goal != static_cast<int>(i))) {
                continue;
            }
            for (std::size_t c = 0; c < pool_.size(); ++c) {
                const auto& cand = pool_[c];
                if (cand.type == ZoneType::Goal && contains(cand.provided, p.item1) && !cand.required.empty()) {
                    goals.emplace_back(static_cast<int>(i), c);
                }
            }
        }
        shuffle(goals, rng_);
        std::size_t town = pool_.size();
        for (std::size_t c = 0; c < pool_.size(); ++c) {
            if (pool_[c].type == ZoneType::Town) {
                town = c;
            }
        }
        if (town == pool_.size()) {
            return std::nullopt;
        }

        for (const auto& [goalPuzzle, goalIndex] : goals) {
            used_.clear();
            items_.clear();
            const Candidate goal = pool_[goalIndex];
            take(pool_[town]);
            if (!free(goal)) {
                continue;
            }
            take(goal);

            // One chain for each list of the goal zone (Yoda Stories has two).
            std::vector<std::vector<std::uint16_t>> heads{goal.required};
            if (!goal.goalItems.empty()) {
                heads.push_back(goal.goalItems);
            }
            const int chains = static_cast<int>(heads.size());
            if (length < chains) {
                continue;
            }
            Site goalSite;
            goalSite.kind = SiteKind::Goal;
            goalSite.zone = goal.zone;
            goalSite.zones = goal.zones;
            goalSite.offerPuzzle = goalPuzzle;

            std::vector<std::vector<Site>> built;
            bool ok = true;
            for (int k = 0; k < chains && ok; ++k) {
                const int part = length / chains + (k < length % chains ? 1 : 0);
                auto options = heads[static_cast<std::size_t>(k)];
                shuffle(options, rng_);
                ok = false;
                for (const auto head : options) {
                    if (!hasOffer(head) || items_.contains(head)) {
                        continue;
                    }
                    budget_ = kSearchBudget;
                    items_.insert(head);
                    std::vector<Site> sites;
                    if (chain(head, part, sites)) {
                        std::reverse(sites.begin(), sites.end());
                        built.push_back(std::move(sites));
                        (k == 0 ? goalSite.requiredItem : goalSite.requiredItem2) = head;
                        ok = true;
                        break;
                    }
                    items_.erase(head);
                }
            }
            if (!ok) {
                continue;
            }
            if (goalSite.requiredItem != data::kNoTile) {
                if (const auto it = requests_.find(goalSite.requiredItem); it != requests_.end()) {
                    goalSite.requestPuzzle = it->second.front();
                }
            }
            return layout(planet, goalPuzzle, pool_[town], std::move(built), std::move(goalSite), length);
        }
        return std::nullopt;
    }

    static int distance(int ax, int ay, int bx, int by) { return std::abs(ax - bx) + std::abs(ay - by); }

    std::vector<std::pair<int, int>> frontier(const Map& map) const {
        std::vector<std::pair<int, int>> out;
        for (int y = 0; y < map.height; ++y) {
            for (int x = 0; x < map.width; ++x) {
                if (map.at(x, y).kind != SiteKind::None) {
                    continue;
                }
                const bool next = (map.inside(x - 1, y) && map.at(x - 1, y).kind != SiteKind::None) ||
                                  (map.inside(x + 1, y) && map.at(x + 1, y).kind != SiteKind::None) ||
                                  (map.inside(x, y - 1) && map.at(x, y - 1).kind != SiteKind::None) ||
                                  (map.inside(x, y + 1) && map.at(x, y + 1).kind != SiteKind::None);
                if (next) {
                    out.emplace_back(x, y);
                }
            }
        }
        return out;
    }

    // Places a site on a free cell next to the map's sites, as close as we
    // can to `wanted` steps from the town.
    bool place(Map& map, Site site, int townX, int townY, int wanted) {
        auto cells = frontier(map);
        if (cells.empty()) {
            return false;
        }
        shuffle(cells, rng_);
        std::stable_sort(cells.begin(), cells.end(), [&](const auto& a, const auto& b) {
            return std::abs(distance(a.first, a.second, townX, townY) - wanted) <
                   std::abs(distance(b.first, b.second, townX, townY) - wanted);
        });
        map.at(cells.front().first, cells.front().second) = std::move(site);
        return true;
    }

    std::optional<Site> emptySite() {
        std::vector<std::size_t> empties;
        for (std::size_t i = 0; i < pool_.size(); ++i) {
            if (pool_[i].type == ZoneType::Empty && free(pool_[i])) {
                empties.push_back(i);
            }
        }
        if (empties.empty()) {
            return std::nullopt;
        }
        const auto& c = pool_[empties[static_cast<std::size_t>(pick(static_cast<int>(empties.size()), rng_))]];
        take(c);
        Site s;
        s.kind = SiteKind::Empty;
        s.zone = c.zone;
        s.zones = c.zones;
        return s;
    }

    WorldMap layout(int planet, int goalPuzzle, const Candidate& town, std::vector<std::vector<Site>> chains,
                    Site goal, int length) {
        WorldMap world;
        world.planet = planet;
        world.goalPuzzle = goalPuzzle;
        world.puzzleCount = length;
        Map& map = world.world;
        map.width = kMapSize;
        map.height = kMapSize;
        map.sites.assign(kMapSize * kMapSize, Site{});

        const int townX = 3 + pick(4, rng_);
        const int townY = 3 + pick(4, rng_);
        Site townSite;
        townSite.kind = SiteKind::Town;
        townSite.zone = town.zone;
        townSite.zones = town.zones;
        map.at(townX, townY) = townSite;

        // Play order: the chains one after the other, then the goal, each a
        // little further from the town than the one before.
        std::vector<Site> order;
        for (auto& c : chains) {
            order.insert(order.end(), std::make_move_iterator(c.begin()), std::make_move_iterator(c.end()));
        }
        order.push_back(std::move(goal));
        const int far = std::min(kMapSize - 2, 2 + static_cast<int>(order.size()) / 2);
        for (std::size_t i = 0; i < order.size(); ++i) {
            // Now and then an empty zone in between.
            if (pick(3, rng_) == 0) {
                if (auto e = emptySite()) {
                    place(map, std::move(*e), townX, townY, 1 + static_cast<int>(i) * far / static_cast<int>(order.size()));
                }
            }
            const int wanted = 1 + static_cast<int>(i + 1) * far / static_cast<int>(order.size());
            if (!place(map, std::move(order[i]), townX, townY, wanted)) {
                throw GeneratorError("the world map is full");
            }
        }
        for (int i = 0; i < fillerCount(); ++i) {
            auto e = emptySite();
            if (!e) {
                break;
            }
            const int wanted = 1 + pick(far + 1, rng_);
            place(map, std::move(*e), townX, townY, wanted);
        }

        if (data_.layout() == data::Layout::Yoda) {
            buildDagobah(data_, world);
        }
        if (world.startZone < 0) {
            world.startZone = town.zone;
        }
        return world;
    }

    const data::GameData& data_;
    GeneratorOptions options_;
    Rng rng_;
    std::vector<data::Puzzle> puzzles_;
    std::map<std::uint16_t, std::vector<int>> requests_;
    std::map<std::uint16_t, std::vector<int>> offers_;
    std::vector<Candidate> pool_;
    std::set<int> used_;
    std::set<std::uint16_t> items_;
    int budget_ = 0;
};

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

WorldMap generateWorld(const data::GameData& data, const GeneratorOptions& options) {
    if (data.layout() != data::Layout::Yoda) {
        return Generator(data, options).run();
    }
    return yoda::generate(data, options);
}

} // namespace das::world
