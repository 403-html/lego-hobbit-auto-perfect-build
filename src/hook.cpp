#include "hook.h"

#include <string.h>
#include <windows.h>

#include "log.h"

namespace apb {

bool BytesMatch(uintptr_t addr, const uint8_t* expected, const uint8_t* mask, size_t len) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(addr);
  for (size_t i = 0; i < len; ++i) {
    if (mask && !mask[i]) continue;
    if (p[i] != expected[i]) return false;
  }
  return true;
}

static void LogMismatch(const char* name, uintptr_t addr, size_t len) {
  char hex[3 * 32 + 1] = {0};
  const uint8_t* p = reinterpret_cast<const uint8_t*>(addr);
  for (size_t i = 0; i < len && i < 32; ++i) wsprintfA(hex + 3 * i, "%02X ", p[i]);
  Log("  [%s] byte mismatch at %08X: found %s", name, (unsigned)addr, hex);
}

static bool WriteCode(uintptr_t addr, const void* src, size_t len) {
  DWORD old;
  if (!VirtualProtect(reinterpret_cast<void*>(addr), len, PAGE_EXECUTE_READWRITE, &old))
    return false;
  memcpy(reinterpret_cast<void*>(addr), src, len);
  VirtualProtect(reinterpret_cast<void*>(addr), len, old, &old);
  FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(addr), len);
  return true;
}

bool InstallHook(Patch& p, const char* name, uintptr_t addr, const uint8_t* expected, size_t len,
                 void* detour) {
  memset(&p, 0, sizeof(p));
  p.name = name;
  if (len < 5 || len > sizeof(p.original)) return false;
  if (!BytesMatch(addr, expected, nullptr, len)) {
    LogMismatch(name, addr, len);
    return false;
  }
  uint8_t* tramp = static_cast<uint8_t*>(
      VirtualAlloc(nullptr, len + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (!tramp) return false;
  memcpy(tramp, reinterpret_cast<void*>(addr), len);
  tramp[len] = 0xE9;
  *reinterpret_cast<int32_t*>(tramp + len + 1) =
      static_cast<int32_t>((addr + len) - (reinterpret_cast<uintptr_t>(tramp) + len + 5));

  uint8_t jmp[32];
  memset(jmp, 0x90, sizeof(jmp));
  jmp[0] = 0xE9;
  *reinterpret_cast<int32_t*>(jmp + 1) =
      static_cast<int32_t>(reinterpret_cast<uintptr_t>(detour) - (addr + 5));

  memcpy(p.original, reinterpret_cast<void*>(addr), len);
  p.address = addr;
  p.length = len;
  p.trampoline = tramp;
  if (!WriteCode(addr, jmp, len)) return false;
  p.active = true;
  Log("  [%s] hooked at %08X", name, (unsigned)addr);
  return true;
}

bool InstallBytePatch(Patch& p, const char* name, uintptr_t addr, const uint8_t* expected,
                      const uint8_t* replacement, size_t len) {
  memset(&p, 0, sizeof(p));
  p.name = name;
  if (len > sizeof(p.original)) return false;
  if (!BytesMatch(addr, expected, nullptr, len)) {
    LogMismatch(name, addr, len);
    return false;
  }
  memcpy(p.original, reinterpret_cast<void*>(addr), len);
  p.address = addr;
  p.length = len;
  if (!WriteCode(addr, replacement, len)) return false;
  p.active = true;
  Log("  [%s] patched %u bytes at %08X", name, (unsigned)len, (unsigned)addr);
  return true;
}

void** FindIatSlot(void* mod, const char* dll, const char* name, unsigned short ordinal) {
  uintptr_t base = reinterpret_cast<uintptr_t>(mod);
  auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!dir.VirtualAddress) return nullptr;
  for (auto imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name;
       ++imp) {
    if (lstrcmpiA(reinterpret_cast<const char*>(base + imp->Name), dll) != 0) continue;
    auto thunkName = reinterpret_cast<IMAGE_THUNK_DATA*>(
        base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
    auto thunkAddr = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
    for (; thunkName->u1.AddressOfData; ++thunkName, ++thunkAddr) {
      if (IMAGE_SNAP_BY_ORDINAL(thunkName->u1.Ordinal)) {
        if (!name && IMAGE_ORDINAL(thunkName->u1.Ordinal) == ordinal)
          return reinterpret_cast<void**>(&thunkAddr->u1.Function);
      } else if (name) {
        auto ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + thunkName->u1.AddressOfData);
        if (!lstrcmpA(reinterpret_cast<const char*>(ibn->Name), name))
          return reinterpret_cast<void**>(&thunkAddr->u1.Function);
      }
    }
  }
  return nullptr;
}

bool InstallPointerPatch(Patch& p, const char* name, void** slot, void* expected, void* repl) {
  return InstallBytePatch(p, name, reinterpret_cast<uintptr_t>(slot),
                          reinterpret_cast<const uint8_t*>(&expected),
                          reinterpret_cast<const uint8_t*>(&repl), sizeof(void*));
}

void RemovePatch(Patch& p) {
  if (!p.active) return;
  WriteCode(p.address, p.original, p.length);
  p.active = false;
  Log("  [%s] restored", p.name);
}

}  // namespace apb
