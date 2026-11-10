# Scripts as text

Zone scripts (see [scripts.md](scripts.md)) can be written as plain text, for
editing them in any text editor. Mods keep changed scripts this way (see
[mod.md](mod.md)), and the editor's *Scripts* row opens a zone's scripts as
text (see [../editor.md](../editor.md)).

```
# A door that talks back.
script
  if Bump 4, 5, 812
  if CounterIs 0
  do SpeakNpc 4, 5, "Who is there?"
  do SetCounter 1
end

script
  if Bump 4, 5, 812
  if CounterIs 1
  do SpeakHero "It is locked."
end
```

- Each script starts with `script` and ends with `end`. They run in the
  order they are written.
- `if` lines are the conditions and come first; `do` lines are the
  instructions. A script runs its instructions when all its conditions hold.
- After the name come up to five numbers (-32768 to 65535, separated by
  commas), then optionally a text in double quotes. Numbers that
  are left out are 0. What each number means is in [scripts.md](scripts.md).
- Names are those of [scripts.md](scripts.md) and of
  `src/core/include/das/script/opcodes.hpp`, written as there, for example
  `PlaceTile`, `HasItem`, `SpeakNpc`. They are the same for both games; a
  name the game does not know is refused.
- A line starting with `#` is a comment. Blank lines are skipped.
- In a text, `\"` is a quote, `\\` a backslash, `\n` and `\r` line breaks
  (the games use `\r\n`), `\t` a tab, and `\xHH` any other control
  character. Text is UTF-8 and may only use characters of Windows-1252, the
  character set of the games.
- `opN` stands for the game's own opcode number N. It is written for opcodes
  that have no name of their own (Indiana Jones uses two numbers for
  `Nothing`), so that every script reads back exactly as it was.

The numbers that an opcode does not use are written too when they are not 0:
the original data often holds leftover values there, and keeping them makes
reading the text give the very same data.

A mistake is reported with its line number, for example
`scripts/0005.txt: line 3: unknown condition "Bumpp"`.

## Implementation

`src/core/include/das/script/script_text.hpp` (`writeScriptText`,
`readScriptText`). `tests/test_script_text.cpp` checks that every script of
both games reads back unchanged.
