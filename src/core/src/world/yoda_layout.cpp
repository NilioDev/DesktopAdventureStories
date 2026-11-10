// SPDX-License-Identifier: GPL-3.0-or-later
//
// The world map layout of Yoda Stories, as specified in
// docs/world-generator.md: which of the 10 x 10 cells hold a zone, where the
// blockades, islands and puzzles go. It draws from the game's random number
// sequence in the same order as the original, so a seed gives the same map.
#include "yoda_layout.hpp"

#include <algorithm>

namespace das::world::yoda {

namespace {

constexpr int kRing[kCells] = {
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, //
    5, 4, 4, 4, 4, 4, 4, 4, 4, 5, //
    5, 4, 3, 3, 3, 3, 3, 3, 4, 5, //
    5, 4, 3, 2, 2, 2, 2, 3, 4, 5, //
    5, 4, 3, 2, 1, 1, 2, 3, 4, 5, //
    5, 4, 3, 2, 1, 1, 2, 3, 4, 5, //
    5, 4, 3, 2, 2, 2, 2, 3, 4, 5, //
    5, 4, 3, 3, 3, 3, 3, 3, 4, 5, //
    5, 4, 4, 4, 4, 4, 4, 4, 4, 5, //
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, //
};

bool open(Cell c) { return c == Cell::None || c == Cell::KeptClear; }
bool reachable(Cell c) {
    return c == Cell::Plain || c == Cell::TravelStart || c == Cell::Town || c == Cell::PuzzleSlot;
}
bool atLeastBehind(Cell c) { return static_cast<int>(c) >= static_cast<int>(Cell::Behind); }

} // namespace

int ring(int x, int y) { return kRing[y * kSide + x]; }

int Rng::next() {
    state_ = state_ * 214013u + 2531011u;
    return static_cast<int>((state_ >> 16) & 0x7fff);
}

// Grows the map by one ring of cells; see "Placing a ring" in the spec.
void Layout::placeRing(int ringType, int count, int maxTravel, int maxBlockades) {
    int lo = 0;
    int span = 0;
    int blockadeOdds = 0;
    int blockadeBelow = 0;
    int travelBelow = 0;
    switch (ringType) {
    case 2: lo = 3, span = 4, blockadeOdds = 9, blockadeBelow = 2, travelBelow = 1; break;
    case 3: lo = 2, span = 6, blockadeOdds = 4, blockadeBelow = 2, travelBelow = 3; break;
    default: lo = 1, span = 8, blockadeOdds = 5, blockadeBelow = 1, travelBelow = 6; break;
    }
    const int hi = lo + span - 1;

    int remaining = count;
    int attempts = 0;
    bool done = false;
    while (!done) {
        int x = 0;
        int y = 0;
        if (rng_.next() % 2 != 0) {
            x = rng_.next() % 2 != 0 ? lo : hi;
            y = lo + rng_.next() % span;
        } else {
            y = rng_.next() % 2 != 0 ? lo : hi;
            x = lo + rng_.next() % span;
        }
        ++attempts;
        Cell& c = at(x, y);
        if (c == Cell::None) {
            Cell& left = at(x - 1, y);
            Cell& right = at(x + 1, y);
            Cell& up = at(x, y - 1);
            Cell& down = at(x, y + 1);
            // Whether none of the four neighbours is a blockade or behind one.
            const auto clearAround = [&] {
                return !atLeastBehind(left) && !atLeastBehind(right) && !atLeastBehind(up) && !atLeastBehind(down);
            };
            const auto blockadeHere = [&](int nx, int ny) {
                if (maxBlockades <= blockades_ || rng_.next() % blockadeOdds >= blockadeBelow) {
                    return 0; // no: a plain cell, if it fits
                }
                return ring(nx, ny) >= ringType ? -1 : 1; // -1: skip this attempt
            };
            bool placed = false;
            const auto plain = [&] {
                if (clearAround()) {
                    c = Cell::Plain;
                    placed = true;
                }
            };
            if (!open(left)) {
                neighbour_ = left;
                if (reachable(left)) {
                    const int b = blockadeHere(x - 1, y);
                    if (b > 0 && open(up) && open(down)) {
                        c = Cell::BlockadeToEast;
                        up = down = Cell::KeptClear;
                        right = Cell::Behind;
                        at(x + 1, y - 1) = at(x + 1, y + 1) = Cell::KeptClear;
                        cells_ += 2;
                        --remaining;
                        ++blockades_;
                    } else if (b >= 0) {
                        plain();
                    }
                } else if (left == Cell::Behind || left == Cell::BlockadeToEast) {
                    if (open(up) && open(down)) {
                        c = Cell::Behind;
                        up = down = Cell::KeptClear;
                        placed = true;
                    }
                }
            } else if (!open(right)) {
                neighbour_ = right;
                if (reachable(right)) {
                    const int b = blockadeHere(x + 1, y);
                    if (b > 0 && open(up) && open(down)) {
                        c = Cell::BlockadeToWest;
                        up = down = Cell::KeptClear;
                        left = Cell::Behind;
                        at(x - 1, y - 1) = at(x - 1, y + 1) = Cell::KeptClear;
                        cells_ += 2;
                        --remaining;
                        ++blockades_;
                    } else if (b >= 0) {
                        plain();
                    }
                } else if (right == Cell::Behind || right == Cell::BlockadeToWest) {
                    if (open(up) && open(down)) {
                        c = Cell::Behind;
                        up = down = Cell::KeptClear;
                        placed = true;
                    }
                }
            } else if (!open(up)) {
                neighbour_ = up;
                if (reachable(up)) {
                    const int b = blockadeHere(x, y - 1);
                    if (b > 0 && open(left) && open(right)) {
                        c = Cell::BlockadeToSouth;
                        left = right = Cell::KeptClear;
                        down = Cell::Behind;
                        at(x - 1, y + 1) = at(x + 1, y + 1) = Cell::KeptClear;
                        cells_ += 2;
                        --remaining;
                        ++blockades_;
                    } else if (b >= 0) {
                        plain();
                    }
                } else if (up == Cell::Behind || up == Cell::BlockadeToSouth) {
                    // Left and right are open here, or we would not have got here.
                    c = Cell::Behind;
                    left = right = Cell::KeptClear;
                    placed = true;
                }
            } else if (!open(down)) {
                neighbour_ = down;
                if (reachable(down)) {
                    const int b = blockadeHere(x, y + 1);
                    if (b > 0 && open(left) && open(right)) {
                        c = Cell::BlockadeToNorth;
                        left = right = Cell::KeptClear;
                        up = Cell::Behind;
                        at(x - 1, y - 1) = at(x + 1, y - 1) = Cell::KeptClear;
                        cells_ += 2;
                        --remaining;
                        ++blockades_;
                    } else if (b >= 0) {
                        plain();
                    }
                } else if (down == Cell::Behind || down == Cell::BlockadeToNorth) {
                    c = Cell::Behind;
                    left = right = Cell::KeptClear;
                    placed = true;
                }
            }
            if (placed) {
                ++cells_;
                --remaining;
            }
            if (c == Cell::Plain && maxTravel > travels_ && rng_.next() % 8 < travelBelow &&
                neighbour_ != Cell::TravelStart && ringType > 2) {
                c = Cell::TravelStart;
                ++travels_;
            }
        }
        if (remaining <= 0 || attempts > 144) {
            done = true;
        }
    }
}

// Adds cells on the outer edge of the map; see "The edge" in the spec.
void Layout::placeEdge(int count) {
    int remaining = count;
    int attempts = 0;
    bool done = false;
    while (!done) {
        int dx = 0;
        int dy = 0;
        int x = 0;
        int y = 0;
        bool leftOrRight = false;
        if (rng_.next() % 2 != 0) {
            leftOrRight = true;
            x = rng_.next() % 2 == 0 ? 9 : 0;
            y = rng_.next() % 10;
        } else {
            y = rng_.next() % 2 == 0 ? 9 : 0;
            x = rng_.next() % 10;
        }
        ++attempts;
        Cell& c = at(x, y);
        if (c == Cell::None) {
            const auto taken = [](Cell n) { return n != Cell::None && n != Cell::KeptClear; };
            if (leftOrRight) {
                if (x == 0 && taken(at(1, y))) {
                    dx = -1;
                } else if (x == 9 && taken(at(8, y))) {
                    dx = 1;
                }
            } else {
                if (y == 0 && taken(at(x, 1))) {
                    dy = -1;
                } else if (y == 9 && taken(at(x, 8))) {
                    dy = 1;
                }
            }
            if (dx != 0 || dy != 0) {
                const Cell n = at(x - dx, y - dy);
                // A neighbour across the edge of the map: free, or not a
                // blockade or what lies behind one.
                const auto freeOrLater = [&](int nx, int ny) {
                    const Cell v = at(nx, ny);
                    return v == Cell::None || static_cast<int>(v) > static_cast<int>(Cell::BlockadeToNorth);
                };
                bool counted = false;
                switch (n) {
                case Cell::Plain:
                case Cell::TravelStart:
                case Cell::Town:
                case Cell::PuzzleSlot:
                    c = Cell::Plain;
                    counted = true;
                    break;
                case Cell::Behind:
                    c = Cell::Behind;
                    if (dx == 0) {
                        if (x > 0) {
                            at(x - 1, y) = Cell::KeptClear;
                        }
                        if (x < 9) {
                            at(x + 1, y) = Cell::KeptClear;
                        }
                    } else if (dy == 0) {
                        if (y > 0) {
                            at(x, y - 1) = Cell::KeptClear;
                        }
                        if (y < 9) {
                            at(x, y + 1) = Cell::KeptClear;
                        }
                    }
                    break;
                case Cell::BlockadeToEast:
                case Cell::BlockadeToWest:
                    if ((n == Cell::BlockadeToWest && dx == -1) || (n == Cell::BlockadeToEast && dx == 1)) {
                        if ((y == 0 || freeOrLater(x, y - 1)) && (y == 9 || freeOrLater(x, y + 1))) {
                            c = Cell::Behind;
                            if (y > 0) {
                                at(x, y - 1) = Cell::KeptClear;
                            }
                            if (y < 9) {
                                at(x, y + 1) = Cell::KeptClear;
                            }
                            counted = true;
                        }
                    }
                    break;
                case Cell::BlockadeToNorth:
                    if (dy == -1 && (x == 0 || freeOrLater(x - 1, y)) && (x == 9 || freeOrLater(x + 1, y))) {
                        c = Cell::Behind;
                        if (x > 0) {
                            at(x - 1, y) = Cell::KeptClear;
                        }
                        if (x < 9) {
                            at(x + 1, y) = Cell::KeptClear;
                        }
                        counted = true;
                    }
                    break;
                case Cell::BlockadeToSouth:
                    // The original looks at the row here where it means the
                    // column; we keep that, it decides which cells get used.
                    if (dy == 1 && (y == 0 || freeOrLater(x - 1, y)) && (y == 9 || freeOrLater(x + 1, y))) {
                        c = Cell::Behind;
                        if (x > 0) {
                            at(x - 1, y) = Cell::KeptClear;
                        }
                        if (x < 9) {
                            at(x + 1, y) = Cell::KeptClear;
                        }
                        counted = true;
                    }
                    break;
                default: break;
                }
                if (counted) {
                    ++cells_;
                    --remaining;
                }
            }
        }
        if (remaining <= 0 || attempts > 400) {
            done = true;
        }
    }
}

// Lays out islands along the edges; see "Islands" in the spec.
void Layout::placeIslands(int count) {
    for (int island = 0; island < count; ++island) {
        int tries = 0;
        bool done = false;
        while (!done) {
            const int side = rng_.next() % 4;
            bool placed = false;
            // Longest run of free edge cells whose inner neighbour is free too.
            const auto longestRun = [&](auto edge, auto inner, int& start) {
                int best = 0;
                int run = 0;
                for (int i = 0; i < kSide; ++i) {
                    if (edge(i) == Cell::None && open(inner(i))) {
                        ++run;
                    } else {
                        if (best < run) {
                            best = run;
                            start = i - run;
                        }
                        run = 0;
                    }
                }
                if (best < run) {
                    best = run;
                    start = kSide - run;
                }
                return best;
            };
            int start = 0;
            int best = 0;
            const bool vertical = side == 0 || side == 3;
            const int line = side == 0 ? 0 : side == 1 ? 0 : 9; // the edge column or row
            if (vertical) {
                const int innerX = line == 0 ? 1 : 8;
                best = longestRun([&](int i) { return at(line, i); }, [&](int i) { return at(innerX, i); }, start);
            } else {
                const int innerY = line == 0 ? 1 : 8;
                best = longestRun([&](int i) { return at(i, line); }, [&](int i) { return at(i, innerY); }, start);
            }
            int& length = islandLength_[side];
            const bool shortOk = side < 2; // left and top accept a run of three
            if (best >= (shortOk ? 3 : 4) && !(best == 3 && start > 0 && start < 7)) {
                if (best == 3) {
                    length = 2;
                }
                if (best >= 4) {
                    length = best - 2;
                }
                length = std::min(length, 4);
                const bool dockFirst = rng_.next() % 2 != 0;
                if (start > 0 && length + start <= 9) {
                    ++start;
                }
                const auto cell = [&](int i) -> Cell& { return vertical ? at(line, i) : at(i, line); };
                if (dockFirst) {
                    cell(start) = Cell::Dock;
                    for (int i = start + 1; i < start + length; ++i) {
                        cell(i) = Cell::Island;
                    }
                } else {
                    cell(start + length - 1) = Cell::Dock;
                    for (int i = start; i < start + length - 1; ++i) {
                        cell(i) = Cell::Island;
                    }
                }
                placed = true;
            }
            if (placed) {
                done = true;
            } else if (++tries > 200) {
                done = true;
            }
        }
    }
}

// Picks the cells that get a puzzle; see "Puzzle slots" in the spec.
int Layout::assignPuzzles() {
    int plain = 0;
    int travel = 0;
    int blockadeCount = 0;
    for (const Cell c : cells) {
        if (c == Cell::Plain || c == Cell::Island || c == Cell::Behind) {
            ++plain;
        } else if (c == Cell::TravelStart) {
            ++travel;
        } else if (c >= Cell::BlockadeToWest && c <= Cell::BlockadeToSouth) {
            ++blockadeCount;
        }
    }
    const int roll = rng_.next();
    int wanted = roll % (plain / 5 + 1) + plain / 4 - blockadeCount - travel + 1 - 3;
    wanted = std::max(wanted, 4);

    int count = 0;
    const auto slot = [&](int x, int y) {
        at(x, y) = Cell::PuzzleSlot;
        puzzle[y * kSide + x] = static_cast<std::int16_t>(count);
        ++count;
    };
    // The far end of what lies behind each blockade.
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            switch (at(x, y)) {
            case Cell::BlockadeToEast:
                if (at(x + 1, y) == Cell::Behind) {
                    slot(x < 8 && at(x + 2, y) == Cell::Behind ? x + 2 : x + 1, y);
                }
                break;
            case Cell::BlockadeToWest:
                if (at(x - 1, y) == Cell::Behind) {
                    slot(x > 1 && at(x - 2, y) == Cell::Behind ? x - 2 : x - 1, y);
                }
                break;
            case Cell::BlockadeToNorth:
                if (at(x, y - 1) == Cell::Behind) {
                    slot(x, y > 1 && at(x, y - 2) == Cell::Behind ? y - 2 : y - 1);
                }
                break;
            case Cell::BlockadeToSouth:
                if (at(x, y + 1) == Cell::Behind) {
                    slot(x, y < 8 && at(x, y + 2) == Cell::Behind ? y + 2 : y + 1);
                }
                break;
            default: break;
            }
        }
    }
    // The far end of each island.
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            if (at(x, y) != Cell::Dock) {
                continue;
            }
            const auto end = [&](int from, int step, auto get) {
                int i = from;
                for (;;) {
                    if (i < 0) {
                        return 0;
                    }
                    if (i > 9) {
                        return 9;
                    }
                    if (get(i) != Cell::Island) {
                        return i - step;
                    }
                    i += step;
                }
            };
            switch (islandDirection(x, y)) {
            case 1: slot(end(x - 1, -1, [&](int i) { return at(i, y); }), y); break;
            case 2: slot(x, end(y - 1, -1, [&](int i) { return at(x, i); })); break;
            case 3: slot(end(x + 1, 1, [&](int i) { return at(i, y); }), y); break;
            case 4: slot(x, end(y + 1, 1, [&](int i) { return at(x, i); })); break;
            default: break;
            }
        }
    }
    // More slots, preferably on the edge and away from the middle.
    int tries = 0;
    bool done = false;
    while (!done) {
        if (count >= wanted || tries > 200) {
            done = true;
        }
        int x = 0;
        int y = 0;
        if (tries < 50) {
            x = rng_.next() % 10;
            if (x > 0 && x < 9) {
                y = rng_.next() % 2 == 0 ? 9 : 0;
            } else {
                y = rng_.next() % 10;
            }
        } else {
            x = rng_.next() % 10;
            y = rng_.next() % 10;
        }
        if (count >= wanted) {
            break;
        }
        if (ring(x, y) < 3 && tries < 150) {
            continue;
        }
        const Cell c = at(x, y);
        if (c == Cell::Plain || c == Cell::Behind) {
            const bool alone = (x == 0 || at(x - 1, y) != Cell::PuzzleSlot) &&
                               (x == 9 || at(x + 1, y) != Cell::PuzzleSlot) &&
                               (y == 0 || at(x, y - 1) != Cell::PuzzleSlot) &&
                               (y == 9 || at(x, y + 1) != Cell::PuzzleSlot);
            if (alone) {
                slot(x, y);
            }
        }
        if (count >= wanted) {
            break;
        }
        ++tries;
    }
    // The last puzzle (the goal) must not be near the middle: swap it with
    // the first puzzle that is far enough out.
    const int last = count - 1;
    int lastX = -1;
    int lastY = -1;
    for (int y = 0; y < kSide && lastX < 0; ++y) {
        for (int x = 0; x < kSide; ++x) {
            if (puzzle[y * kSide + x] == last && ring(x, y) < 3) {
                lastX = x;
                lastY = y;
                break;
            }
        }
    }
    if (lastX >= 0) {
        bool swapped = false;
        for (int y = 0; y < kSide && !swapped; ++y) {
            for (int x = 0; x < kSide; ++x) {
                const int p = puzzle[y * kSide + x];
                if (p >= 0 && ring(x, y) >= 3 && p != last) {
                    puzzle[y * kSide + x] = static_cast<std::int16_t>(last);
                    puzzle[lastY * kSide + lastX] = static_cast<std::int16_t>(p);
                    swapped = true;
                    break;
                }
            }
        }
    }
    return count;
}

int Layout::islandDirection(int x, int y) const {
    if (x > 0 && at(x - 1, y) == Cell::Island) {
        return 1;
    }
    if (x < 9 && at(x + 1, y) == Cell::Island) {
        return 3;
    }
    if (y > 0 && at(x, y - 1) == Cell::Island) {
        return 2;
    }
    if (y < 9 && at(x, y + 1) == Cell::Island) {
        return 4;
    }
    return 0;
}

} // namespace das::world::yoda
