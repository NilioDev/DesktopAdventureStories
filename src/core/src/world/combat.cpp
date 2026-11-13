// SPDX-License-Identifier: GPL-3.0-or-later
//
// Combat: monsters that move and attack, the hero's weapons, shots and
// health. The monsters follow the rules of the original programs, worked out
// from Yodesk.exe and DESKADV.EXE; see docs/combat.md.
#include "das/world/world.hpp"

#include "das/data/tile_flags.hpp"

#include <algorithm>
#include <cstdlib>

namespace das::world {

namespace {

constexpr int kFloorLayer = 0;
constexpr int kObjectLayer = 1;
constexpr std::uint16_t kNone16 = 0xffff;
// Indy: the desk of the mission giver's office.
constexpr std::uint16_t kIndyMissionGiver = 794;

// Character types (Character::type).
constexpr std::uint16_t kTypeEnemy = 2;
constexpr std::uint16_t kTypeWeapon = 4;

// Floor tiles with this flag keep monsters out (doorways, edges).
constexpr std::uint32_t kNoMonsters = 1u << 16;
// Object tiles with this flag are enemies (Indy's rock rolls over them).
constexpr std::uint32_t kEnemyTile = 1u << 17;

// Sounds both games have at these places in their sound lists.
constexpr int kHurtSound = 4;      // the hero is hit
constexpr int kYodaDeathSound = 5; // Yoda Stories: a monster dies

// Both originals run a game tick every 140 ms (their default game speed).
constexpr int kTickMs = 140;
constexpr int kShotStepMs = 70; // the hero's shots fly one cell per step
constexpr int kShotRange = 8;
constexpr int kHeroAttackMs = 300; // between the hero's attacks
constexpr int kAttackShowMs = 200;
constexpr std::uint16_t kSpiritHeart = 1432; // Yoda Stories' extra life
constexpr int kSpiritHeartSound = 58;
constexpr int kScriptTickMs = 100; // a Walk pass on the hero's cell

data::Facing facingOf(int dx, int dy) {
    if (dy < 0 && dx == 0) {
        return data::Facing::Up;
    }
    if (dy > 0 && dx == 0) {
        return data::Facing::Down;
    }
    if (dx < 0) {
        return dy < 0 ? data::Facing::UpLeft : (dy > 0 ? data::Facing::DownLeft : data::Facing::Left);
    }
    if (dx > 0) {
        return dy < 0 ? data::Facing::UpRight : (dy > 0 ? data::Facing::DownRight : data::Facing::Right);
    }
    return data::Facing::Down;
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
int sign(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }

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
            f.invulnerable = v.value == kNone16;
            f.weapon = v.reference == kNone16 ? -1 : v.reference;
        }
    }
    for (const auto& v : data_.characterAux) {
        if (v.character < fighters_.size()) {
            fighters_[v.character].damage = value(v.value);
            fighters_[v.character].harmless = v.value == kNone16;
        }
    }
    for (Fighter& f : fighters_) {
        if (f.character.type == kTypeWeapon) {
            f.item = f.character.frame(0, data::Facing::DownRight);
        }
    }
    characterTile_.assign(fighters_.size(), kNone16);
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
    // Indy starts in front of the door to the mission giver's office, like
    // in the original.
    if (data_.layout() == data::Layout::Indy && zone().hotspots) {
        for (const auto& h : *zone().hotspots) {
            if (h.type != static_cast<std::uint32_t>(HotspotType::DoorIn) || h.argument >= data_.zones.size()) {
                continue;
            }
            const auto& room = data_.zones[h.argument];
            const bool office = std::any_of(room.cells.begin(), room.cells.end(), [](const data::ZoneCell& c) {
                return c.layers[kObjectLayer] == kIndyMissionGiver;
            });
            const Position below{h.x, h.y + 1};
            if (office && inside(below) && walkable(below)) {
                player_ = below;
                break;
            }
        }
    }
}

