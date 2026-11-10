// SPDX-License-Identifier: GPL-3.0-or-later
//
// Combat: monsters that move and attack, the hero's weapons, shots and
// health. Our own rules, built on what the data says about each character;
// see docs/combat.md.
#include "das/world/world.hpp"

#include "das/data/tile_flags.hpp"

#include <cstdlib>

namespace das::world {

namespace {

constexpr int kObjectLayer = 1;
constexpr std::uint16_t kNone16 = 0xffff;

// Character types (Character::type).
constexpr std::uint16_t kTypeEnemy = 2;
constexpr std::uint16_t kTypeWeapon = 4;

// Movement kinds (Character::movement), as we read them.
enum Movement : std::uint16_t {
    Still = 0,
    Chase = 1,     // goes for the hero
    Lurk = 2,      // goes for the hero when close, else wanders
    Wander = 3,
    Sit = 4,       // stays put, attacks what comes close or into its line
    Rolling = 5,   // a rolling object: chases
    Stalk = 6,     // goes for the hero when close, else stays
    Droid = 7,
    Hover = 8,
    Roam = 9,      // wanders and shoots
    Patrol = 10,
    Scared = 11,   // keeps away from the hero
    Scenery = 12,  // part of the zone: never moves or attacks
};

constexpr int kStepMs = 450;         // a monster's step
constexpr int kAttackMs = 1000;      // between a monster's attacks
constexpr int kShootMs = 1600;       // between a monster's shots
constexpr int kShotStepMs = 70;      // a shot flies one cell per step
constexpr int kShotRange = 8;
constexpr int kNearby = 4;           // how close Lurk and Stalk notice the hero
constexpr int kHeroAttackMs = 300;   // between the hero's attacks
constexpr int kAttackShowMs = 200;

int distance(Position a, Position b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }

data::Facing facingOf(int dx, int dy) {
    if (dy < 0) {
        return data::Facing::Up;
    }
    if (dy > 0) {
        return data::Facing::Down;
    }
    return dx < 0 ? data::Facing::Left : data::Facing::Right;
}

void stepOf(data::Facing f, int& dx, int& dy) {
    dx = 0;
    dy = 0;
    switch (f) {
    case data::Facing::Up: dy = -1; break;
    case data::Facing::Down: dy = 1; break;
    case data::Facing::Left: dx = -1; break;
    default: dx = 1; break;
    }
}

int value(std::uint16_t v) { return v == kNone16 ? 0 : v; }

} // namespace

void World::loadFighters() {
    fighters_.clear();
    for (const auto& entry : data_.characters) {
        Fighter f;
        try {
            f.character = data::parseCharacter(entry.body, data_.layout());
        } catch (const std::exception&) {
            // keep an empty character
        }
        fighters_.push_back(f);
    }
    for (const auto& v : data_.characterWeapons) {
        if (v.character >= fighters_.size()) {
            continue;
        }
        Fighter& f = fighters_[v.character];
        if (f.character.type == kTypeWeapon) {
            f.sound = v.reference == kNone16 ? -1 : v.reference;
        } else {
            f.health = value(v.value);
            f.weapon = v.reference == kNone16 ? -1 : v.reference;
        }
    }
    for (const auto& v : data_.characterAux) {
        if (v.character < fighters_.size()) {
            fighters_[v.character].damage = value(v.value);
        }
    }
    for (Fighter& f : fighters_) {
        if (f.character.type == kTypeWeapon) {
            f.item = f.character.frame(0, data::Facing::DownRight);
        }
    }
}

const Fighter* World::fighter(int character) const {
    return character >= 0 && static_cast<std::size_t>(character) < fighters_.size()
               ? &fighters_[static_cast<std::size_t>(character)]
               : nullptr;
}

const Fighter* World::weaponFighter(std::uint16_t item) const {
    for (const Fighter& f : fighters_) {
        if (f.character.type == kTypeWeapon && f.item == item) {
            return &f;
        }
    }
    if (item == data::kNoTile) {
        // Bare hands: a weapon without an inventory tile (Indy's fists).
        for (const Fighter& f : fighters_) {
            if (f.character.type == kTypeWeapon && f.item == kNone16 && f.damage > 0) {
                return &f;
            }
        }
    }
    return nullptr;
}

bool World::isWeapon(std::uint16_t item) const {
    return item != data::kNoTile && weaponFighter(item) != nullptr;
}

bool World::equip(std::uint16_t item) {
    if (!hasItem(item) || !isWeapon(item)) {
        return false;
    }
    weapon_ = item;
    return true;
}

void World::begin() {
    if (data_.layout() == data::Layout::Indy) {
        for (const Fighter& f : fighters_) {
            if (f.character.type == kTypeWeapon && f.item != kNone16) {
                addItem(f.item);
                break;
            }
        }
    }
    start(map_.startZone);
}

void World::initMonsters(int zoneIndex, ZoneState& s) {
    const auto& z = data_.zones.at(static_cast<std::size_t>(zoneIndex));
    s.monsters.clear();
    for (const auto& m : z.monsters) {
        MonsterState st;
        st.at = {m.x, m.y};
        const Fighter* f = fighter(m.character);
        st.health = f != nullptr ? f->health : 0;
        st.moveMs = kStepMs;
        st.attackMs = kAttackMs;
        s.monsters.push_back(st);
    }
}

std::vector<MonsterView> World::monsters() const {
    std::vector<MonsterView> out;
    const auto* s = findState(zone_);
    if (s == nullptr) {
        return out;
    }
    const auto& list = zone().monsters;
    for (std::size_t i = 0; i < s->monsters.size() && i < list.size(); ++i) {
        if (!s->monsterEnabled[i] || s->monsterDead[i]) {
            continue;
        }
        const Fighter* f = fighter(list[i].character);
        if (f == nullptr) {
            continue;
        }
        const auto& m = s->monsters[i];
        std::uint16_t tile = f->character.frame(m.frame % data::kFrameSets, m.facing);
        if (tile == kNone16) {
            tile = f->character.frame(0, m.facing);
        }
        if (tile == kNone16) {
            tile = f->character.frame(0, data::Facing::Down);
        }
        out.push_back({i, m.at, tile});
    }
    return out;
}

bool World::monsterAt(Position at, std::size_t* index) const {
    const auto* s = findState(zone_);
    if (s == nullptr) {
        return false;
    }
    for (std::size_t i = 0; i < s->monsters.size(); ++i) {
        if (s->monsterEnabled[i] && !s->monsterDead[i] && s->monsters[i].at == at) {
            if (index != nullptr) {
                *index = i;
            }
            return true;
        }
    }
    return false;
}

bool World::freeForMonster(Position at) const {
    return inside(at) && walkable(at) && !(at == player_) && !monsterAt(at);
}

bool World::lineClear(Position from, Position to) const {
    if (from.x != to.x && from.y != to.y) {
        return false;
    }
    const int dx = to.x > from.x ? 1 : (to.x < from.x ? -1 : 0);
    const int dy = to.y > from.y ? 1 : (to.y < from.y ? -1 : 0);
    Position p{from.x + dx, from.y + dy};
    while (!(p == to)) {
        if (!walkable(p) || monsterAt(p)) {
            return false;
        }
        p = {p.x + dx, p.y + dy};
    }
    return true;
}

void World::hurtHero(int damage) {
    if (damage <= 0 || outcome_ != Outcome::Playing) {
        return;
    }
    // Difficulty scales what monsters do: half at 0, normal at 50, one and a
    // half times at 100.
    const int scaled = std::max(1, damage * (50 + difficulty_) / 100);
    health_ = std::max(0, health_ - scaled);
    if (health_ == 0) {
        outcome_ = Outcome::Lost;
        effects_.push_back({Effect::Kind::Lost, {}, {}, 0});
    }
}

void World::hurtMonster(std::size_t index, int damage) {
    auto& s = state();
    if (index >= s.monsters.size() || s.monsterDead[index]) {
        return;
    }
    auto& m = s.monsters[index];
    if (m.health <= 0) {
        return; // cannot be hurt
    }
    m.health -= std::max(1, damage);
    if (m.health > 0) {
        return;
    }
    s.monsterDead[index] = true;
    // What it carried drops where it fell.
    const auto& z = zone();
    const std::uint16_t item = z.monsters[index].item;
    if (item != data::kNoTile && item < data_.tiles.size()) {
        auto& layer = s.cells[static_cast<std::size_t>(m.at.y * z.width + m.at.x)].layers[kObjectLayer];
        if (layer == data::kNoTile) {
            layer = item;
        } else {
            s.hidden[{m.at.x, m.at.y}] = item;
        }
    }
    // Scripts may wait for a monster to die.
    trigger(script::Event::Update);
    runScripts();
}

void World::fire(Position from, int dx, int dy, const Fighter& weapon, int damage, bool fromHero) {
    Shot shot;
    shot.at = from;
    shot.dx = dx;
    shot.dy = dy;
    shot.tile = weapon.character.frame(0, facingOf(dx, dy));
    shot.damage = damage;
    shot.range = kShotRange;
    shot.fromHero = fromHero;
    shots_.push_back(shot);
}

void World::moveShots() {
    std::vector<Shot> keep;
    for (Shot shot : shots_) {
        const Position next{shot.at.x + shot.dx, shot.at.y + shot.dy};
        if (!inside(next) || shot.range <= 0) {
            continue;
        }
        std::size_t index = 0;
        if (shot.fromHero && monsterAt(next, &index)) {
            hurtMonster(index, shot.damage);
            continue;
        }
        if (!shot.fromHero && next == player_) {
            hurtHero(shot.damage);
            continue;
        }
        if (!walkable(next)) {
            continue;
        }
        shot.at = next;
        --shot.range;
        keep.push_back(shot);
    }
    shots_ = std::move(keep);
}

bool World::attack() {
    if (paused_ || outcome_ != Outcome::Playing || attackCooldownMs_ > 0) {
        return false;
    }
    const Fighter* w = weaponFighter(weapon_);
    if (w == nullptr) {
        return false;
    }
    attackCooldownMs_ = kHeroAttackMs;
    attackShowMs_ = kAttackShowMs;
    if (w->sound >= 0) {
        effects_.push_back({Effect::Kind::Sound, {}, {}, w->sound});
    }
    int dx = 0;
    int dy = 0;
    stepOf(facing_, dx, dy);
    // A weapon with shot tiles shoots; others hit the cell ahead.
    const bool ranged = w->character.frame(0, facingOf(dx, dy)) != kNone16;
    if (ranged) {
        fire(player_, dx, dy, *w, w->damage, true);
        return true;
    }
    const Position ahead{player_.x + dx, player_.y + dy};
    std::size_t index = 0;
    if (monsterAt(ahead, &index)) {
        hurtMonster(index, w->damage);
    }
    return true;
}

void World::stepMonster(std::size_t index, MonsterState& m, const Fighter& f) {
    const auto kind = static_cast<Movement>(f.character.movement);
    const int near = distance(m.at, player_);
    int dx = 0;
    int dy = 0;
    const auto toward = [&](bool away) {
        const int sx = player_.x > m.at.x ? 1 : (player_.x < m.at.x ? -1 : 0);
        const int sy = player_.y > m.at.y ? 1 : (player_.y < m.at.y ? -1 : 0);
        // Try the longer axis first, then the other.
        const bool horizontal = std::abs(player_.x - m.at.x) >= std::abs(player_.y - m.at.y);
        const std::array<std::pair<int, int>, 2> tries =
            horizontal ? std::array<std::pair<int, int>, 2>{{{sx, 0}, {0, sy}}}
                       : std::array<std::pair<int, int>, 2>{{{0, sy}, {sx, 0}}};
        for (auto [tx, ty] : tries) {
            if (away) {
                tx = -tx;
                ty = -ty;
            }
            if ((tx != 0 || ty != 0) && freeForMonster({m.at.x + tx, m.at.y + ty})) {
                dx = tx;
                dy = ty;
                return;
            }
        }
    };
    const auto wander = [&] {
        static constexpr std::array<std::pair<int, int>, 5> kDirs{{{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
        const auto [tx, ty] = kDirs[rng_() % kDirs.size()];
        if ((tx != 0 || ty != 0) && freeForMonster({m.at.x + tx, m.at.y + ty})) {
            dx = tx;
            dy = ty;
        }
    };
    switch (kind) {
    case Still:
    case Sit:
    case Scenery: break;
    case Chase:
    case Rolling: toward(false); break;
    case Lurk:
        if (near <= kNearby) {
            toward(false);
        } else {
            wander();
        }
        break;
    case Stalk:
        if (near <= kNearby) {
            toward(false);
        }
        break;
    case Scared:
        if (near <= kNearby) {
            toward(true);
        } else {
            wander();
        }
        break;
    case Patrol: {
        // Yoda Stories gives patrols waypoints (x, y pairs; zero ends them).
        const auto& wp = zone().monsters[index].waypoints;
        const int count = [&] {
            int n = 0;
            while (n < 8 && (wp[static_cast<std::size_t>(n * 2)] != 0 || wp[static_cast<std::size_t>(n * 2 + 1)] != 0)) {
                ++n;
            }
            return n;
        }();
        if (count == 0) {
            wander();
            break;
        }
        const Position goal{wp[static_cast<std::size_t>(m.waypoint * 2)], wp[static_cast<std::size_t>(m.waypoint * 2 + 1)]};
        if (m.at == goal) {
            m.waypoint = (m.waypoint + 1) % count;
            break;
        }
        const int sx = goal.x > m.at.x ? 1 : (goal.x < m.at.x ? -1 : 0);
        const int sy = goal.y > m.at.y ? 1 : (goal.y < m.at.y ? -1 : 0);
        if (sx != 0 && freeForMonster({m.at.x + sx, m.at.y})) {
            dx = sx;
        } else if (sy != 0 && freeForMonster({m.at.x, m.at.y + sy})) {
            dy = sy;
        } else {
            m.waypoint = (m.waypoint + 1) % count;
        }
        break;
    }
    default: wander(); break;
    }
    if (dx != 0 || dy != 0) {
        m.at = {m.at.x + dx, m.at.y + dy};
        m.facing = facingOf(dx, dy);
        m.frame = m.frame == 1 ? 2 : 1;
    } else {
        m.frame = 0;
    }
}

void World::update(int ms) {
    if (ms <= 0 || outcome_ != Outcome::Playing) {
        return;
    }
    attackCooldownMs_ = std::max(0, attackCooldownMs_ - ms);
    attackShowMs_ = std::max(0, attackShowMs_ - ms);
    if (paused_) {
        return;
    }
    shotMs_ += ms;
    while (shotMs_ >= kShotStepMs && !shots_.empty()) {
        shotMs_ -= kShotStepMs;
        moveShots();
    }
    if (shots_.empty()) {
        shotMs_ = 0;
    }
    auto& s = state();
    const auto& list = zone().monsters;
    for (std::size_t i = 0; i < s.monsters.size() && i < list.size() && outcome_ == Outcome::Playing; ++i) {
        if (!s.monsterEnabled[i] || s.monsterDead[i]) {
            continue;
        }
        const Fighter* f = fighter(list[i].character);
        if (f == nullptr || f->character.type != kTypeEnemy || f->character.movement == Scenery) {
            continue;
        }
        auto& m = s.monsters[i];
        m.moveMs -= ms;
        m.attackMs -= ms;
        const bool adjacent = distance(m.at, player_) == 1;
        if (adjacent) {
            m.facing = facingOf(player_.x - m.at.x, player_.y - m.at.y);
            if (m.attackMs <= 0 && f->damage > 0) {
                m.attackMs = kAttackMs;
                hurtHero(f->damage);
            }
            continue;
        }
        // A monster with a weapon shoots along a clear row or column.
        const Fighter* gun = f->weapon >= 0 ? fighter(f->weapon) : nullptr;
        if (gun != nullptr && gun->character.type == kTypeWeapon && m.attackMs <= 0 &&
            distance(m.at, player_) <= kShotRange && lineClear(m.at, player_)) {
            const int dx = player_.x > m.at.x ? 1 : (player_.x < m.at.x ? -1 : 0);
            const int dy = player_.y > m.at.y ? 1 : (player_.y < m.at.y ? -1 : 0);
            m.facing = facingOf(dx, dy);
            m.attackMs = kShootMs;
            fire(m.at, dx, dy, *gun, gun->damage, false);
            if (gun->sound >= 0) {
                effects_.push_back({Effect::Kind::Sound, {}, {}, gun->sound});
            }
            continue;
        }
        if (m.moveMs <= 0) {
            m.moveMs += kStepMs;
            if (m.moveMs <= 0) {
                m.moveMs = kStepMs;
            }
            stepMonster(i, m, *f);
        }
    }
}

} // namespace das::world
