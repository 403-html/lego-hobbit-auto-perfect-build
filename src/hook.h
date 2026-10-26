// Auto Perfect Build - minimal, version-checked x86 inline hooks.
//
// Every hook site is described by the exact bytes we expect to find there in the
// one supported executable. If any byte differs (other game version, another mod
// already patched it, ...), the hook is NOT installed. The stolen prologue bytes
// must be position-independent (no rel8/rel32 branches, no absolute addresses),
// which is verified offline per site during reverse engineering.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace apb {

struct Patch {
  const char* name;
  uintptr_t address;       // absolute address in the running process
  size_t length;           // number of bytes replaced
  uint8_t original[32];    // bytes saved before patching
  void* trampoline;        // for hooks: executable copy of the stolen bytes + jmp back
  bool active;
};

// Compares `len` bytes at `addr` against `expected`. `mask` may be null; where a
// mask byte is 0 the byte is ignored (used for relocated absolute addresses).
bool BytesMatch(uintptr_t addr, const uint8_t* expected, const uint8_t* mask, size_t len);

// Installs a 5-byte JMP from `addr` to `detour`. `len` (>=5) bytes are stolen and
// copied to a trampoline that resumes at addr+len. Returns false (and changes
// nothing) if the bytes don't match `expected`.
bool InstallHook(Patch& p, const char* name, uintptr_t addr, const uint8_t* expected, size_t len,
                 void* detour);

// Overwrites `len` bytes with `replacement` after verifying `expected`.
bool InstallBytePatch(Patch& p, const char* name, uintptr_t addr, const uint8_t* expected,
                      const uint8_t* replacement, size_t len);

// Finds the import address table slot of `mod` for dll!name (or dll!#ordinal when
// name is null). Returns null if the module does not import it.
void** FindIatSlot(void* mod, const char* dll, const char* name, unsigned short ordinal);

// Replaces a pointer (IAT slot / vtable entry) after checking its current value.
bool InstallPointerPatch(Patch& p, const char* name, void** slot, void* expected, void* repl);

// Restores the original bytes (trampoline memory is intentionally leaked so any
// thread still inside it can return safely).
void RemovePatch(Patch& p);

}  // namespace apb
