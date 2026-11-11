# Music

Indiana Jones and His Desktop Adventures has its music as Standard MIDI
files (`THEME.MID`, `VICTORY.MID`, ...). They are in the game's sound list
next to the WAV effects, so the scripts start them like any other sound.
Yoda Stories has no MIDI music.

The original games handed MIDI to Windows' synthesizer. Not every system
has one, so we play the files with our own small synthesizer
(`src/core/src/audio/midi.cpp`). It sounds different from a General MIDI
sound card, but the same on every system.

## Reading

Format 0 and 1 files with a ticks-per-quarter-note timing are read: all
tracks are merged, tempo changes are applied, and the channel messages are
kept with their time in seconds. Meta events other than tempo and end of
track, and system exclusive messages, are skipped. A damaged file is refused.

The Indy songs follow the Windows 3.1 and 95 authoring rules and hold the
music twice: once for a General MIDI synthesizer on channels 1 to 10 (drums
on 10), and once more simply for an FM card on channels 13 to 16 (drums on
16). Windows played only one of the two. When a file has notes in both
ranges we keep channels 1 to 12 and drop 13 to 16; playing both doubles
every part and turns the FM drums into melodic notes.

## Sounds

- **Instruments:** each General MIDI instrument family (program / 8) has one
  two-operator FM sound: a sine carrier whose phase is moved by a sine
  modulator. The family sets the modulator's frequency ratio and depth, how
  the depth fades, and an attack, decay, sustain and release envelope.
  Softer notes get a little less modulation, so they sound darker, and the
  bowed and blown families (strings, ensemble, brass, reed, pipe) get a slow
  vibrato once a note has sounded for a quarter of a second.
- **Drums:** channel 10 plays synthesized drums instead: kick and toms (a
  sine falling in pitch), snare (noise and a tone), hi-hats and cymbals
  (noise with its low part removed), and short noise for the rest.
- **Controllers:** volume, expression, pan, sustain pedal, pitch bend (±2
  semitones), reset and all notes off are followed. Volume, expression and
  velocity are squared into loudness, like on General MIDI synthesizers.
- **Repeated notes:** a key struck again on the same channel releases the
  note it was still playing.
- **Voices:** up to 32 at once. When all are busy, the quietest is taken.
- **Mixing:** every voice adds to a stereo mix. Up to 0.6 of full scale the
  mix passes unchanged; only louder peaks are bent softly below full scale,
  so normal passages are not distorted and chords never clip.

## When it plays

- The title screen plays the song with "theme" in its name, once per visit.
- Winning plays the "victory" song and losing the "defeat" song.
- Scripts play songs through their sound commands; a new song replaces the
  one playing.
- *Music* in the options sets the music volume, apart from *Sound* for the
  effects.
