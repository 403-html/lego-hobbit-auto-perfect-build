// Auto Perfect Build - configuration (AutoPerfectBuild.ini next to the game exe).
#pragma once
#include <windows.h>

namespace apb {

enum class Mode : int {
  Vanilla = 0,      // no gameplay change (mod loaded, hooks idle)
  AutoPerfect = 1,  // the game's own correct-piece path is taken for every step
  Instant = 2,      // the piece-selection minigame is skipped entirely
};

struct Config {
  Mode mode = Mode::AutoPerfect;
  bool log = true;
  int cycleKey = 0;      // optional hotkey (0 = disabled)
  DWORD autoDelayMs = 350;  // AutoPerfect: delay before each automatic pick
  int instantSpeed = 8;     // Instant: fast-forward factor for the build sequence
  int panelTopPct = 30;     // Mods panel: top edge, percent of screen height
};

const char* ModeName(Mode m);
void LoadConfig(const wchar_t* iniPath, Config& cfg);
void WriteDefaultConfig(const wchar_t* iniPath);

}  // namespace apb
