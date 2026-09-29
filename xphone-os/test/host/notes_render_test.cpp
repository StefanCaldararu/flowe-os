// Host driver for the Notes engine (notes/NoteLayout + notes/NotesVault)
// over the real Gfx and UI fonts. test_notes.py builds a vault on disk,
// points NOTES_SD_ROOT at it and runs:
//   resolve <fromNote> <target>   -> resolved path, or NONE
//   list <folder>                 -> "d name" / "f name" per entry
//   walk                          -> "d rel" / "f rel size hash" per entry
//   render <note> <outDir>        -> "pages N", then per page its links;
//                                    page_<i>.raw = native framebuffer bytes
#include <cstdio>
#include <cstring>
#include <vector>

#include "Gfx.h"
#include "notes/NoteLayout.h"
#include "notes/NotesVault.h"

Gfx* G_GFX = nullptr;

int main(int argc, char** argv) {
  if (argc < 3) return 2;
  const char* cmd = argv[1];
  if (!strcmp(cmd, "resolve") && argc == 4) {
    char out[notes::kMaxPath];
    printf("%s\n", notes::resolveLink(argv[2], argv[3], out, sizeof(out)) ? out : "NONE");
    return 0;
  }
  if (!strcmp(cmd, "list")) {
    std::vector<notes::Entry> entries;
    if (!notes::listFolder(argv[2], entries)) return 1;
    for (const notes::Entry& e : entries) printf("%c %s\n", e.dir ? 'd' : 'f', e.name.c_str());
    return 0;
  }
  if (!strcmp(cmd, "walk")) {
    const bool ok = notes::walkVault(
        [](const char* rel, bool isDir, uint32_t size, uint32_t hash, void*) {
          if (isDir) printf("d %s\n", rel);
          else printf("f %s %u %08x\n", rel, static_cast<unsigned>(size), static_cast<unsigned>(hash));
          return true;
        },
        nullptr);
    return ok ? 0 : 1;
  }
  if (!strcmp(cmd, "render") && argc == 4) {
    static EInkDisplay display;
    Gfx gfx(display);
    if (!gfx.begin()) return 1;
    auto* layout = new notes::NoteLayout();
    if (!layout->init()) return 1;
    notes::LineReader rd;
    if (!rd.open(argv[2])) return 1;
    // Same geometry as NotesScene: header 46 + 10, soft-key bar 44 + 8.
    const int top = 56, bottom = gfx.height() - 44 - 8;
    std::vector<notes::PageStart> pages{notes::PageStart{}};
    notes::PageStart next;
    while (pages.size() < 999 && layout->page(gfx, rd, pages.back(), top, bottom, false, -1, &next)) {
      if (next.offset == pages.back().offset && next.skip == pages.back().skip) return 3;  // no progress
      pages.push_back(next);
    }
    printf("pages %zu\n", pages.size());
    for (size_t i = 0; i < pages.size(); i++) {
      gfx.clear();
      notes::PageStart after;
      const bool more = layout->page(gfx, rd, pages[i], top, bottom, true, i == 0 ? 0 : -1, &after);
      // Drawing must break exactly where pagination did.
      if (more != (i + 1 < pages.size())) return 4;
      if (more && (after.offset != pages[i + 1].offset || after.skip != pages[i + 1].skip)) return 5;
      printf("page %zu links:", i + 1);
      for (int l = 0; l < layout->linkCount(); l++) {
        char target[notes::kMaxPath];
        if (!layout->linkTarget(rd, l, target, sizeof(target))) return 6;
        printf(" [%s]", target);
      }
      printf("\n");
      char path[512];
      snprintf(path, sizeof(path), "%s/page_%zu.raw", argv[3], i + 1);
      FILE* f = fopen(path, "wb");
      fwrite(display.getFrameBuffer(), 1, display.getBufferSize(), f);
      fclose(f);
    }
    delete layout;
    return 0;
  }
  return 2;
}
