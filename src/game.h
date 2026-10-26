// Auto Perfect Build - game-specific part (LEGO The Hobbit, Steam, DX9/x86 exe).
#pragma once

#include "config.h"

// Release builds pass the GitHub release tag (without "v") via CMake -DAPB_VERSION=...
#ifndef APB_VERSION
#define APB_VERSION "0.3.3"
#endif

namespace apb {

// Identifies the running executable and installs the hooks. Returns false (and
// patches nothing) for any executable other than the verified build.
bool GameInstall();
void GameUninstall();

// Runtime mode switch (hotkey).
void SetMode(Mode m);
Mode GetMode();

}  // namespace apb
