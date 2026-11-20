// Test host della schermata filtri (niente hardware): device/test/filters_test.sh
//
// Con i pezzi e le anteprime di filters_fixture.zip (generato dal configuratore web
// con filters_fixture.js) controlla che:
// - screen.ini v3 si legga con i tasti del simulatore per ogni verso;
// - le regole degli stadi diano gli stessi risultati del configuratore web;
// - comporre sfondo e pezzi dia l'anteprima pixel per pixel, in tutte e quattro
//   le rotazioni, anche aggiornando solo le parti che cambiano;
// - le indicazioni dei tasti stiano dentro lo schermo.
// Con un secondo argomento salva lì le schermate con le indicazioni, da guardare.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Canvas.h"
#include "Filters.h"

using namespace filters;

namespace {

std::string root;  // cartella dello zip estratto
int failures = 0;

#define CHECK(cond, ...)                \
  do {                                  \
    if (!(cond)) {                      \
      failures++;                       \
      fprintf(stderr, "FALLITO: ");     \
      fprintf(stderr, __VA_ARGS__);     \
      fprintf(stderr, "\n");            \
    }                                   \
  } while (0)

constexpr int kPW = 792, kPH = 528, kWB = kPW / 8;

FsFile openFile(const std::string& p) { return FsFile(fopen((root + p).c_str(), "rb")); }

std::vector<int> ints(const std::string& s) {
  std::vector<int> v;
  if (s == "-") return v;
  std::stringstream ss(s);
  std::string t;
  while (std::getline(ss, t, ',')) v.push_back(atoi(t.c_str()));
  return v;
}

void setStages(const Config& c, int8_t* st, const std::string& s) {
  const std::vector<int> v = ints(s);
  for (int i = 0; i < kMaxStages; i++) st[i] = i < static_cast<int>(v.size()) && i < c.stageCount ? v[i] : -1;
}

std::string list(const Config& c, const int8_t* st) {
  std::string s;
  for (int i = 0; i < c.stageCount; i++) s += (i ? "," : "") + std::to_string(st[i]);
  return s;
}

// Schermata intera nella rotazione rot: sfondo + pezzi.
void compose(std::vector<uint8_t>& fb, const Config& c, const State& s, int sel, Canvas::Rotation rot, uint8_t parts,
             bool clear) {
  Canvas cv(fb.data(), kPW, kPH, kWB, rot);
  if (clear) {
    cv.clear();
    FsFile f = openFile(Canvas::isPortrait(rot) ? "/camtag/01_filters.p.bmp" : "/camtag/01_filters.bmp");
    CHECK(cv.drawBmp(f) == Canvas::BmpResult::Ok, "sfondo");
  }
  CHECK(drawParts(cv, c, s, "/camtag/01_filters", sel, parts), "pezzi mancanti");
}

std::vector<uint8_t> reference(const std::string& name, Canvas::Rotation rot) {
  std::vector<uint8_t> fb(kWB * kPH);
  Canvas cv(fb.data(), kPW, kPH, kWB, rot);
  cv.clear();
  FsFile f = openFile("/ref/" + name + (Canvas::isPortrait(rot) ? ".port.bmp" : ".land.bmp"));
  CHECK(cv.drawBmp(f) == Canvas::BmpResult::Ok, "riferimento %s", name.c_str());
  return fb;
}

int diffBits(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
  int n = 0;
  for (size_t i = 0; i < a.size(); i++) n += __builtin_popcount(a[i] ^ b[i]);
  return n;
}

// Salva il framebuffer (pannello orizzontale) come BMP 1 bit.
void saveBmp(const std::vector<uint8_t>& fb, const std::string& path) {
  const int rowBytes = (kPW + 31) / 32 * 4, off = 62, size = off + rowBytes * kPH;  // righe allineate a 4 byte
  std::vector<uint8_t> out(size, 0);
  auto le32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; i++) out[at + i] = (v >> (8 * i)) & 0xFF; };
  out[0] = 'B'; out[1] = 'M'; le32(2, size); le32(10, off); le32(14, 40); le32(18, kPW); le32(22, kPH);
  out[26] = 1; out[28] = 1; le32(34, rowBytes * kPH); le32(46, 2);
  out[58] = out[59] = out[60] = 255;
  for (int y = 0; y < kPH; y++) memcpy(&out[off + (kPH - 1 - y) * rowBytes], &fb[y * kWB], kWB);
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<char*>(out.data()), size);
}

// Canvas::Rotation per quarti di giro dalla posizione naturale (kQuarterTurns in main.cpp).
constexpr Canvas::Rotation kByQuarter[4] = {Canvas::ROT_270, Canvas::ROT_180, Canvas::ROT_90, Canvas::ROT_0};

}  // namespace

