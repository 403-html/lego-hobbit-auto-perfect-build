// Auto Perfect Build - game-specific hooks for LEGO The Hobbit (Steam, DX9, x86).
//
// How the instruction-build minigame works in this executable (all names below
// are RTTI class names or our own descriptive labels; addresses are for the
// verified build with the default image base 0x00400000):
//
//   InstructionBuildPlayBubble (vtable 0x012F4EA4) owns the whole minigame.
//     +0x038  FlowController*   (+0x08 page, +0x09 current piece index, +0x1C pieces
//                                on page, +0x34 state: 2 = waiting for a pick)
//     +0x048  play-bubble state (0 init, 1 intro, 2 playing, 3 completed, 4 cancelled,
//                                5 leaving)
//     +0x204  RewardPot         (+0x54 max = 20000, +0x58 current, +0x5C min,
//                                +0x60 drain step 1000, +0x64 first-loss 6000)
//     +0x27C  WheelInputController
//     +0x24B8 piece wheel of player 1 (+0x34 for player 2); +0x08 enabled,
//             +0x10.. item pointers [8], +0x30 highlighted item
//     +0x2538 8 wheel slots of 0x68 bytes; slot+0x54 = index of the piece that slot
//             shows (decoys never match the current piece index)
//
//   0x00AEB1D0  WheelInputController::IsSelectPressed(player)   (thiscall, ret 4)
//   0x00B65C10  PlayBubble::OnSlotSelected(playerIdx, slot)      (thiscall, ret 8)
//               -> compares slot piece index with FlowController current piece:
//                  equal  => correct path (piece placed, build progresses)
//                  differ => RewardPot::Lose(1000) + "incorrect" feedback
//   0x00B4E910  RewardPot::Lose(amount)                          (thiscall, ret 4)
//               called on a wrong pick and by the pot's drain timer (2.5 s after
//               each new piece the pot starts draining)
//   0x00B4D800  PlayBubble::Complete()  awards the pot as studs (20000 = perfect)
//
// Auto Perfect therefore does three small things, all through call-site patches
// inside InstructionBuildPlayBubble code only:
//   1. when the game itself polls "select pressed" while a wheel is enabled, the
//      wheel is pointed at the correct slot and the press is reported;
//   2. any pick is redirected to the slot holding the correct piece, so the
//      game's own "correct" branch runs (animations, progression unchanged);
//   3. RewardPot::Lose is skipped, so no studs are lost to time or mistakes.
#include "game.h"

#include <string.h>
#include <windows.h>

#include "config.h"
#include "hook.h"
#include "log.h"

