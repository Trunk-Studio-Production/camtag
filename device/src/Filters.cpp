#include "Filters.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "TinyFont.h"

namespace filters {

namespace {

// --- Lettura di screen.ini -----------------------------------------------------

uint32_t crc32Update(uint32_t crc, uint8_t b) {
  crc ^= b;
  for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  return crc;
}

// Legge il file un blocco alla volta, tenendo il CRC di tutto.
class Reader {
 public:
  explicit Reader(FsFile& f) : f_(f) {}
  int get() {
    if (pos_ == len_) {
      const int n = f_.read(buf_, sizeof(buf_));
      if (n <= 0) return -1;
      len_ = n;
      pos_ = 0;
    }
    const uint8_t b = buf_[pos_++];
    crc_ = crc32Update(crc_, b);
    return b;
  }
  uint32_t crc() const { return ~crc_; }

 private:
  FsFile& f_;
  uint8_t buf_[64];
  int len_ = 0, pos_ = 0;
  uint32_t crc_ = 0xFFFFFFFFu;
};

// Rotazione in gradi ("0", "90", …) -> quarti di giro, -1 se non valida.
int quarter(const char* s) {
  static const char* const kDeg[4] = {"0", "90", "180", "270"};
  for (int q = 0; q < 4; q++)
    if (!strcmp(s, kDeg[q])) return q;
  return -1;
}

int keyIndex(char k) { return k >= 'A' && k <= 'D' ? k - 'A' : -1; }

// Chiama fn(item, index) per ogni voce separata da '|'. Le voci vuote contano.
template <typename Fn>
int eachItem(char* v, Fn fn) {
  int i = 0;
  if (!*v) return 0;
  for (char* p = v;; i++) {
    char* bar = strchr(p, '|');
    if (bar) *bar = 0;
    fn(p, i);
    if (!bar) return i + 1;
    p = bar + 1;
  }
}

bool parseRect(const char* v, Rect& r) {
  int x, y, w, h;
  if (sscanf(v, "%d,%d,%d,%d", &x, &y, &w, &h) != 4) return false;
  r.x = static_cast<int16_t>(x);
  r.y = static_cast<int16_t>(y);
  r.w = static_cast<int16_t>(w);
  r.h = static_cast<int16_t>(h);
  return true;
}

// Testo di un'indicazione: ASCII, più ▲ ▼ − in UTF-8.
void copyHint(const char* src, char* dst) {
  int n = 0;
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(src); *p && n < kHintLen - 1;) {
    if (p[0] == 0xE2 && p[1] == 0x96 && p[2] == 0xB2) {
      dst[n++] = tinyfont::kUp;
      p += 3;
    } else if (p[0] == 0xE2 && p[1] == 0x96 && p[2] == 0xBC) {
      dst[n++] = tinyfont::kDown;
      p += 3;
    } else if (p[0] == 0xE2 && p[1] == 0x88 && p[2] == 0x92) {
      dst[n++] = '-';
      p += 3;
    } else if (*p < 0x80) {
      dst[n++] = static_cast<char>(*p++);
    } else {
      p++;
    }
  }
  dst[n] = 0;
}

void parseHints(char* v, char (*out)[kHintLen]) {
  eachItem(v, [&](char* item, int) {
    const int k = keyIndex(item[0]);
    if (k >= 0 && item[1] == ':') copyHint(item + 2, out[k]);
  });
}

int sign(const char* s) {
  const size_t n = strlen(s);
  return n && s[n - 1] == '+' ? 1 : -1;
}

void apply(Config& c, const char* key, char* v, bool& type) {
  Layout* L = nullptr;
  const char* sub = nullptr;
  if (!strncmp(key, "land.", 5)) {
    L = &c.land;
    sub = key + 5;
  } else if (!strncmp(key, "port.", 5)) {
    L = &c.port;
    sub = key + 5;
  }
  if (L) {
    if (!strcmp(sub, "nd")) parseRect(v, L->nd);
    else if (!strcmp(sub, "dio")) parseRect(v, L->dio);
    else if (!strcmp(sub, "list")) parseRect(v, L->list);
    else if (!strcmp(sub, "listRow")) L->row = static_cast<int16_t>(atoi(v));
    else if (!strcmp(sub, "listNum")) L->num = static_cast<int16_t>(atoi(v));
    return;
  }
  if (!strcmp(key, "type")) {
    type = !strcmp(v, "filters");
  } else if (!strcmp(key, "nd.count")) {
    c.ndCount = static_cast<uint8_t>(atoi(v));
  } else if (!strcmp(key, "nd.parts")) {
    eachItem(v, [&](char* item, int i) {
      if (i >= kMaxSteps) return;
      char* plus = strchr(item, '+');
      c.ndParts[i][0] = static_cast<uint8_t>(atoi(item));  // "-" -> 0
      c.ndParts[i][1] = plus ? static_cast<uint8_t>(atoi(plus + 1)) : 0;
    });
  } else if (!strcmp(key, "nd.start")) {
    c.ndStart = static_cast<uint8_t>(atoi(v));
  } else if (!strcmp(key, "dio.count")) {
    c.dioCount = static_cast<uint8_t>(atoi(v));
  } else if (!strcmp(key, "dio.slots")) {
    eachItem(v, [&](char* item, int i) {
      if (i < kMaxSteps) c.dioSlots[i] = static_cast<uint8_t>(atoi(item));
    });
  } else if (!strcmp(key, "dio.start")) {
    c.dioStart = static_cast<uint8_t>(atoi(v));
  } else if (!strcmp(key, "kit.count")) {
    c.kitCount = static_cast<uint8_t>(atoi(v));
  } else if (!strcmp(key, "kit.slots")) {
    eachItem(v, [&](char* item, int i) {
      if (i < kMaxKit) c.kitSlots[i] = atoi(item) == 2 ? 2 : 1;
    });
  } else if (!strcmp(key, "stages.count")) {
    c.stageCount = static_cast<uint8_t>(atoi(v));
  } else if (!strcmp(key, "stages.start")) {
    eachItem(v, [&](char* item, int i) {
      if (i < kMaxStages) c.stageStart[i] = item[0] == '-' || !item[0] ? -1 : static_cast<int8_t>(atoi(item));
    });
  } else if (!strcmp(key, "stages.settle")) {
    c.settleS = static_cast<uint8_t>(atoi(v));
  } else if (!strcmp(key, "edit.timeout")) {
    c.editTimeoutS = static_cast<uint8_t>(atoi(v));
  } else if (!strncmp(key, "keys.", 5)) {
    const int q = quarter(key + 5);
    if (q < 0) return;
    eachItem(v, [&](char* item, int) {
      const int k = keyIndex(item[0]);
      if (k < 0 || item[1] != ':') return;
      const char* role = item + 2;
      c.keys[q][k].role = !strncmp(role, "nd", 2) ? kNd : !strncmp(role, "dio", 3) ? kDio : kNone;
      c.keys[q][k].dir = static_cast<int8_t>(sign(role));
    });
  } else if (!strncmp(key, "hints.", 6)) {
    const int q = quarter(key + 6);
    if (q >= 0) parseHints(v, c.hints[q]);
  } else if (!strncmp(key, "edit.keys.", 10)) {
    const int q = quarter(key + 10);
    if (q < 0) return;
    eachItem(v, [&](char* item, int) {
      const int k = keyIndex(item[0]);
      if (k < 0 || item[1] != ':') return;
      if (!strncmp(item + 2, "row", 3) && k < 2) c.editRow[q][k] = static_cast<int8_t>(sign(item));
      if (!strncmp(item + 2, "val", 3) && k >= 2) c.editVal[q][k - 2] = static_cast<int8_t>(sign(item));
    });
  } else if (!strncmp(key, "edit.hints.", 11)) {
    const int q = quarter(key + 11);
    if (q >= 0) parseHints(v, c.editHints[q]);
  }
}

bool layoutOk(const Layout& L, const Config& c) {
  if (!L.nd.ok()) return false;
  if (c.dioCount && !L.dio.ok()) return false;
  if (c.stageCount && (!L.list.ok() || L.row <= 0 || L.num <= 0 || L.num >= L.list.w)) return false;
  return true;
}

}  // namespace

