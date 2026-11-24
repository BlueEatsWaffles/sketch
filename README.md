# Sketchbook Menu

A notebook-style mod menu for **Geometry Dash 2.2081** (Geode v5, Windows).
It looks like your drawing: a grey page, light grey side tabs, thick black marker lines.

Press **Tab** in game to open it. To change the key: in the menu go to **Extra** > **Open / close menu**,
or in Geode go to **Mods** > **Sketchbook Menu** > the **gear** (Settings) > **Menu key**.

---

## What's inside

**Predict** (new tab): press **Predict this level!** and a bot plays the level all by itself.
It restarts the level, turns on practice mode (so nothing is saved), and never uses speedhack.
It tries, dies, goes back a bit (backtracking) and tries again until it wins. Then it plays the whole
run again from 0% to double-check it really works.
- **How should it click?** Early (as soon as it safely can), Normal (in the middle, safest) or Late (last moment).
- **Smarts**: look-ahead after a click, double-check from the start, show the bot thinking, thinking speed.
- **Save the run**: save it as a macro (Bot tab list), save it to your **Downloads**, or type your own folder.
  The file is named `<level name> - bot`. Pick **.gdr** (works in Mega Hack, xdBot, zBot, Prism, ToastyReplay)
  or **.skm** (this menu). You can also **Watch it** or **Make a video of it**.
- Normal levels only for now (no platformer, no 2-player). Hard levels can take a few minutes.

**Bot**: Macro bot (saves your clicks), Replay bot (saves clicks + where the player is, so it never drifts),
save / load / delete macros, frame stepper (move 1 tick at a time), block my clicks while playing, auto-stop at the end.