namespace apb {

extern Config g_cfg;

// ---------------------------------------------------------------------------
// Supported executable: LEGOHobbit.exe (Steam, DX9, x86)
//   size 30,191,408 bytes, SHA-256 072e25e2c0be8619d92acd4ae14319164d2ee1cdb1425a295ec2e0f6e2f5c998
//   PE TimeDateStamp 0x543C1BFA, SizeOfImage 0x01E72000, CheckSum 0x01CCE17D
// The exe is ASLR-enabled, so everything is stored as an RVA.
// ---------------------------------------------------------------------------
static const DWORD kTimeDateStamp = 0x543C1BFA;
static const DWORD kSizeOfImage = 0x01E72000;
static const DWORD kCheckSum = 0x01CCE17D;

#define RVA(va) ((uintptr_t)(va) - 0x00400000u)

static const uintptr_t kVtblPlayBubble = RVA(0x012F4EA4);
static const uintptr_t kFnIsSelectPressed = RVA(0x00AEB1D0);
static const uintptr_t kFnOnSlotSelected = RVA(0x00B65C10);
static const uintptr_t kFnPotLose = RVA(0x00B4E910);
static const uintptr_t kFnComplete = RVA(0x00B4D800);
static const uintptr_t kFnWheelSetSel = RVA(0x00ACA6E0);
static const uintptr_t kFnGetPlayer1 = RVA(0x00CD2490);
static const uintptr_t kFnAnimPlayTo = RVA(0x00757B70);    // (obj, float target, float speed)
static const uintptr_t kFnPBUpdate = RVA(0x00B88ED0);      // PlayBubble::Update(float dt)
static const uintptr_t kVtblSlotPBUpdate = kVtblPlayBubble + 7 * 4;
// Global "game is paused" byte. PlayBubble::Update itself starts with
// `cmp byte ptr [0x01A96E35], 0` (at 0x00B88F1A), which is used to verify it.
static const uintptr_t kPausedFlag = RVA(0x01A96E35);
static const uintptr_t kPausedFlagCheckInsn = RVA(0x00B88F1A);

struct CallSite {
  const char* name;
  uintptr_t site;    // RVA of the E8/E9 instruction
  uint8_t opcode;    // 0xE8 call / 0xE9 jmp
  uintptr_t target;  // RVA the instruction must currently point at
};

static const CallSite kSiteIsSel1 = {"IsSelectPressed#P1", RVA(0x00AEB2C3), 0xE8, kFnIsSelectPressed};
static const CallSite kSiteIsSel2 = {"IsSelectPressed#P2", RVA(0x00AEB337), 0xE8, kFnIsSelectPressed};
static const CallSite kSiteOnSlot = {"OnSlotSelected", RVA(0x00B8F49E), 0xE8, kFnOnSlotSelected};
static const CallSite kSitePotLoseWrong = {"PotLose#WrongPick", RVA(0x00B65CEF), 0xE8, kFnPotLose};
static const CallSite kSitePotLoseTimer = {"PotLose#Timer", RVA(0x00B66158), 0xE8, kFnPotLose};
static const CallSite kSiteComplete = {"Complete", RVA(0x00B65D4A), 0xE9, kFnComplete};
// FlowController plays the build animation up to the next piece's keyframe:
static const CallSite kSitePlayTo1 = {"AnimPlayTo#Advance", RVA(0x00B2F1E4), 0xE8, kFnAnimPlayTo};
static const CallSite kSitePlayTo2 = {"AnimPlayTo#Resume", RVA(0x00B76561), 0xE8, kFnAnimPlayTo};

// Offsets inside InstructionBuildPlayBubble (see header comment).
enum : uintptr_t {
  PB_Flow = 0x38,
  PB_State = 0x48,
  PB_Pot = 0x204,
  PB_Wic = 0x27C,
  PB_Wheel0 = 0x24B8,
  PB_WheelStride = 0x34,
  PB_Slots = 0x2538,
  PB_SlotStride = 0x68,
  SLOT_PieceIndex = 0x54,
  WHEEL_Enabled = 0x08,
  WHEEL_Items = 0x10,
  FLOW_Page = 0x08,
  FLOW_Piece = 0x09,
  FLOW_PieceCount = 0x1C,
  FLOW_State = 0x34,
  POT_Max = 0x54,
  POT_Cur = 0x58,
};

static uintptr_t g_base = 0;
static uintptr_t g_pausedFlag = 0;  // set only after verification
static Patch g_patches[16];
static int g_patchCount = 0;

template <class T>
static T& At(const void* p, uintptr_t off) {
  return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(p) + off);
}

// --- game functions (thiscall emulated with fastcall: ecx=this, edx unused) ---
typedef bool(__fastcall* IsSelectPressedFn)(void* wic, void* edx, void* player);
typedef void(__fastcall* OnSlotSelectedFn)(void* pb, void* edx, int playerIdx, unsigned slot);
typedef void(__fastcall* PotLoseFn)(void* pot, void* edx, int amount);
typedef void(__fastcall* CompleteFn)(void* pb, void* edx);
typedef void(__fastcall* WheelSetSelFn)(void* wheel, void* edx, unsigned slot, char show);
typedef void*(__cdecl* GetPlayerFn)();
typedef void(__fastcall* AnimPlayToFn)(void* obj, void* edx, float target, float speed);
typedef void(__fastcall* PBUpdateFn)(void* pb, void* edx, float dt);

static IsSelectPressedFn o_IsSelectPressed;
static OnSlotSelectedFn o_OnSlotSelected;
static PotLoseFn o_PotLose;
static CompleteFn o_Complete;
static WheelSetSelFn g_WheelSetSel;
static GetPlayerFn g_GetPlayer1;
static AnimPlayToFn o_AnimPlayTo;
static PBUpdateFn o_PBUpdate;

static volatile LONG g_mode = (LONG)Mode::AutoPerfect;
static Mode CurMode() { return (Mode)g_mode; }

static bool IsPlayBubble(const void* pb) {
  return pb && *reinterpret_cast<const uintptr_t*>(pb) == g_base + kVtblPlayBubble;
}