bool parse(FsFile& f, Config& c) {
  c = Config();
  if (!f) return false;
  Reader r(f);
  char key[24];
  char val[512];
  bool header = false, type = false, first = true;
  for (int ch = r.get(); ch >= 0;) {
    // Una riga: chiave fino a '=', poi il valore (saltato se la chiave non serve).
    int kn = 0, vn = 0;
    bool inVal = false, comment = ch == '#';
    for (; ch >= 0 && ch != '\n'; ch = r.get()) {
      if (ch == '\r') continue;
      if (comment) {
        if (first && vn < static_cast<int>(sizeof(val)) - 1) val[vn++] = static_cast<char>(ch);
      } else if (!inVal) {
        if (ch == '=') inVal = true;
        else if (kn < static_cast<int>(sizeof(key)) - 1) key[kn++] = static_cast<char>(ch);
      } else if (vn < static_cast<int>(sizeof(val)) - 1) {
        val[vn++] = static_cast<char>(ch);
      }
    }
    if (ch == '\n') ch = r.get();
    key[kn] = 0;
    val[vn] = 0;
    if (comment && first) header = strstr(val, "screen v3") != nullptr;
    else if (!comment && inVal) apply(c, key, val, type);
    first = false;
  }
  c.crc = r.crc();

  if (!header || !type) return false;
  if (c.ndCount < 1 || c.ndCount > kMaxSteps || c.dioCount > kMaxSteps || c.kitCount > kMaxKit) return false;
  if (c.stageCount > kMaxStages || (c.stageCount && !c.kitCount)) return false;
  if (!layoutOk(c.land, c) || !layoutOk(c.port, c)) return false;
  if (c.ndStart >= c.ndCount) c.ndStart = 0;
  if (c.dioStart >= (c.dioCount ? c.dioCount : 1)) c.dioStart = 0;
  for (int i = 0; i < c.stageCount; i++)
    if (c.stageStart[i] >= c.kitCount) c.stageStart[i] = -1;
  if (!c.settleS) c.settleS = 2;
  if (!c.editTimeoutS) c.editTimeoutS = 5;
  c.ok = true;
  return true;
}

