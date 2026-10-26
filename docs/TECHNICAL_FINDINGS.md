# Technical findings

How the instruction-build minigame in **LEGO The Hobbit (PC, Steam)** works, and how
Auto Perfect Build changes it. Everything here was worked out from the installed
game files with the steps listed under "How to reproduce". No game source code was
used. No game assets are redistributed. The executable is not modified on disk.

## 1. The installation

| File | Notes |
|---|---|
| `LEGOHobbit.exe` | **What Steam launches.** 32-bit, DX9. 30,191,408 bytes, SHA-256 `072e25e2…c998`, PE TimeDateStamp `0x543C1BFA`, ASLR enabled. No SteamStub section (`.bind`), so there is no DRM wrapper to deal with. |
| `LEGOHobbit_DX11.exe` | 64-bit DX11 build. **Not supported:** the DLL detects it and stays inactive. |
| `GAME*.DAT`, `DLC*.DAT` | TT Games archives (old/"‑5" table format, LZ2K and raw entries). |

Both executables import `DINPUT8.dll`. A `dinput8.dll` placed next to the exe is
therefore loaded by Windows before the system copy. That is the loader used here.
It forwards every DirectInput export to `%WINDIR%\SysWOW64\dinput8.dll`.

## 2. Is the minigame data, script or code?

**The game data defines which pieces a build uses. The minigame itself is compiled C++.**

* `tools/ttdat.py` lists and extracts the archives. It is a port of the public
  QuickBMS `ttgames.bms` table reader.
  Each instruction build has a plain-text file, for example
  `levels\1_auj\1_2bagend\1_2bagenda\1_2bagenda_diningtable_inb\inb_pieces.txt`.
  It lists the pages, the correct pieces in order (`pieces_start … pieces_end`)
  and the decoys (`decoys_start … decoys_end`). 34 such files exist in the
  archives.
* Nothing in the data controls the timer, the stud pot, or what happens when a
  piece is picked. Those live in classes found through RTTI names in the exe:
  `InstructionBuildPlayBubble`, `InstructionBuildPlayBubble::WheelInputController`,
  `InstructionBuild::RewardPot`, `InstructionBuildInstance::FlowController`, and
  `HobbitInstructionBuildUI`.
* A data-only mod could remove decoys, but the player would still have to pick
  every piece against the clock. It would also need the whole game unpacked and
  every `.DAT` moved away, which is how loose-file mods for this game work.
  **Conclusion: a small runtime patch is required. No data files are changed.**

## 3. How the minigame works

All addresses are virtual addresses at the default image base `0x00400000`. The
mod converts them to RVAs because the exe is relocated by ASLR. The function names
are our own labels; the class names are the game's RTTI names.

### Objects

`InstructionBuildPlayBubble` (vtable `0x012F4EA4`) owns one build session.

| Offset | Meaning |
|---|---|
| `+0x038` | `FlowController*`. Its fields: `+0x08` page, `+0x09` index of the piece now expected, `+0x0A` page sub-state (0 intro, 1 pieces, 2 outro, 3 finished), `+0x1C` pieces on page, `+0x34` state (1 animating, 2 waiting for a pick, 3 page transition, 5 resume, 6 finished) |
| `+0x048` | session state: 0 init, 1 intro, 2 playing, 3 completed, 4 cancelled, 5 leaving |
| `+0x204` | `RewardPot`. Its fields: `+0x54` max, `+0x58` current, `+0x5C` min, `+0x60` drain step, `+0x64` first loss, `+0x70/+0x74` drain timers. Initialised in `0x00B76600` to **20000 / 20000 / 1000 / 6000** |
| `+0x27C` | `WheelInputController` (an event source) |
| `+0x24B8` | player-1 piece wheel (`+0x34` for player 2). `+0x08` enabled, `+0x10…+0x2C` 8 item pointers, `+0x30` highlighted item |
| `+0x2538` | 8 wheel slots of `0x68` bytes. `slot+0x54` is the index of the piece that slot shows; decoys never match the expected index |

### Per-frame flow

* `0x00B88ED0`, `PlayBubble::Update(dt)` (vtable slot 7). It returns immediately
  while the global pause byte `0x01A96E35` is set. In state 1/2 it calls
  `0x00B4D6F0`. That function enables the wheels only when
  `FlowController.state == 2` and the session state is 2. It then polls the
  `WheelInputController` (`0x00AEB260`).
* `0x00AEB260` turns the stick into a highlighted slot (`0x00AE9810` →
  `0x00ACA6E0 Wheel::SetSelection(slot, show)`). It asks
  `0x00AEB1D0 IsSelectPressed(player)` for each player. On a press it raises a
  `SelectionEventData` event that carries the player index.
* `0x00B8F360`, the play bubble's event handler, receives the event. It reads the
  highlighted slot and calls
  **`0x00B65C10 OnSlotSelected(playerIdx, slot)`**:

```
if (slot[slot].pieceIndex == flow->expectedPiece)   // 0x00B65C4D
    correct: Slot::MarkCorrect, update "n/m" text, FlowController::Advance (0x00B2F190)
else
    if (!slot.alreadyTried) RewardPot::Lose(pot, 1000)   // 0x00B65CEF
    Slot::MarkIncorrect                                   // 0x00B0D340
```

