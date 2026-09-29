#pragma once

// xphone-os — launcher: status bar + a grid of up to six apps, two per row
// (which built-in apps, and their order, is a build choice: XP_LAUNCHER_APPS,
// apps/AppRegistry.h). Settings opens from the BACK soft-key; About lives in
// Settings. Icon bitmaps come from IconStyle (multi-pack).

#include "../Scene.h"

class LauncherScene : public Scene {
 public:
  void onEnter() override;  // reloads the slots from NVS
  void handleInput(Input& in) override;
  void render(Gfx& gfx) override;
  const char* const* softKeys() const override;  // [gear] / OPEN / PREV / NEXT
  uint8_t softKeyIconMask() const override { return 0x01; }  // Settings = gear tab

  static constexpr int COLS = 2;
  static constexpr int MAX_APPS = 6;
  int appCount() const { return _count; }

  // Bench dev console ("where"): expose the grid selection so remote
  // navigation is fact-based, never dead reckoning.
  int selection() const { return _sel; }

  // Phase 3: the slots come from NVS ("homeSlots" csv: builtin ids or
  // installed app names), limited to the built-in apps this build enables.
  // applyHomeConfigLive() calls this after a home.layout card.
  void loadSlots();

 private:
  struct Slot {
    char id[32];      // builtin id or app dir name
    char label[20];   // what the tile says
    int8_t icon;      // LauncherIcons index, -1 = installed app (monogram)
  };
  Slot _slots[MAX_APPS];
  int _count = 0;  // tiles in use, 1..MAX_APPS
  bool _slotsLoaded = false;

  void moveSelection(int dCol, int dRow);
  // Logical rect of grid cell i (tile + label, small slop), from the layout
  // cached by render(). Empty rect until the first render.
  XpRect cellRect(int i) const;
  // Top-left of grid cell i from the cached layout. A lone tile on the last
  // row sits centered rather than under the left column.
  void cellOrigin(int i, int& x, int& y) const;

  int _sel = 0;  // persists across scene switches (static instance)

  // M2.1a: grid layout cached by render() so moveSelection() can dirty only
  // the two affected cells (soft-key labels are static on the launcher).
  int16_t _gridX = 0, _gridY = 0, _side = 0, _cellH = 0;
};
