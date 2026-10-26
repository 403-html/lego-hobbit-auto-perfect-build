// Auto Perfect Build - entry point.
#include <windows.h>

#include "config.h"
#include "game.h"
#include "input_hooks.h"
#include "overlay_d3d9.h"
#include "ui.h"
#include "log.h"

namespace apb {

Config g_cfg;
wchar_t g_dir[MAX_PATH];
wchar_t g_ini[MAX_PATH];
static HANDLE g_hotkeyThread = nullptr;
static volatile LONG g_stop = 0;

static void BuildPaths(HMODULE self) {
  GetModuleFileNameW(self, g_dir, MAX_PATH);
  wchar_t* slash = wcsrchr(g_dir, L'\\');
  if (slash) slash[1] = 0;
  lstrcpyW(g_ini, g_dir);
  lstrcatW(g_ini, L"AutoPerfectBuild.ini");
}

static bool GameWindowFocused() {
  HWND fg = GetForegroundWindow();
  DWORD pid = 0;
  if (fg) GetWindowThreadProcessId(fg, &pid);
  return pid == GetCurrentProcessId();
}

// Cycles AutoPerfect -> Instant -> Vanilla -> AutoPerfect while the game has focus.
static DWORD WINAPI HotkeyThread(LPVOID) {
  bool down = false;
  while (!g_stop) {
    Sleep(50);
    bool now = (GetAsyncKeyState(g_cfg.cycleKey) & 0x8000) != 0 && GameWindowFocused();
    if (now && !down) {
      Mode m = GetMode();
      Mode next = m == Mode::AutoPerfect ? Mode::Instant
                : m == Mode::Instant     ? Mode::Vanilla
                                         : Mode::AutoPerfect;
      SetMode(next);
      // audible feedback: 1 beep AutoPerfect, 2 Instant, 3 Vanilla
      int beeps = next == Mode::AutoPerfect ? 1 : next == Mode::Instant ? 2 : 3;
      for (int i = 0; i < beeps; ++i) {
        MessageBeep(MB_OK);
        Sleep(180);
      }
    }
    down = now;
  }
  return 0;
}

static void Init(HMODULE self) {
  ui::Init();
  BuildPaths(self);
  WriteDefaultConfig(g_ini);
  LoadConfig(g_ini, g_cfg);
  wchar_t logPath[MAX_PATH];
  lstrcpyW(logPath, g_dir);
  lstrcatW(logPath, L"AutoPerfectBuild.log");
  LogOpen(logPath, g_cfg.log);
  Log("Auto Perfect Build %s loaded. Mode=%s CycleKey=%d AutoDelayMs=%lu", APB_VERSION,
      ModeName(g_cfg.mode), g_cfg.cycleKey, g_cfg.autoDelayMs);
  if (!GameInstall()) {
    Log("Mod inactive: the game runs unmodified.");
    return;
  }
  // The Mods panel is optional: if these fail the build hooks still work and the
  // mode can be set in AutoPerfectBuild.ini (or with CycleKey).
  OverlayInstall();
  InputInstallXInput();
  if (g_cfg.cycleKey > 0 && g_cfg.cycleKey < 256)
    g_hotkeyThread = CreateThread(nullptr, 0, HotkeyThread, nullptr, 0, nullptr);
}

}  // namespace apb

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(inst);
    apb::Init(inst);
  } else if (reason == DLL_PROCESS_DETACH) {
    apb::g_stop = 1;
    // On process exit (reserved != null) other threads are already gone and the
    // code is about to disappear anyway: only restore bytes on a real unload.
    if (!reserved) {
      apb::InputUninstall();
      apb::OverlayUninstall();
      apb::GameUninstall();
    }
    apb::Log("Unloaded.");
    apb::LogClose();
  }
  return TRUE;
}
