#pragma once
// Drawing on the panel framebuffer: 1 bpp, MSB first, 1 = white, 0 = black.
// All coordinates are logical: they follow the current rotation, so
// width()/height() swap for 90 and 270.
#include <SdFat.h>

#include <cstdint>

class Canvas {
 public:
  // Quarter turns applied on top of the panel's native (landscape) layout.
  enum Rotation : uint8_t { ROT_0 = 0, ROT_90 = 1, ROT_180 = 2, ROT_270 = 3 };

  Canvas(uint8_t* fb, uint16_t panelWidth, uint16_t panelHeight, uint16_t widthBytes, Rotation rot = ROT_0)
      : fb_(fb), pw_(panelWidth), ph_(panelHeight), wb_(widthBytes), rot_(rot) {}

  static bool isPortrait(Rotation r) { return r == ROT_90 || r == ROT_270; }

  uint16_t width() const { return isPortrait(rot_) ? ph_ : pw_; }
  uint16_t height() const { return isPortrait(rot_) ? pw_ : ph_; }

  void clear(bool black = false);
  void setPixel(int x, int y, bool black);
  void fillRect(int x, int y, int w, int h, bool black);
  void strokeRect(int x, int y, int w, int h, int t, bool black);

  // Status text with the built-in 5x7 font (uppercase, digits, . / : - % ! _).
  void text(int x, int y, const char* s, int scale, bool black = true);
  void textCentered(int y, const char* s, int scale, bool black = true);

  // Small battery glyph; (x, y) is the top-left corner. 40x20 px.
  void battery(int x, int y, int percent);
  // Black banner across the bottom with white text.
  void banner(const char* s);

  enum class BmpResult { Ok, OpenFailed, NotBmp, Unsupported };
  // Draws a 1-bit uncompressed BMP centred on the screen (cropped if larger).
  BmpResult drawBmp(FsFile& file);
  // Draws it with its top-left corner at (x, y); maxH >= 0 crops it to that height.
  BmpResult drawBmpAt(FsFile& file, int x, int y, int maxH = -1);

  // Panel coordinates (native framebuffer) <-> logical coordinates of this canvas.
  void toPanel(int lx, int ly, int& px, int& py) const;
  void fromPanel(int px, int py, int& lx, int& ly) const;

 private:
  BmpResult drawBmpImpl(FsFile& file, bool centred, int x, int y, int maxH);

  uint8_t* fb_;
  uint16_t pw_;
  uint16_t ph_;
  uint16_t wb_;
  Rotation rot_;
};
