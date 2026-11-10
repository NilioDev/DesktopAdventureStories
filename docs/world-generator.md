# The world generator

A new game of Yoda Stories gets a world that is made on the spot: a planet map
of 10 x 10 cells, a goal (the mission), two chains of puzzles that lead to the
goal, and a zone for each cell. This document says how the original game does
that, in our own words, worked out by observing the original program. It is
the specification for `src/core/src/world/yoda_layout.cpp` and
`yoda_generator.cpp`.

Our version follows the original step by step and draws from the same random
number sequence in the same order. A seed, a planet and a world size therefore
give the same world as in the original game. Where the original has a quirk
that changes the outcome (reading a row where a column is meant, an item that
is unmarked twice), we keep it and say so.

Indiana Jones and His Desktop Adventures has its own generator. Until that one
is specified, it gets a world from our own algorithm (`generator.cpp`).

## Terms

- **Zone type**: the role a zone was made for (`Zone::type`). The ones that
  matter here: 1 empty, 2 to 5 blockade (north, south, east, west), 6 travel
  start, 7 travel end, 10 goal, 11 town, 15 use, 16 trade, 17 find,
  18 find a weapon.
- **Item lists of a zone**: A, the items it can ask for (IZAX, first list);
  B, the items a goal zone can also ask for (IZAX, second list); C, the items
  it can hand out (IZX2); and its characters (IZX3).
- **Rooms**: a zone together with the zones behind its doors (hotspot type 9,
  the argument is the room). Most checks below look at the zone and then at
  each room in hotspot order, recursively, and stop at the first success.
- **Puzzle** (IPUZ): types 0 trade, 1 use, 2 for the goal zone, 3 goal. A
  puzzle's `item1` is the item it is about; a goal also has `item2`.
- **Ring** of a cell: 1 for the four middle cells, growing by one per square
  ring outwards, 5 on the edge.

## Random numbers

The original uses the random function of its C library:

    state = state * 214013 + 2531011   (32 bit, wrapping)
    value = (state >> 16) & 0x7fff

The generator seeds it with the absolute value of the seed (read as a signed
32 bit number). "Draw n" below means `value % n`.

## Inputs

- the seed;
- the planet (1 desert, 2 snow, 3 forest): the original picks it from the
  previous planet and the number of games won, not from the seed, so it is an
  input here; when the caller leaves it open we take `1 + seed % 3`;
- the size: small, medium or large;
- optionally a fixed goal;
- the number of games won and the goals of the last games on this planet
  (the original keeps these in a file; a new installation has none).

## 1. Opening draws

1. `maxTravel` = draw 3, `maxBlockades` = draw 4, then one draw that is not
   used.
2. The town: x = 4 + draw 2, y = 4 + draw 2.
3. All cells empty; the four middle cells (4,4), (5,4), (4,5), (5,5) plain;
   then the town cell is set to town.

## 2. The map layout

Cells hold one of: empty, plain, travel start, landing (of an island),
island, town, behind a blockade, a blockade facing one of four ways, kept
clear (must stay empty), puzzle. A blockade "to the east" guards what lies
east of it.

Cells may be addressed one step past the end of a row; like the original,
that reaches the first cell of the next row (or the last of the previous).

The number of cells per ring depends on the size (low..high, a draw picks a
number in between):

| size   | inner ring | middle ring | outer ring | edge |
|--------|-----------|-------------|------------|------|
| small  | 5..8      | 4..6        | 1..1       | 1..1 |
| medium | 5..9      | 5..9        | 4..8       | 3..8 |
| large  | 6..12     | 6..12       | 6..11      | 4..11 |

The inner count gets `maxBlockades + maxTravel` added and is capped at 12.

### Placing a ring

Ring 2 uses the square from 3 to 6, ring 3 from 2 to 7, ring 4 from 1 to 8.
Until the wanted number of cells is placed or more than 144 attempts were
made:

1. Pick a cell on the ring's square: draw 2 chooses a vertical or horizontal
   side, draw 2 which of the two, and a draw over the side's length the
   position.
2. Only an empty cell can be used. Look at the neighbours left, right, up,
   down, in that order, and act on the first that is not empty or kept clear
   (remember its value as "the last neighbour"):
   - If it is reachable (plain, travel start, town or puzzle): maybe place a
     blockade. A blockade is possible when fewer than `maxBlockades` were
     placed and a draw (9 for ring 2, 4 for ring 3, 5 for ring 4) comes out
     below 2 (2, 1). If the neighbour lies in a ring at least as far out as
     the one being placed, the attempt is dropped. Otherwise, if the two cells
     beside the new one (across the direction of the neighbour) are open, the
     cell becomes a blockade facing away from the neighbour, the cell behind
     it becomes "behind", the cells beside both are kept clear, and the count
     goes up by two. If no blockade is drawn, the cell becomes plain when none
     of its four neighbours is a blockade or behind one.
   - If it is behind a blockade, or the blockade facing this way: the cell
     becomes "behind" too when the two cells beside it are open; they are then
     kept clear.
