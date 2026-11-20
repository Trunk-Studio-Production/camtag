// CamTag MVP — mostra "card" BMP 1-bit dalla microSD (/camtag) su Xteink X3/X4.
//
// Ogni card può avere due file:  NOME.bmp   (orizzontale, 792x528)
//                                NOME.p.bmp (verticale,   528x792)
// L'accelerometro (solo X3) sceglie orientamento e variante.
//
// Tasti (come nel simulatore dello strumento web):
//         E (lato destro)           = card successiva, in ciclo
//         F (lato sinistro)         = card precedente, in ciclo
//         E tenuto premuto          = blocca/sblocca la rotazione
//         F tenuto premuto          = ricezione delle card via Bluetooth
//         La pressione lunga scatta allo scadere di LONG_PRESS_MS, con il tasto
//         ancora giù; quella breve al rilascio. I tasti li legge keyScan anche
//         durante gli aggiornamenti dello schermo: nessuna pressione va persa.
//         A B C D (bilancieri)      = mai cambio pagina; sulla lens chart di più
//                                     pagine sfogliano le sue pagine, sulla schermata
//                                     filtri cambiano ND e diottria e, tenendo A o B,
//                                     aprono la modifica degli stadi (Filters.h)
//         power                     = spegni subito
// Dopo IDLE_SLEEP_MS senza input si spegne da solo: l'e-paper resta visibile.
// In ricezione: E o F escono; senza collegamenti per RECEIVE_IDLE_MS esce da sola.

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <Imu.h>
#include <InputManager.h>
#include <PowerManager.h>
#include <Preferences.h>
#include <SDCardManager.h>
#include <SPI.h>
#include <XteinkDetect.h>
#include <driver/gpio.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "BleUpload.h"
#include "Canvas.h"
#include "Filters.h"

