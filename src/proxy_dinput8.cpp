// dinput8.dll proxy: forwards every export to the real system dinput8.dll.
// LEGOHobbit.exe imports DINPUT8.dll (DirectInput8Create), so Windows loads this
// file from the game folder first. Nothing about input is changed.
#include <windows.h>

#include "input_hooks.h"
#include "log.h"

static HMODULE g_real = nullptr;

static FARPROC Real(const char* name) {
  if (!g_real) {
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);  // SysWOW64 for a 32-bit process
    lstrcpyW(path + n, L"\\dinput8.dll");
    g_real = LoadLibraryW(path);
    if (!g_real) {
      apb::Log("FATAL: could not load system dinput8.dll (err %lu)", GetLastError());
      return nullptr;
    }
  }
  return GetProcAddress(g_real, name);
}

extern "C" {

HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD ver, REFIID riid, LPVOID* out,
                                        void* outer) {
  typedef HRESULT(WINAPI * Fn)(HINSTANCE, DWORD, REFIID, LPVOID*, void*);
  Fn fn = (Fn)(void*)Real("DirectInput8Create");
  if (!fn) return E_FAIL;
  HRESULT hr = fn(inst, ver, riid, out, outer);
  if (SUCCEEDED(hr) && out && *out) apb::InputOnDirectInputCreated(*out);
  return hr;
}

HRESULT WINAPI Proxy_DllCanUnloadNow() {
  typedef HRESULT(WINAPI * Fn)();
  Fn fn = (Fn)(void*)Real("DllCanUnloadNow");
  return fn ? fn() : S_FALSE;
}

HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID c, REFIID r, LPVOID* o) {
  typedef HRESULT(WINAPI * Fn)(REFCLSID, REFIID, LPVOID*);
  Fn fn = (Fn)(void*)Real("DllGetClassObject");
  return fn ? fn(c, r, o) : E_FAIL;
}

HRESULT WINAPI Proxy_DllRegisterServer() {
  typedef HRESULT(WINAPI * Fn)();
  Fn fn = (Fn)(void*)Real("DllRegisterServer");
  return fn ? fn() : E_FAIL;
}

HRESULT WINAPI Proxy_DllUnregisterServer() {
  typedef HRESULT(WINAPI * Fn)();
  Fn fn = (Fn)(void*)Real("DllUnregisterServer");
  return fn ? fn() : E_FAIL;
}

const void* WINAPI Proxy_GetdfDIJoystick() {
  typedef const void*(WINAPI * Fn)();
  Fn fn = (Fn)(void*)Real("GetdfDIJoystick");
  return fn ? fn() : nullptr;
}

}  // extern "C"
