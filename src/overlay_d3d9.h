#pragma once

namespace apb {

// Installs the Direct3D 9 hooks used to draw the Mods panel. Must run before the
// game creates its Direct3D object (i.e. from DllMain). Returns false (and does
// nothing) if the import looks different from what is expected.
bool OverlayInstall();
void OverlayUninstall();

}  // namespace apb