// --- Regole degli stadi ---------------------------------------------------------

void startState(const Config& c, State& s) {
  s = State();
  s.nd = s.ndShown = c.ndStart;
  s.dio = s.dioShown = c.dioStart;
  for (int i = 0; i < c.stageCount; i++) s.stages[i] = c.stageStart[i];
}

bool validState(const Config& c, const State& s) {
  if (s.nd >= c.ndCount || s.ndShown >= c.ndCount) return false;
  const int dioSteps = c.dioCount ? c.dioCount : 1;
  if (s.dio >= dioSteps || s.dioShown >= dioSteps) return false;
  for (int i = 0; i < kMaxStages; i++) {
    if (s.stages[i] >= c.kitCount || (i >= c.stageCount && s.stages[i] >= 0)) return false;
    for (int j = 0; j < i; j++)
      if (s.stages[i] >= 0 && s.stages[i] == s.stages[j]) return false;
  }
  return true;
}

int ndUsed(const Config& c, int nd) {
  if (nd < 0 || nd >= c.ndCount) return 0;
  return (c.ndParts[nd][0] ? 1 : 0) + (c.ndParts[nd][1] ? 1 : 0);
}

int dioUsed(const Config& c, int dio) {
  if (!c.dioCount || dio < 0 || dio >= c.dioCount) return 0;
  return c.dioSlots[dio];
}

void stageRange(const Config& c, int nd, int dio, int& lo, int& hi) {
  const int n = c.stageCount, d = dioUsed(c, dio);
  lo = ndUsed(c, nd);
  if (lo > n) lo = n;
  hi = d && lo + d <= n ? n - d : n;
}

static int slotsOf(const Config& c, int g) { return g >= 0 && g < c.kitCount ? c.kitSlots[g] : 1; }

bool fitStages(const Config& c, int8_t* stages, int lo, int hi, int back) {
  const int n = c.stageCount;
  int8_t old[kMaxStages], k[kMaxStages];
  int pos[kMaxStages], size[kMaxStages], p[kMaxStages], m = 0;
  for (int i = 0; i < n; i++) {
    old[i] = stages[i];
    if (stages[i] >= 0) {
      k[m] = stages[i];
      pos[m] = i - back;
      size[m] = slotsOf(c, stages[i]);
      m++;
    }
  }
  for (;;) {
    int cur = lo;
    for (int j = 0; j < m; j++) {
      p[j] = pos[j] > cur ? pos[j] : cur;
      cur = p[j] + size[j];
    }
    cur = hi;
    for (int j = m - 1; j >= 0; j--) {
      if (p[j] > cur - size[j]) p[j] = cur - size[j];
      cur = p[j];
    }
    if (!m || p[0] >= lo) break;
    m--;  // non c'è posto: esce il vetro più avanti
  }
  for (int i = 0; i < n; i++) stages[i] = -1;
  for (int j = 0; j < m; j++)
    if (p[j] >= 0 && p[j] < n) stages[p[j]] = k[j];
  for (int i = 0; i < n; i++)
    if (stages[i] != old[i]) return true;
  return false;
}

