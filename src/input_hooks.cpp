// Auto Perfect Build - feeds keyboard / controller input to the Mods panel and
// hides it from the game while the panel has focus.
//
// The game reads the keyboard through DirectInput 8 (it imports DINPUT8.dll, which
// is this proxy) and controllers through XInput 1.3 (ordinal 2, XInputGetState).
// Both are intercepted only at the API boundary; no game code is touched.
#include "input_hooks.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <string.h>
#include <windows.h>
#include <xinput.h>

#include "hook.h"
#include "log.h"
#include "ui.h"

namespace apb {

// GUID_SysKeyboard {6F1D2B61-D5A0-11CF-BFC7-444553540000} (defined here to avoid dxguid.lib)
static const GUID kGuidSysKeyboard = {0x6F1D2B61, 0xD5A0, 0x11CF,
                                      {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

typedef HRESULT(STDMETHODCALLTYPE* DICreateDeviceFn)(void* di, REFGUID, void** dev, void* outer);
typedef HRESULT(STDMETHODCALLTYPE* GetDeviceStateFn)(void* dev, DWORD cb, void* data);
typedef HRESULT(STDMETHODCALLTYPE* GetDeviceDataFn)(void* dev, DWORD cb, DIDEVICEOBJECTDATA* rg,
                                                     DWORD* inout, DWORD flags);
typedef DWORD(WINAPI* XInputGetStateFn)(DWORD, XINPUT_STATE*);

static Patch g_pCreateDev, g_pGetState, g_pGetData, g_pXInput;
static DICreateDeviceFn o_DICreateDevice;
static GetDeviceStateFn o_GetDeviceState;
static GetDeviceDataFn o_GetDeviceData;
static XInputGetStateFn o_XInputGetState;
static void* volatile g_keyboard = nullptr;

// Keys/buttons pressed while the panel had focus stay hidden from the game until
// released, so closing the panel with Esc or B does not also close the pause menu.
static unsigned char g_swallowKey[256];
static WORD g_swallowPad[4];
static unsigned char g_prevKeys[256];
static WORD g_prevPad[4];
static DWORD g_lastStatePoll = 0;

static ui::Nav KeyToNav(int dik) {
  switch (dik) {
    case DIK_TAB: return ui::Nav::Toggle;
    case DIK_ESCAPE:
    case DIK_BACK: return ui::Nav::Close;
    case DIK_UP: return ui::Nav::Up;
    case DIK_DOWN: return ui::Nav::Down;
    case DIK_LEFT: return ui::Nav::Left;
    case DIK_RIGHT: return ui::Nav::Right;
    case DIK_RETURN:
    case DIK_NUMPADENTER:
    case DIK_SPACE: return ui::Nav::Accept;
  }
  return ui::Nav::None;
}

static void OnKeyDown(int dik) {
  bool wasFocused = ui::Focused();
  ui::HandleNav(KeyToNav(dik));
  if (wasFocused || ui::Focused()) g_swallowKey[dik] = 1;
}

static HRESULT STDMETHODCALLTYPE Hk_GetDeviceState(void* dev, DWORD cb, void* data) {
  HRESULT hr = o_GetDeviceState(dev, cb, data);
  if (dev != g_keyboard || FAILED(hr) || !data || cb < 256) return hr;
  unsigned char* k = static_cast<unsigned char*>(data);
  g_lastStatePoll = GetTickCount();
  for (int i = 0; i < 256; ++i) {
    bool down = (k[i] & 0x80) != 0;
    if (down && !(g_prevKeys[i] & 0x80)) OnKeyDown(i);
    g_prevKeys[i] = k[i];
    if (!down) g_swallowKey[i] = 0;
  }
  if (ui::Focused()) {
    memset(data, 0, cb);
  } else {
    for (int i = 0; i < 256; ++i)
      if (g_swallowKey[i]) k[i] = 0;
  }
  return hr;
}

static HRESULT STDMETHODCALLTYPE Hk_GetDeviceData(void* dev, DWORD cb, DIDEVICEOBJECTDATA* rg,
                                                  DWORD* inout, DWORD flags) {
  HRESULT hr = o_GetDeviceData(dev, cb, rg, inout, flags);
  if (dev != g_keyboard || FAILED(hr) || !rg || !inout || cb < sizeof(DIDEVICEOBJECTDATA_DX3))
    return hr;
  bool useEvents = GetTickCount() - g_lastStatePoll > 500;  // only if the game never polls state
  DWORD keep = 0;
  for (DWORD i = 0; i < *inout; ++i) {
    DIDEVICEOBJECTDATA* e =
        reinterpret_cast<DIDEVICEOBJECTDATA*>(reinterpret_cast<uint8_t*>(rg) + i * cb);
    int dik = (int)(e->dwOfs & 0xFF);
    bool down = (e->dwData & 0x80) != 0;
    if (useEvents && !(flags & DIGDD_PEEK)) {
      if (down) OnKeyDown(dik);
      else g_swallowKey[dik] = 0;
    }
    bool hide = ui::Focused() || g_swallowKey[dik];
    if (!hide) {
      if (keep != i) memcpy(reinterpret_cast<uint8_t*>(rg) + keep * cb, e, cb);
      ++keep;
    }
  }
  *inout = keep;
  return hr;
}

static HRESULT STDMETHODCALLTYPE Hk_DICreateDevice(void* di, REFGUID guid, void** dev, void* outer) {
  HRESULT hr = o_DICreateDevice(di, guid, dev, outer);
  if (SUCCEEDED(hr) && dev && *dev && IsEqualGUID(guid, kGuidSysKeyboard)) {
    g_keyboard = *dev;
    void** vt = *reinterpret_cast<void***>(*dev);
    if (!g_pGetState.active) {
      o_GetDeviceState = reinterpret_cast<GetDeviceStateFn>(vt[9]);
      InstallPointerPatch(g_pGetState, "IDirectInputDevice8::GetDeviceState", &vt[9], vt[9],
                          reinterpret_cast<void*>(&Hk_GetDeviceState));
    }
    if (!g_pGetData.active) {
      o_GetDeviceData = reinterpret_cast<GetDeviceDataFn>(vt[10]);
      InstallPointerPatch(g_pGetData, "IDirectInputDevice8::GetDeviceData", &vt[10], vt[10],
                          reinterpret_cast<void*>(&Hk_GetDeviceData));
    }
    Log("Keyboard device %p hooked for the Mods panel", *dev);
  }
  return hr;
}

uintptr_t GamePausedFlagAddress();  // game.cpp: non-zero only when the mod is active

void InputOnDirectInputCreated(void* di) {
  if (!di || g_pCreateDev.active || !GamePausedFlagAddress()) return;
  void** vt = *reinterpret_cast<void***>(di);
  o_DICreateDevice = reinterpret_cast<DICreateDeviceFn>(vt[3]);
  InstallPointerPatch(g_pCreateDev, "IDirectInput8::CreateDevice", &vt[3], vt[3],
                      reinterpret_cast<void*>(&Hk_DICreateDevice));
}

static DWORD WINAPI Hk_XInputGetState(DWORD idx, XINPUT_STATE* s) {
  DWORD r = o_XInputGetState(idx, s);
  if (r != ERROR_SUCCESS || !s || idx >= 4) return r;
  WORD b = s->Gamepad.wButtons;
  WORD pressed = b & ~g_prevPad[idx];
  g_prevPad[idx] = b;
  bool wasFocused = ui::Focused();
  struct {
    WORD mask;
    ui::Nav nav;
  } map[] = {{XINPUT_GAMEPAD_BACK, ui::Nav::Toggle},     {XINPUT_GAMEPAD_B, ui::Nav::Close},
             {XINPUT_GAMEPAD_DPAD_UP, ui::Nav::Up},      {XINPUT_GAMEPAD_DPAD_DOWN, ui::Nav::Down},
             {XINPUT_GAMEPAD_DPAD_LEFT, ui::Nav::Left},  {XINPUT_GAMEPAD_DPAD_RIGHT, ui::Nav::Right},
             {XINPUT_GAMEPAD_A, ui::Nav::Accept}};
  for (auto& m : map)
    if (pressed & m.mask) ui::HandleNav(m.nav);
  if (wasFocused || ui::Focused()) g_swallowPad[idx] |= pressed;
  g_swallowPad[idx] &= b;  // released buttons are no longer hidden
  if (ui::Focused()) {
    memset(&s->Gamepad, 0, sizeof(s->Gamepad));
  } else {
    s->Gamepad.wButtons &= ~g_swallowPad[idx];
  }
  return r;
}

bool InputInstallXInput() {
  HMODULE exe = GetModuleHandleW(nullptr);
  void** slot = FindIatSlot(exe, "XINPUT1_3.dll", nullptr, 2);
  if (!slot || !*slot) {
    Log("XInput import (ordinal 2) not found - controller cannot drive the Mods panel");
    return false;
  }
  o_XInputGetState = reinterpret_cast<XInputGetStateFn>(*slot);
  return InstallPointerPatch(g_pXInput, "IAT XINPUT1_3!#2 XInputGetState", slot, *slot,
                             reinterpret_cast<void*>(&Hk_XInputGetState));
}

void InputUninstall() {
  RemovePatch(g_pXInput);
  RemovePatch(g_pGetData);
  RemovePatch(g_pGetState);
  RemovePatch(g_pCreateDev);
}

}  // namespace apb
