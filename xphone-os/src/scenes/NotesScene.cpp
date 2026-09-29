#include "NotesScene.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <cstring>
#include <new>

#include "../Fonts.h"
#include "AppScenes.h"

namespace {

constexpr int kMarginX = 20;
constexpr int kHeaderH = 46;
constexpr int kRowPad = 16;
constexpr int kMaxPages = 999;
constexpr const char* kPrefsNamespace = "notes";

// Clip `src` to `maxWidth`, keeping the END ("...Projects/Ideas") — for
// folder paths, whose last segment is the part that matters.
void clipLeft(Gfx& gfx, const XpFont& f, const char* src, const int maxWidth, char* dst, const size_t n) {
  snprintf(dst, n, "%s", src);
  if (gfx.textWidth(f, dst) <= maxWidth) return;
  const char* p = src;
  char buf[notes::kMaxPath + 4];
  while (*p) {
    p++;
    while ((static_cast<uint8_t>(*p) & 0xC0) == 0x80) p++;  // codepoint boundary
    snprintf(buf, sizeof(buf), "...%s", p);
    if (gfx.textWidth(f, buf) <= maxWidth) break;
  }
  snprintf(dst, n, "%s", buf);
}

// Clip `src` to `maxWidth`, keeping the START, ending in "...".
void clipRight(Gfx& gfx, const XpFont& f, const char* src, const int maxWidth, char* dst, const size_t n) {
  snprintf(dst, n, "%s", src);
  if (gfx.textWidth(f, dst) <= maxWidth) return;
  size_t len = strlen(dst);
  while (len > 0) {
    do len--;
    while (len > 0 && (static_cast<uint8_t>(dst[len]) & 0xC0) == 0x80);
    char buf[notes::kMaxPath + 4];
    snprintf(buf, sizeof(buf), "%.*s...", static_cast<int>(len), dst);
    if (gfx.textWidth(f, buf) <= maxWidth) {
      snprintf(dst, n, "%s", buf);
      return;
    }
  }
}

// A small folder glyph (tab + body), 20x15, top-left at (x, y).
void drawFolderGlyph(Gfx& gfx, const int x, const int y, const bool black) {
  gfx.fillRect(x, y, 9, 3, black);
  gfx.drawRect(x, y + 3, 20, 12, 2, black);
}

}  // namespace

void NotesScene::onEnter() {
  _message[0] = 0;
  _historyCount = 0;

  if (!SdMan.ready()) SdMan.begin();
  // Back where the person left off: a note (and page) or a folder.
  char path[notes::kMaxPath] = {0};
  uint16_t page = 0;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, /*readOnly=*/true)) {
    prefs.getString("path", path, sizeof(path));
    page = prefs.getUShort("page", 0);
    prefs.end();
  }
  if (notes::isNotePath(path) && SdMan.exists(path)) {
    char folder[notes::kMaxPath];
    notes::parentOf(path, folder, sizeof(folder));
    char name[notes::kMaxPath];
    const char* slash = strrchr(path, '/');
    snprintf(name, sizeof(name), "%s", slash ? slash + 1 : path);
    openFolder(folder, name);
    openNote(path, page);
  } else {
    openFolder(path[0] && SdMan.exists(path) ? path : notes::kVaultRoot, nullptr);
  }
}

void NotesScene::onExit() {
  _reader.close();
  releaseLayout();
  _pages.clear();
  _pages.shrink_to_fit();
  _entries.clear();
  _entries.shrink_to_fit();
}

void NotesScene::saveSpot() const {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, /*readOnly=*/false)) return;
  const bool inNote = _view != View::Browse;
  prefs.putString("path", inNote ? _note : _folder);
  prefs.putUShort("page", inNote ? static_cast<uint16_t>(_page) : 0);
  prefs.end();
}

