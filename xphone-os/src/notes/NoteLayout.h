#pragma once

// xphone-os — Notes: Obsidian-flavoured Markdown laid out on the UI fonts.
//
// Streaming and page-at-a-time: a page is drawn by reading the note from its
// PageStart (a file offset plus how many wrapped lines of that source line
// the previous page already showed), so no note is ever held in RAM whole.
// Pagination is the same pass with drawing off, run once per opened note.
//
// Read-only subset: headings, paragraphs (every source newline is a line
// break, Obsidian's default), bullet / numbered / task lists with nesting,
// quotes and callouts, fenced code, tables (as plain rows), rules, **bold**,
// `code`, [[wikilinks]] and [markdown](links). Italic, strikethrough,
// highlight and %%comments%% markers are stripped (the UI fonts have no
// italic face). YAML frontmatter is hidden. Characters the UI fonts lack
// (emoji, most symbols) are dropped or mapped to ASCII look-alikes.

#include <cstddef>
#include <cstdint>

#include "../Gfx.h"
#include "NotesVault.h"

namespace notes {

struct PageStart {
  uint32_t offset = 0;  // file offset of the first source line
  uint16_t skip = 0;    // wrapped lines of that source line already shown
  uint8_t inCode = 0;   // inside a ``` fence at that line
  uint8_t inFront = 0;  // inside the YAML frontmatter at that line
};

// Memory (X3: the heap is fragmented, so no request here is over 1 KB):
// three 1 KB line buffers, each allocated on its own by init(), plus about
// 750 B of fixed fields in the object. Link targets are not copied: a page
// keeps each target's position in the note and linkTarget() reads it back.
class NoteLayout {
 public:
  static constexpr size_t kLineCap = 1024;  // longer source lines are cut
  static constexpr int kMaxPageLinks = 24;

  NoteLayout() = default;
  NoteLayout(const NoteLayout&) = delete;
  NoteLayout& operator=(const NoteLayout&) = delete;
  ~NoteLayout();
  // Claim the line buffers. False (nothing held) when any allocation fails.
  bool init();

  // Lay out one page between y = top and y = bottom (logical px). Draws when
  // `draw`; `selLink` is the page link to highlight (-1 = none). Returns true
  // and fills `next` when the note continues on another page.
  bool page(Gfx& gfx, LineReader& rd, const PageStart& from, int top, int bottom, bool draw,
            int selLink, PageStart* next);

  // Links drawn by the last page() call, in reading order.
  int linkCount() const { return _pageLinks; }
  // Read link i's target (as written in the note) back from `rd`.
  bool linkTarget(LineReader& rd, int i, char* out, size_t n) const;

 private:
  struct LocalLink {
    uint16_t off, len;  // target within _line
  };
  struct PageLink {
    uint32_t offset;  // target's file offset
    uint16_t len;
  };
  // _attr per display byte: style bits, then the 1-based local link index.
  enum : uint8_t { kBold = 1, kCode = 2, kLinkShift = 2 };
  static uint8_t linkOf(const uint8_t attr) { return attr >> kLinkShift; }

  // Markdown inline syntax of `src` -> _disp/_attr. `baseSty` applies to
  // every character (headings are bold).
  void inlineToDisplay(const char* src, uint8_t baseSty, bool verbatim);
  void putText(const char* s, size_t n, uint8_t sty, uint8_t lnk);
  void addLocalLink(const char* target, size_t len, uint8_t& lnk);
  int measure(Gfx& gfx, int a, int b);
  int wrap(Gfx& gfx, int width, bool anywhere);
  void drawLine(Gfx& gfx, int a, int b, int x, int y, int selLink);

  char* _line = nullptr;     // the source line (kLineCap)
  char* _disp = nullptr;     // what is drawn (kLineCap)
  uint8_t* _attr = nullptr;  // per _disp byte (kLineCap)
  uint32_t _lineStart = 0;   // file offset of _line[0]
  int _len = 0;
  LocalLink _local[32];
  int _localCount = 0;
  int8_t _localToPage[33];  // per source line: local link -> page link, -1 = unseen
  uint16_t _brk[160];
  PageLink _pageTargets[kMaxPageLinks];
  int _pageLinks = 0;
};

}  // namespace notes