### Timer

`0x00B660A0 RewardPot::Update(dt)` runs a 2.5 s grace timer for each new piece
(`0x0121DC4C` = 2.5f, reset in `0x00B769C0`). It then drains the pot through
**`0x00B4E910 RewardPot::Lose(amount)`**. The first loss from a full pot is 6000;
each later loss is 1000. The pot never goes below its minimum.

### Completion and reward

* After the last page, `0x00B65D10` calls **`0x00B4D800 PlayBubble::Complete()`**.
  It sends `RewardedEventData` with the pot value: once overall (event type
  `0x01BE5FD0`) and once per player (`0x01BE6134`, halved for two players). It
  plays the build spiral and sets the session state to 3.
* `HobbitInstructionBuildUI` (`0x0040BDB0`) plays its special "perfect" effect
  only when the reward is exactly **20000**.
* `HobbitAchievements` (`0x004C0DF0`) adds the per-player reward towards the
  150,000-stud "Master Builder" achievement.
* The in-world construction is finished by `MechInstructionBuildTriggerInstance`
  (`0x00B828E0`). It only completes when the FlowController has reached state 6.
  **This is why Instant mode fast-forwards the flow instead of jumping to
  `Complete()`.**

### Pause

`0x00E3E380` (enter pause) sets byte `0x01A96E35` to 1 and the pause object's
`+0x3C` to 1, then raises `PausedStateChangeEventData`. `0x00DF2B70` (leave pause)
clears both.

## 4. What the mod changes

Every code change is a **5-byte relative call/jmp retarget** at a known call site.
Each one is checked before writing: the instruction must still be `E8/E9` pointing
at the expected function. All sites are checked first, then all are patched; if
any check fails, nothing is patched. The executable is identified by its PE
TimeDateStamp, SizeOfImage and CheckSum. The `InstructionBuildPlayBubble` vtable
contents and the instruction that reads the pause flag are also checked.

| Site (VA) | Original target | Purpose |
|---|---|---|
| `0x00AEB2C3`, `0x00AEB337` | `IsSelectPressed` | When the game is polling with a wheel enabled, point the wheel at the correct slot with the game's own `Wheel::SetSelection` and report a press. Delay is `AutoDelayMs` in Auto Perfect, none in Instant. |
| `0x00B8F49E` | `OnSlotSelected` | Replace the slot with the one whose piece index matches what the game expects. The game's own "correct" branch then runs. |
| `0x00B65CEF`, `0x00B66158` | `RewardPot::Lose` | Skipped in Auto Perfect / Instant, so time and mistakes never cost studs. The pot stays at 20000. |
| `0x00B65D4A` (`jmp`) | `PlayBubble::Complete` | Observation only: logs the awarded pot. |
| `0x00B2F1E4`, `0x00B76561` | `0x00757B70 Anim::PlayTo(target, speed)` | Instant only: multiply the build-animation speed by `InstantSpeed`. |
| vtable `0x012F4EC0` | `PlayBubble::Update` | Instant only: scale `dt` by `InstantSpeed` (each step capped at 0.25 s). |

Mods panel (no game code changed):

| Hook | Purpose |
|---|---|
| exe IAT `d3d9!Direct3DCreate9` → `IDirect3D9::CreateDevice` → `IDirect3DDevice9::Present` | Draw the panel. The text is rendered with GDI into a managed texture and drawn as one alpha-blended quad, inside a captured state block. |
| `IDirectInput8::CreateDevice` → keyboard `GetDeviceState` / `GetDeviceData` | Read keys for the panel. Hide them from the game while the panel has focus. |
| exe IAT `XINPUT1_3!#2` (`XInputGetState`) | The same for controllers. |

Vanilla mode leaves the hooks in place but passes every call straight through.
Everything except the Complete logger and the panel hooks is in
`InstructionBuildPlayBubble` code, so other minigames (blacksmithing, mining,
dance), saving and progression are not involved.

## 5. How to reproduce

1. **Archive layout:** `python tools/ttdat.py GAME.DAT --grep inb_pieces` lists
   all build definitions. Add `--extract inb_pieces out/` to read them.
2. **Classes:** `strings LEGOHobbit.exe | grep InstructionBuild` shows the RTTI
   names. Each vtable is found from its RTTI Complete Object Locator.
3. **Static analysis:** Ghidra 12.2 headless auto-analysis, then decompile the
   functions listed above. The UI sound table at `0x00ACBB00`
   ("UI_IB_Correct" / "UI_IB_Incorrect") and the strings `InstructBuild_Minigame_1`
   and `Spk_BuildSpiral` are good entry points.
4. **Offline check of an exe:** `python tools/verify_exe.py LEGOHobbit.exe`
   re-checks every site in the table above.
5. **Runtime confirmation:** every hook writes to `AutoPerfectBuild.log`: picks,
   the slot→piece mapping, pot value, suppressed losses and the final reward. See
   `docs/TESTING.md`.
