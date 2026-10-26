// Auto Perfect Build - in-game "Mods" panel (model + navigation).
//
// The panel is drawn by overlay_d3d9.cpp while the game is paused. It is
// navigated with the keyboard or a controller; input reaches it through the
// game's own DirectInput / XInput polling (input_hooks.cpp), and while the panel
// has focus that input is hidden from the game.
#pragma once
#include <windows.h>

namespace apb {
namespace ui {

void Init();  // call once from DllMain before any hook can fire

enum class Nav { None, Toggle, Close, Up, Down, Left, Right, Accept };

bool GamePaused();          // the game's own pause flag
bool Visible();             // panel drawn this frame
bool Focused();             // panel owns the input
void HandleNav(Nav n);      // called by the input hooks (edge-triggered)

// Snapshot used by the renderer. Lines are UTF-16; `selected` is -1 when the
// panel is collapsed. `serial` changes whenever the text changes.
struct View {
  int lineCount;
  wchar_t lines[8][96];
  int selected;       // index into lines of the highlighted row, or -1
  int firstRow;       // first line that is a setting row
  unsigned serial;
};
void GetView(View& v);

// Called once per frame by the renderer (drops focus when the game unpauses).
void Tick();

}  // namespace ui
}  // namespace apb
