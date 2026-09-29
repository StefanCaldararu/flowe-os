#include "NotesVault.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <strings.h>

namespace notes {
namespace {

bool sdReady() { return SdMan.ready() || SdMan.begin(); }

bool endsWithMd(const char* s) {
  const size_t n = strlen(s);
  return n > 3 && strcasecmp(s + n - 3, ".md") == 0;
}

// Decode %XX escapes (markdown links write spaces as %20).
void urlDecode(const char* in, char* out, size_t n) {
  size_t o = 0;
  for (size_t i = 0; in[i] && o + 1 < n; i++) {
    auto hex = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    if (in[i] == '%' && hex(in[i + 1]) >= 0 && hex(in[i + 2]) >= 0) {
      out[o++] = static_cast<char>(hex(in[i + 1]) * 16 + hex(in[i + 2]));
      i += 2;
    } else {
      out[o++] = in[i];
    }
  }
  out[o] = 0;
}

// Case-insensitive lookup of `rel` ("Folder/Note.md") segment by segment
// under `base`, so a link typed with other capitals still lands. Writes the
// on-card path.
bool findPathNoCase(const char* base, const char* rel, char* out, size_t n) {
  char cur[kMaxPath];
  snprintf(cur, sizeof(cur), "%s", base);
  const char* seg = rel;
  while (*seg) {
    const char* slash = strchr(seg, '/');
    const size_t segLen = slash ? static_cast<size_t>(slash - seg) : strlen(seg);
    if (segLen == 0) {  // "a//b"
      seg = slash + 1;
      continue;
    }
    char want[128];
    if (segLen >= sizeof(want)) return false;
    memcpy(want, seg, segLen);
    want[segLen] = 0;
    if (!strcmp(want, ".")) {
      seg = slash ? slash + 1 : seg + segLen;
      continue;
    }
    if (!strcmp(want, "..")) {
      char up[kMaxPath];
      parentOf(cur, up, sizeof(up));
      snprintf(cur, sizeof(cur), "%s", up);
      seg = slash ? slash + 1 : seg + segLen;
      continue;
    }
    FsFile dir = SdMan.open(cur, O_RDONLY);
    if (!dir || !dir.isDir()) return false;
    FsFile e;
    bool found = false;
    char name[128];
    while (!found && e.openNext(&dir, O_RDONLY)) {
      if (e.getName(name, sizeof(name)) > 0 && strcasecmp(name, want) == 0) found = true;
      e.close();
    }
    dir.close();
    if (!found) return false;
    const size_t len = strlen(cur);
    if (len + 1 + strlen(name) >= sizeof(cur)) return false;
    snprintf(cur + len, sizeof(cur) - len, "/%s", name);
    seg = slash ? slash + 1 : seg + segLen;
  }
  if (!isNotePath(cur)) return false;
  snprintf(out, n, "%s", cur);
  return true;
}

// Depth-first search for a note file named `file` (case-insensitive).
bool findByName(const char* dirPath, const char* file, char* out, size_t n, int depth) {
  if (depth > 8) return false;
  FsFile dir = SdMan.open(dirPath, O_RDONLY);
  if (!dir || !dir.isDir()) return false;
  FsFile e;
  char name[128];
  std::vector<std::string> subdirs;
  bool found = false;
  while (!found && e.openNext(&dir, O_RDONLY)) {
    if (e.getName(name, sizeof(name)) > 0 && name[0] != '.') {
      if (e.isDir()) {
        subdirs.emplace_back(name);
      } else if (strcasecmp(name, file) == 0) {
        found = snprintf(out, n, "%s/%s", dirPath, name) < static_cast<int>(n);
      }
    }
    e.close();
  }
  dir.close();
  for (const std::string& s : subdirs) {
    if (found) break;
    char sub[kMaxPath];
    if (snprintf(sub, sizeof(sub), "%s/%s", dirPath, s.c_str()) >= static_cast<int>(sizeof(sub))) continue;
    found = findByName(sub, file, out, n, depth + 1);
  }
  return found;
}

bool walkFolder(const char* path, const size_t rootLen, VaultVisitor visit, void* ctx, const bool readNotes,
                const int depth) {
  if (depth > 8) return true;
  FsFile dir = SdMan.open(path, O_RDONLY);
  if (!dir || !dir.isDir()) return depth > 0;  // a missing vault root is a failure
  std::vector<std::string> subdirs;
  FsFile e;
  char name[128];
  bool go = true;
  while (go && e.openNext(&dir, O_RDONLY)) {
    const int len = e.getName(name, sizeof(name));
    if (len > 0 && name[0] != '.' && strlen(path) + 1 + static_cast<size_t>(len) < kMaxPath) {
      if (e.isDir()) {
        subdirs.emplace_back(name);
      } else if (endsWithMd(name)) {
        char full[kMaxPath];
        snprintf(full, sizeof(full), "%s/%s", path, name);
        uint32_t size = 0, hash = readNotes ? fnv1a32(nullptr, 0) : 0;
        uint8_t buf[256];
        for (int got; readNotes && (got = e.read(buf, sizeof(buf))) > 0;) {
          hash = fnv1a32(buf, static_cast<size_t>(got), hash);
          size += static_cast<uint32_t>(got);
        }
        go = visit(full + rootLen + 1, false, size, hash, ctx);
      }
    }
    e.close();
  }
  dir.close();
  for (const std::string& s : subdirs) {
    if (!go) break;
    char sub[kMaxPath];
    snprintf(sub, sizeof(sub), "%s/%s", path, s.c_str());
    go = visit(sub + rootLen + 1, true, 0, 0, ctx) && walkFolder(sub, rootLen, visit, ctx, readNotes, depth + 1);
  }
  return go;
}

}  // namespace

uint32_t fnv1a32(const uint8_t* data, const size_t n, uint32_t h) {
  for (size_t i = 0; i < n; i++) h = (h ^ data[i]) * 16777619u;
  return h;
}

bool walkVault(const VaultVisitor visit, void* ctx, const bool readNotes) {
  if (!sdReady()) return false;
  return walkFolder(kVaultRoot, strlen(kVaultRoot), visit, ctx, readNotes, 0);
}

bool isNotePath(const char* path) { return path && endsWithMd(path); }

bool listFolder(const char* path, std::vector<Entry>& out) {
  out.clear();
  if (!sdReady()) return false;
  FsFile dir = SdMan.open(path, O_RDONLY);
  if (!dir || !dir.isDir()) return false;
  const size_t pathLen = strlen(path);
  FsFile e;
  char name[128];
  while (e.openNext(&dir, O_RDONLY)) {
    const int len = e.getName(name, sizeof(name));
    const bool isDir = e.isDir();
    e.close();
    if (len <= 0 || name[0] == '.' || pathLen + 1 + static_cast<size_t>(len) >= kMaxPath) continue;
    if (!isDir && !endsWithMd(name)) continue;
    out.push_back(Entry{std::string(name), isDir});
  }
  dir.close();
  std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
    if (a.dir != b.dir) return a.dir;
    return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
  });
  return true;
}