bool settle(const Config& c, State& s) {
  if (s.ndShown == s.nd && s.dioShown == s.dio) return false;
  const int was = ndUsed(c, s.ndShown), now = ndUsed(c, s.nd);
  s.ndShown = s.nd;
  s.dioShown = s.dio;
  if (c.stageCount) {
    int lo, hi;
    stageRange(c, s.nd, s.dio, lo, hi);
    fitStages(c, s.stages, lo, hi, was > now ? was - now : 0);
  }
  return true;
}

static int dioStart(const Config& c, const int8_t* stages, int lo, int hi) {
  int e = lo;
  for (int i = lo; i < hi; i++)
    if (stages[i] >= 0) e = i + slotsOf(c, stages[i]);
  return e < hi ? e : hi;
}

void stageCells(const Config& c, const int8_t* stages, int nd, int dio, Cell* out) {
  const int n = c.stageCount;
  int lo, hi;
  stageRange(c, nd, dio, lo, hi);
  const int d = n - hi, ds = d ? dioStart(c, stages, lo, hi) : n;
  for (int i = 0, cont = 0; i < n; i++) {
    Cell& cell = out[i];
    cell = Cell();
    const int g = i >= lo && i < ds && stages[i] >= 0 && stages[i] < c.kitCount ? stages[i] : -1;
    if (i < lo) {
      cell.kind = kCellNd;
      cell.glass = static_cast<int8_t>(c.ndParts[nd][i]);
    } else if (i == ds) {
      cell.kind = kCellDio;
      cell.span = static_cast<uint8_t>(d);
      cell.insert = ds < hi;
    } else if (i > ds && i < ds + d) {
      cell.kind = kCellCont;
    } else if (i >= ds + d) {
      cell.kind = kCellFront;
    } else if (i < cont) {
      cell.kind = kCellCont;
    } else if (g >= 0) {
      const int span = slotsOf(c, g) < hi - i ? slotsOf(c, g) : hi - i;
      cell.kind = kCellGlass;
      cell.glass = static_cast<int8_t>(g);
      cell.span = static_cast<uint8_t>(span);
      cont = i + span;
    } else {
      cell.kind = kCellEmpty;
    }
  }
}

int editableStages(const Config& c, const int8_t* stages, int nd, int dio, int* out) {
  Cell cells[kMaxStages];
  stageCells(c, stages, nd, dio, cells);
  int m = 0;
  for (int i = 0; i < c.stageCount; i++) {
    const Cell& x = cells[i];
    if (x.kind == kCellGlass || x.kind == kCellEmpty || (x.kind == kCellDio && x.insert)) out[m++] = i;
  }
  return m;
}

int stageOptions(const Config& c, const int8_t* stages, int i, int hi, int* out) {
  int m = 0;
  out[m++] = -1;
  for (int g = 0; g < c.kitCount; g++) {
    bool used = false;
    for (int j = 0; j < c.stageCount; j++)
      if (j != i && stages[j] == g) used = true;
    if (used) continue;
    if (c.kitSlots[g] < 2 || (i + 1 < hi && stages[i + 1] < 0)) out[m++] = g;
  }
  return m;
}

int editFix(const Config& c, const State& s, int sel) {
  int ok[kMaxStages];
  const int m = editableStages(c, s.stages, s.ndShown, s.dioShown, ok);
  if (!m) return -1;
  int best = -1;
  for (int j = 0; j < m; j++) {
    if (ok[j] == sel) return sel;
    if (ok[j] < sel) best = ok[j];
  }
  return best >= 0 ? best : ok[0];
}

int editNav(const Config& c, const State& s, int sel, int step) {
  int ok[kMaxStages];
  const int m = editableStages(c, s.stages, s.ndShown, s.dioShown, ok);
  if (!m) return -1;
  int at = -1;
  for (int j = 0; j < m; j++)
    if (ok[j] == sel) at = j;
  at += step;
  if (at < 0) at = 0;
  if (at > m - 1) at = m - 1;
  return ok[at];
}

void editValue(const Config& c, State& s, int sel, int dir) {
  if (sel < 0 || sel >= c.stageCount) return;
  int lo, hi;
  stageRange(c, s.ndShown, s.dioShown, lo, hi);
  int opts[kMaxKit + 1];
  const int m = stageOptions(c, s.stages, sel, hi, opts);
  int j = -1;
  for (int t = 0; t < m; t++)
    if (opts[t] == s.stages[sel]) j = t;
  s.stages[sel] = static_cast<int8_t>(opts[((j + dir) % m + m) % m]);
}