**Macros from other bots** (Bot tab): plays macros made with **Eclipse** (.gdr2), **Mega Hack, xdBot, zBot, Prism**
(.gdr / .gdr.json), **yBot 2** (.ybot) and **ToastyReplay** (.ttr / .ttr2 / .ttr3), plus old xdBot (.xd).
The menu finds them on your PC by itself (every Geode mod's folders and the Geometry Dash folders), or you can drop
the files into the `macros` folder. Pick one and press **Load & Play**. If a macro dies right away, change
**Frame offset** to -1 or +1 and load it again. Only the clicks are used (not other bots' position fixes),
and macros made with a TPS bypass (not 240) may not play perfectly.

**Video Maker** (Render tab), with 2 renderers you can pick from:

| | FFmpeg (best) | Built-in (fast) |
|---|---|---|
| Install something? | Yes, FFmpeg (free) | No |
| Quality | Best, you pick the size (up to 4K+) | Lower, size = your game window |
| Music | Yes, adds the level's song | No (silent video) |
| Speed | Game runs slow while rendering | Records while you play normally |
| Start | Restarts the level from the start (closes the pause menu too) | Same |
| GPU encoding | NVIDIA / AMD / Intel if you have them | Yes if Windows finds one |

**Hitboxes & Visuals**: show hitboxes, show on death, only my hitbox, highlight my box, hitbox trail.
**Hit borders** has its own checkbox (it turns on hitboxes too): thickness, rainbow borders, fill,
colors for spikes, blocks, orbs/portals, other and player, and an on/off key.
**Markers**: an X where you died and a dot where you clicked (with a Clear button).
Also rainbow icon + trail, no wave trail, solid wave trail, no trail, hide player, hide attempts / percent /
progress bar, exact percentage with decimals.

**Speed**: speedhack (with presets and music pitch), **slow motion while you hold a key**, FPS unlock, lock delta.

**Player**: noclip (P1/P2, red flash), safe mode + auto safe mode, custom/instant respawn, no death effect,
no respawn effect, jump hack, auto clicker, don't pause on unfocus, no "New best" popup.

**Labels**: cheat indicator, FPS, CPS, noclip accuracy, noclip deaths, level name, attempts, deaths, best run,
attempt time, time played, jumps, clock, speed, bot status, player position, **FPS graph** and an
**input display** (lights up when you hold jump). Pick the corner, size and see-through.

**Extra**: no screen fade, unlock all icons & colors, **mute when you click away**, **screenshots** (F9, game only,
no menu), restart key, practice key, menu key, popups on/off.

**Themes**: 29 themes plus Custom: Sketch (your drawing), Paper, Midnight, Toast, Mint, Bubblegum, Neon, Lava,
Ocean, Blueprint, Chalkboard, Graph Paper, Sticky Note, Cardboard, Sakura, Matcha, Grape Soda, Sunset, Arctic,
Forest, Terminal, Vaporwave, Coffee, Bumblebee, Comic, Galaxy, Ink, Lavender, Rose Gold.
Many have page patterns (lined paper, grid, dots, pencil hatching, twinkling stars, scanlines, comic dots),
color fades, and some glow like neon. **Surprise me!** picks a random one.
The Custom theme lets you pick every color, the pattern and the glow.

**Look**: hand-drawn pencil lines (with a **Sketchiness** slider), double pencil lines, wiggly "cartoon boil",
paint blob with pencil shading, tape on the corners, little doodles, marker thickness, round corners, 5 fonts.

**Animations**: the menu bounces in, tabs slide in one by one, the picked tab pops out, the marker dot bounces
between tabs, titles and underlines write themselves, rows slide in one after another, checkboxes pop and
squish, theme cards pop in, themes fade into each other. **Bounciness** slider: 0 = smooth, 2 = super bouncy.

---

## How to turn this into a mod (easy way, no coding tools)

You need a free GitHub account. GitHub's computers build the mod for you.
There are **no folders** to upload, just files.

1. Unzip `SketchbookMenu.zip`.
2. On github.com, tap **+** (top right), then **New repository**. Name it `sketchbook-menu`,
   tick **Add a README file**, and tap **Create repository**.
3. Tap **Add file**, then **Upload files**. Pick **all the files** from the unzipped folder
   (select them all at once). Tap **Commit changes**.
4. Open the **Actions** tab and tap **set up a workflow yourself**.
   Delete everything in the box, paste in everything from `build-workflow.txt`, then tap **Commit changes**.
5. The build starts by itself. Go back to the **Actions** tab and wait 5 to 10 minutes for the green tick.
6. Open the finished run, scroll down to **Artifacts**, and download **Build Output**.
7. Unzip it. Inside is `sketchbook.sketchbook-menu.geode`.
8. On your PC, put that file in your Geometry Dash folder, in `geode\mods`.
   (Steam: right-click Geometry Dash, then **Manage**, then **Browse local files**.)
9. Start the game and press **Tab**.

**If the build fails (red X):** open the run, tap the failed step, copy the red error text,
and send it to me. I'll fix it.

---

## Installing FFmpeg (only for the FFmpeg renderer)

The Built-in renderer needs nothing. For the FFmpeg one:

1. In game, open the menu, go to **Render**, and press **Get FFmpeg**.
2. Pick a Windows build (gyan.dev or BtbN) and download the **essentials** .zip. Unzip it.
3. Press **Open mod folder** in the menu and copy `bin\ffmpeg.exe` into that folder.
   (Or paste the full path to `ffmpeg.exe` into the **FFmpeg path** box.)
4. Press **Check again**. The red X turns into a green tick when it's found.

The **Setup report** at the top of the Render tab always tells you what's working and what's missing.

---

## Building on your own PC (advanced)

Install the Geode SDK and CLI (docs.geode-sdk.org, "Getting Started"), Visual Studio 2022 with C++,
and CMake. Then in this folder:

```
geode build
```

The menu UI uses **gd-imgui-cocos**, which CMake downloads by itself.
For builds that never change, replace `#geode` in `CMakeLists.txt` with a commit hash from that repo.

---

## Good to know

- Speedhack, noclip, the bot and the other cheats turn on **auto safe mode**, so your progress and stars
  are not saved while they're on. The dot in the corner shows green (legit), yellow (cheats, safe) or red.
- Labels and the menu are not recorded in videos. Only the game is.
- Settings save by themselves to the mod's save folder (`config.txt`). Macros go in `macros\`, videos in `renders\`.
- Predict's thinking was tested on a pretend practice game, not inside real Geometry Dash yet.
  It always double-checks its run from the start before saying it won.
- This was written and checked with a compiler, but not yet run inside Geometry Dash.
  If something acts weird in game, tell me what happened and I'll fix it.
