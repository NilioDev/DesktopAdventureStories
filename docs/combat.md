# Combat

How monsters move and fight, and how the hero fights back. These are our own
rules. They are built on what the game data says about each character, but
they are not taken from the original programs. The code is
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

A health or damage of 0xFFFF means none: such an enemy cannot be hurt, or does
no harm.

A weapon character's first frame set holds its shots, one tile per direction.
Its last tile in that set is the inventory tile of the weapon. A weapon with
shot tiles shoots; one without hits the cell in front of the hero. Its second
frame set is the hero holding it, shown while he attacks. A weapon without an
inventory tile (Indy's fists) is what the hero uses when his hands are empty.

## Monsters

Each placed monster (IZAX) starts with its character's health. Scripts can
switch monsters on and off and test whether they are dead. A monster blocks
the hero and other monsters. A dead monster drops the item it carries (Yoda
Stories) where it fell, and the zone's scripts run again so that they can
react.

The character's movement number decides how it moves. This is how we read
the numbers:

| movement | behaviour |
|---|---|
| 0, 4 | stays put |
| 1, 5 | goes for the hero |
| 2 | goes for the hero when within 4 cells, else wanders |
| 6 | goes for the hero when within 4 cells, else stays |
| 3, 7, 8, 9 | wanders |
| 10 | walks its waypoints (Yoda Stories), else wanders |
| 11 | keeps away from the hero when within 4 cells, else wanders |
| 12 | part of the scenery: never moves or attacks |

A monster takes a step every 0.45 seconds. Next to the hero, it turns to him
and hits once a second for its damage. A monster with a weapon shoots when the
hero is in its row or column, at most 8 cells away, with nothing in between:
once every 1.6 seconds, for the weapon's damage.

## The hero

Picking up a weapon puts it in his hands when they are empty. Using a weapon
from the inventory where no script reacts to it also takes it in hand. Attacking
(at most every 0.3 seconds) hits the monster in front of him or fires a shot.
Shots fly one cell every 70 ms, up to 8 cells, and stop at walls.

The difficulty (0 to 100, normally 50) scales the damage monsters do: from half
at 0 to one and a half times at 100. When health reaches 0, the game is lost.
