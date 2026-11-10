// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/script/opcodes.hpp"

#include <array>

namespace das::script {

namespace {

using C = Condition;
using I = Instruction;

// Indiana Jones numbers its opcodes differently.
constexpr std::array<C, 23> kIndyConditions{
    C::Unknown,          C::StandingOn,       C::Bump,
    C::PlaceItem,        C::FirstEnter,       C::Enter,
    C::CounterIs,        C::Always,           C::MissionNotDone,
    C::MissionDone,      C::HasItem,          C::RandomIsSet,
    C::RandomIsGreaterThan, C::RandomIs,      C::RandomIsLessThan,
    C::ZoneIsSolved,     C::Update,           C::MonsterIsDead,
    C::NoMonstersActive, C::TileIs,           C::RequiredItemIs,
    C::EndingIs,         C::HealthIsLessThan,
};

constexpr std::array<I, 36> kIndyInstructions{
    I::PlaceTile,       I::MoveTile,           I::RemoveTile,         I::EnableHotspot,
    I::DisableHotspot,  I::SpeakHero,          I::Wait,               I::Nothing,
    I::Nothing,         I::SetCounter,         I::AddToCounter,       I::PlaySound,
    I::Render,          I::RollDice,           I::HideHero,           I::ShowHero,
    I::DrawTile,        I::RedrawTile,         I::SetHeroPosition,    I::RedrawTiles,
    I::RedrawZone,      I::DisableAction,      I::EnableMonster,      I::DisableMonster,
    I::EnableAllMonsters, I::DisableAllMonsters, I::MoveHeroTo,       I::WinGame,
    I::SpeakNpc,        I::AddItem,            I::DropItem,           I::LoseGame,
    I::RemoveItem,      I::MarkAsSolved,       I::ChangeZone,         I::StopSound,
};

constexpr std::uint16_t kYodaConditionCount = 36;
constexpr std::uint16_t kYodaInstructionCount = 38;

struct Named {
    std::uint16_t value;
    std::string_view name;
};

constexpr Named kConditionNames[] = {
    {0, "FirstEnter"},
    {1, "Enter"},
    {2, "Bump"},
    {3, "PlaceItem"},
    {4, "StandingOn"},
    {5, "CounterIs"},
    {6, "RandomIs"},
    {7, "RandomIsGreaterThan"},
    {8, "RandomIsLessThan"},
    {9, "Update"},
    {10, "TileIs"},
    {11, "MonsterIsDead"},
    {12, "NoMonstersActive"},
    {13, "HasItem"},
    {14, "RequiredItemIs"},
    {15, "EndingIs"},
    {16, "ZoneIsSolved"},
    {17, "MissionNotDone"},
    {18, "MissionDone"},
    {19, "HealthIsLessThan"},
    {20, "HealthIsGreaterThan"},
    {21, "Unused21"},
    {22, "FindItemIs"},
    {23, "PlaceItemIsNot"},
    {24, "HeroIsAt"},
    {25, "SharedCounterIs"},
    {26, "SharedCounterIsLessThan"},
    {27, "SharedCounterIsGreaterThan"},
    {28, "GamesWonIs"},
    {29, "HasQuestHotspotAt"},
    {30, "HasRequiredItem"},
    {31, "CounterIsNot"},
    {32, "RandomIsNot"},
    {33, "SharedCounterIsNot"},
    {34, "VariableIs"},
    {35, "GamesWonIsGreaterThan"},
    {100, "RandomIsSet"},
    {101, "Always"},
};

constexpr Named kInstructionNames[] = {
    {0, "PlaceTile"},
    {1, "RemoveTile"},
    {2, "MoveTile"},
    {3, "DrawTile"},
    {4, "SpeakHero"},
    {5, "SpeakNpc"},
    {6, "RedrawTile"},
    {7, "RedrawTiles"},
    {8, "Render"},
    {9, "Wait"},
    {10, "PlaySound"},
    {11, "StopSound"},
    {12, "RollDice"},
    {13, "SetCounter"},
    {14, "AddToCounter"},
    {15, "SetVariable"},
    {16, "HideHero"},
    {17, "ShowHero"},
    {18, "SetHeroPosition"},
    {19, "MoveHeroTo"},
    {20, "DisableAction"},
    {21, "EnableHotspot"},
    {22, "DisableHotspot"},
    {23, "EnableMonster"},
    {24, "DisableMonster"},
    {25, "EnableAllMonsters"},
    {26, "DisableAllMonsters"},
    {27, "DropItem"},
    {28, "AddItem"},
    {29, "RemoveItem"},
    {30, "MarkAsSolved"},
    {31, "WinGame"},
    {32, "LoseGame"},
    {33, "ChangeZone"},
    {34, "SetSharedCounter"},
    {35, "AddToSharedCounter"},
    {36, "SetRandom"},
    {37, "AddHealth"},
    {100, "RedrawZone"},
    {101, "Nothing"},
};

template <typename T, std::size_t N>
std::string_view lookup(const Named (&names)[N], T value) {
    for (const auto& n : names) {
        if (n.value == static_cast<std::uint16_t>(value)) {
            return n.name;
        }
    }
    return "Unknown";
}

template <typename T, std::size_t N>
int indexOf(const std::array<T, N>& table, T value) {
    for (std::size_t i = 0; i < N; ++i) {
        if (table[i] == value) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

Condition condition(data::Layout game, std::uint16_t op) {
    if (game == data::Layout::Indy) {
        return op < kIndyConditions.size() ? kIndyConditions[op] : C::Unknown;
    }
    return op < kYodaConditionCount ? static_cast<C>(op) : C::Unknown;
}

Instruction instruction(data::Layout game, std::uint16_t op) {
    if (game == data::Layout::Indy) {
        return op < kIndyInstructions.size() ? kIndyInstructions[op] : I::Unknown;
    }
    return op < kYodaInstructionCount ? static_cast<I>(op) : I::Unknown;
}

int opcode(data::Layout game, Condition c) {
    if (c == C::Unknown) {
        return -1;
    }
    if (game == data::Layout::Indy) {
        return indexOf(kIndyConditions, c);
    }
    return static_cast<std::uint16_t>(c) < kYodaConditionCount ? static_cast<int>(c) : -1;
}

int opcode(data::Layout game, Instruction i) {
    if (i == I::Unknown) {
        return -1;
    }
    if (game == data::Layout::Indy) {
        return indexOf(kIndyInstructions, i);
    }
    return static_cast<std::uint16_t>(i) < kYodaInstructionCount ? static_cast<int>(i) : -1;
}

std::string_view name(Condition c) { return lookup(kConditionNames, c); }
std::string_view name(Instruction i) { return lookup(kInstructionNames, i); }

} // namespace das::script