// Returns the wheel slot whose piece is the one the game currently expects, or -1.
static int FindCorrectSlot(void* pb, int playerIdx) {
  void* flow = At<void*>(pb, PB_Flow);
  if (!flow) return -1;
  int want = At<signed char>(flow, FLOW_Piece);
  void* wheel = reinterpret_cast<uint8_t*>(pb) + PB_Wheel0 + playerIdx * PB_WheelStride;
  for (int i = 0; i < 8; ++i) {
    if (!At<void*>(wheel, WHEEL_Items + 4 * i)) continue;  // empty wheel position
    if (At<int>(pb, PB_Slots + i * PB_SlotStride + SLOT_PieceIndex) == want) return i;
  }
  return -1;
}

static void LogSlots(void* pb) {
  char buf[160];
  int n = 0;
  for (int i = 0; i < 8; ++i)
    n += wsprintfA(buf + n, "%d ", At<int>(pb, PB_Slots + i * PB_SlotStride + SLOT_PieceIndex));
  Log("    slot piece indices: %s", buf);
}

// --- 1. synthesized press ---------------------------------------------------
static DWORD g_lastAutoPick = 0;
static DWORD g_enabledSince = 0;
static bool g_wasEnabled = false;

static bool __fastcall Hk_IsSelectPressed(void* wic, void* edx, void* player) {
  bool pressed = o_IsSelectPressed(wic, edx, player);
  Mode mode = CurMode();
  if (mode == Mode::Vanilla) return pressed;

  void* pb = reinterpret_cast<uint8_t*>(wic) - PB_Wic;
  if (!IsPlayBubble(pb)) return pressed;

  int idx = (player == g_GetPlayer1()) ? 0 : 1;
  void* wheel = reinterpret_cast<uint8_t*>(pb) + PB_Wheel0 + idx * PB_WheelStride;
  bool enabled = At<char>(wheel, WHEEL_Enabled) != 0;
  bool anyEnabled = At<char>(pb, PB_Wheel0 + WHEEL_Enabled) != 0 ||
                    At<char>(pb, PB_Wheel0 + PB_WheelStride + WHEEL_Enabled) != 0;
  DWORD now = GetTickCount();
  if (!anyEnabled) {
    g_wasEnabled = false;
    return pressed;
  }
  if (!g_wasEnabled) {
    g_wasEnabled = true;
    g_enabledSince = now;
  }
  if (!enabled || pressed) return pressed;  // a real press is redirected anyway

  DWORD delay = (mode == Mode::Instant) ? 0 : g_cfg.autoDelayMs;
  if (now - g_enabledSince < delay) return false;
  if (now - g_lastAutoPick < (delay > 100 ? delay : 100)) return false;

  int slot = FindCorrectSlot(pb, idx);
  if (slot < 0) {
    void* flow = At<void*>(pb, PB_Flow);
    Log("  auto-pick: no slot holds piece %d (page %d) - leaving input to player",
        flow ? At<signed char>(flow, FLOW_Piece) : -99, flow ? At<signed char>(flow, FLOW_Page) : -99);
    LogSlots(pb);
    g_lastAutoPick = now;
    return false;
  }
  g_WheelSetSel(wheel, nullptr, (unsigned)slot, 1);  // what turning the stick does
  g_lastAutoPick = now;
  void* flow = At<void*>(pb, PB_Flow);
  Log("  auto-pick: player %d page %d piece %d/%d -> slot %d", idx + 1,
      At<signed char>(flow, FLOW_Page), At<signed char>(flow, FLOW_Piece) + 1,
      At<int>(flow, FLOW_PieceCount), slot);
  return true;
}

// --- 2. correct-slot redirect ----------------------------------------------
static void __fastcall Hk_OnSlotSelected(void* pb, void* edx, int playerIdx, unsigned slot) {
  Mode mode = CurMode();
  if (mode != Mode::Vanilla && IsPlayBubble(pb) && (playerIdx == 0 || playerIdx == 1)) {
    int correct = FindCorrectSlot(pb, playerIdx);
    if (correct >= 0 && (unsigned)correct != slot) {
      Log("  pick redirected: slot %u -> %d", slot, correct);
      slot = (unsigned)correct;
    } else if (correct < 0) {
      Log("  pick: correct slot not found, passing slot %u unchanged", slot);
      LogSlots(pb);
    }
  }
  void* flow = IsPlayBubble(pb) ? At<void*>(pb, PB_Flow) : nullptr;
  int wantBefore = flow ? At<signed char>(flow, FLOW_Piece) : -99;
  int slotPiece = (slot < 8) ? At<int>(pb, PB_Slots + slot * PB_SlotStride + SLOT_PieceIndex) : -99;
  o_OnSlotSelected(pb, edx, playerIdx, slot);
  Log("  OnSlotSelected(player %d, slot %u): slot piece %d, expected %d -> %s, pot %d", playerIdx + 1,
      slot, slotPiece, wantBefore, slotPiece == wantBefore ? "CORRECT" : "WRONG",
      At<int>(pb, PB_Pot + POT_Cur));
}

