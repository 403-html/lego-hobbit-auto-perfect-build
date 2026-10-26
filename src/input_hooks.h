#pragma once

namespace apb {

// Called by the dinput8 proxy with the IDirectInput8 object the game created.
void InputOnDirectInputCreated(void* directInput);
// Hooks the game's XInputGetState import. Call from DllMain.
bool InputInstallXInput();
void InputUninstall();

}  // namespace apb
