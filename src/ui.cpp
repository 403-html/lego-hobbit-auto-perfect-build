#include "ui.h"

#include <stdio.h>

#include "config.h"
#include "game.h"
#include "log.h"

namespace apb {

extern Config g_cfg;
extern wchar_t g_ini[MAX_PATH];
uintptr_t GamePausedFlagAddress();  // game.cpp (0 if unverified)

namespace ui {

static CRITICAL_SECTION g_cs;
static bool g_csInit = false;
static volatile bool g_focused = false;
static int g_row = 0;
static unsigned g_serial = 1;

enum Row { RowMode, RowDelay, RowSpeed, RowCount };

void Init() {
  if (!g_csInit) {
    InitializeCriticalSection(&g_cs);
    g_csInit = true;
  }
}

static void Lock() { EnterCriticalSection(&g_cs); }
static void Unlock() { LeaveCriticalSection(&g_cs); }

bool GamePaused() {
  uintptr_t a = GamePausedFlagAddress();
  return a && *reinterpret_cast<volatile unsigned char*>(a) != 0;
}

bool Visible() { return GamePaused(); }
bool Focused() { return g_focused && GamePaused(); }

static const wchar_t* ModeLabel(Mode m) {
  switch (m) {
    case Mode::AutoPerfect: return L"Auto Perfect";
    case Mode::Instant: return L"Instant Build";
    case Mode::Vanilla: return L"Off (Vanilla)";
  }
  return L"?";
}

static void SaveIni() {
  const wchar_t* modeName = GetMode() == Mode::Vanilla   ? L"Vanilla"
                            : GetMode() == Mode::Instant ? L"Instant"
                                                         : L"AutoPerfect";
  WritePrivateProfileStringW(L"AutoPerfectBuild", L"Mode", modeName, g_ini);
  wchar_t buf[32];
  wsprintfW(buf, L"%lu", g_cfg.autoDelayMs);
  WritePrivateProfileStringW(L"AutoPerfectBuild", L"AutoDelayMs", buf, g_ini);
  wsprintfW(buf, L"%d", g_cfg.instantSpeed);
  WritePrivateProfileStringW(L"AutoPerfectBuild", L"InstantSpeed", buf, g_ini);
}

static void Change(int dir) {
  switch (g_row) {
    case RowMode: {
      // order shown to the player: Auto Perfect, Instant Build, Off
      static const Mode order[3] = {Mode::AutoPerfect, Mode::Instant, Mode::Vanilla};
      int i = 0;
      while (i < 3 && order[i] != GetMode()) ++i;
      i = (i + (dir > 0 ? 1 : 2)) % 3;
      SetMode(order[i]);
      break;
    }
    case RowDelay: {
      int v = (int)g_cfg.autoDelayMs + dir * 50;
      if (v < 0) v = 0;
      if (v > 2000) v = 2000;
      g_cfg.autoDelayMs = (DWORD)v;
      break;
    }
    case RowSpeed: {
      int v = g_cfg.instantSpeed + dir;
      if (v < 1) v = 1;
      if (v > 20) v = 20;
      g_cfg.instantSpeed = v;
      break;
    }
  }
  SaveIni();
  ++g_serial;
}

void HandleNav(Nav n) {
  if (!GamePaused() || n == Nav::None) return;
  Lock();
  if (!g_focused) {
    if (n == Nav::Toggle) {
      g_focused = true;
      ++g_serial;
      Log("Mods panel opened");
    }
    Unlock();
    return;
  }
  switch (n) {
    case Nav::Toggle:
    case Nav::Close:
      g_focused = false;
      Log("Mods panel closed (Mode=%s, AutoDelayMs=%lu, InstantSpeed=%d)", ModeName(GetMode()),
          g_cfg.autoDelayMs, g_cfg.instantSpeed);
      break;
    case Nav::Up: g_row = (g_row + RowCount - 1) % RowCount; break;
    case Nav::Down: g_row = (g_row + 1) % RowCount; break;
    case Nav::Left: Change(-1); break;
    case Nav::Right:
    case Nav::Accept: Change(+1); break;
    default: break;
  }
  ++g_serial;
  Unlock();
}

void Tick() {
  static bool wasPaused = false;
  bool p = GamePaused();
  if (wasPaused && !p && g_focused) {
    Lock();
    g_focused = false;
    ++g_serial;
    Unlock();
  }
  if (p != wasPaused) ++g_serial;
  wasPaused = p;
}

void GetView(View& v) {
  Lock();
  v.serial = g_serial + (unsigned)GetMode() * 1000003u;  // mode can change via hotkey too
  v.lineCount = 0;
  auto add = [&](const wchar_t* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(v.lines[v.lineCount], 95, fmt, ap);
    va_end(ap);
    v.lines[v.lineCount][95] = 0;
    ++v.lineCount;
  };
  if (!g_focused) {
    add(L"MODS    press Tab (pad: Back)");
    add(L"Auto Perfect Build: %s", ModeLabel(GetMode()));
    v.selected = -1;
    v.firstRow = 1;
  } else {
    add(L"MODS - Auto Perfect Build");
    v.firstRow = v.lineCount;
    add(L"Build assist:      <  %s  >", ModeLabel(GetMode()));
    add(L"Auto-pick delay:  <  %lu.%02lu s  >", g_cfg.autoDelayMs / 1000,
        (g_cfg.autoDelayMs % 1000) / 10);
    add(L"Instant speed:     <  %dx  >", g_cfg.instantSpeed);
    add(L"Up/Down select    Left/Right change    Tab / Esc / B close");
    v.selected = v.firstRow + g_row;
  }
  Unlock();
}

}  // namespace ui
}  // namespace apb