// --- 3. no stud loss ---------------------------------------------------------
static void __fastcall Hk_PotLose(void* pot, void* edx, int amount) {
  if (CurMode() != Mode::Vanilla) {
    static DWORD lastLog = 0;
    DWORD now = GetTickCount();
    if (now - lastLog > 1000) {
      Log("  pot loss of %d suppressed (pot %d/%d)", amount, At<int>(pot, POT_Cur), At<int>(pot, POT_Max));
      lastLog = now;
    }
    return;
  }
  o_PotLose(pot, edx, amount);
  Log("  pot lost %d (vanilla) -> %d", amount, At<int>(pot, POT_Cur));
}

// --- observation only: completion ------------------------------------------
static void __fastcall Hk_Complete(void* pb, void* edx) {
  if (IsPlayBubble(pb))
    Log("BUILD COMPLETE: reward pot %d / %d (mode %s)", At<int>(pb, PB_Pot + POT_Cur),
        At<int>(pb, PB_Pot + POT_Max), ModeName(CurMode()));
  o_Complete(pb, edx);
}

// --- Instant: fast-forward the build sequence -------------------------------
// The world-side build only completes once the FlowController has walked every
// page and reached its finished state, so Instant does not jump past the flow:
// it removes the wheel (zero-delay correct picks, see Hk_IsSelectPressed) and runs
// the build animation and the play bubble's timers InstantSpeed times faster.
static void __fastcall Hk_AnimPlayTo(void* obj, void* edx, float target, float speed) {
  if (CurMode() == Mode::Instant) speed *= (float)g_cfg.instantSpeed;
  o_AnimPlayTo(obj, edx, target, speed);
}

static void __fastcall Hk_PBUpdate(void* pb, void* edx, float dt) {
  if (CurMode() == Mode::Instant && dt > 0.0f) {
    float fast = dt * (float)g_cfg.instantSpeed;
    dt = fast > 0.25f ? (dt > 0.25f ? dt : 0.25f) : fast;  // never feed huge steps
  }
  o_PBUpdate(pb, edx, dt);
}

// ---------------------------------------------------------------------------
static bool IdentifyExecutable() {
  HMODULE exe = GetModuleHandleW(nullptr);
  g_base = reinterpret_cast<uintptr_t>(exe);
  auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(exe);
  auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(g_base + dos->e_lfanew);
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  char p8[MAX_PATH * 2];
  WideCharToMultiByte(CP_UTF8, 0, path, -1, p8, sizeof(p8), nullptr, nullptr);
  Log("Host: %s base=%08X machine=%04X stamp=%08X image=%08X checksum=%08X", p8,
      (unsigned)g_base, nt->FileHeader.Machine, nt->FileHeader.TimeDateStamp,
      nt->OptionalHeader.SizeOfImage, nt->OptionalHeader.CheckSum);
  return nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
         nt->FileHeader.TimeDateStamp == kTimeDateStamp &&
         nt->OptionalHeader.SizeOfImage == kSizeOfImage &&
         nt->OptionalHeader.CheckSum == kCheckSum;
}

static bool SiteMatches(const CallSite& cs) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(g_base + cs.site);
  int32_t rel = *reinterpret_cast<const int32_t*>(p + 1);
  return p[0] == cs.opcode && cs.site + 5 + rel == cs.target;
}

static bool PatchSite(const CallSite& cs, void* detour) {
  uint8_t expected[5], repl[5];
  memcpy(expected, reinterpret_cast<void*>(g_base + cs.site), 5);
  repl[0] = cs.opcode;
  *reinterpret_cast<int32_t*>(repl + 1) =
      static_cast<int32_t>(reinterpret_cast<uintptr_t>(detour) - (g_base + cs.site + 5));
  return InstallBytePatch(g_patches[g_patchCount++], cs.name, g_base + cs.site, expected, repl, 5);
}

static bool PatchVtableSlot() {
  uint32_t expected = (uint32_t)(g_base + kFnPBUpdate);
  uint32_t repl = (uint32_t)reinterpret_cast<uintptr_t>(&Hk_PBUpdate);
  return InstallBytePatch(g_patches[g_patchCount++], "PlayBubble::Update(vtable)",
                          g_base + kVtblSlotPBUpdate, reinterpret_cast<uint8_t*>(&expected),
                          reinterpret_cast<uint8_t*>(&repl), 4);
}