void NotesScene::openFolder(const char* path, const char* selectName) {
  snprintf(_folder, sizeof(_folder), "%s", path);
  _folderOk = notes::listFolder(_folder, _entries);
  _sel = 0;
  _scroll = 0;
  if (selectName) {
    for (size_t i = 0; i < _entries.size(); i++)
      if (_entries[i].name == selectName) _sel = static_cast<int>(i);
  }
  _reader.close();
  releaseLayout();  // browsing holds no note memory
  _view = View::Browse;
  saveSpot();
  markDirty();
}

// The note engine's working memory (about 3.8 KB in pieces of 1 KB or
// less), claimed when a note opens and given back on the way out, so the
// rest of the device never pays for it.
bool NotesScene::acquireLayout() {
  if (_layout) return true;
  _layout = new (std::nothrow) notes::NoteLayout();
  if (_layout && _layout->init()) return true;
  releaseLayout();
  _largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  Serial.printf("[xphone-os] notes: no memory for a note (largest block %u B)\n",
                static_cast<unsigned>(_largestBlock));
  return false;
}

void NotesScene::releaseLayout() {
  delete _layout;
  _layout = nullptr;
}

void NotesScene::openNote(const char* path, const int page) {
  _reader.close();
  if (!_reader.open(path)) {
    snprintf(_message, sizeof(_message), "Could not open that note");
    markDirty();
    return;
  }
  snprintf(_note, sizeof(_note), "%s", path);
  acquireLayout();  // on failure the note view says so, with the numbers
  _pages.clear();  // paginated on the next render (needs the Gfx)
  _page = page;
  _linkSel = 0;
  _view = View::Note;
  saveSpot();
  markDirty();
}

void NotesScene::followLink(const int i) {
  char target[notes::kMaxPath];
  if (!_layout || !_layout->linkTarget(_reader, i, target, sizeof(target))) return;
  char path[notes::kMaxPath];
  if (!notes::resolveLink(_note, target, path, sizeof(path))) {
    snprintf(_message, sizeof(_message), "No note named \"%.40s\"", target);
    _view = View::Note;
    markDirty();
    return;
  }
  if (_historyCount == kHistory) {  // forget the oldest
    memmove(&_history[0], &_history[1], sizeof(Spot) * (kHistory - 1));
    _historyCount--;
  }
  Spot& s = _history[_historyCount++];
  snprintf(s.path, sizeof(s.path), "%s", _note);
  s.page = static_cast<uint16_t>(_page);
  openNote(path, 0);
}

void NotesScene::goBack() {
  if (_view == View::Links) {
    _view = View::Note;
    markDirty();
    return;
  }
  if (_view == View::Note) {
    if (_historyCount > 0) {
      const Spot& s = _history[--_historyCount];
      char path[notes::kMaxPath];
      snprintf(path, sizeof(path), "%s", s.path);
      openNote(path, s.page);
      return;
    }
    // Out to the note's own folder, with the note selected.
    char folder[notes::kMaxPath];
    notes::parentOf(_note, folder, sizeof(folder));
    const char* slash = strrchr(_note, '/');
    char name[notes::kMaxPath];
    snprintf(name, sizeof(name), "%s", slash ? slash + 1 : _note);
    openFolder(folder, name);
    return;
  }
  if (strcmp(_folder, notes::kVaultRoot) == 0 || !_folder[0]) {
    showLauncher();
    return;
  }
  char parent[notes::kMaxPath];
  notes::parentOf(_folder, parent, sizeof(parent));
  const char* slash = strrchr(_folder, '/');
  char name[notes::kMaxPath];
  snprintf(name, sizeof(name), "%s", slash ? slash + 1 : _folder);
  openFolder(parent, name);
}