void displayName(const char* path, char* out, const size_t n) {
  const char* slash = strrchr(path, '/');
  snprintf(out, n, "%s", slash ? slash + 1 : path);
  if (endsWithMd(out)) out[strlen(out) - 3] = 0;
  if (!out[0]) snprintf(out, n, "Notes");
}

void parentOf(const char* path, char* out, const size_t n) {
  snprintf(out, n, "%s", path);
  char* slash = strrchr(out, '/');
  if (slash) *slash = 0;
  if (strlen(out) < strlen(kVaultRoot) || strncmp(out, kVaultRoot, strlen(kVaultRoot)) != 0)
    snprintf(out, n, "%s", kVaultRoot);
}

bool resolveLink(const char* fromNote, const char* target, char* out, const size_t n) {
  if (!target || !target[0] || strstr(target, "://") || !sdReady()) return false;
  char t[kMaxPath];
  urlDecode(target, t, sizeof(t));
  // Drop the heading / block part: [[Note#Heading]], [[Note#^block]].
  if (char* hash = strchr(t, '#')) *hash = 0;
  if (char* caret = strchr(t, '^')) *caret = 0;
  size_t len = strlen(t);
  while (len && t[len - 1] == ' ') t[--len] = 0;
  if (!len) return false;  // [[#Heading]]: a link into this same note
  char file[kMaxPath];
  snprintf(file, sizeof(file), "%s%s", t, endsWithMd(t) ? "" : ".md");

  char folder[kMaxPath];
  parentOf(fromNote, folder, sizeof(folder));
  if (strchr(file, '/')) {
    // A path: relative to this note first (markdown links), then to the
    // vault root (Obsidian's "shortest path" wikilinks).
    if (file[0] != '/' && findPathNoCase(folder, file, out, n)) return true;
    return findPathNoCase(kVaultRoot, file[0] == '/' ? file + 1 : file, out, n);
  }
  if (findPathNoCase(folder, file, out, n)) return true;
  return findByName(kVaultRoot, file, out, n, 0);
}

bool LineReader::open(const char* path) {
  _len = _pos = 0;
  _bufStart = 0;
  if (!sdReady()) return false;
  _f = SdMan.open(path, O_RDONLY);
  return static_cast<bool>(_f);
}

bool LineReader::seek(const uint32_t offset) {
  _len = _pos = 0;
  _bufStart = offset;
  return _f.seekSet(offset);
}

int LineReader::getc() {
  if (_pos >= _len) {
    _bufStart += static_cast<uint32_t>(_len);
    _len = _f.read(_buf, sizeof(_buf));
    _pos = 0;
    if (_len <= 0) {
      _len = 0;
      return -1;
    }
  }
  return _buf[_pos++];
}

int LineReader::readAt(const uint32_t offset, char* buf, const size_t n) {
  if (!seek(offset)) return -1;
  const int got = _f.read(buf, n);
  _bufStart = offset + static_cast<uint32_t>(got > 0 ? got : 0);  // the file position now
  return got;
}

bool LineReader::next(char* buf, const size_t cap, uint32_t& start) {
  start = _bufStart + static_cast<uint32_t>(_pos);
  size_t o = 0;
  int c = getc();
  if (c < 0) return false;
  for (; c >= 0 && c != '\n'; c = getc()) {
    if (c == '\r') continue;
    if (o + 1 < cap) buf[o++] = static_cast<char>(c);
  }
  buf[o] = 0;
  return true;
}

}  // namespace notes