uintptr_t GamePausedFlagAddress() { return g_pausedFlag; }

static void VerifyPausedFlag() {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(g_base + kPausedFlagCheckInsn);
  uint32_t addr = *reinterpret_cast<const uint32_t*>(p + 2);
  if (p[0] == 0x80 && p[1] == 0x3D && p[6] == 0x00 && addr == g_base + kPausedFlag) {
    g_pausedFlag = g_base + kPausedFlag;
    Log("  pause flag verified at %08X", (unsigned)g_pausedFlag);
  } else {
    Log("  pause flag check failed - Mods panel will not appear");
  }
}

void SetMode(Mode m) {
  InterlockedExchange(&g_mode, (LONG)m);
  Log("Mode -> %s", ModeName(m));
}

Mode GetMode() { return CurMode(); }

bool GameInstall() {
  if (!IdentifyExecutable()) {
    Log("Unsupported executable (expected Steam LEGOHobbit.exe DX9 build).");
    return false;
  }
  Log("Executable verified.");

  // All sites are checked before anything is written: either every patch goes in
  // or none does.
  const CallSite* sites[] = {&kSiteIsSel1,       &kSiteIsSel2,       &kSiteOnSlot,
                             &kSitePotLoseWrong, &kSitePotLoseTimer, &kSiteComplete,
                             &kSitePlayTo1,      &kSitePlayTo2};
  for (const CallSite* cs : sites) {
    if (!SiteMatches(*cs)) {
      Log("  [%s] expected instruction not found at RVA %08X - nothing patched", cs->name,
          (unsigned)cs->site);
      return false;
    }
  }
  if (*reinterpret_cast<const uint32_t*>(g_base + kVtblPlayBubble) != g_base + RVA(0x00B651D0)) {
    Log("  InstructionBuildPlayBubble vtable not where expected - nothing patched");
    return false;
  }
  if (*reinterpret_cast<const uint32_t*>(g_base + kVtblSlotPBUpdate) != g_base + kFnPBUpdate) {
    Log("  PlayBubble::Update vtable slot not as expected - nothing patched");
    return false;
  }

  o_IsSelectPressed = reinterpret_cast<IsSelectPressedFn>(g_base + kFnIsSelectPressed);
  o_OnSlotSelected = reinterpret_cast<OnSlotSelectedFn>(g_base + kFnOnSlotSelected);
  o_PotLose = reinterpret_cast<PotLoseFn>(g_base + kFnPotLose);
  o_Complete = reinterpret_cast<CompleteFn>(g_base + kFnComplete);
  g_WheelSetSel = reinterpret_cast<WheelSetSelFn>(g_base + kFnWheelSetSel);
  g_GetPlayer1 = reinterpret_cast<GetPlayerFn>(g_base + kFnGetPlayer1);
  o_AnimPlayTo = reinterpret_cast<AnimPlayToFn>(g_base + kFnAnimPlayTo);
  o_PBUpdate = reinterpret_cast<PBUpdateFn>(g_base + kFnPBUpdate);
  g_mode = (LONG)g_cfg.mode;

  bool ok = PatchSite(kSiteIsSel1, (void*)&Hk_IsSelectPressed) &&
            PatchSite(kSiteIsSel2, (void*)&Hk_IsSelectPressed) &&
            PatchSite(kSiteOnSlot, (void*)&Hk_OnSlotSelected) &&
            PatchSite(kSitePotLoseWrong, (void*)&Hk_PotLose) &&
            PatchSite(kSitePotLoseTimer, (void*)&Hk_PotLose) &&
            PatchSite(kSiteComplete, (void*)&Hk_Complete) &&
            PatchSite(kSitePlayTo1, (void*)&Hk_AnimPlayTo) &&
            PatchSite(kSitePlayTo2, (void*)&Hk_AnimPlayTo) && PatchVtableSlot();
  if (!ok) {
    Log("Patching failed - restoring everything.");
    GameUninstall();
    return false;
  }
  VerifyPausedFlag();
  Log("All %d patches installed. Mode=%s", g_patchCount, ModeName(CurMode()));
  return true;
}

void GameUninstall() {
  for (int i = g_patchCount - 1; i >= 0; --i) RemovePatch(g_patches[i]);
  g_patchCount = 0;
}

}  // namespace apb
