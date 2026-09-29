#include "AppRegistry.h"

#include <Arduino.h>

#include <cstdio>
#include <cstring>

namespace apps {
namespace {

// Icon columns follow the XPhoneIconPacks order in art/LauncherIcons.h:
// Today, Notifications, Priorities, Block, Read, Workout.
constexpr BuiltinApp kApps[] = {
    {"today", "Today", 0, SceneId::Today, showToday},
    {"notifications", "Notifications", 1, SceneId::Notifications, showNotifications},
    {"priorities", "Priorities", 2, SceneId::Priorities, showPriorities},
    {"block", "Block", 3, SceneId::Block, showBlock},
    {"read", "Read", 4, SceneId::Reader, showReader},
    {"workout", "Workout", 5, SceneId::Workout, showWorkout},
    {"notes", "Notes", -1, SceneId::Notes, showNotes},
};
constexpr int kMaxEnabled = 6;  // LauncherScene::MAX_APPS

const BuiltinApp* gEnabled[kMaxEnabled];
int gEnabledCount = -1;  // -1 = XP_LAUNCHER_APPS not parsed yet
char gDefaultCsv[128];

void parse() {
  if (gEnabledCount >= 0) return;
  gEnabledCount = 0;
  gDefaultCsv[0] = 0;
  char list[sizeof(XP_LAUNCHER_APPS)];
  snprintf(list, sizeof(list), "%s", XP_LAUNCHER_APPS);
  char* save = nullptr;
  for (char* tok = strtok_r(list, ", ", &save); tok; tok = strtok_r(nullptr, ", ", &save)) {
    const BuiltinApp* app = find(tok);
    if (!app) {
      Serial.printf("[xphone-os] apps: XP_LAUNCHER_APPS names unknown app \"%s\"; skipped\n", tok);
      continue;
    }
    bool dup = false;
    for (int i = 0; i < gEnabledCount; i++) dup = dup || gEnabled[i] == app;
    if (dup) continue;
    if (gEnabledCount == kMaxEnabled) {
      Serial.printf("[xphone-os] apps: XP_LAUNCHER_APPS has more than %d apps; \"%s\" skipped\n",
                    kMaxEnabled, tok);
      continue;
    }
    gEnabled[gEnabledCount++] = app;
    const size_t len = strlen(gDefaultCsv);
    snprintf(gDefaultCsv + len, sizeof(gDefaultCsv) - len, "%s%s", len ? "," : "", app->id);
  }
  if (gEnabledCount == 0) {
    // A launcher with no tiles is a dead end: take the registry order.
    Serial.println("[xphone-os] apps: XP_LAUNCHER_APPS enables nothing; using every built-in app");
    for (const BuiltinApp& a : kApps) {
      if (gEnabledCount == kMaxEnabled) break;
      gEnabled[gEnabledCount++] = &a;
      const size_t len = strlen(gDefaultCsv);
      snprintf(gDefaultCsv + len, sizeof(gDefaultCsv) - len, "%s%s", len ? "," : "", a.id);
    }
  }
}

}  // namespace

const BuiltinApp* find(const char* id) {
  if (!id) return nullptr;
  for (const BuiltinApp& a : kApps)
    if (!strcmp(a.id, id)) return &a;
  return nullptr;
}

const BuiltinApp* findByScene(const SceneId scene) {
  for (const BuiltinApp& a : kApps)
    if (a.scene == scene) return &a;
  return nullptr;
}

int enabledCount() {
  parse();
  return gEnabledCount;
}

const BuiltinApp* enabledAt(const int i) {
  parse();
  return (i >= 0 && i < gEnabledCount) ? gEnabled[i] : nullptr;
}

bool enabled(const char* id) {
  parse();
  const BuiltinApp* app = find(id);
  for (int i = 0; app && i < gEnabledCount; i++)
    if (gEnabled[i] == app) return true;
  return false;
}

bool sceneEnabled(const SceneId scene) {
  const BuiltinApp* app = findByScene(scene);
  return !app || enabled(app->id);
}

const char* defaultSlotsCsv() {
  parse();
  return gDefaultCsv;
}

}  // namespace apps