FsFile filtersOpen(const char* path) { return openFile(path); }

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "uso: %s cartella-dello-zip [cartella-per-le-immagini]\n", argv[0]);
    return 2;
  }
  root = argv[1];
  const std::string dump = argc > 2 ? argv[2] : "";

  Config c;
  {
    FsFile f = openFile("/camtag/01_filters/screen.ini");
    CHECK(parse(f, c), "screen.ini non letto");
  }
  CHECK(c.ndCount == 8 && c.dioCount == 4 && c.kitCount == 4 && c.stageCount == 5, "conteggi %d %d %d %d", c.ndCount,
        c.dioCount, c.kitCount, c.stageCount);
  CHECK(c.ndParts[5][0] == 4 && c.ndParts[5][1] == 1 && c.ndParts[1][0] == 1 && !c.ndParts[1][1], "nd.parts");
  CHECK(c.kitSlots[3] == 2 && c.kitSlots[0] == 1 && c.dioSlots[1] == 1 && c.dioSlots[3] == 2, "slots");
  CHECK(c.ndStart == 1 && c.dioStart == 2 && c.settleS == 2 && c.editTimeoutS == 5, "partenza e tempi");
  CHECK(!strcmp(c.hints[0][0], "-DIO") && !strcmp(c.editHints[0][0], "\x1e") && !strcmp(c.editHints[0][2], "-"),
        "indicazioni '%s' '%s' '%s'", c.hints[0][0], c.editHints[0][0], c.editHints[0][2]);

  std::ifstream in(root + "/cases.txt");
  std::string line;
  int counts[6] = {};
  struct Case {
    std::string name;
    State s;
    int sel;
  };
  std::vector<Case> cases;
  while (std::getline(in, line)) {
    std::stringstream ss(line);
    std::string kind;
    ss >> kind;
    if (kind == "keys") {
      int q;
      ss >> q;
      for (int k = 0; k < 4; k++) {
        std::string t;
        ss >> t;
        const Role role = t[0] == 'n' ? kNd : t[0] == 'd' ? kDio : kNone;
        const int dir = role ? (t.back() == '+' ? 1 : -1) : 0;
        CHECK(c.keys[q][k].role == role && (!role || c.keys[q][k].dir == dir), "tasto %c a %d°", 'A' + k, q * 90);
      }
      counts[0]++;
    } else if (kind == "edit") {
      int q, a, b, cc, d;
      ss >> q >> a >> b >> cc >> d;
      CHECK(c.editRow[q][0] == a && c.editRow[q][1] == b && c.editVal[q][0] == cc && c.editVal[q][1] == d,
            "tasti in modifica a %d°", q * 90);
      counts[0]++;
    } else if (kind == "settle") {
      int nd0, dio0, nd1, dio1;
      std::string before, after;
      ss >> nd0 >> dio0 >> nd1 >> dio1 >> before >> after;
      State s;
      s.ndShown = static_cast<uint8_t>(nd0);
      s.dioShown = static_cast<uint8_t>(dio0);
      s.nd = static_cast<uint8_t>(nd1);
      s.dio = static_cast<uint8_t>(dio1);
      setStages(c, s.stages, before);
      settle(c, s);
      CHECK(list(c, s.stages) == after, "%s -> %s invece di %s", line.c_str(), list(c, s.stages).c_str(), after.c_str());
      counts[1]++;
    } else if (kind == "cells") {
      int nd, dio;
      std::string st;
      ss >> nd >> dio >> st;
      int8_t stages[kMaxStages];
      setStages(c, stages, st);
      Cell cells[kMaxStages];
      stageCells(c, stages, nd, dio, cells);
      for (int i = 0; i < c.stageCount; i++) {
        std::string t;
        ss >> t;
        int k, span, g, ins;
        sscanf(t.c_str(), "%d:%d:%d:%d", &k, &span, &g, &ins);
        const Cell& x = cells[i];
        const int myG = x.kind == kCellGlass ? x.glass : -1;
        CHECK(x.kind == k && (x.kind == kCellCont || x.span == span) && myG == g && x.insert == !!ins,
              "%s: stadio %d %d:%d:%d:%d", line.c_str(), i, x.kind, x.span, myG, x.insert);
      }
      counts[2]++;
    } else if (kind == "editable") {
      int nd, dio;
      std::string st, want;
      ss >> nd >> dio >> st >> want;
      int8_t stages[kMaxStages];
      setStages(c, stages, st);
      int ok[kMaxStages];
      const int m = editableStages(c, stages, nd, dio, ok);
      std::string got;
      for (int j = 0; j < m; j++) got += (j ? "," : "") + std::to_string(ok[j]);
      if (got.empty()) got = "-";
      CHECK(got == want, "%s: %s", line.c_str(), got.c_str());
      counts[3]++;
    } else if (kind == "options") {
      int nd, dio, i;
      std::string st, want;
      ss >> nd >> dio >> st >> i >> want;
      int8_t stages[kMaxStages];
      setStages(c, stages, st);
      int lo, hi, opts[kMaxKit + 1];
      stageRange(c, nd, dio, lo, hi);
      const int m = stageOptions(c, stages, i, hi, opts);
      std::string got;
      for (int j = 0; j < m; j++) got += (j ? "," : "") + std::to_string(opts[j]);
      CHECK(got == want, "%s: %s", line.c_str(), got.c_str());
      counts[4]++;
    } else if (kind == "case") {
      Case k;
      int nd, dio;
      std::string st;
      ss >> k.name >> nd >> dio >> k.sel >> st;
      k.s.nd = k.s.ndShown = static_cast<uint8_t>(nd);
      k.s.dio = k.s.dioShown = static_cast<uint8_t>(dio);
      setStages(c, k.s.stages, st);
      cases.push_back(k);
    }
  }
  CHECK(counts[0] == 8 && counts[1] == 400 && counts[2] == 400 && counts[3] == 400 && counts[4] > 0 && cases.size() == 5,
        "righe di cases.txt");

  // Composizione: ogni caso in tutte le rotazioni; poi dal caso prima al caso dopo
  // ridisegnando solo riquadri e stadi, come fa il firmware.
  std::vector<uint8_t> fb(kWB * kPH);
  for (int r = 0; r < 4; r++) {
    const Canvas::Rotation rot = static_cast<Canvas::Rotation>(r);
    for (size_t i = 0; i < cases.size(); i++) {
      const Case& k = cases[i];
      const std::vector<uint8_t> ref = reference(k.name, rot);
      compose(fb, c, k.s, k.sel, rot, kPartAll, true);
      const int d = diffBits(fb, ref);
      CHECK(d == 0, "%s a ROT_%d: %d pixel diversi dall'anteprima", k.name.c_str(), r * 90, d);
      if (i) {
        compose(fb, c, cases[i - 1].s, cases[i - 1].sel, rot, kPartAll, true);
        compose(fb, c, k.s, k.sel, rot, kPartNd | kPartDio | kPartList, false);
        const int p = diffBits(fb, ref);
        CHECK(p == 0, "%s a ROT_%d aggiornando a pezzi: %d pixel diversi", k.name.c_str(), r * 90, p);
      }
      counts[5]++;
    }
  }

  // Indicazioni: dentro lo schermo e fuori dai riquadri e dagli stadi, nei 4 versi.
  for (int q = 0; q < 4; q++) {
    for (int editing = 0; editing < 2; editing++) {
      const Canvas::Rotation rot = kByQuarter[q];
      const Case& k = cases[editing ? 0 : 3];
      compose(fb, c, k.s, editing ? k.sel : -1, rot, kPartAll, true);
      std::vector<uint8_t> before = fb;
      Canvas phys(fb.data(), kPW, kPH, kWB, rot);
      const Canvas natural(fb.data(), kPW, kPH, kWB, Canvas::ROT_270);
      drawHints(phys, natural, c, q, editing);
      // Le indicazioni cambiano solo le fasce e la barra del titolo: fuori resta tutto uguale.
      const Layout& L = c.layout(Canvas::isPortrait(rot));
      int touched = 0, outside = 0;
      for (int y = 0; y < phys.height(); y++)
        for (int x = 0; x < phys.width(); x++) {
          int px, py;
          phys.toPanel(x, y, px, py);
          const int at = py * kWB + (px >> 3), bit = 0x80 >> (px & 7);
          if ((fb[at] ^ before[at]) & bit) {
            touched++;
            auto in = [&](const Rect& R) { return x >= R.x && x < R.x + R.w && y >= R.y && y < R.y + R.h; };
            if (in(L.nd) || in(L.dio) || in(L.list)) outside++;
          }
        }
      CHECK(touched > 0 && outside == 0, "indicazioni a %d° (modifica %d): %d pixel, %d sopra i valori", q * 90, editing,
            touched, outside);
      if (!dump.empty()) saveBmp(fb, dump + "/hints-" + std::to_string(q * 90) + (editing ? "-modifica" : "") + ".bmp");
    }
  }

  printf("ini e tasti: %d, riordini: %d, celle: %d, selezionabili: %d, scelte: %d, schermate: %d\n", counts[0],
         counts[1], counts[2], counts[3], counts[4], counts[5]);
  if (failures) {
    printf("%d controlli falliti\n", failures);
    return 1;
  }
  printf("tutto come il configuratore web\n");
  return 0;
}