void NotesScene::handleInput(Input& in) {
  const bool prev = in.wasPressed(Btn::Left) || in.wasPressed(Btn::Up);
  const bool next = in.wasPressed(Btn::Right) || in.wasPressed(Btn::Down);
  const bool back = in.wasPressed(Btn::Back);
  const bool confirm = in.wasPressed(Btn::Confirm);
  if ((prev || next || back || confirm) && _message[0]) {
    _message[0] = 0;
    markDirty();
  }
  if (back) {
    goBack();
    return;
  }
  switch (_view) {
    case View::Browse: {
      const int n = static_cast<int>(_entries.size());
      if (n > 0 && (prev || next)) {
        _sel = (_sel + (next ? 1 : n - 1)) % n;
        markDirty();
      }
      if (confirm && n > 0) {
        const notes::Entry& e = _entries[_sel];
        char path[notes::kMaxPath];
        snprintf(path, sizeof(path), "%s/%s", _folder, e.name.c_str());
        if (e.dir) openFolder(path, nullptr);
        else openNote(path, 0);
      }
      break;
    }
    case View::Note: {
      const int pages = static_cast<int>(_pages.size());
      if (next && _page + 1 < pages) {
        _page++;
        saveSpot();
        markDirty();
      } else if (prev && _page > 0) {
        _page--;
        saveSpot();
        markDirty();
      }
      if (confirm) {
        if (_layout && _layout->linkCount() > 0) {
          _view = View::Links;
          _linkSel = 0;
        } else {
          snprintf(_message, sizeof(_message), "No links on this page");
        }
        markDirty();
      }
      break;
    }
    case View::Links: {
      const int n = _layout ? _layout->linkCount() : 0;
      if (n > 0 && (prev || next)) {
        _linkSel = (_linkSel + (next ? 1 : n - 1)) % n;
        markDirty();
      }
      if (confirm && n > 0) followLink(_linkSel);
      break;
    }
  }
}

const char* const* NotesScene::softKeys() const {
  static constexpr const char* kBrowseKeys[4] = {"BACK", "OPEN", SoftKey::Up, SoftKey::Down};
  static constexpr const char* kNoteKeys[4] = {"BACK", "LINKS", SoftKey::Left, SoftKey::Right};
  static constexpr const char* kLinkKeys[4] = {"BACK", "GO", SoftKey::Left, SoftKey::Right};
  switch (_view) {
    case View::Note: return kNoteKeys;
    case View::Links: return kLinkKeys;
    default: return kBrowseKeys;
  }
}

int NotesScene::contentTop() const { return kHeaderH + 10; }
int NotesScene::contentBottom(const Gfx& gfx) const { return gfx.height() - Scene::SOFTKEY_BAR_H - 8; }

void NotesScene::drawHeader(Gfx& gfx, const char* title, const char* right) {
  const int rightW = (right && right[0]) ? gfx.textWidth(kFontRegular, right) + 12 : 0;
  char clipped[notes::kMaxPath + 4];
  clipRight(gfx, kFontBold, title, gfx.width() - 2 * kMarginX - rightW, clipped, sizeof(clipped));
  gfx.drawText(kFontBold, kMarginX, 8, clipped);
  if (rightW) gfx.drawText(kFontRegular, gfx.width() - kMarginX - (rightW - 12), 8, right);
  gfx.fillRect(0, kHeaderH - 2, gfx.width(), 2, true);
}

void NotesScene::render(Gfx& gfx) {
  if (_view == View::Browse) renderBrowse(gfx);
  else renderNote(gfx);
  if (_message[0]) {  // a one-shot notice in a black band above the soft keys
    const int h = gfx.lineHeight(kFontRegular) + 12;
    const int y = contentBottom(gfx) - h;
    gfx.fillRect(0, y, gfx.width(), h, true);
    gfx.drawTextCentered(kFontRegular, gfx.width() / 2, y + 6, _message, false);
  }
}

