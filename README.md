# Sketchbook Menu

A notebook-style mod menu for **Geometry Dash 2.2081** (Geode v5, Windows).
It looks like your drawing: a grey page, light grey side tabs, thick black marker lines.

Press **Tab** in game to open it (you can change the key in the **Extra** tab).

---

## What's inside

**Bot**: Macro bot (saves your clicks), Replay bot (saves clicks + where the player is, so it never drifts),
save / load / delete macros, frame stepper (move 1 tick at a time), block my clicks while playing, auto-stop at the end.

**Video Maker** (Render tab), with 2 renderers you can pick from:

| | FFmpeg (best) | Built-in (fast) |
|---|---|---|
| Install something? | Yes, FFmpeg (free) | No |
| Quality | Best, you pick the size (up to 4K+) | Lower, size = your game window |
| Music | Yes, adds the level's song | No (silent video) |
| Speed | Game runs slow while rendering | Records while you play normally |
| GPU encoding | NVIDIA / AMD / Intel if you have them | Yes if Windows finds one |

**Hitboxes & hit borders**: show hitboxes, show on death, hitbox trail, highlight your own box,
border thickness, fill, and separate colors for spikes, blocks, orbs/portals, other, and player.

**Speed**: speedhack (with presets and music pitch), FPS unlock, lock delta.

**Player**: noclip (P1/P2, red flash), safe mode + auto safe mode, custom/instant respawn, no death effect,
no respawn effect, jump hack, auto clicker, hide player, don't pause on unfocus, no "New best" popup.

**Visual**: rainbow icon + trail, no wave trail, solid wave trail, no trail, hide attempts / percent / progress bar,
exact percentage with decimals.

**Labels**: cheat indicator, FPS, CPS, noclip accuracy, noclip deaths, attempts, best run, attempt time, jumps,
clock, speed, bot status, player position. Pick the corner, size and see-through.

**Extra**: no screen fade, unlock all icons & colors, restart key, practice key, menu key, popups on/off.

**Themes**: Sketch (your drawing), Paper, Midnight, Toast, Mint, Bubblegum, Neon, Lava, Ocean, plus a Custom
theme you can color yourself. Hand-drawn wobbly lines, "cartoon boil" wiggle, paint blob, marker thickness,
round corners, 5 fonts (including Comic Sans for extra sketchiness), menu size and animation speed.

**Animations**: the page pops in, tabs slide out, pages slide when you switch, checkboxes pop,
sliders glide, buttons press down, themes fade into each other, popups slide in.

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
- This was written and checked with a compiler, but not yet run inside Geometry Dash.
  If something acts weird in game, tell me what happened and I'll fix it.