// --- Composizione -----------------------------------------------------------------

namespace {

bool piece(Canvas& c, const char* dir, bool port, const char* name, int x, int y, int maxH = -1) {
  char path[96];
  snprintf(path, sizeof(path), "%s/%s%s", dir, name, port ? ".p.bmp" : ".bmp");
  FsFile f = filtersOpen(path);
  const bool ok = c.drawBmpAt(f, x, y, maxH) == Canvas::BmpResult::Ok;
  f.close();
  return ok;
}

}  // namespace

bool drawParts(Canvas& c, const Config& cfg, const State& s, const char* dir, int sel, uint8_t parts) {
  const bool port = c.height() > c.width();
  const Layout& L = cfg.layout(port);
  char name[16];
  bool ok = true;
  if ((parts & kPartNd) && L.nd.ok()) {
    snprintf(name, sizeof(name), "nd%u", s.nd);
    ok &= piece(c, dir, port, name, L.nd.x, L.nd.y);
  }
  if ((parts & kPartDio) && cfg.dioCount && L.dio.ok()) {
    snprintf(name, sizeof(name), "dio%u", s.dio);
    ok &= piece(c, dir, port, name, L.dio.x, L.dio.y);
  }
  if ((parts & kPartList) && cfg.stageCount) {
    Cell cells[kMaxStages];
    stageCells(cfg, s.stages, s.ndShown, s.dioShown, cells);
    const int selEnd = sel >= 0 && sel < cfg.stageCount ? sel + cells[sel].span : -1;
    for (int i = 0; i < cfg.stageCount; i++) {
      const Cell& x = cells[i];
      const int y = L.list.y + i * L.row;
      const bool on = i >= sel && i < selEnd;
      snprintf(name, sizeof(name), on ? "sts%d" : "st%d", i + 1);
      ok &= piece(c, dir, port, name, L.list.x, y);
      if (x.kind != kCellCont) {
        switch (x.kind) {
          case kCellNd:    snprintf(name, sizeof(name), "fxd%d", x.glass); break;
          case kCellDio:   snprintf(name, sizeof(name), on ? "fxso%u" : "fxo%u", s.dioShown); break;
          case kCellGlass: snprintf(name, sizeof(name), on ? "fxs%d" : "fx%d", x.glass); break;
          default:         snprintf(name, sizeof(name), on ? "fxsn" : "fxn"); break;
        }
        ok &= piece(c, dir, port, name, L.list.x + L.num, y, L.row * x.span);
      }
      // Separatore in cima a ogni riga dopo la prima; nel secondo stadio di un
      // vetro da due solo sotto la colonna del numero.
      if (i > 0) c.fillRect(L.list.x, y, x.kind == kCellCont ? L.num : L.list.w, 2, true);
    }
  }
  return ok;
}

void drawHints(Canvas& phys, const Canvas& natural, const Config& cfg, int q, bool editing) {
  // Centro dei tasti sul bordo in basso, nella posizione naturale (KEY_POS).
  static constexpr float kKeyPos[4] = {0.14f, 0.44f, 0.57f, 0.86f};
  q &= 3;
  // Bordo dei tasti orizzontale per chi guarda (0° e 180°) o verticale (90°, 270°):
  // di lato il testo corre verso l'interno, quindi più piccolo.
  const bool along = q % 2 == 0;
  const int scale = along ? 3 : 2, pad = 2 + 2 * scale;
  for (int k = 0; k < 4; k++) {
    const char* t = editing ? cfg.editHints[q][k] : cfg.hints[q][k];
    if (!*t) continue;
    const int bw = tinyfont::textWidth(t, scale) + 2 * pad, bh = 7 * scale + 2 * pad;
    const int ext = along ? bh : bw;
    const int d = ext / 2 + 4 > 20 ? ext / 2 + 4 : 20;  // distanza del centro dal bordo
    int px, py, lx, ly;
    natural.toPanel(static_cast<int>(kKeyPos[k] * natural.width() + 0.5f), natural.height() - d, px, py);
    phys.fromPanel(px, py, lx, ly);
    const int x0 = lx - bw / 2, y0 = ly - bh / 2;
    phys.fillRect(x0, y0, bw, bh, false);
    phys.strokeRect(x0, y0, bw, bh, 2, true);
    phys.text(x0 + pad, y0 + pad, t, scale, true);
  }
}

}  // namespace filters
