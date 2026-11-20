#pragma once
// SdFat finto per i test sull'host: FsFile sopra un FILE* di stdio.
#include <cstdint>
#include <cstdio>

class FsFile {
 public:
  FsFile() = default;
  explicit FsFile(FILE* f) : f_(f) {}
  FsFile(const FsFile&) = delete;
  FsFile& operator=(const FsFile&) = delete;
  FsFile(FsFile&& o) noexcept : f_(o.f_) { o.f_ = nullptr; }
  FsFile& operator=(FsFile&& o) noexcept {
    close();
    f_ = o.f_;
    o.f_ = nullptr;
    return *this;
  }
  ~FsFile() { close(); }

  explicit operator bool() const { return f_ != nullptr; }
  int read(void* buf, size_t n) { return f_ ? static_cast<int>(fread(buf, 1, n, f_)) : -1; }
  bool seekSet(uint32_t pos) { return f_ && fseek(f_, static_cast<long>(pos), SEEK_SET) == 0; }
  void close() {
    if (f_) fclose(f_);
    f_ = nullptr;
  }

 private:
  FILE* f_ = nullptr;
};
