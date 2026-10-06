# com.recomp.mod.base — the shared mod layer of the recomp runtimes

One way to expose a recompiled game's mods to Polyphase users and to the people who play
it, the same on every runtime:

| Runtime | Package | Games |
|---|---|---|
| PS1 | `com.recomp.ps1` | Digimon World, LSD: Dream Emulator |
| GameCube | `com.recomp.gcn` | Star Fox Adventures |
| N64 | `com.recomp.ssb64` (on `com.recomp.n64`) | Super Smash Bros. |
| GBA | `com.recomp.gba` | Kingdom Hearts: Chain of Memories |

What it gives you:

- **Mod Maps** (`ModMap` assets): the settings of a game (cheats, options, stats to show),
  made in an editor window, filled in automatically from the game.
- **A generated Mod Settings scene**: a gamepad-driven settings menu built from the map,
  updated in place when the map changes.
- **Mod settings at runtime**: the player's choices are applied to the game, kept applied,
  and saved (Windows / Wii `Saves/`, GameCube memory card).
- **A resolution scaler** for every player node: Fit, Integer, Native, Full Screen, Scale
  ×N, sharp or smooth filtering, window sizes on Windows.
- **Lua**: `Recomp.*` (the running game, any runtime) and `Mods.*` (the settings).
- **A launcher**: a generated, customizable front-end scene (ROM, mods, Play) for every
  runtime that registers its games, plus the `RecompLauncher` node and Lua to build your own.
- **Widgets** for your own UIs: `RecompText`, `RecompButton`, `RecompBar`,
  `RecompMenuController`.

It contains no game code and no game data.

## Install

Each runtime package lists it as a dependency (`package.json`):

```json
"dependencies": {
    "com.recomp.mod.base": "https://github.com/polyphase-recomps/com.recomp.mod.base"
}
```

Put this folder in your project's `Packages/` next to the runtime (the editor fetches it
when the URL is reachable). The editor builds it before the runtime and links them. After
changing this package, **restart the editor**: addons that depend on it are not reloaded
when it changes.

Working on several recomp projects? Clone this repository into each project's `Packages/`
and keep them current with `git pull`.

## For game authors

### 1. Make a Mod Map

**Tools > Recomp > Mods > Mod Map Editor** → **New...** → pick the game package. The map
is saved in `Packages/<game>/Assets/ModMaps/`.

Then **Import...**:

- **Running game**: every variable and request the game publishes right now (play the
  scene with the game's player node first).
- **Game sources (no game needed)**: scans the game package's `Native/` C files for bridge
  tables and finds:
  - PS1 / N64 `PortBridgeVar` rows, including rows written through macros;
  - GameCube `gcn_mod_variable` / `gcn_mod_request`;
  - GBA `AgbBridgeVar` rows;
  - request tables;
  - `port_debug_values("name")` calls and `game.json` `"options"`, which become startup
    options.

Tick what you want and **Add selected**. Each entry gets a guessed widget kind, label and
group:

- `cheat_*` switches become toggles and multipliers become numbers, both in the "Cheats"
  group;
- requests become actions;
- everything else becomes a read-only display.

Change whatever you like, then **Validate** (checks names against the running game or the
sources) and **Save**.

An entry has:

| Field | Meaning |
|---|---|
| Id | Unique key, also the save key |
| Label, Group, Help | What the settings menu shows |
| Widget | Toggle, Int, Float, Choice, Action, Display, Bar |
| Source | **Variable** (bridge variable + index), **Address** (raw memory, PS1 / GameCube), **Symbol** (decomp global, GameCube), **Request** (+ arguments), **Startup Option** (passed to the game at start, PS1), **Display** (built-in scaler setting) |
| Min / Max / Step / Default | Number range, toggle's on value, default value |
| Choices | Label / value pairs for Choice |
| Format | Display text, `{v}` = the value, other `{tokens}` work too |
| Save with the user's settings | Persist the player's choice |
| Lock | Re-apply the player's value whenever the game changes it (cheats) |

**Tools > Recomp > Mods > Live Variables** shows the running game's variables with live
values. You can edit them, call requests, and add a variable to the open map with **+**.
It's the quickest way to find what a cheat should poke.

### 2. Generate the settings scene

**Tools > Recomp > Mods > Generate Mod Settings Scene...** (or **Generate Scene...** in the
map editor). Pick the map, a scene name (default `SC_<Title>ModSettings`), the panel
position and the button that opens it. It is saved in the project's `Assets/Scenes/`.

The scene contains:

- a Canvas (kept visible) with a panel that fits the screen: it fills a 640x480 Wii /
  GameCube screen and is a centred panel of at most 760x560 on bigger ones;
- inside, an `ArrayWidget` column: the title, a row of tabs (one per group, scrolls
  sideways when they don't fit), the pages, and the footer;
- a page per group: a `ScrollContainer` around an `ArrayWidget` list of rows that
  stretch with the panel:
  - **Toggle / Choice**: a button ("Infinite HP: ON");
  - **Int / Float**: the value with − / +;
  - **Action**: a button;
  - **Display / Bar**: text or a bar;
- a footer with Save, Reset to defaults and Close;
- a Display page with the resolution scaler.

Gamepad: left / right over the tabs switches pages as you go, down / up walks the rows
(the list scrolls to keep the selected one in view), the last row goes down to the
footer, and the right stick scrolls the page. Mouse wheel and drag scroll too. A scene
made by the first, fixed-size version gets its panel rebuilt when you Update it.

A `RecompMenuController` makes the scene a menu:

- It's hidden at start.
- It opens with its toggle button, from the HOME menu (PS1 player) or from Lua
  `Mods.Open()`.
- While it's open it has the gamepad and the game gets no input. B closes it.

Instance the scene in your game's scene.

**Generating again updates the scene.** Nodes are matched by name: what exists is left
exactly as you changed it, and only new entries get rows. Rows of entries you removed
from the map are listed in the log, not deleted. The exception is the look: the map's
menu style (below) is applied on every Generate / Update.

#### Menu style

**Tools > Recomp > Mods > Menu Style** (or **Menu Style** on the Mod Map's inspector)
sets how the menu looks. It is saved with the Mod Map, so it ships with the game and
survives regenerating:

- **Background**: the panel's tint and texture (the tint multiplies the texture).
- **Buttons**: a color and a texture per state (Normal, Hovered, Pressed, Locked; a state
  without a texture uses Normal's), text color, **button text size** and **tab text size**,
  and the border of the gamepad-selected button.
- **Text**: the font (none = the engine default) and the title, label, read-only label
  and value colors, with sizes for the title, labels, values and the note.

Textures and fonts are picked from a filterable list, or dragged from the asset browser.
**Live preview** restyles the menu in the open level as you edit. **Update Scene** saves
the style and restyles the scene asset (layout and navigation untouched). The same
restyle is available to code as `ModStyle_Apply(root, style)`.

### 3. A launcher (optional)

A front-end scene the game opens with. The player sets their ROM up, opens the mods and
presses Play. It works for every runtime that registers its games as launchers (N64 does).

**Tools > Recomp > Mods > Launcher** (or **Launcher** on the Mod Map's inspector). Pick the
map, then customize:

| Section | Settings |
|---|---|
| **Text** | Title (empty = the map's title), Subtitle |
| **Pictures** | Logo and its size, Background picture and tint |
| **Panel and buttons** | Panel size, Position (centre / left / right), each button's label, and whether Forget ROM, Mods and Quit are shown |
| **Starting the game** | **Game Scene**: the scene with the game's player node, opened once the game starts. **Start at once when the ROM is known**: later launches go straight to the game |

**Generate Scene** makes `SC_<Title>Launcher` in the project's `Assets/Scenes`:
- a background;
- a panel with the logo, title, subtitle and ROM line;
- what happened last;
- the buttons Play, Choose ROM..., Forget ROM, Mods and Quit.

The panel, buttons and fonts follow the **Menu Style**; everything else follows these settings.

**Update Scene** applies the settings again and keeps the nodes you changed or added, as the
settings scene does. **Live preview** updates a launcher open in the editor as you edit.

Generate the Mod Settings scene first: the launcher's **Mods** button opens it. The launcher
adds it to its scene itself (its node's **Mods Scene**). Make the launcher scene the one the
project opens with.

What it's made of (all usable in your own UIs, no script needed):
- **`RecompLauncher`** node, put in the UI's root. Properties:
  - **Game**: a package id; empty = the only game;
  - **Game Scene**;
  - **Mods Scene**;
  - **Auto Start**;
  - **Load Mods**: the game's mod settings work before it runs.

  Play shows "Starting..." for a frame before it starts the game, because a live recompile takes about half a second. A script on the node gets `OnRomChosen(node, path, ok)`, `OnGameStarted(node)` and `OnGameStartFailed(node, message)`, and the matching signals.
- **`RecompButton` Settings**:
  - `@launcher:play`;
  - `@launcher:browse`: a file dialog, then it checks and remembers the ROM;
  - `@launcher:forget`;
  - `@launcher:mods`;
  - `@launcher:quit`: packaged games only.
- **`RecompText` tokens**:
  - `{@launcher.title}`;
  - `{@launcher.rom}` (the path);
  - `{@launcher.romfile}` (the file name, or "No ROM chosen");
  - `{@launcher.message}`;
  - `{@launcher.status}` (idle / starting / running / failed);
  - `{@launcher.ready}`.
- **`RecompMenuController` Close On Back**: off for a UI that must stay open. A UI opened over another one, such as the mods over the launcher, has the gamepad until it closes.

A runtime supports launching by registering a `RecompGameLauncher` per game
(`Source/ModBaseLauncher.h`) with `Recomp_RegisterLauncher`. It covers checking, remembering and
forgetting the ROM, whether the build ships its game data, and starting the game.
com.recomp.n64 registers one per game package (`N64Launcher`); com.recomp.ps1 registers one per
game in its addon (`Ps1Launcher`: the "ROM" is the disc image, checked by what its SYSTEM.CNF boots).

### 4. Script it (optional)

```lua
-- any runtime
if Recomp.IsRunning() then
    local hp = Recomp.Get("hp")
    Recomp.Set("money", 99999)
    local id = Recomp.Request("heal")
end

-- the settings
Mods.Set("cheat_infhp", 1)
print(Mods.Text("display.mode"))   -- "Fit"
Mods.Open()                        -- the generated settings menu
```

| `Recomp.` | |
|---|---|
| `IsRunning()` | A game runs and its bridge works |
| `Game()` | Package id and runtime id of the running game |
| `Get(name [, i])` / `Set(name, v [, i])` | A published variable |
| `Request(name, ...)` → id, `Result(id)` | Game requests (integer arguments) |
| `Read(addr or symbol, type)` / `Write(...)` | Raw memory (PS1, GameCube), `type` `"s32"`, `"u8"`, `"f32"`... |
| `Variables()` / `Requests()` | Lists with name, type, count, help |
| `SetInputBlocked(b)` / `IsInputBlocked()` | Keep the gamepad away from the game (your own menus) |
| `Games()` | The games that can be launched: package, title, runtime, rom, started |
| `SetRomLocation(path [, game])` | Checks the ROM and remembers it: ok, message |
| `GetRomLocation([game])` / `ClearRomLocation([game])` | The remembered ROM (nil if none) / forget it |
| `CheckRom(path [, game])` | ok, message; nothing saved |
| `BrowseForRom()` | A file dialog: the path, or nil |
| `LoadMods([game])` | The game's mod settings, so `Mods.*` works before it runs |
| `StartGame([game])` (also `StartRecomp`) | Starts the game now: ok, message |
| `IsStarted([game])` / `LaunchStatus([game])` | Started? / "idle" or "running" and the last message |

| `Mods.` | |
|---|---|
| `Get(id)` / `Text(id)` / `Set(id, v)` | A setting's value, its menu text, change it |
| `Step(id, dir)` | Toggle, ± step, next / previous choice, run an action |
| `Reset([id])` | One or all back to default |
| `Save()` | Save now (it also saves a moment after each change) |
| `List()` | The map's entries |
| `Open()` / `Close()` / `Toggle()` | The generated settings menu |

The runtime tables (`Ps1`, `Gcn`, `N64`) still exist for runtime-specific calls.

### Widgets for your own UIs

All are in the Add Node list. Their bindings are inspector properties (category
"Recomp"), so a UI made of them needs no script.

- **`RecompText`**: a **Format** with tokens:
  - `{hp}`, `{name[3]}` (array element), `{type>names}` (look up a table), `{n:02}` (zero-padded), `{v?ON|OFF}`;
  - `{@id}`: a setting's value text;
  - `{@id.label}`: a setting's label.
- **`RecompButton`**: one of:
  - **Setting** + **Direction**, plus the special settings `@save`, `@reset`, `@close`
    and `@page:<Group>`;
  - **Request** + **Arguments**;
  - **Toggle Variable**;
  - **Step Variable**.

  The selected button gets a border (**Highlight Color / Width**).
- **`RecompBar`**: **Variable** against **Max Variable**.
- **`RecompMenuController`**: put it in a UI's root, next to the panel it shows and hides.
  Keep the root visible: Polyphase widgets don't tick while hidden, so a controller inside
  the widget it hides could never open it again. Properties:
  - **Panel** (the widget shown/hidden; empty = the sibling named `Panel`, else the parent);
  - **Max Panel Size** / **Panel Margin** / **Panel Align** (a full-stretch panel fills the
    screen minus the margin, at most that size, aligned centre / left / right; 0 = off);
  - **Scroll Speed** (right stick scrolling, pixels per second);
  - **Title**;
  - **Start Visible**;
  - **Capture Input** (gamepad navigation; the game gets nothing; B closes; the selected
    button is kept visible and scrolled into view in any `ScrollContainer`);
  - **First Button**;
  - **Toggle Button** (a gamepad button code, controller 1; Select is Back on XInput);
  - **Toggle Action** / **Close Action** (a PlayerInput action, `Category/Name` or just
    `Name`; keyboard bindings work too. Needs an engine that exports `PlayerInputSystem`,
    i.e. defines `POLYPHASE_PLAYER_INPUT_EXPORTED`);
  - **In HOME Menu**;
  - **Bound Variable** (shown while a game variable is non-zero, e.g. a pause flag).

  On play it logs `Recomp menu '<title>': opens with ...`, then `opened` / `closed` on each
  toggle. No such line means the UI isn't in the running scene. The Generate dialog's
  **Open with** lists the gamepad buttons and the project's input actions, and is applied
  on every run, including Update of an existing scene.

`Source/ModBaseUiBuilder.h` has the non-destructive builder the generator uses (`Ensure`,
`Group`, `Label`, `Bound`, `SettingButton`, `LinkNavigation`, `RootCanvas`) for your own
editor tools.

## The resolution scaler

Every player node places its picture by the player's **Screen** setting:

| Screen | |
|---|---|
| Fit (default) | As large as fits, with the game's real shape: 4:3 (PS1, N64, GameCube), 3:2 (GBA) |
| Integer | The largest whole multiple of the native size that fits: sharp, even pixels |
| Native | 1:1, the game's own resolution |
| Full Screen | Fills the screen (stretched) |
| Scale ×N | N times native (Scale setting, 1-6) |

**Filter**: Auto (each runtime's own choice: GameCube smooth, the others sharp), Sharp or
Smooth.

What "full screen" and resolutions mean on each platform:

- **Windows, packaged game:** **Window** offers:
  - Full Screen (the engine's borderless desktop fullscreen);
  - 2×, 3× or 4× the game's native size;
  - 1280×720, 1600×900, 1920×1080, 2560×1440, 3840×2160.

  In the editor only the picture inside the game view changes.
- **Wii / GameCube:** the screen is the console's video mode (640×480, PAL 640×528), so
  only the Screen modes apply. A Wii set to 16:9 widens the picture on the TV; Fit
  compensates, so 4:3 stays 4:3. N64 games drawn by the GX renderer get the same placement.

It does not render above the game's native resolution. The PS1 and GameCube GPUs are software
rasterisers working at the console's resolution.

Settings ids: `display.mode`, `display.scale`, `display.filter`, `display.window`. They are
saved with the player's mod settings, can be listed in a Mod Map (source Display), and get
a Display page in generated scenes.

## Where settings are saved

`<save name>.mods`: the map's **Save Name**, default the last part of the game id. It's a
small binary of id / value pairs, kept under one GameCube memory-card sector.

| Platform | Location |
|---|---|
| Editor, Windows, Wii | `<project>/Saves/` |
| GameCube | Memory card slot A |

## Adding a runtime (writing a provider)

Implement `RecompProvider` (`Source/ModBaseProvider.h`) over your runtime's bridge and
register it:

```cpp
class MyProvider : public RecompProvider { /* RuntimeId, GamePackage, IsLive, Variables,
    Requests, Get, Set, Request, Result; optionally ReadAddress / WriteAddress /
    ResolveSymbol / SetStartupOptions / FrameInfo */ };

static int OnLoad(PolyphaseEngineAPI* api) { Recomp_RegisterProvider(&MyProvider::Get()); ... }
static void OnUnload() { Recomp_UnregisterProvider(&MyProvider::Get()); ... }
```

In the player node:

1. Call `ModSettings::Get().Tick(&MyProvider::Get())` every frame.
2. Give the game no input while `Recomp_IsInputCaptured()`, and hold it until the buttons
   are released.
3. After updating the frame texture, call
   `Recomp_DisplayApply(quad, texture, w, h, aspect[, defaultLinear])`. If it returns true,
   recreate the texture with `Recomp_DisplayFilterLinear()`.
4. Call `Recomp_DisplayApplyWindow(w, h)` once per frame.
5. Pass `ModSettings::Get().StartupOptions(game)` to the game when it starts, if it has
   options.

Add the dependency to the runtime's `package.json`. In its check CMakeLists, add
`add_subdirectory(../com.recomp.mod.base)` and link it, as the four runtimes do.

The existing providers:

- **PS1:** `com.recomp.ps1/Source/Ps1Provider.cpp`.
  - Bridge plus little-endian guest memory.
  - Startup options are merged into `game.json` options.
- **GameCube:** `com.recomp.gcn/Source/GcnProvider.cpp`.
  - Mod variables and any decomp global by name.
  - Big-endian memory.
- **N64:** `com.recomp.ssb64/Source/N64Provider.cpp`.
  - Bridge; floats as 16.16 in `set`.
  - GX path placement through `n64_set_display_rect`.
- **GBA:** `com.recomp.gba/Source/GbaProvider.cpp` over `GbaBridge`.
  - The game packs its variables each frame (`Runtime/include/agb_bridge.h`).
  - In-process through `AgbHostApi` v2.
  - In the editor through `PortShm`'s bridge region.

## Files

| File | |
|---|---|
| `ModBaseProvider.*` | `RecompProvider`, registry, input capture, type helpers |
| `ModBaseModMap.*` | `ModMap` asset |
| `ModBaseSettings.*` | `ModSettings`: values, apply, lock, save, startup options, display |
| `ModBaseDisplay.*` | Resolution scaler |
| `ModBaseWidgets.*` | `Recomp*` widgets, format tokens |
| `ModBaseUiBuilder.h` | Non-destructive UI building (editor tools) |
| `ModBaseSceneGen.*` | Generate / update the settings scene (editor) |
| `ModBaseImport.*` | Automatic import: live game, source scan (editor) |
| `ModBaseEditor.*` | Tools > Recomp > Mods windows, ModMap inspector, Create Asset item |
| `ModBaseLua.*` | `Recomp` and `Mods` tables |
| `Tools/check_addon.ps1` | Compile check outside the editor |