void World::initMonsters(int zoneIndex, ZoneState& s) {
    const auto& z = data_.zones.at(static_cast<std::size_t>(zoneIndex));
    s.monsters.clear();
    for (const auto& m : z.monsters) {
        MonsterState st;
        st.at = {m.x, m.y};
        st.shotAt = st.at;
        const Fighter* f = fighter(m.character);
        st.health = f != nullptr ? f->health : 0;
        if (f != nullptr) {
            st.tile = f->character.frame(0, data::Facing::Down);
        }
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
        const auto& m = s->monsters[i];
        if (m.hidden) {
            continue;
        }
        std::uint16_t tile = m.tile;
        if (tile == kNone16) {
            if (const Fighter* f = fighter(list[i].character)) {
                tile = f->character.frame(0, data::Facing::Down);
            }
        }
        out.push_back({i, m.at, tile});
    }
    return out;
}

std::vector<Shot> World::shots() const {
    std::vector<Shot> out = shots_;
    const auto* s = findState(zone_);
    if (s == nullptr) {
        return out;
    }
    const auto& list = zone().monsters;
    for (std::size_t i = 0; i < s->monsters.size() && i < list.size(); ++i) {
        const auto& m = s->monsters[i];
        if (!m.shotShown || !s->monsterEnabled[i] || s->monsterDead[i]) {
            continue;
        }
        const Fighter* f = fighter(list[i].character);
        const Fighter* gun = f != nullptr ? fighter(f->weapon) : nullptr;
        if (gun == nullptr) {
            continue;
        }
        Shot shot;
        shot.at = m.shotAt;
        shot.dx = m.shotDx;
        shot.dy = m.shotDy;
        shot.tile = gun->character.frame(0, facingOf(m.shotDx, m.shotDy));
        out.push_back(shot);
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

bool World::monsterShotAt(Position at) const {
    const auto* s = findState(zone_);
    if (s == nullptr) {
        return false;
    }
    for (std::size_t i = 0; i < s->monsters.size(); ++i) {
        const auto& m = s->monsters[i];
        if (m.shotShown && s->monsterEnabled[i] && !s->monsterDead[i] && m.shotAt == at) {
            return true;
        }
    }
    return false;
}

void World::hurtHero(int damage) {
    if (damage > 0) {
        changeHealth(-damage);
    }
}

// Health as the originals keep it: a bar (1..3, the one being used up) and
// the points lost in it (1..100); health = 301 - points - 100 * (bar - 1),
// so 300 is full. The odd steps for big changes are the originals' own.
void World::changeHealth(int amount) {
    if (amount == 0 || outcome_ != Outcome::Playing) {
        return;
    }
    const int lost = kMaxHealth - std::clamp(health_, 1, kMaxHealth);
    int bar = 1 + lost / 100;
    int points = 1 + lost % 100;
    const bool yoda = data_.layout() == data::Layout::Yoda;
    if (amount < 0) {
        // Damage is divided by 100 / difficulty (whole numbers): half at
        // the normal 50, all of it at 100, and at least 1.
        const int divisor = std::max(1, 100 / std::max(1, difficulty_));
        const int d = std::min(-1, amount / divisor);
        const int bars = !yoda ? 0 : d <= -300 ? 3 : d <= -200 ? 2 : d <= -100 ? 1 : 0;
        if (bars > 0) {
            // Yoda Stories: whole bars, and a third of the damage on top.
            bar += bars;
            points += d / -3 - 1;
        } else {
            points -= d;
            if (points > 100) {
                points -= 100;
                ++bar;
            }
        }
        if (bar > 3) {
            bar = 3;
            points = 100;
        }
    } else {
        const int a = std::max(1, amount);
        if (yoda && a >= 300) {
            bar = 1;
            points = 1;
        } else if (yoda && a >= 200) {
            bar -= 2;
            points += a / -3 + 1;
        } else if (yoda && a >= 100) {
            bar -= 1;
        } else {
            points -= a;
            if (points < 1) {
                points += 100;
                --bar;
            }
        }
        if (bar < 1) {
            bar = 1;
            points = 1;
        }
    }
    health_ = std::clamp(kMaxHealth + 1 - points - 100 * (bar - 1), 1, kMaxHealth);
    if (points <= 99 || bar < 3) {
        return;
    }
    // Yoda Stories: a Spirit Heart in the backpack brings the hero back.
    if (yoda) {
        const auto heart = std::find(inventory_.begin(), inventory_.end(), kSpiritHeart);
        if (heart != inventory_.end()) {
            inventory_.erase(heart);
            health_ = kMaxHealth;
            effects_.push_back({Effect::Kind::Sound, {}, {}, kSpiritHeartSound});
            return;
        }
    }
    health_ = 0;
    outcome_ = Outcome::Lost;
    mission_ = -1;
    effects_.push_back({Effect::Kind::Lost, {}, {}, 0});
}

void World::hurtMonster(std::size_t index, int damage) {
    auto& s = state();
    if (index >= s.monsters.size() || s.monsterDead[index]) {
        return;
    }
    const auto& z = zone();
    const Fighter* f = fighter(z.monsters[index].character);
    const bool yoda = data_.layout() == data::Layout::Yoda;
    if (f == nullptr || (yoda && f->invulnerable)) {
        return; // Yoda Stories: health 0xFFFF cannot be hurt
    }
    auto& m = s.monsters[index];
    // The originals add up the damage taken and compare it with the
    // character's health, so a health of 0 dies at the first hit.
    m.health -= damage;
    if (m.health > 0) {
        return;
    }
    s.monsterDead[index] = true;
    m.shotShown = false;
    if (yoda) {
        effects_.push_back({Effect::Kind::Sound, {}, {}, kYodaDeathSound});
        dropItem(index, m.at);
    }
    // Scripts may wait for a monster to die.
    trigger(script::Event::Update);
    runScripts();
}

void World::dropItem(std::size_t index, Position at) {
    // Yoda Stories: only monsters marked to drop something do. Their item
    // number is one more than the tile; 0 means the zone's own item: the
    // first item spot with an item.
    const auto& z = zone();
    const auto& placed = z.monsters[index];
    if (placed.unknown == 0) {
        return;
    }
    std::uint16_t item = data::kNoTile;
    if (placed.item != 0) {
        item = static_cast<std::uint16_t>(placed.item - 1);
    } else if (z.hotspots) {
        const Site* s = site(zone_);
        const auto& list = *z.hotspots;
        for (std::size_t i = 0; i < list.size() && item == data::kNoTile; ++i) {
            const auto& h = list[i];
            if (h.type != static_cast<std::uint32_t>(HotspotType::ItemSpot)) {
                continue;
            }
            std::uint16_t arg = h.argument;
            if (s != nullptr && s->itemZone == zone_ && s->itemSpot == static_cast<int>(i)) {
                arg = s->findItem;
            }
            if (arg != 0 && arg < 0x8000) {
                item = arg;
            }
        }
        // Our worlds already put the zone's item on its spot: no second one.
        if (item != data::kNoTile && hasItem(item)) {
            item = data::kNoTile;
        }
        for (const auto& c : cells()) {
            if (item != data::kNoTile && c.layers[kObjectLayer] == item) {
                item = data::kNoTile;
            }
        }
    }
    if (item == data::kNoTile || item >= data_.tiles.size()) {
        return;
    }
    auto& s = state();
    auto& layer = s.cells[static_cast<std::size_t>(at.y * z.width + at.x)].layers[kObjectLayer];
    if (layer == data::kNoTile) {
        layer = item;
    } else {
        s.hidden[{at.x, at.y}] = item;
    }
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
    if (paused_ || drop_ || outcome_ != Outcome::Playing || attackCooldownMs_ > 0) {
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
    // Both originals: below the normal difficulty the hero hits harder, up
    // to ten times at 0.
    int damage = w->damage;
    if (difficulty_ < 50) {
        damage *= 10 + difficulty_ / -5;
    }
    damage = std::max(1, damage);
    int dx = 0;
    int dy = 0;
    stepOf(facing_, dx, dy);
    // A weapon with shot tiles shoots; others hit the cell ahead.
    const bool ranged = w->character.frame(0, facingOf(dx, dy)) != kNone16;
    if (ranged) {
        fire(player_, dx, dy, *w, damage, true);
        return true;
    }
    const Position ahead{player_.x + dx, player_.y + dy};
    std::size_t index = 0;
    if (monsterAt(ahead, &index)) {
        hurtMonster(index, damage);
    }
    return true;
}

// --- Monsters, as the originals move them --------------------------------
//
// Once per game tick each monster of the zone decides one step. What it does
// depends on its character's movement number; docs/combat.md describes every
// kind. The originals keep monsters and their shots in the zone's object
// layer, so they block each other like walls do.

std::uint16_t World::objectAt(Position at) const {
    if (!inside(at)) {
        return kNone16;
    }
    const auto* s = findState(zone_);
    if (s != nullptr) {
        for (std::size_t i = 0; i < s->monsters.size(); ++i) {
            const auto& m = s->monsters[i];
            if (!s->monsterEnabled[i] || s->monsterDead[i]) {
                continue;
            }
            if (m.at == at && !m.hidden) {
                return m.tile == kNone16 ? std::uint16_t{0} : m.tile;
            }
            if (m.shotShown && m.shotAt == at) {
                const Fighter* f = fighter(zone().monsters[i].character);
                const Fighter* gun = f != nullptr ? fighter(f->weapon) : nullptr;
                const std::uint16_t shot =
                    gun != nullptr ? gun->character.frame(0, facingOf(m.shotDx, m.shotDy)) : kNone16;
                return shot == kNone16 ? std::uint16_t{0} : shot;
            }
        }
    }
    return tile(at, kObjectLayer);
}

int World::slide(Position from, int dx, int dy) {
    const int nx = from.x + dx;
    const int ny = from.y + dy;
    if (!inside({nx, ny})) {
        return -1;
    }
    if (objectAt({nx, ny}) == kNone16) {
        return 1;
    }
    // Blocked: try one cell aside, starting on a random side.
    const int r = (rng_() & 1) != 0 ? -1 : 1;
    const auto vacant = [&](int x, int y) { return inside({x, y}) && objectAt({x, y}) == kNone16; };
    int tx = nx;
    int ty = ny;
    bool found = false;
    if (dx == 0 && dy != 0) {
        if (nx - r >= 0 && vacant(nx - r, ny)) {
            tx = nx - r;
            found = true;
        } else if (nx + r < zone().width && vacant(nx + r, ny)) {
            tx = nx + r;
            found = true;
        }
    } else if (dy == 0 && dx != 0) {
        if (ny - r >= 0 && vacant(nx, ny - r)) {
            ty = ny - r;
            found = true;
        } else if (ny + r < zone().height && vacant(nx, ny + r)) {
            ty = ny + r;
            found = true;
        }
    } else {
        // Diagonal (or standing): drop one of the two parts.
        const int sx = dx < 0 ? nx + 1 : nx - 1;
        if (vacant(sx, ny)) {
            tx = sx;
            found = true;
        } else {
            const int sy = dy < 0 ? ny + 1 : ny - 1;
            if (vacant(nx, sy)) {
                ty = sy;
                found = true;
            }
        }
    }
    if (!found) {
        return 0;
    }
    if (ty < ny) {
        return 5;
    }
    if (ty > ny) {
        return 4;
    }
    if (tx < nx) {
        return 3;
    }
    return tx > nx ? 2 : 0;
}

void World::meleeHit(const Fighter& f) {
    // Yoda Stories skips a damage of 0xFFFF; a damage of 0 only sounds.
    if (data_.layout() == data::Layout::Yoda && f.harmless) {
        return;
    }
    effects_.push_back({Effect::Kind::Sound, {}, {}, kHurtSound});
    hurtHero(f.damage);
}

void World::faceMonster(MonsterState& m, int character, int dx, int dy) {
    const Fighter* f = fighter(character);
    if (f == nullptr) {
        return;
    }
    const data::Facing facing = facingOf(sign(dx), sign(dy));
    std::uint16_t tile = f->character.frame(0, facing);
    if (tile == kNone16) {
        tile = f->character.frame(0, data::Facing::Down);
    }
    m.facing = facing;
    if (static_cast<std::size_t>(character) < characterTile_.size()) {
        characterTile_[static_cast<std::size_t>(character)] = tile;
    }
}

void World::tickMonsters() {
    ++monsterTicks_;
    auto& s = state();
    const auto& list = zone().monsters;
    // The originals keep the step and one mode between monsters of a tick.
    int dir[2] = {0, 0};
    bool attackMode = false;
    for (std::size_t i = 0; i < s.monsters.size() && i < list.size() && outcome_ == Outcome::Playing; ++i) {
        if (s.monsterDead[i]) {
            continue;
        }
        auto& m = s.monsters[i];
        if (!s.monsterEnabled[i]) {
            m.shotShown = false;
            m.shotStep = 0;
            m.shotAt = m.at;
            m.shotDx = 0;
            m.shotDy = 0;
            continue;
        }
        const Fighter* f = fighter(list[i].character);
        if (f == nullptr || f->character.type != kTypeEnemy) {
            continue;
        }
        tickMonster(i, m, *f, dir, attackMode);
    }
}

void World::tickMonster(std::size_t index, MonsterState& m, const Fighter& f, int (&dir)[2], bool& attackMode) {
    const bool yoda = data_.layout() == data::Layout::Yoda;
    const int character = zone().monsters[index].character;
    const int movement = f.character.movement;
    const int hx = player_.x;
    const int hy = player_.y;
    const auto rnd = [&]() { return static_cast<int>(rng_() & 0x7fff); };
    const auto rand3 = [&]() { return rnd() % 3 - 1; };
    int& dx = dir[0];
    int& dy = dir[1];
    const auto toward = [&] {
        dx = sign(hx - m.at.x);
        dy = sign(hy - m.at.y);
    };
    const auto away = [&] {
        dx = -sign(hx - m.at.x);
        dy = -sign(hy - m.at.y);
    };
    const auto wander = [&] {
        dx = rand3();
        dy = rand3();
    };
    const auto none = [&] {
        dx = 0;
        dy = 0;
    };
    const auto within = [&](int cells) {
        return std::abs(m.at.x - hx) < cells && std::abs(m.at.y - hy) < cells;
    };
    const auto adjust = [&](int r) {
        switch (r) {
        case 1: break;
        case 2: ++dx; break;
        case 3: --dx; break;
        case 4: ++dy; break;
        case 5: --dy; break;
        default: none(); break;
        }
    };
    const auto heading = [&] {
        switch (m.heading) {
        case -1: dx = 0; dy = -1; break;
        case 0: dx = 0; dy = 1; break;
        case 1: dx = 1; dy = 0; break;
        case 2: dx = -1; dy = 0; break;
        default: break;
        }
    };
    const auto atHero = [&] { return m.at.x + dx == hx && m.at.y + dy == hy; };
    const Fighter* gun = f.weapon >= 0 ? fighter(f.weapon) : nullptr;
    bool moved = false;
    // Moves when the target cell is free and has a floor monsters may use.
    // Returns false when the floor keeps it out.
    const auto tryMove = [&]() -> bool {
        const Position to{m.at.x + dx, m.at.y + dy};
        if (objectAt(to) != kNone16) {
            none();
            return true;
        }
        const std::uint16_t floor = tile(to, kFloorLayer);
        if (floor == kNone16) {
            return true;
        }
        if ((flags(floor) & kNoMonsters) != 0) {
            return false;
        }
        m.at = to;
        moved = true;
        return true;
    };

    // Its shot: one cell per tick, three cells at most.
    if (gun != nullptr) {
        bool fired = false;
        bool fly = false;
        if (m.shotStep == 0 && (!yoda || m.wait <= 0)) {
            if (within(4)) {
                m.shotAt = m.at;
                const bool sitting = movement == 4;
                const auto chance = [&] { return (!yoda && sitting) ? (rnd() & 3) == 0 : rnd() % 7 == 3; };
                if (m.at.x == hx) {
                    if (m.at.y != hy) {
                        m.shotDy = sign(hy - m.at.y);
                    }
                    m.shotDx = 0;
                    fly = chance();
                    if (!fly) {
                        m.shotDy = 0;
                    }
                } else if (m.at.y == hy) {
                    m.shotDx = sign(hx - m.at.x);
                    m.shotDy = 0;
                    fly = chance();
                    if (!fly) {
                        m.shotDx = 0;
                    }
                } else {
                    fly = true; // the originals go on with no direction: nothing flies
                }
                fired = fly;
            }
        } else {
            fly = true;
        }
        if (fly) {
            ++m.shotStep;
            bool going = false;
            if (slide(m.shotAt, m.shotDx, m.shotDy) == 1) {
                const Position next{m.shotAt.x + m.shotDx, m.shotAt.y + m.shotDy};
                if (next == player_) {
                    m.shotDx = 0;
                    m.shotDy = 0;
                    if (!yoda && gun->sound >= 0) {
                        effects_.push_back({Effect::Kind::Sound, {}, {}, gun->sound});
                    }
                    // The shot does the monster's own damage.
                    effects_.push_back({Effect::Kind::Sound, {}, {}, kHurtSound});
                    hurtHero(f.harmless ? 0 : f.damage);
                } else {
                    m.shotAt = next;
                    going = true;
                    if (m.shotStep == 1 && fired && gun->sound >= 0) {
                        effects_.push_back({Effect::Kind::Sound, {}, {}, gun->sound});
                    }
                }
            }
            if (going && m.shotStep < 4) {
                m.shotShown = true;
            } else {
                m.shotShown = false;
                m.shotStep = 0;
                m.shotAt = m.at;
                m.shotDx = 0;
                m.shotDy = 0;
            }
        }
        if (outcome_ != Outcome::Playing) {
            return;
        }
    }

    const auto odd = [&] { return (monsterTicks_ & 1) != 0; };
    bool rockReset = false;
    if (yoda) {
        // Most kinds stand still while they wait.
        const auto waiting = [&] {
            if (m.wait != 0) {
                --m.wait;
                none();
                return true;
            }
            return false;
        };
        switch (movement) {
        case 1: // chases the hero; after 15 steps or two hits it backs off for 4
            if (waiting()) {
                break;
            }
            if (rnd() % 2 == 0) {
                none();
                break;
            }
            if (m.retreat) {
                if (rnd() % 2 == 0) {
                    away();
                } else {
                    wander();
                }
                if (++m.count > 3) {
                    m.retreat = false;
                    m.count = 0;
                }
            } else {
                toward();
            }
            adjust(slide(m.at, dx, dy));
            if (atHero()) {
                none();
                meleeHit(f);
                if (++m.count > 1) {
                    m.retreat = true;
                    m.count = 0;
                }
            }
            (void)tryMove();
            if (!m.retreat && ++m.count >= 15) {
                m.retreat = true;
                m.count = 0;
            }
            break;
        case 2: // walks its way; close by, attacks or steps at random
        case 7: // walks its way; close by, attacks
        case 8: // as 7, and now and then a random step
            if (waiting()) {
                break;
            }
            if (movement != 8 && rnd() % 2 == 0) {
                none();
                break;
            }
            if (within(2)) {
                if (movement == 7) {
                    attackMode = true;
                    toward();
                } else if (movement == 8) {
                    attackMode = true;
                    if (rnd() % 2 == 0) {
                        wander();
                    } else {
                        toward();
                    }
                } else if (rnd() % 2 == 0) {
                    wander(); // keeps the mode of the monster before it
                } else {
                    attackMode = true;
                    toward();
                }
            } else if (movement == 8 && rnd() % 3 == 0) {
                attackMode = true;
                wander();
            } else {
                attackMode = false;
                heading();
            }
            {
                const int r = slide(m.at, dx, dy);
                if (attackMode) {
                    adjust(r);
                } else if (r != 1) {
                    none();
                    m.wait = movement == 2 ? rnd() % 3 : (movement == 7 ? rnd() % 8 : rnd() % 10);
                    m.heading = rnd() % 4 - 1;
                }
            }
            if (atHero()) {
                none();
                // Movement 8 hits half the time, and not with a weapon in hand.
                if (movement != 8 || (gun == nullptr && !f.harmless && rnd() % 2 == 0)) {
                    meleeHit(f);
                }
            }
            (void)tryMove();
            break;
        case 3: // wanders; close by, now and then goes for the hero
            if (waiting()) {
                break;
            }
            if (rnd() % 2 == 0) {
                none();
                break;
            }
            if (within(2) && rnd() % 2 != 0) {
                toward();
            } else {
                wander();
            }
            adjust(slide(m.at, dx, dy));
            if (atHero()) {
                none();
                if (gun == nullptr) {
                    meleeHit(f);
                }
            }
            (void)tryMove();
            break;
        case 4: // sits and turns to the hero
            toward();
            break;
        case 6: // keeps away, now and then comes closer; close by, random
            if (waiting()) {
                break;
            }
            if (monsterTicks_ % 3 != 0) {
                none();
                break;
            }
            if (m.approach == 0) {
                m.approach = monsterTicks_ % 10 == 0 ? 1 : 0;
            }
            if (m.approach != 0) {
                toward();
            } else {
                away();
            }
            if (within(6)) {
                wander();
            }
            adjust(slide(m.at, dx, dy));
            if (atHero()) {
                none();
                if (gun == nullptr) {
                    meleeHit(f);
                }
                if (++m.approach > 4) {
                    m.approach = 0;
                }
            }
            (void)tryMove();
            break;
        case 9: // walks its way, every other tick
            if (waiting()) {
                break;
            }
            if (!odd()) {
                none();
                break;
            }
            heading();
            if (slide(m.at, dx, dy) != 1) {
                none();
                m.wait = rnd() % 3;
                m.heading = rnd() % 4 - 1;
            }
            if (atHero()) {
                none();
                if (gun == nullptr) {
                    meleeHit(f);
                }
            }
            (void)tryMove();
            break;
        case 10: { // walks its four waypoints, every other tick
            const auto& wp = zone().monsters[index].waypoints;
            const auto k = static_cast<std::size_t>((m.waypoint + 1) * 4);
            const int wx = k + 1 < wp.size() ? static_cast<std::int32_t>(wp[k] | (wp[k + 1] << 16)) : m.at.x;
            const int wy = k + 3 < wp.size() ? static_cast<std::int32_t>(wp[k + 2] | (wp[k + 3] << 16)) : m.at.y;
            if (waiting()) {
                break;
            }
            if (!odd()) {
                none();
                break;
            }
            dx = sign(wx - m.at.x);
            dy = sign(wy - m.at.y);
            if (dx == 0 && dy == 0 && ++m.waypoint > 2) {
                m.waypoint = -1;
            }
            if (atHero()) {
                none();
                meleeHit(f);
            }
            (void)tryMove();
            break;
        }
        case 11: { // keeps away from the hero when close, else wanders
            bool first = true;
            for (int tries = 0; tries < 1000; ++tries) {
                if (first && within(6)) {
                    away();
                } else {
                    wander();
                }
                first = false;
                adjust(slide(m.at, dx, dy));
                if (atHero()) {
                    none();
                    meleeHit(f);
                }
                if (tryMove() || outcome_ != Outcome::Playing) {
                    break;
                }
            }
            break;
        }
        case 12: // part of the scenery
            none();
            break;
        default: none(); break;
        }
    } else {
        switch (movement) {
        case 1: // chases the hero; after 15 steps or two hits it backs off for 4
            if (!odd()) {
                none();
                break;
            }
            if (m.retreat) {
                if ((rnd() & 1) != 0) {
                    wander();
                } else {
                    away();
                }
                if (++m.count > 3) {
                    m.retreat = false;
                    m.count = 0;
                }
            } else {
                toward();
            }
            adjust(slide(m.at, dx, dy));
            if (atHero()) {
                none();
                if (gun == nullptr) {
                    meleeHit(f);
                }
                if (++m.count > 1) {
                    m.retreat = true;
                    m.count = 0;
                }
            }
            (void)tryMove();
            if (!m.retreat && ++m.count >= 15) {
                m.retreat = true;
                m.count = 0;
            }
            break;
        case 2: // half its steps for the hero, half at random
        case 3: // random; close by, now and then for the hero
            if (!odd()) {
                none();
                break;
            }
            if (movement == 2 ? (rnd() & 1) != 0 : (within(2) && (rnd() & 1) != 0)) {
                toward();
            } else {
                wander();
            }
            adjust(slide(m.at, dx, dy));
            if (atHero()) {
                none();
                if (gun == nullptr) {
                    meleeHit(f);
                }
            }
            (void)tryMove();
            break;
        case 4: // sits and turns to the hero
            toward();
            break;
        case 5: { // a rock that rolls down and starts again at the top
            if (!m.homeSet) {
                m.home = m.at;
                m.homeSet = true;
            }
            if (!m.placed) {
                m.at = m.home;
                m.placed = true;
            }
            if (!odd()) {
                none();
                break;
            }
            dx = 0;
            dy = 1;
            if (gun == nullptr && m.at.x == hx && (m.at.y + 1 == hy || (m.at.y == hy && hy == 0))) {
                meleeHit(f);
            }
            if (slide(m.at, 0, 1) == 1) {
                ++m.at.y;
                moved = true;
                break;
            }
            const std::uint16_t below = objectAt({m.at.x, m.at.y + 1});
            if (below == 0) {
                break;
            }
            if (below != kNone16 && (flags(below) & kEnemyTile) != 0) {
                ++m.at.y; // rolls over an enemy
                moved = true;
                break;
            }
            m.placed = false;
            none();
            rockReset = true;
            break;
        }
        case 6: // keeps away, now and then comes closer; close by, random
            if (monsterTicks_ % 3 != 0) {
                none();
                break;
            }
            if (m.approach == 0) {
                m.approach = monsterTicks_ % 10 == 0 ? 1 : 0;
            }
            if (m.approach != 0) {
                toward();
            } else {
                away();
            }
            if (within(6)) {
                wander();
            }
            adjust(slide(m.at, dx, dy));
            if (atHero()) {
                none();
                if (gun == nullptr) {
                    meleeHit(f);
                }
                if (++m.approach > 4) {
                    m.approach = 0;
                }
            }
            (void)tryMove();
            break;
        default: none(); break;
        }
    }

    // Which way it looks, and the tile it shows.
    m.hidden = false;
    const auto ch = static_cast<std::size_t>(character);
    if (yoda) {
        if (movement == 3) {
            if (dx == 0 && dy == 0) {
                toward();
            }
            faceMonster(m, character, dx, dy);
        } else {
            if (dx == 0 && dy == 0) {
                if (m.at.x == hx || m.at.y == hy) {
                    toward();
                    faceMonster(m, character, dx, dy);
                }
            } else if (moved) {
                faceMonster(m, character, dx, dy);
            } else if (m.faceNext || movement == 4) {
                faceMonster(m, character, dx, dy);
                m.faceNext = false;
            }
            if (m.faceNext) {
                faceMonster(m, character, dx, dy);
                m.faceNext = false;
            }
        }
        if (ch < characterTile_.size() && characterTile_[ch] != kNone16) {
            m.tile = characterTile_[ch];
        }
        if (movement == 12) {
            // Scenery shows its first six frames in turn.
            const int frame = std::clamp(m.waypoint, 0, data::kFacingCount - 1);
            m.tile = f.character.frame(0, static_cast<data::Facing>(frame));
            if (ch < characterTile_.size()) {
                characterTile_[ch] = m.tile;
            }
            if (++m.waypoint > 5) {
                m.waypoint = 0;
            }
        }
    } else {
        if (dx == 0 && dy == 0) {
            toward();
        }
        if (rockReset) {
            // Back to the top; it shows again on the next tick.
            m.at = m.home;
            m.roll = 0;
            m.hidden = true;
        } else {
            if (movement == 5) {
                static constexpr int kRoll[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {-1, 1}};
                faceMonster(m, character, kRoll[m.roll][0], kRoll[m.roll][1]);
                if (++m.roll > 3) {
                    m.roll = 0;
                }
            } else {
                faceMonster(m, character, dx, dy);
            }
            if (ch < characterTile_.size() && characterTile_[ch] != kNone16) {
                m.tile = characterTile_[ch];
            }
        }
    }
}

void World::update(int ms) {
    if (ms <= 0 || outcome_ != Outcome::Playing) {
        return;
    }
    attackCooldownMs_ = std::max(0, attackCooldownMs_ - ms);
    attackShowMs_ = std::max(0, attackShowMs_ - ms);
    if (paused_ || drop_) {
        return;
    }
    {
        // The originals check the cell the hero stands on on every game
        // tick, so StandingOn and HeroIsAt scripts also notice a hero that
        // stands still or that a script put somewhere.
        scriptTickMs_ += ms;
        if (scriptTickMs_ >= kScriptTickMs) {
            scriptTickMs_ = 0;
            if (passes_.empty()) {
                trigger(script::Event::Walk, {player_.x, player_.y, 0, 0, -1});
                runScripts();
                if (paused_ || outcome_ != Outcome::Playing) {
                    return;
                }
                if (passes_.empty() && travel()) {
                    return;
                }
            }
        }
    }
    shotMs_ += ms;
    while (shotMs_ >= kShotStepMs && !shots_.empty()) {
        shotMs_ -= kShotStepMs;
        moveShots();
    }
    if (shots_.empty()) {
        shotMs_ = 0;
    }
    monsterMs_ += ms;
    while (monsterMs_ >= kTickMs && outcome_ == Outcome::Playing && !paused_) {
        monsterMs_ -= kTickMs;
        tickMonsters();
    }
}

} // namespace das::world
