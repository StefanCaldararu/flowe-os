#pragma once
// Host stand-in for the SDK SD card: card paths ("/notes/a.md") map onto a
// real folder named by the NOTES_SD_ROOT environment variable. Enough of
// SdFat's FsFile for notes/NotesVault: read, seek, directory iteration.
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

inline std::string hostPath(const char* card) {
  const char* root = getenv("NOTES_SD_ROOT");
  return std::string(root ? root : ".") + card;
}

class FsFile {
  std::shared_ptr<FILE> f;
  std::shared_ptr<DIR> d;
  std::string path;  // host path
  bool dir = false;

 public:
  FsFile() = default;
  static FsFile openHost(const std::string& p) {
    FsFile r;
    struct stat st {};
    if (stat(p.c_str(), &st) != 0) return r;
    r.path = p;
    if (S_ISDIR(st.st_mode)) {
      r.dir = true;
      r.d.reset(opendir(p.c_str()), [](DIR* x) { if (x) closedir(x); });
    } else {
      r.f.reset(fopen(p.c_str(), "rb"), [](FILE* x) { if (x) fclose(x); });
    }
    return r;
  }
  explicit operator bool() const { return f || d; }
  bool isDir() const { return dir; }
  bool seekSet(uint64_t off) { return f && !fseeko(f.get(), static_cast<off_t>(off), SEEK_SET); }
  int read(void* p, size_t n) { return f ? static_cast<int>(fread(p, 1, n, f.get())) : -1; }
  bool close() { f.reset(); d.reset(); return true; }
  int getName(char* out, size_t n) {
    const size_t slash = path.rfind('/');
    snprintf(out, n, "%s", path.c_str() + (slash == std::string::npos ? 0 : slash + 1));
    return static_cast<int>(strlen(out));
  }
  bool openNext(FsFile* parent, int) {
    if (!parent || !parent->d) return false;
    while (dirent* e = readdir(parent->d.get())) {
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      *this = openHost(parent->path + "/" + e->d_name);
      if (*this) return true;
    }
    return false;
  }
};

struct HostSD {
  bool ready() { return true; }
  bool begin() { return true; }
  FsFile open(const char* p, int) { return FsFile::openHost(hostPath(p)); }
  bool exists(const char* p) { return access(hostPath(p).c_str(), F_OK) == 0; }
};
inline HostSD SdMan;
#ifndef O_RDONLY
#define O_RDONLY 0
#endif
