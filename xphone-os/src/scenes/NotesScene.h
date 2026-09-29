#pragma once

// xphone-os — Notes: a read-only viewer for an Obsidian vault mirrored to
// the SD card under /notes (notes/NotesVault.h), folder tree preserved.
//
// Browse: the current folder's folders then notes. OPEN enters, BACK goes
//   up a folder (out to the launcher from the vault root).
// Note: paged Markdown (notes/NoteLayout.h). Side keys and the arrow tabs
//   turn pages. LINKS enters link mode: the arrows step through the links
//   on this page, GO opens the linked note. BACK walks back through the
//   notes you followed links from, then out to the note's folder.
// The folder or note (and page) on glass is remembered in NVS ("notes").

#include <vector>

#include "../Scene.h"
#include "../notes/NoteLayout.h"
#include "../notes/NotesVault.h"

class NotesScene : public Scene {
 public:
  void onEnter() override;
  void onExit() override;
  void handleInput(Input& in) override;
  void render(Gfx& gfx) override;
  const char* const* softKeys() const override;

 private:
  enum class View : uint8_t { Browse, Note, Links };

  void openFolder(const char* path, const char* selectName);
  void openNote(const char* path, int page);
  void followLink(int i);
  void goBack();
  void saveSpot() const;
  bool acquireLayout();
  void releaseLayout();
  void paginate(Gfx& gfx);
  void renderBrowse(Gfx& gfx);
  void renderNote(Gfx& gfx);
  void drawHeader(Gfx& gfx, const char* title, const char* right);
  int contentTop() const;
  int contentBottom(const Gfx& gfx) const;

  View _view = View::Browse;
  char _folder[notes::kMaxPath] = {0};
  std::vector<notes::Entry> _entries;
  bool _folderOk = false;
  int _sel = 0, _scroll = 0;

  char _note[notes::kMaxPath] = {0};
  std::vector<notes::PageStart> _pages;  // empty = not paginated yet
  int _page = 0;
  int _linkSel = 0;

  // Notes the person followed links from (BACK returns to them).
  struct Spot {
    char path[notes::kMaxPath];
    uint16_t page;
  };
  static constexpr int kHistory = 8;
  Spot _history[kHistory];
  int _historyCount = 0;

  notes::NoteLayout* _layout = nullptr;  // ~3.8 KB, only while a note is open
  size_t _largestBlock = 0;              // heap diagnostic from the last failed claim
  notes::LineReader _reader;
  char _message[64] = {0};  // one-shot notice ("No note named ...")
};
