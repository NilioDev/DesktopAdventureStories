# Combat

How monsters move and fight, and how the hero fights back. The monsters
follow the rules of the original programs: we read how Yodesk.exe and
DESKADV.EXE move their monsters (with a disassembler, to observe only) and
describe the rules here in our own words. The code is
`src/core/src/world/combat.cpp`.

## What the data gives

Every character (CHAR) has a type: 1 the hero, 2 an enemy, 4 a weapon. Two
more sections give each character two numbers:

- **CHWP**, a reference and a value.
  - For an enemy, the value is its health and the reference is the weapon
    character it shoots with (none: it only fights up close).
  - For a weapon, the reference is the sound it makes.
- **CAUX**, the damage the character does: an enemy up close, or a weapon per
  hit.

In Yoda Stories a health of 0xFFFF means the enemy cannot be hurt, and a
damage of 0xFFFF that it does no harm; Indy uses neither.

A weapon character's first frame set holds its shots, one tile per direction.
Its last tile in that set is the inventory tile of the weapon. A weapon with
shot tiles shoots; one without hits the cell in front of the hero. Its second
frame set is the hero holding it, shown while he attacks. A weapon without an
inventory tile (Indy's fists) is what the hero uses when his hands are empty.

## Monsters

Each placed monster (IZAX) starts with its character's health. Scripts can
switch monsters on and off and test whether they are dead. A monster blocks
the hero and other monsters, and so does a monster's shot in flight: the
originals keep both in the zone's object layer.

### The game tick

Both originals run the game in ticks of 140 ms (their default game speed).
On every tick each monster that is switched on decides one step, in the
order the zone lists them. A tick counter that both games keep decides which
ticks some kinds use: every other tick, or every third.

A step can go in any of the eight directions. A monster may step onto a cell
when nothing is in its object layer (no wall, item, other monster or shot)
and it has a floor tile without flag bit 16; floors with that bit (door
openings, edges) keep monsters out. When the cell it wants is taken, the
monster tries one cell aside first: for a straight step one of the two
diagonal cells beside the target, the left or right one first at random; for
a diagonal step the straight step that keeps one of the two parts. If the
cell it would step into is the hero's, it does not move but hits the hero
instead (see below).

Most kinds turn towards the hero when they stand still. The tile a monster
shows is its standing frame (set 0) for the way it looks; monsters do not
use the walk frames. As in the originals, the last tile is kept per
character, so standing monsters of the same kind may share one.

### Kinds of movement

The character's movement number decides the kind. "Close" means at most one
cell away in both directions (diagonals count), "near" fewer than 6 cells in
both directions. "Random" is a step of -1, 0 or 1 cells along each axis.

Yoda Stories:

| movement | behaviour |
|---|---|
| 1 | Skips half its ticks. Goes for the hero; after 15 ticks of that, or after hitting the hero twice, it backs off for 4 ticks (half its steps away from the hero, half random). |
| 2 | Skips half its ticks. Walks straight in its direction (up, down, left or right); when blocked it waits 0 to 2 ticks and picks a new direction. Close by: half its ticks it attacks, half it steps at random. |
| 3 | Skips half its ticks. Random steps; close by, half its ticks it goes for the hero. With a weapon it does not hit up close. |
| 4 | Sits and turns to the hero. It only shoots. |
| 6 | Every third tick. Keeps away from the hero; every tenth tick it starts to come closer, until it has bumped into the hero four times. Near: random steps. |
| 7 | As 2, but close by it always attacks, and when blocked it waits 0 to 7 ticks. |
| 8 | As 2 without skipping ticks: a third of its far-away steps are random, close by half are random, it hits only half the times and not with a weapon, and when blocked it waits 0 to 9 ticks. |
| 9 | Every other tick. Walks straight in its direction; when blocked it waits 0 to 2 ticks and picks a new direction. |
| 10 | Every other tick. Walks to its four waypoints in turn (straight or diagonally), starting with the second. |
| 11 | Near: steps away from the hero, else random. A floor that keeps monsters out makes it try another random step at once. |
| 12 | Part of the scenery: never moves or attacks, and shows its first six frames in turn, one per tick. |

Each Yoda Stories monster can also be made to wait a few ticks (see 2, 7, 8
and 9); while it waits it stands still and does not shoot.

Indy:

| movement | behaviour |
|---|---|
| 1 | Every other tick. As Yoda Stories' 1: chases, and backs off for 4 ticks after 15 ticks or two hits. |
| 2 | Every other tick. Half its steps go for the hero, half are random. |
| 3 | Every other tick. Random; close by, half its steps go for the hero. |
| 4 | Sits and turns to the hero. It only shoots. |
| 5 | A rock. Every other tick it rolls one cell down (over an enemy in its way, too), showing four frames in turn. When something else blocks it, it disappears and starts again where it began. It hurts the hero standing right below it. |
| 6 | As Yoda Stories' 6. |

In Indy a monster with a weapon never hits up close; Yoda Stories' kinds 1,
2, 7, 10 and 11 do.

### Hitting and shooting

A monster hits by trying to step into the hero's cell: it plays the hurt
sound (sound 4) and does its damage (CAUX). In Yoda Stories a damage of
0xFFFF does nothing, and a damage of 0 only plays the sound.

A monster with a weapon (its CHWP reference) shoots when the hero is fewer
than 4 cells away in both directions and in its row or column: on each tick
with a chance of 1 in 7, or 1 in 4 for Indy's sitting monsters. The shot
flies one cell per tick, from the monster towards the hero, and is gone after
three cells or when something is in the way. When it reaches the hero it
does the monster's own damage (not the weapon's) and plays the hurt sound
(Indy plays the weapon's sound too). The weapon's sound plays when the shot
leaves. A monster has one shot at a time and does not shoot while it waits.

### Taking damage

The originals add up the damage a monster takes and compare it with its
character's health: when the damage reaches the health it dies, so a health
of 0 dies at the first hit. In Yoda Stories a health of 0xFFFF cannot be
hurt. A dead monster's scripts run again so that they can react. In Yoda
Stories it plays sound 5 and, only when its IZAX entry says so (the 32-bit
field after the item), drops an item where it fell: the tile one below its
item number, or for item number 0 the zone's own item (the first item spot
that has one). Indy's monsters drop nothing.

## The hero

Picking up a weapon puts it in his hands when they are empty. Using a weapon
from the inventory where no script reacts to it also takes it in hand. Attacking
(at most every 0.3 seconds) hits the monster in front of him or fires a shot.
Below the normal difficulty his blows hit harder, as in both originals: the
weapon's damage is multiplied by 10 - difficulty / 5 (whole numbers), so ten
times at 0 and once from 45 up.
Shots fly one cell every 70 ms, up to 8 cells, and stop at walls.

The difficulty (0 to 100, normally 50) scales the damage the hero takes, as
in both originals: the damage is divided by 100 / difficulty (whole numbers),
so the hero takes half at 50 and all of it at 100, and at least 1. This also
holds for health that a script takes away. When health reaches 0, the game is
lost.
