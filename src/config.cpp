#include "config.h"

namespace apb {

const char* ModeName(Mode m) {
  switch (m) {
    case Mode::Vanilla: return "Vanilla";
    case Mode::AutoPerfect: return "AutoPerfect";
    case Mode::Instant: return "Instant";
  }
  return "?";
}

static const char kDefaultIni[] =
    "; Auto Perfect Build for LEGO The Hobbit (PC, Steam DX9 build)\r\n"
    "; Mode = AutoPerfect | Instant | Vanilla\r\n"
    ";   AutoPerfect : every piece is picked correctly and immediately (max stud bonus)\r\n"
    ";   Instant     : no piece selection at all; the build sequence is fast-forwarded\r\n"
    ";                 to completion through the game's own flow\r\n"
    ";   Vanilla     : unmodified gameplay\r\n"
    "[AutoPerfectBuild]\r\n"
    "Mode=AutoPerfect\r\n"
    "; All settings below can also be changed in game: pause the game and press Tab\r\n"
    "; (keyboard) or Back/View (controller) to open the Mods panel.\r\n"
    "; Optional extra hotkey that cycles AutoPerfect -> Instant -> Vanilla without\r\n"
    "; pausing (virtual-key code, e.g. 119 = F8; 0 = disabled)\r\n"
    "CycleKey=0\r\n"
    "; Milliseconds to wait before each automatic pick in AutoPerfect mode, so the\r\n"
    "; building animation stays watchable. 0 = as fast as the game accepts input.\r\n"
    "AutoDelayMs=350\r\n"
    "; Instant mode fast-forward factor for the build animation and minigame timers\r\n"
    "; (1-20). Lower it if anything looks wrong in Instant mode.\r\n"
    "InstantSpeed=8\r\n"
    "; Vertical position of the Mods panel's top edge, in percent of the screen\r\n"
    "; height (0 = top, 90 = bottom). The panel is right-aligned.\r\n"
    "PanelTop=30\r\n"
    "; Write AutoPerfectBuild.log next to the game exe (1/0)\r\n"
    "Log=1\r\n";

void WriteDefaultConfig(const wchar_t* iniPath) {
  HANDLE h = CreateFileW(iniPath, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
  if (h == INVALID_HANDLE_VALUE) return;  // already exists
  DWORD w;
  WriteFile(h, kDefaultIni, sizeof(kDefaultIni) - 1, &w, nullptr);
  CloseHandle(h);
}

void LoadConfig(const wchar_t* iniPath, Config& cfg) {
  wchar_t buf[64];
  GetPrivateProfileStringW(L"AutoPerfectBuild", L"Mode", L"AutoPerfect", buf, 64, iniPath);
  if (!lstrcmpiW(buf, L"Vanilla") || !lstrcmpiW(buf, L"Off") || !lstrcmpW(buf, L"0"))
    cfg.mode = Mode::Vanilla;
  else if (!lstrcmpiW(buf, L"Instant") || !lstrcmpiW(buf, L"InstantBuild") || !lstrcmpW(buf, L"2"))
    cfg.mode = Mode::Instant;
  else
    cfg.mode = Mode::AutoPerfect;
  cfg.log = GetPrivateProfileIntW(L"AutoPerfectBuild", L"Log", 1, iniPath) != 0;
  cfg.cycleKey = (int)GetPrivateProfileIntW(L"AutoPerfectBuild", L"CycleKey", 0, iniPath);
  cfg.autoDelayMs = GetPrivateProfileIntW(L"AutoPerfectBuild", L"AutoDelayMs", 350, iniPath);
  if (cfg.autoDelayMs > 5000) cfg.autoDelayMs = 5000;
  cfg.instantSpeed = (int)GetPrivateProfileIntW(L"AutoPerfectBuild", L"InstantSpeed", 8, iniPath);
  if (cfg.instantSpeed < 1) cfg.instantSpeed = 1;
  if (cfg.instantSpeed > 20) cfg.instantSpeed = 20;
  cfg.panelTopPct = (int)GetPrivateProfileIntW(L"AutoPerfectBuild", L"PanelTop", 30, iniPath);
  if (cfg.panelTopPct < 0) cfg.panelTopPct = 0;
  if (cfg.panelTopPct > 90) cfg.panelTopPct = 90;
}

}  // namespace apb
