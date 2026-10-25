# Faust's Deck — C++ / raylib edition (v2: unique cards, animated duels, native resolution)

A philosophy card game: answer everyday-dilemma scenarios the way six schools of thought would,
collect a Duel card for every scenario you get right, then build a deck and duel a bot opponent.
Written in **C++17** with a **raylib** graphical interface; the rules engine contains no graphics code.

## Build and run

You need a C++17 compiler and [CMake](https://cmake.org) 3.16+. The first configure downloads
raylib 5.5 automatically (internet needed once); an installed raylib 5.0+ is used if CMake finds one.

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build            # optional: regression suite + randomised stress test
```

Run `build/FaustsDeck` (Visual Studio: `build/Release/FaustsDeck.exe`). The build copies `data/` and
`assets/` next to the program; saves go to `saves/` and tutor reports to `reports/` beside it.
Linux also needs `sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev`.

`third_party/nlohmann/json.hpp` is the official **single-header** release (v3.11.3). Do not replace it with the
multi-header `include/nlohmann/json.hpp`, which needs ~40 more files and only builds where the library is installed system-wide.

Options: `--size 1920x1080` window size in pixels · `--seed N` reproducible shuffles · `--root DIR` data folder ·
`--script FILE` drive the UI without a person (see *Extras*). **F11** toggles borderless fullscreen.

## Screen and resolution

The window opens at a size that fits your monitor (about 90% of it) and can be resized or made fullscreen.
Everything is drawn at the window's **real pixel size**: shapes are scaled and fonts are rasterised at the exact
pixel size they are drawn at, so text stays sharp at 1280x720, 1920x1080, 2560x1440, ultrawide, etc. The 16:9
layout is centred; extra window area is filled with the background colour. (v1 drew a fixed 1280x720 picture and
stretched it, which looked blurry on a 1080p monitor. It looked buns so I just skipped over the v1 upload lolz.) Text colour on coloured buttons is chosen automatically
from the button colour so it always has enough contrast.

## Playing

* **Play a Round** — pick a focus school (or random), read the scenario, pick A–D with your mouse (keys A–D work too).
  Correct answers earn 10 points and a Duel card; replaying a scenario correctly levels the card up (max 3).
* **Duel Mode** unlocks at 40 collected cards. Build decks in *Manage decks* (exactly 40 cards, max 3 copies),
  or use a preset. Pick Easy / Normal / Hard, watch the coin toss, take a mulligan, then duel.
* **The duel board** (Master Duel style): your cards at the bottom, the opponent's at the top. Each side has Deck,
  **Graveyard** and **Banish** piles and a Trap zone. Hover any card for its full effect text. **Click a Graveyard or
  Banish pile — yours or the opponent's — to look inside**; switch side/zone with the tabs.
* Click a hand card, then *Summon / Activate / Set Trap*; click a ready monster, then an enemy monster (or the
  opponent's life bar for a direct attack, i.e. Opponent's Face in card texts). Keys: **E** end turn, **Enter** play, **H** history, **Esc** cancel,
  **Space** skip the current animations. The **Anim** button cycles Normal / Fast / Off (pretty self-explanatory here).
* **Graveyard effects**: some cards can be used *from* the Graveyard on your turn (pay Insight, the card is banished).
  When one is usable, your Graveyard pile pulses gold with a count badge; inside, those cards are marked READY and a
  *Use effect* button appears.
* **Animations** replay everything that happens — draws, summons, spell and Trap activations, every card effect,
  attacks, damage, destruction, banishing, cards going to the Graveyard, coin flips and turn changes. The opponent's
  turn is played out the same way; there's also a History log that keeps the full record.

## Cards

All 60 cards are different: no two share cost, stats and effects(well, supposedly LMFAO). Costs run from 1 to 6 (23 cards cost 2 or less), and
Monsters with higher costs have better base stats. Level 2 adds +1 ATK, level 3 adds +1 HP, and effect numbers grow
with level (shown as `[Lv1, Lv2, Lv3]` in the data). Effects are built from a small set of building blocks
(see `OpKind` in `src/engine/types.hpp`) combined with a timing: **On Summon**, **when destroyed**, **when milled**,
**when attacking**, **start / end of turn**, **from the Graveyard**, or a **Trap** trigger (attack / summon / Spell).
Examples: destroy or reveal the opponent's Set Traps, block Traps for a turn, negate a summon effect or a Spell,
freeze or silence a monster, revive from your Graveyard or take a monster from the opponent's, coin-flip gambles,
sacrifice for the team, pay-LP for cards or Insight.

The text on every card is generated from the same data that drives the rules, with the exact numbers for the card's
level (for example "Deal 3 damage to the enemy's face."), so the description can never disagree with what happens.
`data/cards.json` holds the quiz text and the battle data; the battle fields are authored in `tools/design_cards.py`
(`python3 tools/design_cards.py` rewrites them without touching the scenarios).

## Project layout

| File | Role |
|---|---|
| `src/engine/card_engine.*`, `types.hpp` | loads `data/*.json`; card, effect and zone types |
| `src/engine/duel_cards.*` | card stats per level, exact-number card text, keyword glossary |
| `src/engine/battle_engine.*` | the duel rules; optional recorded **events** with a table snapshot after each change |
| `src/engine/bot_ai.*` | Easy / Normal / Hard bot |
| `src/engine/collection.*`, `deck_builder.*`, `preset_decks.*` | ownership/levels, decks, presets |
| `src/engine/evaluator.hpp`, `progress.hpp`, `profile_manager.*`, `report_generator.*` | quiz scoring, Wisdom Meter, saves, tutor report |
| `src/ui/gfx.cpp`, `app.hpp` | window, native-resolution rendering, fonts, widgets, card drawing |
| `src/ui/anim.cpp` | duel animations (replays engine events) |
| `src/ui/screens_*.cpp`, `src/main.cpp` | screens |
| `tests/dev_checks.cpp` | 55 regression checks |
| `tests/stress_test.cpp` | randomised bot-vs-bot matches with rule invariants |
| `tools/design_cards.py` | card battle-data authoring script |

## Tests

* `build/dev_checks data` — 55 checks: rules, every new effect type, card uniqueness, cost curve, exact card text,
  event recording, bot behaviour, save files (including corrupted-save recovery and Python-save compatibility).
* `build/stress_test data 600` — hundreds of random bot-vs-bot matches with every mechanic live; after each turn it
  checks LP/Shield/Insight limits, zone limits, that all 80 deck cards are still accounted for, and that the last
  recorded event matches the real table. For a memory-safety run build it with sanitizers:
  `g++ -std=c++17 -O1 -g -fsanitize=address,undefined -Isrc/engine -Ithird_party tests/stress_test.cpp src/engine/{battle_engine,bot_ai,card_engine,duel_cards}.cpp -o stress_san && ./stress_san data 200`.

## Extras

* `--script file.txt` commands: `click X Y`, `move X Y`, `type TEXT`, `key NAME`, `wait N`, `shot FILE.png`, `quit`
  (coordinates are in the 1280x720 layout space; animations run at a fixed 60 steps per second so runs are repeatable).
* Save files use the same JSON: copy `extras/maxtest_profile.json` into `saves/` for a test
  profile with all 60 cards at Level 3 and a ready 40-card deck(not very well made :P).
* Text input accepts Latin characters only (the bundled DejaVu font has no CJK glyphs).
* Fonts: DejaVu Sans (free licence, see `assets/fonts/DEJAVU-LICENSE.txt`). JSON: nlohmann/json (MIT). Graphics: raylib (zlib).
