#pragma once

// xphone-os — the built-in app registry: ONE table of every native app the
// firmware knows (id, launcher label, icon, scene, how to open it), and the
// build-time choice of which of them the launcher offers.
//
// Choosing apps at build time: -DXP_LAUNCHER_APPS='"today,read,notifications"'
// in platformio.ini (see the [apps] section there). A comma-separated list of
// registry ids, launcher order, at most six (LauncherScene::MAX_APPS). An
// app left out never gets a tile, never restores on wake, and its shortcuts
// (Deep Work long-press, the phone's reader.goto from the launcher) stand
// down. Its background companion sync keeps running — Priorities, Today and
// Block also feed the sleep poster, so their stores stay whatever the list.
//
// Adding a native app: write its Scene, add a SceneId (AppScenes.h) and a
// show*() helper (AppScenes.cpp), then add one row to kApps in
// AppRegistry.cpp. Put its id in XP_LAUNCHER_APPS to give it a tile.

#include <cstddef>
#include <cstdint>

#include "../scenes/AppScenes.h"

#ifndef XP_LAUNCHER_APPS
#define XP_LAUNCHER_APPS "today,notifications,priorities,block,read,workout"
#endif

namespace apps {

struct BuiltinApp {
  const char* id;     // stable id: XP_LAUNCHER_APPS, NVS "homeSlots", phone home.layout
  const char* label;  // launcher tile text
  int8_t icon;        // IconStyle pack column (0..5), -1 = monogram tile
  SceneId scene;      // for wake restore
  void (*open)();     // the show*() helper
};

// Every native app compiled into this firmware, enabled or not.
const BuiltinApp* find(const char* id);
const BuiltinApp* findByScene(SceneId scene);

// The apps XP_LAUNCHER_APPS enables, in its order (unknown ids and repeats
// are dropped with a serial warning; more than six are cut at six).
int enabledCount();
const BuiltinApp* enabledAt(int i);
bool enabled(const char* id);
bool sceneEnabled(SceneId scene);  // true for non-app scenes (Settings, About...)

// XP_LAUNCHER_APPS after filtering, as a csv: the default launcher slots.
const char* defaultSlotsCsv();

}  // namespace apps