3. A placed plain cell in ring 3 or 4 may become a travel start: when fewer
   than `maxTravel` exist, draw 8 is below 1 (ring 3) or 6 (ring 4), and the
   last neighbour was not a travel start.

### The edge

Until the count is reached or more than 400 attempts: draw 2 picks the left
and right columns or the top and bottom rows, draw 2 which one, draw 10 the
position. An empty edge cell whose inner neighbour is used extends that
neighbour: a plain neighbour gives a plain cell; one behind a blockade gives
another "behind" cell, with the cells beside it kept clear; a blockade that
faces out over the edge gives a "behind" cell when the cells beside it are
free or past a blockade. For a blockade facing south the original checks the
row number where it means the column; we do the same.

### Islands

For each travel start, up to 200 tries: draw 4 picks a side (left, top,
bottom, right). Find the longest run of empty edge cells whose inner
neighbours are open. Left and top accept a run of three when it starts at
the very beginning or at 7 (the island is then 2 long); every side accepts a
run of four or more (the island is the run minus two, at most 4). Draw 2
decides whether the landing comes first or last; the island moves one cell
in when it does not start at 0 and still fits. The landing cell is the
landing, the others are island.

### Puzzle slots

Count plain, island and behind cells (P), travel starts (T) and blockades
(B). The wanted number of puzzles is `draw (P/5 + 1) + P/4 - B - T - 2`, at
least 4. Slots are given out in this order, each numbered from 0 up:

1. Behind each blockade, the far cell of the area it guards (two cells away
   when that is still behind it, else one).
2. On each island, the far end from its landing.
3. Random cells, until the wanted number is reached or more than 200 tries:
   during the first 50 tries the cell is biased to the edge (draw 10 for x; for
   x from 1 to 8, draw 2 picks row 9 or 0, else draw 10 for y), later both
   draws are 10. Until try 150, cells in rings below 3 are skipped without
   counting a try. The cell must be plain or behind, and have no puzzle next
   to it.

The highest number is the goal. If it lies in a ring below 3, it swaps
numbers with the first puzzle (row by row) that lies in ring 3 or further out.

## 3. The goal and the chains

With N puzzle slots, chain B gets `N/2` puzzles and chain A one more when N
is even; both get `(N+1)/2` when N is odd. Each chain holds its puzzles from
the first (index 0) to the goal (last index, A or B). New entries start as
puzzle 0.

The goal: the fixed one, or a random goal of the planet that is not among the
recent ones (planet 1: 85, 115, 185, 199, 201; planet 2: 103, 108, 135, 189,
197; planet 3: 131 to 134, 198). The recent list loses its oldest entry when
it holds more than three. On planet 2, goal 189 waits until a game was won
and goal 197 until ten were won. The chosen goal is added to the recent list
and counts as used.

### Choosing a puzzle

Collect all puzzles of the wanted type that are not used (goals: of the
planet and not recent), shuffle them (below), and take the first unused one
whose `item1` is the item asked for (any, for a goal).

### Shuffling

The original's shuffle is not uniform, and its exact steps decide which zone
and puzzle come first:

1. For each entry in order, draw n; if that place in a new list is free, move
   the entry there.
2. For each entry from the last to the first, as long as the new list still
   has a free place: draw n until a free place is hit and move the entry
   there (an entry already moved leaves an empty value, which is moved all
   the same and leaves the place looking free).
3. The new list replaces the old one.

### Bookkeeping

The generator keeps lists of used zones, used puzzles, used items (with the
ring they belong to), the items the player must find (with a ring; new ones
go to the front), and the single items handed out by blockade zones.

## 4. Finding a zone for a cell

To find a zone of a type for a cell: collect the zones of the planet whose
type matches (find also takes find-a-weapon), in index order, and shuffle
them. Try them in that order; skip used ones (except for the goal type when a
goal was fixed). The first zone that can take the role wins:

- **Empty**: any. When "only zones without hotspots" is on, the zone must
  have no hotspots at all.
