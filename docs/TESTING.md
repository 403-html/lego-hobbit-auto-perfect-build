# In-game tests

All tests use the normal game started from Steam (`LEGOHobbit.exe`). Logging is
off by default, so first set `Log=1` in `AutoPerfectBuild.ini`. After each test,
quit the game and read `AutoPerfectBuild.log` next to the exe. The log is
recreated every time the game starts.

A good build to repeat is **An Unexpected Party → the Bag End dining table**.
It has three pages with 2 + 3 + 4 pieces (`inb_pieces.txt` in the archives).
Any other instruction build works too.

## T0: loader and version check
1. Start the game and reach the title screen.
2. Expected log lines:
   `Executable verified.`, eight `[...] patched 5 bytes` lines plus the vtable
   patch, `pause flag verified`, `All 9 patches installed`, and three overlay/IAT
   pointer patches.
3. Swap in `LEGOHobbit_DX11.exe` (Steam launch option) or a different build.
   Expected: `Unsupported executable … Mod inactive` and an unmodified game.

## T1: Auto Perfect (default)
1. Set Mode = Auto Perfect, either in the Mods panel or in the ini.
2. Start an instruction build and do not touch the controls once the wheel shows.
3. Expected on screen:
   * every piece highlighted and picked about `AutoDelayMs` after its wheel opens;
   * normal placing animations;
   * the stud bar never drops;
   * the "perfect" effect and a 20,000-stud bonus at the end.
4. Expected in the log: one `auto-pick` / `OnSlotSelected … CORRECT` pair per
   piece. The number of picks per page equals that page's `pieces_start` count.
   The run ends with `BUILD COMPLETE: reward pot 20000 / 20000`.
5. Variation: press select yourself while the wheel shows the wrong piece.
   Expected: `pick redirected: slot X -> Y`, then CORRECT.

## T2: Instant Build
1. Set Mode = Instant Build and start an instruction build.
2. Expected:
   * no wheel interaction is needed;
   * the build animation runs about InstantSpeed× faster;
   * the minigame ends and the finished object appears in the world, exactly as
     after a normal build;
   * the reward is 20,000.
3. Expected in the log: picks with no delay, then `BUILD COMPLETE: reward pot 20000`.
4. Then leave and re-enter the area, or save and reload. The build must still be
   complete.

## T3: Vanilla / Off
1. Set Mode = Off.
2. Play a build normally. Make one deliberate mistake and wait more than 3 s on
   one piece.
3. Expected: unmodified behaviour. The log shows `pot lost 1000 (vanilla)` /
   `pot lost 6000` lines and a reward below 20000.

## T4: Mods panel
1. In game, pause. The MODS box shows on the right edge, with its top at `PanelTop`% of the
   screen height (30% by default).
2. Press Tab (keyboard) or Back/View (controller). The full panel opens.
   * Up/Down moves between rows; the pause menu behind it must not move.
   * Left/Right changes values.
3. Close with Tab, Esc or B. The pause menu must not also close or go back.
4. Unpause, then check `AutoPerfectBuild.ini` holds the new values.
5. Expected in the log: `Mods panel opened`, `Mode -> …`, and
   `Mods panel closed (Mode=…)`.

## T5: no side effects
* Play a non-instruction minigame (blacksmithing, mining or dance). It behaves
  normally, and the log has no new lines from the build hooks.
* Save, quit, reload. Progress is kept.
* Remove `dinput8.dll` (uninstall). The game starts and plays exactly as before.

## Results so far

| Date | Build | Test | Result |
|---|---|---|---|
| 2026-10-26 | v0.1.0 | T0 loader | Pass: loaded, executable verified (base `0x00970000` under ASLR) |
| 2026-10-26 | v0.2.0 | T1 Auto Perfect, Bag End dining table | Pass: 9/9 picks correct (2+3+4), pot 20000/20000, build completed (user-confirmed) |
| 2026-10-26 | v0.3.0 | T0 loader/overlay | Pass: 9 game patches + IAT/vtable hooks installed, pause flag verified |
| 2026-10-26 | v0.3.0 | T4 Mods panel | Pass: opens/closes with Tab, mode changes logged and saved; user asked to move it lower (fixed in v0.3.1: `PanelTop=30`) |
| 2026-10-26 | v0.3.0 | T1 Auto Perfect, 3 more builds | Pass: every pick CORRECT, reward 20000/20000 each time |
| 2026-10-26 | v0.3.1 | T2 Instant, T3 Vanilla, panel position | pending |
