# Auto Perfect Build: LEGO The Hobbit (PC)

A small runtime mod for **LEGO The Hobbit (2014, Steam, DX9)**. It takes over the
piece-selection minigame of instruction builds.

| Mode | What happens |
|---|---|
| **Auto Perfect** (default) | The game asks for a piece, and the correct one is highlighted and picked automatically. The game's own building animations and progression play as normal. The stud pot never drains, so every build pays the **maximum 20,000-stud bonus**, including the game's "perfect" effect. Material (loot) costs are unchanged. |
| **Instant Build** | No wheel and no input at all. The build sequence and minigame timers are fast-forwarded (8× by default) until the game finishes the build itself. The finished object then appears exactly as after a normal build, and the 20,000 bonus is paid. |
| **Off (Vanilla)** | Unmodified game. |

Switch modes **in game**: pause, then press **Tab** (keyboard) or **Back/View**
(controller) to open the **MODS** panel on the right side of the screen, 30% down by
default (`PanelTop` in the ini):

```
 MODS - Auto Perfect Build
 Build assist:      <  Auto Perfect  >
 Auto-pick delay:  <  0.35 s  >
 Instant speed:     <  8x  >
 Up/Down select    Left/Right change    Tab / Esc / B close
```

While the panel is open, the game does not receive your input. Settings are saved
to `AutoPerfectBuild.ini` at once.

> Only the Steam `LEGOHobbit.exe` (32-bit DX9, PE timestamp `0x543C1BFA`) is
> supported. On any other executable, including `LEGOHobbit_DX11.exe`, the mod
> detects the difference and does nothing.

## Install

1. Get `dinput8.dll`: download the zip from the repository's **Releases**
   page, or [build it](#build).
2. Copy it into the game folder, next to `LEGOHobbit.exe`. By default that is
   `C:\Program Files (x86)\Steam\steamapps\common\LEGO - The Hobbit\`.
3. Start the game from Steam as usual. The first start creates
   `AutoPerfectBuild.ini` in the same folder (and `AutoPerfectBuild.log` if `Log=1`).

No game files are replaced or edited. The archives and the exe are untouched on
disk, and DRM is not touched either: the Steam exe has no DRM wrapper, and the mod
only patches the game in memory after Windows has loaded it.

## Uninstall

Delete `dinput8.dll` from the game folder. You can also delete
`AutoPerfectBuild.ini` and `AutoPerfectBuild.log`. Nothing else was changed.
Saves made with the mod are normal saves.

Keep `dinput8.dll` but turn the mod off: set *Build assist* to **Off** in the
panel, or `Mode=Vanilla` in the ini.

## Configuration (`AutoPerfectBuild.ini`)

| Key | Default | Meaning |
|---|---|---|
| `Mode` | `AutoPerfect` | `AutoPerfect`, `Instant` or `Vanilla` |
| `AutoDelayMs` | `350` | Pause before each automatic pick (0–2000), so the build stays watchable |
| `InstantSpeed` | `8` | Instant Build fast-forward factor (1–20) |
| `PanelTop` | `30` | Mods panel top edge, % of screen height (right-aligned) |
| `CycleKey` | `0` | Optional hotkey that cycles modes without pausing (virtual-key code, e.g. `119` = F8; 0 = off) |
| `Log` | `0` | Write `AutoPerfectBuild.log` next to the exe (set to `1` when testing or reporting a problem) |

## Build

The DLL must be **32-bit**. Only Windows SDK headers are needed (d3d9, dinput,
xinput); no DirectX SDK.

**Visual Studio 2019/2022 (Windows):** (the CMake project is written for MSVC, but only the MinGW build has been compiled and tested so far)
```
cmake -B build -A Win32
cmake --build build --config Release
:: output: build\Release\dinput8.dll
```

**MinGW-w64 (Linux / WSL / MSYS2):**
```
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
# output: build/dinput8.dll
```
Or without CMake:
```
i686-w64-mingw32-g++ -std=c++17 -O2 -shared -static -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN \
  -o dinput8.dll src/*.cpp src/dinput8.def -Wl,--enable-stdcall-fixup -s
```

**Releases (GitHub Actions):** create a release on GitHub (Releases → *Draft a new
release*), choose a new tag such as `v0.3.4`, and publish it.
`.github/workflows/release.yml` then builds `dinput8.dll` from that tag and stamps
the tag's version into it. It attaches `AutoPerfectBuild-v0.3.4.zip`
(`dinput8.dll`, `README.md`, `LICENSE`, `INSTALL.txt`) and `SHA256SUMS.txt` to the
same release. You can rebuild an existing release from the Actions tab
(*Release* → *Run workflow*, enter the tag). `.github/workflows/build.yml` is a
compile check, with MinGW and MSVC, that runs on pull requests or on demand.

To check whether your exe is the supported one, without starting the game:
`python tools/verify_exe.py "<game folder>\LEGOHobbit.exe"`. It needs
`pip install pefile`.

## How it works

The minigame is compiled C++, not data. The data only lists which pieces and
decoys each page uses (`inb_pieces.txt`). The mod redirects eight existing call
instructions and one vtable entry inside the game's `InstructionBuildPlayBubble`
code:

* the "select pressed" check;
* the slot→piece check;
* the stud-pot loss;
* the animation speed (Instant only).

Each site is verified byte by byte before anything is written. All are patched or
none are. See [docs/TECHNICAL_FINDINGS.md](docs/TECHNICAL_FINDINGS.md) for the
reverse-engineering notes and how to reproduce them, and
[docs/TESTING.md](docs/TESTING.md) for the in-game test procedure and results.

## Repository layout

```
src/                    the DLL
  main.cpp              entry point, config, optional hotkey
  game.cpp              exe identification and the build-minigame hooks
  hook.cpp              verified byte/pointer patching, IAT lookup
  overlay_d3d9.cpp      Mods panel rendering (Direct3D 9 Present hook)
  input_hooks.cpp       keyboard (DirectInput) / controller (XInput) for the panel
  ui.cpp                Mods panel model and settings
  proxy_dinput8.cpp     forwards DirectInput8 to the system DLL
tools/ttdat.py          read-only TT Games .DAT lister/extractor
tools/verify_exe.py     offline check of LEGOHobbit.exe against all patch sites
docs/                   technical findings and tests
```

## Legal

This is an independent fan mod. It is not affiliated with TT Games, Warner Bros.
or the LEGO Group. It contains no game code or assets, and you need your own legal
copy of the game. MIT licensed (see `LICENSE`).