- **Blockade, travel start or end**: the zone must ask for something (list A)
  and its crate items must be free. Draw over its free list-A items. If the
  zone hands out exactly one item, that item must not have been handed out by
  another blockade zone yet. The item goes to the front of the items to find
  (a travel start with ring 5, others with the cell's ring) and is used.
- **Goal**: the zone must hand out both goal items. Draw a free list-A item
  and a free list-B item (from rooms when the zone has none). Choose a goal
  puzzle (type 2) for each; both count as used even if the zone fails. They
  become the entries just before the goal in chains A and B. The goal setup
  (below) must succeed, else both entries are cleared.
- **Town**: any.
- **Use / trade**: the zone must hand out the item the next puzzle is about.
  Draw a free list-A item, choose a use (type 1) or trade (type 0) puzzle for
  it, put it in the chain, then run the use or trade setup. On success the
  puzzle and the item are used.
- **Find**: the find setup.

A zone's crate items are free when no crate in it (or its rooms) holds a used
item. After a successful setup they are all marked used.

### Setups

- **Use**: no characters in the zone. The item of this puzzle and of the next
  are marked used. If its crate items are not free, the first is unmarked
  (twice; the second stays marked) and the zone fails. Else the zone's first
  lock hotspot asks for this puzzle's item, and a random item spot gets the
  next puzzle's item.
- **Trade**: the crate items must be free. Draw one of the zone's free
  characters (from rooms when needed). The zone must ask for this puzzle's
  item and hand out the next one. The character goes on a random spawn
  hotspot.
- **Goal**: the crate items must be free. Draw a free character. The zone
  must ask for the last item of chain A and hand out the first goal item.
  With a character, it goes on a spawn spot. Without one, each of the two
  goal items goes on a random item spot, together with the matching asked
  item; if any of the four is missing, all four are unmarked and the zone
  fails.
- **Find**: the zone must not ask for anything and have no characters, and
  must hand out the item (else a room is tried). The spot type follows the
  item: weapon spot for weapons, locator spot for the locator kind, else item
  spot. A random spot of that type gets the item.

## 5. Order of the whole run

1. Opening draws and layout (sections 1 and 2).
2. Goal (section 3).
3. **Travel**: row by row, every travel start without a zone gets a travel
   start zone. Its vehicle hotspot names the destination zone. The first free
   landing on the left column, then the top row, the right column and the
   bottom row gets that destination as travel end, unless it is used. With
   no free landing the travel start becomes plain again and its item is no
   longer needed.
4. **Chain A**, from the goal down: find the cell with the next lower puzzle
   number. The first one gets the goal zone; the others draw 2 for trade or
   use (on 0 trade), and try the other kind when that fails. When the first
   puzzle of the chain is placed, the item it asks for joins the items to
   find, with its cell's ring.
5. **Chain B** the same, without the goal (on draw 0 use first, then trade).
   The first item of chain B is handed out on Dagobah (section 7).
6. **Town and blockades**: row by row. When no zone fits, the cell gets an
   empty zone.
7. **Items to find**: two fixed items join the list (one for ring 1, one for
   ring 2). Each item gets a cell: a plain or behind cell within its ring and
   with no puzzle next to it; else one within the ring; else a plain cell
   further out (a draw picks among them). A find zone for the item goes
   there.
8. **Empty zones** on every remaining plain, behind and island cell, row by
   row. Islands and the two inner rings take zones without hotspots. Two zones
   with a teleporter must not be within one cell of each other (two on a
   large map); a zone that would be is kept for a later cell and another one
   is taken. If only one teleporter zone was placed, it is swapped for a zone
   without hotspots.
9. One more draw (the original saves the goal history here).

When a step finds no zone or no cell, the seed gives no world. The original
then takes a fresh seed from the clock. We mix a new seed from the old one
(drawing from a sequence seeded with it), so that a world stays repeatable.

## 6. What a cell records

The zone, its role (zone type), the items it asks for and hands out, the
character, and the chain index of its puzzle. Hotspots that got an item or a
character are switched on, with the item or character as argument.

## 7. Dagobah

The game starts on Dagobah, a 2 x 2 map: zone 94 top left, 95 top right, 93
bottom left (the landing, where the game starts), 96 bottom right. One of
them hands out the first item of chain B: draw 4 picks 94, the hut room 535
of zone 95, 96 or 93 (goal 132 always uses 93; goal 189 uses the hut with a
different helper). The helper is placed on a random spawn spot of that zone,
or, in the hut, on the spot at (3,3) (the other helper at (3,2)).

## 8. Our own choices

- When the planet is left open: `1 + seed % 3`.
- When a seed fails: the next seed comes from the old one (section 5).
- The original's map cells keep a few values from the previous game; we start
  each world clean.
