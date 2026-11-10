# gamedata/

Put your own copy of the original game files here. Everything in this folder
except this README is ignored by Git and must never be committed.

```
gamedata/
├── yoda/      Star Wars: Yoda Stories
│   ├── YODESK.DTA
│   ├── YODESK.EXE
│   └── sfx/            sound effects (*.wav)
└── indy/      Indiana Jones and His Desktop Adventures
    ├── DESKTOP.DAW
    ├── DESKADV.EXE
    └── *.WAV, *.MID    sound effects and music
```

The easiest way is to copy the complete game folder from your CD or
installation into `yoda/` or `indy/`. File names are matched without regard to
case, and other files in the folders are ignored.

The program looks here when started without a path (from the repository root,
or with a `gamedata` folder next to the executable). The tests that need the
original files run with:

```sh
DAS_GAMEDATA_DIR=gamedata ctest --test-dir build --output-on-failure
```