void NotesScene::renderBrowse(Gfx& gfx) {
  // Header: the folder's place in the vault ("Notes/Projects/Ideas").
  char title[notes::kMaxPath + 4];
  snprintf(title, sizeof(title), "Notes%s", _folder + strlen(notes::kVaultRoot));
  char clipped[notes::kMaxPath + 4];
  clipLeft(gfx, kFontBold, title, gfx.width() - 2 * kMarginX, clipped, sizeof(clipped));
  drawHeader(gfx, clipped, nullptr);

  const int top = contentTop();
  const int lh = gfx.lineHeight(kFontRegular);
  if (!_folderOk || _entries.empty()) {
    const char* lines[3] = {"No notes here yet.", "Copy your Obsidian vault", "into /notes on the SD card."};
    if (_folderOk) lines[1] = lines[2] = "";
    for (int i = 0; i < 3; i++) gfx.drawTextCentered(kFontRegular, gfx.width() / 2, top + 60 + i * lh, lines[i]);
    return;
  }

  const int rowH = gfx.lineHeight(kFontBold) + kRowPad;
  const int visible = (contentBottom(gfx) - top) / rowH;
  const int n = static_cast<int>(_entries.size());
  if (_sel < _scroll) _scroll = _sel;
  if (_sel >= _scroll + visible) _scroll = _sel - visible + 1;
  for (int r = 0; r < visible && _scroll + r < n; r++) {
    const int i = _scroll + r;
    const notes::Entry& e = _entries[i];
    const int y = top + r * rowH;
    const bool sel = i == _sel;
    if (sel) gfx.fillRect(0, y, gfx.width(), rowH, true);
    const int textY = y + (rowH - gfx.lineHeight(kFontBold)) / 2;
    int x = kMarginX;
    if (e.dir) {
      drawFolderGlyph(gfx, x, textY + 8, !sel);
      x += 30;
    }
    char name[notes::kMaxPath];
    notes::displayName(e.name.c_str(), name, sizeof(name));
    char clippedName[notes::kMaxPath + 4];
    clipRight(gfx, sel ? kFontBold : kFontRegular, name, gfx.width() - kMarginX - x, clippedName,
              sizeof(clippedName));
    gfx.drawText(sel ? kFontBold : kFontRegular, x, textY, clippedName, !sel);
  }
  // Scroll hint: "3/14" when the list runs past the screen.
  if (n > visible) {
    char pos[16];
    snprintf(pos, sizeof(pos), "%d/%d", _sel + 1, n);
    gfx.drawText(kFontSmall, gfx.width() - kMarginX - gfx.textWidth(kFontSmall, pos),
                 contentBottom(gfx) - gfx.lineHeight(kFontSmall) + 4, pos);
  }
}

void NotesScene::paginate(Gfx& gfx) {
  _pages.clear();
  notes::PageStart cur;
  _pages.push_back(cur);
  notes::PageStart next;
  while (static_cast<int>(_pages.size()) < kMaxPages &&
         _layout->page(gfx, _reader, cur, contentTop(), contentBottom(gfx), /*draw=*/false, -1, &next)) {
    if (next.offset == cur.offset && next.skip == cur.skip) break;  // no progress: stop, never spin
    _pages.push_back(next);
    cur = next;
  }
  if (_page >= static_cast<int>(_pages.size())) _page = static_cast<int>(_pages.size()) - 1;
  if (_page < 0) _page = 0;
}

void NotesScene::renderNote(Gfx& gfx) {
  char title[notes::kMaxPath];
  notes::displayName(_note, title, sizeof(title));
  if (!_layout && !acquireLayout()) {
    drawHeader(gfx, title, nullptr);
    char detail[48];
    snprintf(detail, sizeof(detail), "(largest free block: %u B)", static_cast<unsigned>(_largestBlock));
    const int lh = gfx.lineHeight(kFontRegular);
    gfx.drawTextCentered(kFontRegular, gfx.width() / 2, contentTop() + 60, "Not enough memory to open");
    gfx.drawTextCentered(kFontRegular, gfx.width() / 2, contentTop() + 60 + lh, "this note right now.");
    gfx.drawTextCentered(kFontSmall, gfx.width() / 2, contentTop() + 70 + 2 * lh, detail);
    return;
  }
  if (_pages.empty()) paginate(gfx);
  char pos[16];
  snprintf(pos, sizeof(pos), "%d/%d", _page + 1, static_cast<int>(_pages.size()));
  drawHeader(gfx, title, pos);
  _layout->page(gfx, _reader, _pages[_page], contentTop(), contentBottom(gfx), /*draw=*/true,
                _view == View::Links ? _linkSel : -1, nullptr);
  if (_view == View::Links && _linkSel >= _layout->linkCount()) _linkSel = 0;
}
