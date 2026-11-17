#include "Canvas.h"

#include <cstring>

#include "TinyFont.h"

namespace {

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// Widest row we accept: 2048 px at 1 bpp, padded to 4 bytes.
constexpr size_t kMaxRowBytes = 256;

}  // namespace

void Canvas::clear(bool black) { memset(fb_, black ? 0x00 : 0xFF, static_cast<size_t>(wb_) * ph_); }

void Canvas::setPixel(int lx, int ly, bool black) {
  if (lx < 0 || ly < 0 || lx >= width() || ly >= height()) return;
  int x, y;
  switch (rot_) {
    case ROT_90:  x = pw_ - 1 - ly; y = lx; break;
    case ROT_180: x = pw_ - 1 - lx; y = ph_ - 1 - ly; break;
    case ROT_270: x = ly; y = ph_ - 1 - lx; break;
    default:      x = lx; y = ly; break;
  }
  uint8_t& b = fb_[static_cast<size_t>(y) * wb_ + (x >> 3)];
  const uint8_t mask = static_cast<uint8_t>(0x80 >> (x & 7));
  if (black)
    b &= static_cast<uint8_t>(~mask);
  else
    b |= mask;
}

void Canvas::fillRect(int x, int y, int w, int h, bool black) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++) setPixel(xx, yy, black);
}

void Canvas::strokeRect(int x, int y, int w, int h, int t, bool black) {
  fillRect(x, y, w, t, black);
  fillRect(x, y + h - t, w, t, black);
  fillRect(x, y, t, h, black);
  fillRect(x + w - t, y, t, h, black);
}

void Canvas::text(int x, int y, const char* s, int scale, bool black) {
  for (; *s; s++, x += tinyfont::advance(scale)) {
    const uint8_t* g = tinyfont::glyphFor(*s);
    for (int col = 0; col < 5; col++)
      for (int row = 0; row < 7; row++)
        if (g[col] & (1 << row)) fillRect(x + col * scale, y + row * scale, scale, scale, black);
  }
}

void Canvas::textCentered(int y, const char* s, int scale, bool black) {
  text((width() - tinyfont::textWidth(s, scale)) / 2, y, s, scale, black);
}

void Canvas::banner(const char* s) {
  constexpr int kScale = 3, kH = 7 * kScale + 24;
  fillRect(0, height() - kH, width(), kH, true);
  textCentered(height() - kH + 12, s, kScale, false);
}

void Canvas::battery(int x, int y, int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  fillRect(x - 2, y - 2, 44, 24, false);  // white halo so it reads on any card
  strokeRect(x, y, 36, 20, 2, true);
  fillRect(x + 36, y + 6, 4, 8, true);
  fillRect(x + 4, y + 4, (28 * percent) / 100, 12, true);
}

Canvas::BmpResult Canvas::drawBmp(FsFile& f) {
  if (!f) return BmpResult::OpenFailed;

  uint8_t hdr[62];
  if (f.read(hdr, sizeof(hdr)) != static_cast<int>(sizeof(hdr))) return BmpResult::NotBmp;
  if (hdr[0] != 'B' || hdr[1] != 'M') return BmpResult::NotBmp;

  const uint32_t dataOffset = rd32(hdr + 10);
  const uint32_t dibSize = rd32(hdr + 14);
  const int32_t bw = static_cast<int32_t>(rd32(hdr + 18));
  const int32_t bhRaw = static_cast<int32_t>(rd32(hdr + 22));
  const uint16_t bpp = rd16(hdr + 28);
  const uint32_t compression = rd32(hdr + 30);
  if (dibSize < 40 || bpp != 1 || compression != 0 || bw <= 0 || bhRaw == 0) return BmpResult::Unsupported;

  const bool bottomUp = bhRaw > 0;
  const int32_t bh = bottomUp ? bhRaw : -bhRaw;
  const size_t rowBytes = ((static_cast<size_t>(bw) + 31) / 32) * 4;
  if (rowBytes > kMaxRowBytes) return BmpResult::Unsupported;

  // Palette sits right after the DIB header. If entry 0 is the lighter colour,
  // bit 0 means white, so the source bits must be inverted.
  uint8_t pal[8];
  if (!f.seekSet(14 + dibSize) || f.read(pal, sizeof(pal)) != static_cast<int>(sizeof(pal)))
    return BmpResult::NotBmp;
  const int lum0 = pal[0] + pal[1] + pal[2];
  const int lum1 = pal[4] + pal[5] + pal[6];
  const bool bitOneIsWhite = lum1 >= lum0;

  // Centre the image; negative offsets crop.
  const int lw = width(), lh = height();
  const int offX = (lw - bw) / 2;
  const int offY = (lh - bh) / 2;

  uint8_t row[kMaxRowBytes];
  for (int32_t r = 0; r < bh; r++) {
    const int32_t srcRow = bottomUp ? (bh - 1 - r) : r;
    if (!f.seekSet(dataOffset + static_cast<uint32_t>(srcRow) * rowBytes)) return BmpResult::NotBmp;
    if (f.read(row, rowBytes) != static_cast<int>(rowBytes)) return BmpResult::NotBmp;

    const int dy = offY + r;
    if (dy < 0 || dy >= lh) continue;
    for (int32_t x = 0; x < bw; x++) {
      const int dx = offX + x;
      if (dx < 0 || dx >= lw) continue;
      const bool bit = row[x >> 3] & (0x80 >> (x & 7));
      setPixel(dx, dy, bit != bitOneIsWhite);
    }
  }
  return BmpResult::Ok;
}