namespace {

constexpr const char* kCardDir = "/camtag";
#ifdef CAMTAG_DEBUG
constexpr uint32_t IDLE_SLEEP_MS = 600000;  // prove al banco: niente corse col power
#else
constexpr uint32_t IDLE_SLEEP_MS = 20000;
#endif
constexpr uint32_t RECEIVE_IDLE_MS = 180000;
// Durante un trasferimento il display si aggiorna al massimo ogni tanto: ogni
// aggiornamento ferma la scrittura sulla microSD.
constexpr uint32_t RECEIVE_REDRAW_MS = 4000;
constexpr int LOW_BATTERY_PERCENT = 20;
// Ogni N cambi di card usa un refresh completo per togliere il ghosting.
constexpr uint8_t FULL_REFRESH_EVERY = 6;
constexpr uint32_t LONG_PRESS_MS = 800;
constexpr uint32_t BANNER_MS = 1500;

// --- Tasti fisici -> tasti logici dell'SDK ------------------------------------
// I quattro tasti in basso (A|B, C|D) stanno sull'ingresso analogico 1, che
// l'SDK chiama BACK/CONFIRM/LEFT/RIGHT; i due laterali (E, F) sull'ingresso 2,
// UP/DOWN. Verificato sull'X3 (con CAMTAG_DEBUG il monitor seriale stampa il
// tasto logico di ogni pressione): F, sul lato sinistro, è UP; E è DOWN.
constexpr uint8_t kKeyE = InputManager::BTN_DOWN;
constexpr uint8_t kKeyF = InputManager::BTN_UP;
// Bilancieri in basso: A|B a sinistra, C|D a destra (dispositivo verticale).
// Verificato sull'X3: da sinistra a destra sono BACK, CONFIRM, LEFT, RIGHT.
constexpr uint8_t kKeyA = InputManager::BTN_BACK;
constexpr uint8_t kKeyB = InputManager::BTN_CONFIRM;
constexpr uint8_t kKeyC = InputManager::BTN_LEFT;
constexpr uint8_t kKeyD = InputManager::BTN_RIGHT;

// --- Orientamento -----------------------------------------------------------
// Mappatura assi IMU -> pannello, tarata sull'X3 con scripts/taratura.py: in
// verticale con i tasti in basso l'IMU dà ax = -1 g, con i tasti a sinistra
// ay = +1 g (con CAMTAG_DEBUG il monitor seriale stampa ax/ay/az).
constexpr bool kImuSwapXY = true;
constexpr float kImuSignX = -1.0f;
constexpr float kImuSignY = 1.0f;
constexpr float kMinTiltG = 0.5f;     // sotto questa inclinazione non decide
constexpr float kMarginG = 0.25f;     // isteresi tra i due assi
constexpr float kFlatG = 0.85f;       // appoggiato piatto: tiene l'orientamento
// Rotazione fisica, in quarti di giro in senso orario dalla posizione naturale
// (verticale, tasti in basso), per ogni Canvas::Rotation. Verificato sull'X3:
// la posizione naturale è ROT_270, e ROT_0 è con i tasti a destra.
constexpr uint8_t kQuarterTurns[4] = {3, 2, 1, 0};  // ROT_0, ROT_90, ROT_180, ROT_270
constexpr Canvas::Rotation kNaturalRot = Canvas::ROT_270;
constexpr uint32_t IMU_POLL_MS = 150;
constexpr uint32_t ROTATE_STABLE_MS = 600;

#ifdef CAMTAG_DEBUG
#define LOGF(...) Serial.printf(__VA_ARGS__)
#else
#define LOGF(...) \
  do {            \
  } while (0)
#endif

struct Card {
  String base;  // nome senza ".bmp" / ".p.bmp"
  bool landscape = false;
  bool portrait = false;
};

EInkDisplay* display = nullptr;
InputManager input;
Preferences prefs;
freeink::Imu imu;

std::vector<Card> cards;
int current = 0;
Canvas::Rotation rotation = Canvas::ROT_0;  // orientamento desiderato del dispositivo
// Posizione vera del dispositivo, seguita anche con la rotazione bloccata: le
// indicazioni dei tasti e i bilancieri della schermata filtri vanno con lei.
Canvas::Rotation physical = Canvas::ROT_0;
Canvas::Rotation drawnRot = Canvas::ROT_0;  // rotazione usata per la card a schermo
bool rotationLocked = false;
uint8_t changesSinceFull = 0;
bool fastShadow = false;  // sullo schermo restano ombre di aggiornamenti veloci
uint32_t lastActivityMs = 0;
uint32_t lastImuPollMs = 0;
Canvas::Rotation pendingRot = Canvas::ROT_0;
uint32_t pendingSinceMs = 0;
uint32_t bannerUntilMs = 0;
bool sdReady = false;

// Modalità ricezione via Bluetooth.
bool receiving = false;
// Una pressione lunga ha già fatto la sua azione: il rilascio che segue non conta.
bool holdFired = false;
char bleName[16] = "";
uint16_t drawnFiles = 0;
uint32_t lastReceiveDrawMs = 0;

// Schermata filtri (specifica: docs/filtri.md; logica e composizione in Filters.cpp).
filters::Config fltCfg;
filters::State flt;
String fltDir;                // cartella dei pezzi, "/camtag/NN_filters"
int fltSel = -1;              // in modifica: stadio selezionato; -1 = fuori dalla modifica
uint32_t fltSettleAt = 0;     // quando rimettere a posto gli stadi; 0 = nulla in sospeso
uint32_t fltEditUntil = 0;    // fine della modifica senza tocchi
bool fltDrawn = false;        // il framebuffer ha la schermata filtri: si aggiorna a pezzi

Canvas canvas(Canvas::Rotation rot) {
  return Canvas(display->getFrameBuffer(), display->getDisplayWidth(), display->getDisplayHeight(),
                display->getDisplayWidthBytes(), rot);
}

// --- Elenco card --------------------------------------------------------------
void loadCardList() {
  cards.clear();
  for (const String& n : SdMan.listFiles(kCardDir, 400)) {
    if (n.startsWith(".")) continue;  // file "._x.bmp" creati da macOS
    String lower = n;
    lower.toLowerCase();
    if (!lower.endsWith(".bmp")) continue;
    const bool port = lower.endsWith(".p.bmp");
    const String base = n.substring(0, n.length() - (port ? 6 : 4));
    auto it = std::find_if(cards.begin(), cards.end(), [&](const Card& c) { return c.base == base; });
    if (it == cards.end()) {
      cards.push_back({base});
      it = cards.end() - 1;
    }
    (port ? it->portrait : it->landscape) = true;
  }
  std::sort(cards.begin(), cards.end(), [](const Card& a, const Card& b) { return a.base.compareTo(b.base) < 0; });
  LOGF("[camtag] %u card in %s\n", static_cast<unsigned>(cards.size()), kCardDir);
}

// Rotazione effettiva per una card: usa la variante adatta all'orientamento;
// se manca, ripiega sull'altra variante nel verso più vicino.
Canvas::Rotation rotationFor(const Card& c, Canvas::Rotation want) {
  const bool wantPortrait = Canvas::isPortrait(want);
  if (wantPortrait ? c.portrait : c.landscape) return want;
  return static_cast<Canvas::Rotation>((want + 3) % 4);
}

String pathFor(const Card& c, Canvas::Rotation rot) {
  return String(kCardDir) + "/" + c.base + (Canvas::isPortrait(rot) ? ".p.bmp" : ".bmp");
}

// --- Accelerometro ----------------------------------------------------------
// Restituisce l'orientamento letto, oppure `fallback` se il dato è ambiguo.
Canvas::Rotation readOrientation(Canvas::Rotation fallback) {
  if (!imu.present()) return fallback;
  freeink::Imu::Sample s;
  if (!imu.read(s)) return fallback;
  float x = kImuSwapXY ? s.ay : s.ax;
  float y = kImuSwapXY ? s.ax : s.ay;
  x *= kImuSignX;
  y *= kImuSignY;
#ifdef CAMTAG_DEBUG
  static uint32_t lastLog = 0;
  if (millis() - lastLog > 250) {  // abbastanza fitto per scripts/taratura.py
    lastLog = millis();
    LOGF("[imu] ax=%.2f ay=%.2f az=%.2f\n", s.ax, s.ay, s.az);
  }
#endif
  if (fabsf(s.az) > kFlatG) return fallback;
  if (fabsf(x) < kMinTiltG && fabsf(y) < kMinTiltG) return fallback;
  if (fabsf(x) > fabsf(y) + kMarginG) return x > 0 ? Canvas::ROT_0 : Canvas::ROT_180;
  if (fabsf(y) > fabsf(x) + kMarginG) return y > 0 ? Canvas::ROT_90 : Canvas::ROT_270;
  return fallback;
}

// Media di qualche lettura all'avvio, quando non c'è tempo per l'isteresi.
Canvas::Rotation settleOrientation(Canvas::Rotation fallback) {
  int votes[4] = {0, 0, 0, 0};
  for (int i = 0; i < 6; i++) {
    votes[readOrientation(fallback)]++;
    delay(15);
  }
  return static_cast<Canvas::Rotation>(std::max_element(votes, votes + 4) - votes);
}

// --- Disegno ------------------------------------------------------------------
int batteryPercent() {
  BatteryMonitor battery;
  uint16_t pct = 0;
  return battery.readPercentageChecked(pct) ? static_cast<int>(pct) : -1;
}

// --- Tasti -------------------------------------------------------------------
// Un aggiornamento dello schermo ferma il loop per 0,6–2,6 s: leggendo i tasti solo
// lì, una pressione breve fatta nel frattempo andava persa. Li legge invece un compito
// a parte ogni 5 ms (anche durante gli aggiornamenti, che aspettano con delay()) e
// mette pressioni e rilasci in coda con l'ora; il loop li smaltisce appena può e
// distingue breve e lunga dalla durata vera. Il power resta all'InputManager.
// Due analogRead insieme fanno fallire uno dei due, che legge 0 (= D o E premuti):
// il loop (input.update) e il compito si passano l'ADC con adcLock.
struct KeyEvent {
  uint8_t btn;  // InputManager::BTN_*
  bool down;
  uint32_t t;  // millis() della lettura
};
SemaphoreHandle_t adcLock = nullptr;
QueueHandle_t keyQueue = nullptr;

void keyScan(void*) {
  int stable[2] = {-1, -1}, seen[2] = {-1, -1}, count[2] = {0, 0};
  for (;;) {
    InputManager::ButtonAdcSample g[2];
    xSemaphoreTake(adcLock, portMAX_DELAY);
    input.readButtonAdc(g[0], g[1]);
    xSemaphoreGive(adcLock);
    const uint32_t now = millis();
    for (int i = 0; i < 2; i++) {
      // Passando da un capo all'altro della scala si attraversano le altre fasce:
      // conta solo un valore stabile per due letture (10 ms).
      count[i] = g[i].button == seen[i] ? count[i] + 1 : 1;
      seen[i] = g[i].button;
      if (count[i] != 2 || seen[i] == stable[i]) continue;
      if (stable[i] >= 0) {
        const KeyEvent up{static_cast<uint8_t>(stable[i]), false, now};
        xQueueSend(keyQueue, &up, 0);
        LOGF("[adc] su %d t=%lu\n", stable[i], static_cast<unsigned long>(now));
      }
      if (seen[i] >= 0) {
        const KeyEvent down{static_cast<uint8_t>(seen[i]), true, now};
        xQueueSend(keyQueue, &down, 0);
        LOGF("[adc] giu %d raw=%d t=%lu\n", seen[i], g[i].raw, static_cast<unsigned long>(now));
      }
      stable[i] = seen[i];
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

#ifdef CAMTAG_DEBUG
// Inizio e fine di ogni aggiornamento, per taratura.py reattivita.
#define EPD_BEGIN(mode)          \
  const uint32_t epdT0 = millis(); \
  LOGF("[epd] inizio t=%lu\n", static_cast<unsigned long>(epdT0))
#define EPD_END(mode) LOGF("[epd] %s da %lu a %lu\n", mode, static_cast<unsigned long>(epdT0), millis())
#else
#define EPD_BEGIN(mode)
#define EPD_END(mode)
#endif

void present(bool forceFull) {
  const bool full = forceFull || changesSinceFull >= FULL_REFRESH_EVERY;
  // Pannello spento subito dopo ogni aggiornamento: lasciato alimentato,
  // l'immagine scivola in pochi minuti verso quella di prima (fantasma).
  EPD_BEGIN();
  display->displayBuffer(full ? EInkDisplay::FULL_REFRESH : EInkDisplay::HALF_REFRESH, true);
  EPD_END(full ? "completo" : "mezzo");
  changesSinceFull = full ? 0 : changesSinceFull + 1;
  fastShadow = false;  // mezzo e completo portano ogni pixel al suo valore
}

// Aggiornamento veloce (schermata filtri): anche qui il pannello si spegne subito.
// Lascia ombra: la toglie il mezzo refresh quando ND e diottria restano fermi.
void presentFast() {
  EPD_BEGIN();
  display->displayBuffer(EInkDisplay::FAST_REFRESH, true);
  EPD_END("veloce");
  fastShadow = true;
}

// --- Schermata filtri: dati ----------------------------------------------------------
bool isFilters(const Card& c) {
  const int us = c.base.indexOf('_');
  return us >= 0 && c.base.substring(us + 1) == "filters";
}

bool filtersOn() { return !cards.empty() && fltCfg.ok && isFilters(cards[current]); }

void logFilters(const char* what) {
#ifdef CAMTAG_DEBUG
  char st[3 * filters::kMaxStages + 1] = "";
  for (int i = 0, n = 0; i < fltCfg.stageCount; i++)
    n += snprintf(st + n, sizeof(st) - n, i ? ",%d" : "%d", flt.stages[i]);
  LOGF("[filtri] %s nd=%u dio=%u stadi_nd=%u stadi_dio=%u sel=%d stadi=%s\n", what, flt.nd, flt.dio, flt.ndShown,
       flt.dioShown, fltSel, st);
#else
  (void)what;
#endif
}

void saveFilters() {
  if (!fltCfg.ok) return;
  prefs.putULong("fltCrc", fltCfg.crc);
  prefs.putBytes("flt", &flt, sizeof(flt));
}

// Legge screen.ini della schermata filtri, se c'è, e riprende lo stato salvato
// (ND, diottria, stadi) se il file è lo stesso; altrimenti i valori di partenza.
void loadFilters() {
  fltCfg = filters::Config();
  fltSel = -1;
  fltSettleAt = 0;
  fltDrawn = false;
  const auto it = std::find_if(cards.begin(), cards.end(), isFilters);
  if (it == cards.end()) return;
  fltDir = String(kCardDir) + "/" + it->base;
  FsFile f = SdMan.open((fltDir + "/screen.ini").c_str());
  const bool ok = filters::parse(f, fltCfg);
  f.close();
  if (!ok) {
    LOGF("[filtri] %s/screen.ini mancante o non v3: solo lo sfondo\n", fltDir.c_str());
    return;
  }
  LOGF("[filtri] screen.ini: nd=%u dio=%u vetri=%u stadi=%u\n", fltCfg.ndCount, fltCfg.dioCount, fltCfg.kitCount,
       fltCfg.stageCount);
  filters::State saved;
  if (prefs.getULong("fltCrc", 0) == fltCfg.crc && prefs.getBytes("flt", &saved, sizeof(saved)) == sizeof(saved) &&
      filters::validState(fltCfg, saved))
    flt = saved;
  else
    filters::startState(fltCfg, flt);
  logFilters("caricati");
}

void showMessage(const char* line1, const char* line2) {
  Canvas c = canvas(rotation);
  c.clear();
  c.strokeRect(16, 16, c.width() - 32, c.height() - 32, 4, true);
  c.textCentered(c.height() / 2 - 60, "CAMTAG", Canvas::isPortrait(rotation) ? 6 : 8);
  c.textCentered(c.height() / 2 + 20, line1, Canvas::isPortrait(rotation) ? 3 : 4);
  if (line2) c.textCentered(c.height() / 2 + 70, line2, 2);
  present(true);
  prefs.putString("shown", "");  // al prossimo avvio ridisegna
}

// Indicazioni nel verso del contenuto: con la rotazione bloccata restano ferme con lui
// (chi monta l'X3 a testa in giù vuole vedere tutto a testa in giù).
void drawHints() {
  Canvas content = canvas(rotation);
  const Canvas natural = canvas(kNaturalRot);
  filters::drawHints(content, natural, fltCfg, kQuarterTurns[rotation], fltSel >= 0);
}

enum class Refresh { Fast, Half, Full };

void showCard(int index, Refresh mode, const char* banner = nullptr) {
  if (cards.empty()) return;
  const int n = static_cast<int>(cards.size());
  current = (index % n + n) % n;
  const Card& card = cards[current];
  const Canvas::Rotation rot = rotationFor(card, rotation);
  const String path = pathFor(card, rot);
  LOGF("[camtag] card %d rot %d: %s\n", current, rot * 90, path.c_str());

  Canvas c = canvas(rot);
  c.clear();
  FsFile f = SdMan.open(path.c_str());
  const Canvas::BmpResult res = c.drawBmp(f);
  f.close();
  if (res != Canvas::BmpResult::Ok) {
    showMessage("BMP NON VALIDO", card.base.c_str());
    return;
  }
  fltDrawn = filtersOn();
  if (fltDrawn) {
    if (!filters::drawParts(c, fltCfg, flt, fltDir.c_str(), fltSel, filters::kPartAll))
      LOGF("[filtri] mancano dei pezzi in %s\n", fltDir.c_str());
    drawHints();
  }

  const int pct = batteryPercent();
  if (pct >= 0 && pct < LOW_BATTERY_PERCENT) c.battery(c.width() - 52, 12, pct);
  if (banner) c.banner(banner);

  if (mode == Refresh::Fast)
    presentFast();
  else
    present(mode == Refresh::Full);
  drawnRot = rot;
  prefs.putInt("idx", current);
  prefs.putString("shown", card.base);
  prefs.putUChar("shownRot", rot);
}

void showCard(int index, bool forceFull, const char* banner = nullptr) {
  showCard(index, forceFull ? Refresh::Full : Refresh::Half, banner);
}

// Le pagine della lens chart si chiamano NN_lenschart, NN_lenschart-2, …
bool isLensChart(const Card& c) {
  const int us = c.base.indexOf('_');
  if (us < 0) return false;
  const String rest = c.base.substring(us + 1);
  return rest == "lenschart" || rest.startsWith("lenschart-");
}

// Su una lens chart di più pagine A–D sfogliano solo quelle, in ciclo. I due tasti di
// un bilanciere fanno la stessa cosa: avanza il bilanciere che chi guarda vede a destra
// o in basso, arretra quello a sinistra o in alto (A|B: sinistra a 0°, in alto a 90°,
// a destra a 180°, in basso a 270°). Specifica: lensPageDir nello strumento web.
void stepLensChart(bool rockerAB, Canvas::Rotation rot) {
  std::vector<int> pages;
  for (int i = 0; i < static_cast<int>(cards.size()); i++)
    if (isLensChart(cards[i])) pages.push_back(i);
  const auto it = std::find(pages.begin(), pages.end(), current);
  if (pages.size() < 2 || it == pages.end()) return;
  const uint8_t q = kQuarterTurns[rot];
  const bool abForward = q == 2 || q == 3;
  const int dir = rockerAB == abForward ? 1 : -1;
  const int n = static_cast<int>(pages.size());
  const int at = static_cast<int>(it - pages.begin());
  bannerUntilMs = 0;
  showCard(pages[((at + dir) % n + n) % n], false);
}

// --- Schermata filtri: tasti -----------------------------------------------------------
// Ridisegna solo alcune parti (riquadri, stadi) sopra la schermata già composta.
// Se il framebuffer non la contiene (dopo l'avvio, con un banner) la ricompone tutta.
void filtersUpdate(uint8_t parts, Refresh mode) {
  if (!fltDrawn || bannerUntilMs || drawnRot != rotationFor(cards[current], rotation)) {
    bannerUntilMs = 0;
    showCard(current, mode == Refresh::Fast ? Refresh::Half : mode);
    return;
  }
  Canvas c = canvas(drawnRot);
  filters::drawParts(c, fltCfg, flt, fltDir.c_str(), fltSel, parts);
  if (mode == Refresh::Fast)
    presentFast();
  else
    present(mode == Refresh::Full);
}

// Rimette a posto gli stadi quando ND e diottria sono fermi (simSettle). redraw =
// aggiorna lo schermo se la schermata filtri resta visibile. true se è cambiato qualcosa.
bool filtersSettle(bool redraw) {
  fltSettleAt = 0;
  if (!fltCfg.ok) return false;
  filters::Cell before[filters::kMaxStages], after[filters::kMaxStages];
  filters::stageCells(fltCfg, flt.stages, flt.ndShown, flt.dioShown, before);
  const uint8_t dioWas = flt.dioShown;
  const bool changed = filters::settle(fltCfg, flt);
  if (changed) {
    saveFilters();
    logFilters("riordino");
  }
  if (!redraw || !filtersOn()) return changed;
  filters::stageCells(fltCfg, flt.stages, flt.ndShown, flt.dioShown, after);
  bool same = true;
  for (int i = 0; i < fltCfg.stageCount; i++) {
    const filters::Cell &a = before[i], &b = after[i];
    if (a.kind != b.kind || a.span != b.span || a.glass != b.glass || (b.kind == filters::kCellDio && dioWas != flt.dioShown))
      same = false;
  }
  // Mezzo refresh quando cambiano gli stadi o restano ombre dei cambi veloci di ND e
  // diottria: porta ogni pixel al suo valore.
  if (!same || fastShadow) {
    LOGF("[filtri] mezzo refresh (%s)\n", same ? "ombra" : "stadi");
    filtersUpdate(filters::kPartList, Refresh::Half);
  }
  return changed;
}

void filtersEnterEdit() {
  const bool settled = filtersSettle(false);  // si ridisegna tutto qui sotto
  int ok[filters::kMaxStages];
  if (!fltCfg.stageCount || !filters::editableStages(fltCfg, flt.stages, flt.ndShown, flt.dioShown, ok)) {
    if (settled && fltCfg.stageCount) showCard(current, false);
    return;
  }
  fltSel = ok[0];
  fltEditUntil = millis() + fltCfg.editTimeoutS * 1000UL;
  logFilters("modifica");
  showCard(current, Refresh::Fast);  // cambiano anche le indicazioni dei tasti
}

void filtersExitEdit() {
  fltSel = -1;
  saveFilters();
  logFilters("fine modifica");
  showCard(current, true);  // refresh completo all'uscita
}

// Prima di spegnersi: niente modifica né riordino in sospeso. true se va ridisegnata.
bool filtersFlush() {
  bool dirty = fltSel >= 0;
  fltSel = -1;
  if (fltSettleAt) {
    filtersSettle(false);
    dirty = true;
  }
  if (dirty) saveFilters();
  return dirty && filtersOn();
}

// Pressione breve sulla schermata filtri ('A'…'F'). false = il tasto non è suo
// (E e F fuori dalla modifica cambiano pagina).
bool filtersPress(char k) {
  const int q = kQuarterTurns[rotation];  // con il blocco i tasti restano quelli del contenuto
  if (fltSel >= 0) {
    if (k == 'E' || k == 'F') {
      filtersExitEdit();
      return true;
    }
    if (k == 'A' || k == 'B')
      fltSel = filters::editNav(fltCfg, flt, fltSel, fltCfg.editRow[q][k - 'A']);
    else
      filters::editValue(fltCfg, flt, fltSel, fltCfg.editVal[q][k - 'C']);
    fltSel = filters::editFix(fltCfg, flt, fltSel);
    fltEditUntil = millis() + fltCfg.editTimeoutS * 1000UL;
    logFilters("modifica");
    if (fltSel < 0)
      filtersExitEdit();
    else
      filtersUpdate(filters::kPartList, Refresh::Fast);
    return true;
  }
  const filters::Config::Key a = k <= 'D' ? fltCfg.keys[q][k - 'A'] : filters::Config::Key();
  if (a.role == filters::kNone) {
    // Qualsiasi altro tasto applica subito il riordino in sospeso.
    const bool stay = k != 'E' && k != 'F';
    filtersSettle(stay);
    return stay;
  }
  uint8_t& v = a.role == filters::kNd ? flt.nd : flt.dio;
  const int count = a.role == filters::kNd ? fltCfg.ndCount : fltCfg.dioCount;
  const int next = std::max(0, std::min(count - 1, v + a.dir));
  // Il riquadro grande cambia subito; gli stadi quando i valori restano fermi.
  fltSettleAt = millis() + fltCfg.settleS * 1000UL;
  if (!fltSettleAt) fltSettleAt = 1;
  if (next == v) return true;
  v = static_cast<uint8_t>(next);
  logFilters(a.role == filters::kNd ? "nd" : "diottria");
  filtersUpdate(a.role == filters::kNd ? filters::kPartNd : filters::kPartDio, Refresh::Fast);
  return true;
}

void toggleRotationLock() {
  rotationLocked = !rotationLocked;
  LOGF("[camtag] rotation %s\n", rotationLocked ? "locked" : "auto");
  // Sbloccando, il contenuto va subito nella posizione vera.
  if (!rotationLocked && rotation != physical) {
    rotation = physical;
    LOGF("[camtag] verso %u\n", rotation);
  }
  prefs.putBool("lock", rotationLocked);
  prefs.putUChar("rot", rotation);
  showCard(current, false, rotationLocked ? "ROTAZIONE BLOCCATA" : "ROTAZIONE AUTOMATICA");
  bannerUntilMs = millis() + BANNER_MS;
}

// --- Ricezione via Bluetooth ----------------------------------------------------
void drawReceive(const char* status, const char* detail, bool full) {
#ifdef CAMTAG_DEBUG
  const uint32_t t0 = millis();
#endif
  Canvas c = canvas(rotation);
  const bool port = Canvas::isPortrait(rotation);
  const int mid = c.height() / 2;
  c.clear();
  c.strokeRect(16, 16, c.width() - 32, c.height() - 32, 4, true);
  c.textCentered(mid - 150, "BLUETOOTH", port ? 5 : 7);
  c.textCentered(mid - 60, bleName, port ? 3 : 4);
  c.textCentered(mid + 20, status, port ? 3 : 4);
  if (detail) c.textCentered(mid + 80, detail, 2);
  c.textCentered(c.height() - 64, "E O F PER USCIRE", 2);
  present(full);
  lastReceiveDrawMs = millis();
  LOGF("[camtag] schermo ricezione: %lu ms\n", static_cast<unsigned long>(lastReceiveDrawMs - t0));
  prefs.putString("shown", "");  // al prossimo avvio ridisegna la card
}

void showCurrentOrEmpty() {
  if (!sdReady)
    showMessage("NESSUNA MICROSD", nullptr);
  else if (cards.empty())
    showMessage("NESSUNA CARD", "TIENI F PER RICEVERLE");
  else
    showCard(current, true);
}

void enterReceive() {
  if (!sdReady) {
    showMessage("NESSUNA MICROSD", nullptr);
    return;
  }
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(bleName, sizeof(bleName), "CAMTAG-%02X%02X", static_cast<unsigned>((mac >> 32) & 0xFF),
           static_cast<unsigned>((mac >> 40) & 0xFF));
  bannerUntilMs = 0;
  if (!bleupload::start(bleName)) {
    showMessage("BLUETOOTH NON DISPONIBILE", nullptr);
    return;
  }
  LOGF("[camtag] ricezione: %s\n", bleName);
  receiving = true;
  drawnFiles = 0;
  lastActivityMs = millis();
  drawReceive("IN ATTESA", "APRI CAMTAG SUL TELEFONO", true);
}

void exitReceive() {
  bleupload::stop();
  receiving = false;
  lastActivityMs = millis();
  showCurrentOrEmpty();
}

void receiveDone() {
  // Lascia arrivare al telefono l'OK del COMMIT: il client si scollega da solo.
  for (uint32_t t0 = millis(); bleupload::connected() && millis() - t0 < 1500;) delay(20);
  bleupload::stop();
  receiving = false;
  lastActivityMs = millis();
  loadCardList();
  loadFilters();
  current = 0;
  if (cards.empty()) {
    showMessage("NESSUNA CARD", "TIENI F PER RICEVERLE");
    return;
  }
  showCard(0, true, "CARD AGGIORNATE");
  bannerUntilMs = millis() + BANNER_MS;
}

[[noreturn]] void goToSleep();

void loopReceive() {
  if (!receiving) return;  // E o F l'hanno appena chiusa (shortPress)

  char line[24];
  switch (bleupload::poll()) {
    case bleupload::Event::Connected:
      lastActivityMs = millis();
      drawReceive("CONNESSO", nullptr, false);
      break;
    case bleupload::Event::Disconnected:
      lastActivityMs = millis();
      drawReceive("IN ATTESA", "COLLEGAMENTO CHIUSO", false);
      break;
    case bleupload::Event::Progress:
      lastActivityMs = millis();
      if (bleupload::filesReceived() != drawnFiles && millis() - lastReceiveDrawMs > RECEIVE_REDRAW_MS) {
        drawnFiles = bleupload::filesReceived();
        snprintf(line, sizeof(line), "RICEVUTI %u FILE", static_cast<unsigned>(drawnFiles));
        drawReceive(line, nullptr, false);
      }
      break;
    case bleupload::Event::Done:
      receiveDone();
      return;
    case bleupload::Event::Failed:
      lastActivityMs = millis();
      drawReceive("ERRORE", bleupload::lastError(), false);
      break;
    case bleupload::Event::Sending:
      lastActivityMs = millis();
      drawReceive("INVIO DEL PROGETTO", nullptr, false);
      break;
    case bleupload::Event::Sent:
      lastActivityMs = millis();
      drawReceive("PROGETTO INVIATO", "CARD INVARIATE", false);
      break;
    case bleupload::Event::Aborted:
      lastActivityMs = millis();
      drawReceive("ANNULLATO", "CARD INVARIATE", false);
      break;
    case bleupload::Event::None:
      break;
  }
  if (bleupload::transferring()) lastActivityMs = millis();
  if (millis() - lastActivityMs > RECEIVE_IDLE_MS) exitReceive();
}

[[noreturn]] void goToSleep() {
  LOGF("[camtag] sleep\n");
  if (receiving) {
    bleupload::stop();
    receiving = false;
    showCurrentOrEmpty();  // non lasciare a schermo la ricezione
  }
  // Non lasciare a schermo il banner, la modifica degli stadi o un riordino in sospeso.
  if (filtersFlush() || bannerUntilMs) showCard(current, false);
  prefs.putUChar("rot", rotation);
  imu.sleep();
  display->deepSleep();
  SdMan.shutdown();
  xSemaphoreTake(adcLock, portMAX_DELAY);  // keyScan non legge più l'ADC
  input.prepareForDeepSleep();
  prefs.end();

#if !SOC_PM_SUPPORT_EXT1_WAKEUP
  // Come CrossPoint: su X3/X4 GPIO13 portato basso = taglio batteria/SD.
  // Il tasto power riaccende il dispositivo.
  gpio_hold_dis(GPIO_NUM_13);
  gpio_set_direction(GPIO_NUM_13, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_NUM_13, 0);
  gpio_hold_en(GPIO_NUM_13);
#endif
  freeink::PowerManager::powerDownRailsForSleep();
  freeink::PowerManager::deepSleepUntilPowerButton();
}

void pollOrientation() {
  if (!imu.present() || millis() - lastImuPollMs < IMU_POLL_MS) return;
  lastImuPollMs = millis();
  const Canvas::Rotation seen = readOrientation(physical);
  if (seen == physical) {
    pendingSinceMs = 0;
    return;
  }
  if (seen != pendingRot || pendingSinceMs == 0) {
    pendingRot = seen;
    pendingSinceMs = millis();
    return;
  }
  if (millis() - pendingSinceMs < ROTATE_STABLE_MS) return;
  pendingSinceMs = 0;
  physical = seen;
  if (rotationLocked) {
    // Resta fermo tutto: contenuto, indicazioni e funzione dei tasti.
    LOGF("[camtag] posizione %u (rotazione bloccata)\n", physical);
    return;
  }
  rotation = seen;
  lastActivityMs = millis();
  LOGF("[camtag] verso %u\n", rotation);
  if (cards.empty()) {
    showCurrentOrEmpty();  // anche i messaggi seguono il verso
    return;
  }
  if (rotationFor(cards[current], rotation) != drawnRot) showCard(current, false);
}

}  // namespace

// I pezzi della schermata filtri stanno sulla microSD.
FsFile filtersOpen(const char* path) { return SdMan.open(path); }

void setup() {
  BoardConfig::holdPowerRails();
#ifdef CAMTAG_DEBUG
  delay(250);
  Serial.begin(115200);
#endif

  const bool isX3 = freeink::selectXteinkDevice();
  if (!isX3) freeink::applyXteinkDisplayController();
  LOGF("[camtag] device: %s\n", BoardConfig::ACTIVE.name);

  const auto& pins = BoardConfig::ACTIVE.display;
  SPI.begin(pins.sclk, BoardConfig::ACTIVE.sd.miso, pins.mosi, pins.cs);
  input.begin();
  adcLock = xSemaphoreCreateMutex();
  keyQueue = xQueueCreate(32, sizeof(KeyEvent));
  xTaskCreate(keyScan, "keyScan", 3072, nullptr, 2, nullptr);

  static EInkDisplay einkDisplay(pins.sclk, pins.mosi, pins.cs, pins.dc, pins.rst, pins.busy);
  display = &einkDisplay;
  if (isX3) display->setDisplayX3();
  display->begin();

  prefs.begin("camtag", false);
  rotationLocked = prefs.getBool("lock", false);
  rotation = static_cast<Canvas::Rotation>(prefs.getUChar("rot", kNaturalRot) & 3);
  physical = rotation;
  if (imu.begin()) {
    physical = settleOrientation(rotation);
    if (!rotationLocked) rotation = physical;
  } else {
    LOGF("[camtag] nessun IMU: rotazione fissa\n");
  }
  pendingRot = physical;
  lastActivityMs = millis();
  LOGF("[camtag] verso %u\n", rotation);

  sdReady = SdMan.begin();
  if (!sdReady) {
    showMessage("NESSUNA MICROSD", nullptr);
    return;
  }
  loadCardList();
  loadFilters();
  if (cards.empty()) {
    showMessage("NESSUNA CARD", "TIENI F PER RICEVERLE");
    return;
  }

  current = std::min(std::max(static_cast<int>(prefs.getInt("idx", 0)), 0), static_cast<int>(cards.size()) - 1);
  // L'e-paper conserva l'immagine: se mostra già questa card, nello stesso
  // verso, non ridisegnare.
  const Canvas::Rotation want = rotationFor(cards[current], rotation);
  drawnRot = static_cast<Canvas::Rotation>(prefs.getUChar("shownRot", 255) & 3);
  if (prefs.getString("shown", "") != cards[current].base || drawnRot != want) showCard(current, true);
}

char keyName(uint8_t btn) {
  static constexpr struct {
    uint8_t btn;
    char key;
  } kMap[] = {{kKeyA, 'A'}, {kKeyB, 'B'}, {kKeyC, 'C'}, {kKeyD, 'D'}, {kKeyE, 'E'}, {kKeyF, 'F'}};
  for (const auto& m : kMap)
    if (m.btn == btn) return m.key;
  return 0;
}

// Tasti che fanno qualcosa anche tenuti: E (blocco), F (ricezione) e, sulla schermata
// filtri, A o B (modifica degli stadi).
bool hasLongPress(char k) { return k == 'E' || k == 'F' || (filtersOn() && (k == 'A' || k == 'B')); }

void longPress(char k) {
  if (fltSel >= 0) {
    filtersExitEdit();  // in modifica E, F e A o B tenuti chiudono soltanto
  } else if (k == 'A' || k == 'B') {
    filtersEnterEdit();
  } else {
    filtersSettle(false);
    if (k == 'F')
      enterReceive();
    else if (!cards.empty())
      toggleRotationLock();
  }
}

void shortPress(char k) {
  if (receiving) {
    if (k == 'E' || k == 'F') exitReceive();
    return;
  }
  if (cards.empty()) return;
  if (filtersOn() && filtersPress(k)) {
    // fatto dalla schermata filtri
  } else if (k == 'E' || k == 'F') {
    bannerUntilMs = 0;
    showCard(current + (k == 'E' ? 1 : -1), false);
  } else if (isLensChart(cards[current])) {
    // Con la rotazione bloccata vale il verso del contenuto, come per i filtri.
    stepLensChart(k == 'A' || k == 'B', rotation);
  }
}

// Pressione lunga: scatta allo scadere del tempo con il tasto ancora giù, senza
// aspettare il rilascio, che poi non conta (holdFired). Quella breve si legge al
// rilascio. Se pressione e rilascio sono arrivati entrambi durante un aggiornamento,
// decide la durata misurata dal compito.
char keyHeld = 0;
uint32_t keyHeldAt = 0;

void handleKeys() {
  KeyEvent ev;
  while (xQueueReceive(keyQueue, &ev, 0) == pdTRUE) {
    lastActivityMs = millis();
    const char k = keyName(ev.btn);
    if (!k) continue;
    if (ev.down) {
      LOGF("[camtag] tasto logico %u t=%lu ritardo=%lu\n", ev.btn, static_cast<unsigned long>(ev.t),
           static_cast<unsigned long>(millis() - ev.t));
      keyHeld = k;
      keyHeldAt = ev.t;
      holdFired = false;
      continue;
    }
    if (k != keyHeld) continue;
    keyHeld = 0;
    if (holdFired) {
      holdFired = false;
    } else if (!receiving && ev.t - keyHeldAt >= LONG_PRESS_MS && hasLongPress(k)) {
      longPress(k);
    } else {
      shortPress(k);
    }
  }
  if (keyHeld && !holdFired && !receiving && millis() - keyHeldAt >= LONG_PRESS_MS && hasLongPress(keyHeld)) {
    holdFired = true;
    longPress(keyHeld);
  }
}

void readPower() {
  xSemaphoreTake(adcLock, portMAX_DELAY);
  input.update();
  xSemaphoreGive(adcLock);
}

void loop() {
  readPower();
  if (input.wasPressed(InputManager::BTN_POWER)) goToSleep();
  handleKeys();

  if (receiving) {
    loopReceive();
    // Corto: i dati arrivano in coda e vanno scritti in fretta sulla microSD.
    delay(2);
    return;
  }

  if (fltSettleAt && static_cast<int32_t>(millis() - fltSettleAt) >= 0) filtersSettle(true);
  if (fltSel >= 0 && static_cast<int32_t>(millis() - fltEditUntil) >= 0) {
    LOGF("[filtri] modifica chiusa dopo %u s senza tocchi\n", fltCfg.editTimeoutS);
    filtersExitEdit();
  }

  if (bannerUntilMs && millis() > bannerUntilMs) {
    bannerUntilMs = 0;
    showCard(current, false);
  }
  pollOrientation();

  if (millis() - lastActivityMs > IDLE_SLEEP_MS) goToSleep();
  delay(input.isDebouncePending() ? 2 : 15);
}
