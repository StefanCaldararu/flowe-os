#pragma once

// xphone-os — Notes: the Obsidian vault mirror on the SD card.
//
// The vault lives under kVaultRoot with Obsidian's folder tree kept as-is
// (/notes/Projects/Ideas/X3.md). Notes are the *.md files; hidden entries
// (.obsidian, .trash, dotfiles) are never listed. Everything here is
// read-only and main-loop only.

#include <SDCardManager.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace notes {

constexpr const char* kVaultRoot = "/notes";
// Longest vault path handled (FileTransferServer: 128-byte folder + 96-byte
// file name). Longer paths are skipped by listings.
constexpr size_t kMaxPath = 224;

struct Entry {
  std::string name;  // file or folder name (".md" kept for notes)
  bool dir;
};

// One folder's folders then notes, each group sorted case-insensitively.
// False when the SD card or the folder cannot be opened.
bool listFolder(const char* path, std::vector<Entry>& out);

// "/notes/Projects/Idea.md" -> "Idea"; "/notes/Projects" -> "Projects".
void displayName(const char* path, char* out, size_t n);
// Parent folder of a path ("/notes/a/b.md" -> "/notes/a"), never above root.
void parentOf(const char* path, char* out, size_t n);
bool isNotePath(const char* path);  // ends in .md (any case)

// Resolve an Obsidian link target from the note at `fromNote` into a vault
// path. Handles [[Note]], [[Folder/Note]], [[Note#Heading]], [[Note^block]]
// and relative markdown links ("../Other%20Note.md"). Like Obsidian, a bare
// name matches a note of that name anywhere in the vault (same folder
// first); matching ignores case. False when no note matches.
bool resolveLink(const char* fromNote, const char* target, char* out, size_t n);

// Every folder and note under the vault, depth first, for sync. `rel` is
// the path below kVaultRoot ("Projects/Roadmap.md"); notes carry their size
// and fnv1a32() of their bytes, folders size 0 and hash 0. Hidden entries
// are skipped, as in listFolder. Returning false from `visit` stops the
// walk; walkVault then returns false (also when the card cannot be read).
// With `readNotes` false, notes are visited without reading them (size and
// hash 0) — for a caller that only wants the tree.
using VaultVisitor = bool (*)(const char* rel, bool isDir, uint32_t size, uint32_t hash, void* ctx);
bool walkVault(VaultVisitor visit, void* ctx, bool readNotes = true);

// 32-bit FNV-1a, the hash /api/notes reports and the Obsidian plugin checks.
uint32_t fnv1a32(const uint8_t* data, size_t n, uint32_t h = 2166136261u);

// Buffered line reader over one note. Lines longer than the caller's buffer
// are cut there (the rest of that line is skipped); '\r' is dropped.
class LineReader {
 public:
  bool open(const char* path);
  void close() { _f.close(); }
  bool seek(uint32_t offset);
  // Next line into buf (NUL-terminated). `start` = the line's file offset.
  // False at end of file.
  bool next(char* buf, size_t cap, uint32_t& start);
  // Raw bytes at `offset` (a link target). Returns the count read.
  int readAt(uint32_t offset, char* buf, size_t n);

 private:
  int getc();
  FsFile _f;
  uint8_t _buf[512];
  int _len = 0, _pos = 0;
  uint32_t _bufStart = 0;  // file offset of _buf[0]
};

}  // namespace notes
