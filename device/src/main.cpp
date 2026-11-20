// CamTag MVP — mostra "card" BMP 1-bit dalla microSD (/camtag) su Xteink X3/X4.
//
// Ogni card può avere due file:  NOME.bmp   (orizzontale, 792x528)
//                                NOME.p.bmp (verticale,   528x792)
// L'accelerometro (solo X3) sceglie orientamento e variante.
//
// Tasti (come nel simulatore dello strumento web):
//         X (lato sinistro)         = card precedente, in ciclo
//         Y (lato destro)           = card successiva, in ciclo
//         X tenuto premuto          = ricezione delle card via Bluetooth
//         Y tenuto premuto          = blocca/sblocca la rotazione
//         La pressione lunga scatta allo scadere di LONG_PRESS_MS, con il tasto
//         ancora giù; quella breve al rilascio. I tasti li legge keyScan anche
//         durante gli aggiornamenti dello schermo: nessuna pressione va persa.
//         A B C D (bilancieri)      = mai cambio pagina; sulla lens chart di più
//                                     pagine sfogliano le sue pagine, sulla schermata
//                                     filtri cambiano ND e diottria e, tenendo A o B,
//                                     aprono la modifica degli stadi (Filters.h)
//         power                     = blocca/sblocca i tasti: la pagina resta, con un
//                                     lucchetto accanto al power
//         power tenuto premuto      = spegni; da spento, accendi
// In ricezione: X o Y escono; senza collegamenti per RECEIVE_IDLE_MS esce da sola.
//
// Acceso, a riposo e spento (docs/batteria.md):
//   acceso  mostra le card e sente i tasti in ogni momento. Dopo AWAKE_MS senza
//           tocchi va a riposo: sonno leggero, da cui si sveglia ogni REST_POLL_MS
//           per guardare i tasti, e ogni tanto il verso e la batteria. Da fuori
//           non cambia nulla: la prima pressione arriva come le altre.
//   spento  sonno profondo, lo riaccende solo il power tenuto premuto. A schermo
//           la scheda di spento (/camtag/off.bmp, se c'è) con una fascia che lo
//           dice; dopo OFF_BLANK_S lo schermo passa al bianco con la sola fascia.
//           Fino al bianco il chip resta alimentato (serve il timer); dopo, la
//           batteria è staccata e non consuma più nulla.
//           Ci va con il power tenuto, dopo AUTO_OFF_MS senza tasti e a batteria scarica.

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
#include <Wire.h>
#include <XteinkDetect.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Battery.h"
#include "BleUpload.h"
#include "Canvas.h"
#include "Filters.h"
#include "TinyFont.h"

namespace {

constexpr const char* kCardDir = "/camtag";
// Scheda di spento: sta con le card ma non entra nel giro delle pagine.
constexpr const char* kOffCard = "off";
// Dopo l'ultimo tocco resta sveglio per questo tempo, poi va a riposo.
constexpr uint32_t AWAKE_MS = 5000;
// Dopo un avvio resta sveglio almeno così: il tempo che serve alla USB per farsi
// riconoscere dal computer (con la USB collegata a un computer non va mai a riposo).
constexpr uint32_t BOOT_AWAKE_MS = 15000;
// A riposo guarda i tasti ogni REST_POLL_MS: una pressione più corta può sfuggire.
constexpr uint32_t REST_POLL_MS = 25;
constexpr uint32_t REST_IMU_MS = 1000;   // a riposo: ogni quanto guarda il verso
constexpr uint32_t REST_USB_MS = 5000;   // a riposo: ogni quanto guarda se è in carica
constexpr uint32_t POWER_HOLD_MS = 1000;     // power tenuto: spegne
constexpr uint32_t POWER_ON_HOLD_MS = 500;   // da spento, dopo l'avvio del chip: accende
// Dimenticato acceso si spegne da solo: conta solo il tempo senza tasti.
constexpr uint32_t AUTO_OFF_MS = 24UL * 3600 * 1000;
// Da spento, dopo questo tempo lo schermo passa al bianco: un'immagine ferma per
// settimane può lasciare un'ombra.
constexpr uint32_t OFF_BLANK_S = 72UL * 3600;
// Sotto questa tensione, non in carica, si spegne con l'avviso di batteria scarica.
// La tensione e non la percentuale: non dipende da come è configurato il gauge.
constexpr uint16_t EMPTY_MV = 3300;
constexpr uint32_t BATTERY_LOG_S = 300;  // una riga del registro ogni 5 minuti
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
// l'SDK chiama BACK/CONFIRM/LEFT/RIGHT; i due laterali (X, Y) sull'ingresso 2,
// UP/DOWN. Verificato sull'X3 (con CAMTAG_DEBUG il monitor seriale stampa il
// tasto logico di ogni pressione): X, sul lato sinistro, è UP; Y è DOWN.
constexpr uint8_t kKeyX = InputManager::BTN_UP;
constexpr uint8_t kKeyY = InputManager::BTN_DOWN;
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
uint32_t lastActivityMs = 0;  // tasti, rotazione, Bluetooth: tiene sveglio per AWAKE_MS
uint32_t lastKeyMs = 0;       // solo i tasti: per lo spegnimento da dimenticato
uint32_t bootAwakeUntilMs = 0;
uint32_t lastImuPollMs = 0;

// Acceso / spento. In NVS "off": 0 acceso, kOffShown con la scheda di spento a
// schermo, kOffBlank dopo il passaggio al bianco.
constexpr uint8_t kOffShown = 1, kOffBlank = 2, kOffEmpty = 3;  // kOffEmpty: a schermo l'avviso di batteria scarica
enum class Off : uint8_t { User = 0, Idle = 1, Empty = 2, Blank = 3 };
// Tasti bloccati (power breve): la pagina resta com'è, con il lucchetto accanto al
// power; A–D, X e Y non fanno nulla, la rotazione continua. In NVS "keylock".
bool keysLocked = false;
bool powerArmed = false;   // dopo l'accensione il power va prima rilasciato
uint32_t powerDownAt = 0;  // da quando il power è giù; 0 = su
TaskHandle_t keyScanTask = nullptr;

// Registro dei consumi (Battery.h).
battery::Reading lastBattery;
uint32_t lastBatteryLogS = 0;
uint64_t restUs = 0;         // tempo passato a riposo dall'avvio
uint64_t restStartUs = 0;    // inizio del riposo in corso; 0 = sveglio
bool batteryFresh = false;   // c'è una lettura nuova da controllare (batteria scarica)
uint32_t screenUpdates = 0;  // aggiornamenti dello schermo dall'avvio
uint32_t keyPresses = 0;
bool chargeSeen = false;     // a riposo: carica già notata, non risvegliare di nuovo
bool gaugePending = true;    // capacità della batteria ancora da controllare nel gauge
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
    if (base == kOffCard) continue;
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
// Due analogRead insieme fanno fallire uno dei due, che legge 0 (= D o Y premuti):
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
  screenUpdates++;
  changesSinceFull = full ? 0 : changesSinceFull + 1;
  fastShadow = false;  // mezzo e completo portano ogni pixel al suo valore
}

// Aggiornamento veloce (schermata filtri): anche qui il pannello si spegne subito.
// Lascia ombra: la toglie il mezzo refresh quando ND e diottria restano fermi.
void presentFast() {
  EPD_BEGIN();
  display->displayBuffer(EInkDisplay::FAST_REFRESH, true);
  EPD_END("veloce");
  screenUpdates++;
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

// Lucchetto dei tasti bloccati. Come le indicazioni dei tasti sta sul bordo fisico,
// sotto il power che sblocca (in alto a destra nella posizione naturale), e segue la
// rotazione del dispositivo restando dritto per chi legge il contenuto.
void drawLock(Canvas& c) {
  constexpr float kPowerPos = 0.85f;  // centro del power lungo il bordo in alto
  constexpr int kBox = 36;
  const Canvas natural = canvas(kNaturalRot);
  int px, py, lx, ly;
  natural.toPanel(static_cast<int>(kPowerPos * natural.width() + 0.5f), kBox / 2 + 4, px, py);
  c.fromPanel(px, py, lx, ly);
  const int x = lx - kBox / 2, y = ly - kBox / 2;
  c.fillRect(x, y, kBox, kBox, false);
  c.strokeRect(x, y, kBox, kBox, 2, true);
  c.fillRect(x + 12, y + 7, 12, 3, true);  // arco
  c.fillRect(x + 12, y + 7, 3, 10, true);
  c.fillRect(x + 21, y + 7, 3, 10, true);
  c.fillRect(x + 9, y + 16, 18, 13, true);  // corpo
  c.fillRect(x + 17, y + 20, 2, 5, false);  // serratura
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
  if (keysLocked) drawLock(c);
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

// Pressione breve sulla schermata filtri ('A'…'D', 'X', 'Y'). false = il tasto non è suo
// (X e Y fuori dalla modifica cambiano pagina).
bool filtersPress(char k) {
  const int q = kQuarterTurns[rotation];  // con il blocco i tasti restano quelli del contenuto
  if (fltSel >= 0) {
    if (k == 'X' || k == 'Y') {
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
    const bool stay = k != 'X' && k != 'Y';
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
  c.textCentered(c.height() - 64, "X O Y PER USCIRE", 2);
  present(full);
  lastReceiveDrawMs = millis();
  LOGF("[camtag] schermo ricezione: %lu ms\n", static_cast<unsigned long>(lastReceiveDrawMs - t0));
  prefs.putString("shown", "");  // al prossimo avvio ridisegna la card
}

void showCurrentOrEmpty() {
  if (!sdReady)
    showMessage("NESSUNA MICROSD", nullptr);
  else if (cards.empty())
    showMessage("NESSUNA CARD", "TIENI X PER RICEVERLE");
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
    showMessage("NESSUNA CARD", "TIENI X PER RICEVERLE");
    return;
  }
  showCard(0, true, "CARD AGGIORNATE");
  bannerUntilMs = millis() + BANNER_MS;
}

void loopReceive() {
  if (!receiving) return;  // X o Y l'hanno appena chiusa (shortPress)

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

// --- Batteria e registro dei consumi ---------------------------------------------
battery::Counters counters() {
  const uint64_t upUs = static_cast<uint64_t>(esp_timer_get_time());
  const uint64_t rest = restUs + (restStartUs ? upUs - restStartUs : 0);
  battery::Counters c;
  c.restS = static_cast<uint32_t>(rest / 1000000);
  c.awakeS = static_cast<uint32_t>((upUs - rest) / 1000000);
  c.screen = screenUpdates;
  c.keys = keyPresses;
  return c;
}

void printBattery(const battery::Reading& r) {
  LOGF("[batt] ok=%d mV=%u mA=%d mAh=%u pieno=%u progetto=%u pct=%u\n", r.ok, r.mv, r.ma, r.mah, r.fullMah,
       r.designMah, r.pct);
}

void logBattery(battery::Event ev, int info) {
  battery::read(lastBattery);
  lastBatteryLogS = battery::nowS();
  batteryFresh = true;
  if (sdReady) battery::log(ev, info, lastBattery, counters());
  printBattery(lastBattery);
}

bool batteryLow(const battery::Reading& r) { return r.ok && r.ma <= 0 && r.mv < EMPTY_MV; }

// Scarica davvero: tensione bassa per più letture di fila e non in carica.
bool batteryEmpty() {
  for (int i = 0; i < 3; i++) {
    battery::Reading r;
    battery::read(r);
    if (!batteryLow(r)) return false;
    delay(100);
  }
  return true;
}

// --- Spento ----------------------------------------------------------------------
// La scheda di spento, o il nome se non c'è, con una fascia in basso che dice che è
// spento e come si riaccende. Blank: solo la fascia su bianco.
void drawOff(Off why) {
  Canvas::Rotation rot = rotation;
  bool drawn = false;
  if (sdReady && why != Off::Blank) {
    Card off{kOffCard};
    off.landscape = SdMan.exists(pathFor(off, Canvas::ROT_0).c_str());
    off.portrait = SdMan.exists(pathFor(off, Canvas::ROT_90).c_str());
    if (off.landscape || off.portrait) {
      rot = rotationFor(off, rotation);
      Canvas c = canvas(rot);
      c.clear();
      FsFile f = SdMan.open(pathFor(off, rot).c_str());
      drawn = c.drawBmp(f) == Canvas::BmpResult::Ok;
      f.close();
      if (!drawn) rot = rotation;
    }
  }
  Canvas c = canvas(rot);
  if (!drawn) {
    c.clear();
    if (why != Off::Blank) c.textCentered(c.height() / 2 - 40, "CAMTAG", Canvas::isPortrait(rot) ? 6 : 8);
  }
  constexpr int kScale = 2, kH = 7 * kScale + 22;
  c.fillRect(0, c.height() - kH, c.width(), kH, false);
  c.fillRect(0, c.height() - kH, c.width(), 2, true);
  c.textCentered(c.height() - kH + 13,
                 why == Off::Empty ? "BATTERIA SCARICA - METTI IN CARICA" : "SPENTO - TIENI POWER PER ACCENDERE", kScale);
  if (why != Off::Blank) {
    // Quanta batteria aveva quando si è spento.
    const int pct = batteryPercent();
    if (pct >= 0) {
      char label[6];
      snprintf(label, sizeof(label), "%d%%", pct);
      const int w = tinyfont::textWidth(label, kScale);
      c.fillRect(c.width() - 62 - w, 10, w + 10, 24, false);
      c.text(c.width() - 58 - w, 15, label, kScale);
      c.battery(c.width() - 52, 12, pct);
    }
  }
  present(true);
  prefs.putString("shown", "");  // alla riaccensione ridisegna la card
}

// Porta un pin a un livello e lo tiene lì anche nel sonno profondo.
void holdPin(int8_t pin, int level) {
  if (pin < 0) return;
  const auto g = static_cast<gpio_num_t>(pin);
  gpio_hold_dis(g);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
  gpio_hold_en(g);
}

// Sonno profondo, da cui esce solo con il power (o con il timer del bianco). Non torna.
// Sull'X3 e sull'X4 GPIO13 basso stacca la batteria: da spento non consuma nulla, ma
// nessun timer può più svegliarlo. Per questo con la scheda di spento a schermo il chip
// resta alimentato (GPIO13 alto) fino al passaggio al bianco, e la batteria si stacca
// solo dopo. A batteria scarica si stacca subito.
[[noreturn]] void sleepOff(uint8_t stage) {
  // L'ora dello spegnimento serve a riprendere il conto se un tocco del power, troppo
  // breve per accendere, lo risveglia prima del bianco.
  if (prefs.getUChar("off", 0) == 0) prefs.putULong("offAt", battery::nowS());
  prefs.putUChar("off", stage);
  // Se l'orologio è ripartito (è mancata l'alimentazione) il conto ricomincia.
  const uint32_t now = battery::nowS(), at = prefs.getULong("offAt", now);
  const uint32_t passed = now >= at ? now - at : 0;
  const uint32_t blankS = prefs.getULong("blankS", OFF_BLANK_S);  // "blank=N" dalla seriale, per le prove
  prefs.end();

  if (stage == kOffShown) {
    // Come powerDownRailsForSleep, che però sull'X3 porterebbe basso anche GPIO13.
    holdPin(BoardConfig::ACTIVE.display.rst, HIGH);
#if !SOC_PM_SUPPORT_EXT1_WAKEUP
    holdPin(13, HIGH);
#endif
    esp_sleep_enable_timer_wakeup((passed < blankS ? blankS - passed : 5) * 1000000ULL);
  } else {
#if !SOC_PM_SUPPORT_EXT1_WAKEUP
    holdPin(13, LOW);
#endif
    freeink::PowerManager::powerDownRailsForSleep();
  }
  freeink::PowerManager::deepSleepUntilPowerButton();
}

[[noreturn]] void powerOff(Off why) {
  LOGF("[camtag] spento (%u)\n", static_cast<unsigned>(why));
  if (receiving) {
    bleupload::stop();
    receiving = false;
  }
  // Niente modifica degli stadi né riordino in sospeso: alla riaccensione si riparte puliti.
  filtersFlush();
  bannerUntilMs = 0;
  if (why != Off::Blank) logBattery(battery::kOff, static_cast<int>(why));
  drawOff(why);
  prefs.putUChar("rot", rotation);
  prefs.putBool("keylock", false);  // alla riaccensione i tasti sono liberi
  imu.sleep();
  display->deepSleep();
  SdMan.shutdown();
  xSemaphoreTake(adcLock, portMAX_DELAY);  // keyScan non legge più l'ADC
  input.prepareForDeepSleep();
  sleepOff(why == Off::Blank ? kOffBlank : why == Off::Empty ? kOffEmpty : kOffShown);
}

// --- Riposo ----------------------------------------------------------------------
bool usbHost() { return HWCDC::isPlugged(); }

bool canRest() {
  const uint32_t now = millis();
  return !receiving && fltSel < 0 && !fltSettleAt && !bannerUntilMs && !pendingSinceMs && !gaugePending &&
         !powerDownAt && uxQueueMessagesWaiting(keyQueue) == 0 && now - lastActivityMs > AWAKE_MS &&
         static_cast<int32_t>(now - bootAwakeUntilMs) >= 0 && !usbHost();
}

// Nel sonno leggero l'ESP32-C3 isola i pin (protezione del framework contro i riavvii
// da disturbi sugli ingressi): le uscite che devono restare ferme vanno escluse, o
// l'alimentazione della microSD e i comandi dello schermo resterebbero in aria.
void keepOutputsInRest() {
  const auto& b = BoardConfig::ACTIVE;
  for (const int8_t pin : {b.display.sclk, b.display.mosi, b.display.cs, b.display.dc, b.display.rst, b.sd.cs,
                           b.sd.powerEnable, b.power.latch0, b.power.latch1})
    if (pin >= 0) gpio_sleep_sel_dis(static_cast<gpio_num_t>(pin));
#if !SOC_PM_SUPPORT_EXT1_WAKEUP
  gpio_sleep_sel_dis(GPIO_NUM_13);  // X3/X4: alimentazione della microSD o della batteria
#endif
}

bool anyKeyDown() {
  InputManager::ButtonAdcSample g[2];
  input.readButtonAdc(g[0], g[1]);
  return g[0].button >= 0 || g[1].button >= 0;
}

// Sonno leggero finché non succede qualcosa: un tasto, il power, un altro verso, la
// carica che comincia, la riga del registro, lo spegnimento da dimenticato. Il chip
// dorme e si sveglia ogni REST_POLL_MS per circa un millisecondo.
void rest() {
  const auto powerPin = static_cast<gpio_num_t>(BoardConfig::ACTIVE.input.power);
  const int powerDown = BoardConfig::ACTIVE.input.powerActiveHigh ? HIGH : LOW;
  LOGF("[riposo] inizio\n");
  // keyScan fermo: a riposo l'ADC lo legge solo questo ciclo. Preso con adcLock, così
  // non resta sospeso a metà lettura.
  xSemaphoreTake(adcLock, portMAX_DELAY);
  vTaskSuspend(keyScanTask);
  keepOutputsInRest();
  gpio_wakeup_enable(powerPin, powerDown == LOW ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
  esp_sleep_enable_gpio_wakeup();

  restStartUs = static_cast<uint64_t>(esp_timer_get_time());
  uint32_t imuAt = millis(), usbAt = imuAt;
  const char* why = "tasto";
  // Con i tasti bloccati non c'è nulla da guardare tra un controllo del verso e
  // l'altro: il power sveglia da solo, e il chip dorme un secondo alla volta.
  const uint32_t pollMs = keysLocked ? REST_IMU_MS : REST_POLL_MS;
  for (;;) {
    esp_sleep_enable_timer_wakeup(pollMs * 1000ULL);
    // Se il chip rifiuta il sonno leggero si comporta uguale, solo senza risparmiare.
    if (esp_light_sleep_start() != ESP_OK) delay(pollMs);
    if (digitalRead(powerPin) == powerDown) {
      why = "power";
      break;
    }
    // Una lettura sbagliata dell'ADC vale 0, come un tasto premuto: serve la conferma.
    if (!keysLocked && anyKeyDown() && anyKeyDown()) break;
    const uint32_t now = millis();
    if (now - imuAt >= REST_IMU_MS) {
      imuAt = now;
      if (imu.present() && readOrientation(physical) != physical) {
        why = "verso";
        break;
      }
    }
    if (now - usbAt >= REST_USB_MS) {
      usbAt = now;
      // In carica c'è un cavo: sveglio per un po', così la USB si fa riconoscere.
      int16_t ma = 0;
      const bool charging = battery::current(ma) && ma > 0;
      if (charging && !chargeSeen) {
        chargeSeen = true;
        why = "carica";
        break;
      }
      if (!charging) chargeSeen = false;
    }
    if (battery::nowS() - lastBatteryLogS >= BATTERY_LOG_S) {
      logBattery(battery::kSample, 0);
      if (batteryLow(lastBattery)) {
        why = "batteria";
        break;
      }
    }
    if (now - lastKeyMs > AUTO_OFF_MS) {
      why = "dimenticato";
      break;
    }
  }
  restUs += static_cast<uint64_t>(esp_timer_get_time()) - restStartUs;
  restStartUs = 0;

  gpio_wakeup_disable(powerPin);
  // gpio_wakeup_disable lascia il tipo LOW_LEVEL: come interrupt scatterebbe di continuo.
  gpio_set_intr_type(powerPin, GPIO_INTR_DISABLE);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  vTaskResume(keyScanTask);
  xSemaphoreGive(adcLock);
  lastActivityMs = millis();
  LOGF("[riposo] fine: %s\n", why);
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

// L'SDK accende anche il giroscopio del QMI8658, che non usiamo e che consuma sempre:
// per il verso basta l'accelerometro, in basso consumo a 21 Hz.
// Registri dalla scheda tecnica: CTRL7 (0x08) bit 0 = accelerometro, bit 1 = giroscopio;
// CTRL2 (0x03) = fondo scala 2 g e cadenza 0x0D (21 Hz, solo senza giroscopio).
void imuAccelOnly() {
  if (BoardConfig::ACTIVE.sensors.imuType != BoardConfig::ImuType::Qmi8658) return;
  const auto write = [](uint8_t addr, uint8_t reg, uint8_t value) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission(true) == 0;
  };
  for (const uint8_t addr : {static_cast<uint8_t>(0x6B), static_cast<uint8_t>(0x6A)}) {
    Wire.beginTransmission(addr);
    Wire.write(static_cast<uint8_t>(0x00));  // WHO_AM_I
    if (Wire.endTransmission(false) != 0) continue;
    if (Wire.requestFrom(addr, static_cast<uint8_t>(1), static_cast<uint8_t>(true)) < 1 || Wire.read() != 0x05) continue;
    const bool ok = write(addr, 0x08, 0x00) && write(addr, 0x03, 0x0D) && write(addr, 0x08, 0x01);
    LOGF("[camtag] IMU: solo accelerometro %s\n", ok ? "ok" : "NON riuscito");
    delay(60);  // prima lettura valida dopo un periodo della nuova cadenza
    return;
  }
}

// Da spento: il power è rimasto giù per POWER_ON_HOLD_MS?
bool powerHeld() {
  const int8_t pin = BoardConfig::ACTIVE.input.power;
  if (pin < 0) return true;
  const bool activeHigh = BoardConfig::ACTIVE.input.powerActiveHigh;
  pinMode(pin, activeHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  for (uint32_t t0 = millis(); millis() - t0 < POWER_ON_HOLD_MS; delay(10))
    if (digitalRead(pin) != (activeHigh ? HIGH : LOW)) return false;
  return true;
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
  const esp_reset_reason_t reset = esp_reset_reason();
  const esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();

  const bool isX3 = freeink::selectXteinkDevice();
  if (!isX3) freeink::applyXteinkDisplayController();
  LOGF("[camtag] device: %s riavvio=%d risveglio=%d\n", BoardConfig::ACTIVE.name, static_cast<int>(reset),
       static_cast<int>(wake));

  prefs.begin("camtag", false);
  // Era spento: lo riaccende solo il power tenuto premuto; un tocco breve lo rimanda a
  // dormire senza toccare lo schermo. Il timer invece lo sveglia per passare al bianco.
  // Dopo un caricamento del firmware o l'apertura della seriale (altri motivi di
  // riavvio) si accende e basta.
  const uint8_t wasOff = prefs.getUChar("off", 0);
  const bool blank = wasOff == kOffShown && reset == ESP_RST_DEEPSLEEP && wake == ESP_SLEEP_WAKEUP_TIMER;
  if (wasOff && !blank && (reset == ESP_RST_DEEPSLEEP || reset == ESP_RST_POWERON) && !powerHeld()) sleepOff(wasOff);

  const auto& pins = BoardConfig::ACTIVE.display;
  SPI.begin(pins.sclk, BoardConfig::ACTIVE.sd.miso, pins.mosi, pins.cs);
  input.begin();
  adcLock = xSemaphoreCreateMutex();
  keyQueue = xQueueCreate(32, sizeof(KeyEvent));
  xTaskCreate(keyScan, "keyScan", 3072, nullptr, 2, &keyScanTask);

  static EInkDisplay einkDisplay(pins.sclk, pins.mosi, pins.cs, pins.dc, pins.rst, pins.busy);
  display = &einkDisplay;
  if (isX3) display->setDisplayX3();
  display->begin();

  rotationLocked = prefs.getBool("lock", false);
  rotation = static_cast<Canvas::Rotation>(prefs.getUChar("rot", kNaturalRot) & 3);
  physical = rotation;
  battery::begin();
  if (imu.begin()) {
    imuAccelOnly();
    physical = settleOrientation(rotation);
    if (!rotationLocked) rotation = physical;
  } else {
    LOGF("[camtag] nessun IMU: rotazione fissa\n");
  }
  pendingRot = physical;
  lastActivityMs = lastKeyMs = millis();
  bootAwakeUntilMs = millis() + BOOT_AWAKE_MS;
  LOGF("[camtag] verso %u\n", rotation);

  sdReady = SdMan.begin();

  if (blank) powerOff(Off::Blank);
  // Batteria scarica: resta spento con l'avviso, senza ridisegnarlo se c'è già.
  if (batteryEmpty()) {
    if (wasOff == kOffEmpty) {
      display->deepSleep();
      sleepOff(kOffEmpty);
    }
    powerOff(Off::Empty);
  }
  prefs.putUChar("off", 0);
  keysLocked = prefs.getBool("keylock", false);  // il lucchetto può essere ancora a schermo
  logBattery(battery::kBoot, static_cast<int>(reset) * 100 + static_cast<int>(wake));

  if (!sdReady) {
    showMessage("NESSUNA MICROSD", nullptr);
    return;
  }
  loadCardList();
  loadFilters();
  if (cards.empty()) {
    showMessage("NESSUNA CARD", "TIENI X PER RICEVERLE");
    return;
  }

  current = std::min(std::max(static_cast<int>(prefs.getInt("idx", 0)), 0), static_cast<int>(cards.size()) - 1);
  // L'e-paper conserva l'immagine: se mostra già questa card, nello stesso
  // verso, non ridisegnare. Dopo lo spento "shown" è vuoto e si ridisegna sempre.
  const Canvas::Rotation want = rotationFor(cards[current], rotation);
  drawnRot = static_cast<Canvas::Rotation>(prefs.getUChar("shownRot", 255) & 3);
  if (prefs.getString("shown", "") != cards[current].base || drawnRot != want) showCard(current, true);
}

char keyName(uint8_t btn) {
  static constexpr struct {
    uint8_t btn;
    char key;
  } kMap[] = {{kKeyA, 'A'}, {kKeyB, 'B'}, {kKeyC, 'C'}, {kKeyD, 'D'}, {kKeyX, 'X'}, {kKeyY, 'Y'}};
  for (const auto& m : kMap)
    if (m.btn == btn) return m.key;
  return 0;
}

// Tasti che fanno qualcosa anche tenuti: X (ricezione), Y (blocco) e, sulla schermata
// filtri, A o B (modifica degli stadi).
bool hasLongPress(char k) { return k == 'X' || k == 'Y' || (filtersOn() && (k == 'A' || k == 'B')); }

void longPress(char k) {
  if (fltSel >= 0) {
    filtersExitEdit();  // in modifica X, Y e A o B tenuti chiudono soltanto
  } else if (k == 'A' || k == 'B') {
    filtersEnterEdit();
  } else {
    filtersSettle(false);
    if (k == 'X')
      enterReceive();
    else if (!cards.empty())
      toggleRotationLock();
  }
}

void shortPress(char k) {
  if (receiving) {
    if (k == 'X' || k == 'Y') exitReceive();
    return;
  }
  if (cards.empty()) return;
  if (filtersOn() && filtersPress(k)) {
    // fatto dalla schermata filtri
  } else if (k == 'X' || k == 'Y') {
    bannerUntilMs = 0;
    showCard(current + (k == 'Y' ? 1 : -1), false);
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
    if (keysLocked) {
      keyHeld = 0;
      continue;
    }
    lastActivityMs = millis();
    const char k = keyName(ev.btn);
    if (!k) continue;
    if (ev.down) {
      lastKeyMs = millis();
      keyPresses++;
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

// Power breve: blocca o sblocca i tasti. Il lucchetto compare con un aggiornamento
// veloce e sparisce con un mezzo refresh, che non lascia ombra.
void toggleKeyLock() {
  if (receiving || cards.empty()) return;
  keysLocked = !keysLocked;
  LOGF("[camtag] tasti %s\n", keysLocked ? "bloccati" : "liberi");
  prefs.putBool("keylock", keysLocked);
  // Sotto il blocco non resta niente in sospeso: modifica chiusa, stadi a posto.
  filtersFlush();
  bannerUntilMs = 0;
  keyHeld = 0;
  showCard(current, keysLocked ? Refresh::Fast : Refresh::Half);
}

// Power: breve (rilasciato prima di POWER_HOLD_MS) blocca i tasti, tenuto spegne.
void handlePower() {
  if (!input.isPressed(InputManager::BTN_POWER)) {
    const bool tapped = powerArmed && powerDownAt;
    powerArmed = true;
    powerDownAt = 0;
    if (tapped) toggleKeyLock();
    return;
  }
  lastActivityMs = lastKeyMs = millis();
  if (!powerArmed) return;  // è ancora la pressione che l'ha acceso
  if (!powerDownAt)
    powerDownAt = millis() | 1;
  else if (millis() - powerDownAt >= POWER_HOLD_MS)
    powerOff(Off::User);
}

#ifdef CAMTAG_DEBUG
// Comandi dalla seriale (scripts/taratura.py batteria): "batt?" manda il registro dei
// consumi, "batt!" lo cancella, "blank=N" porta a N secondi l'attesa prima del bianco
// da spento (0 = torna a OFF_BLANK_S), per provarla senza aspettare tre giorni.
void serialCommands() {
  static char cmd[20];
  static uint8_t n = 0;
  while (Serial.available()) {
    const char ch = static_cast<char>(Serial.read());
    if (ch != '\n' && ch != '\r') {
      if (n < sizeof(cmd) - 1) cmd[n++] = ch;
      continue;
    }
    cmd[n] = 0;
    n = 0;
    if (!strcmp(cmd, "batt?")) {
      battery::Reading r;
      battery::read(r);
      printBattery(r);
      battery::dump(Serial);
    } else if (!strcmp(cmd, "batt!")) {
      LOGF("[b] %s\n", battery::erase() ? "cancellato" : "errore");
    } else if (!strncmp(cmd, "blank=", 6)) {
      const uint32_t secs = strtoul(cmd + 6, nullptr, 10);
      if (secs)
        prefs.putULong("blankS", secs);
      else
        prefs.remove("blankS");
      LOGF("[b] bianco dopo %lu s\n", static_cast<unsigned long>(secs ? secs : OFF_BLANK_S));
    }
  }
}
#endif

void loop() {
  readPower();
  handlePower();
  handleKeys();
#ifdef CAMTAG_DEBUG
  serialCommands();
#endif
  // La capacità vera della batteria nel gauge, che torna al valore di fabbrica quando
  // resta senza alimentazione: un passo per giro, circa 8 s in tutto.
  if (gaugePending) gaugePending = millis() < 30000 && BatteryMonitor::loadDesignCapacity();

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

  if (battery::nowS() - lastBatteryLogS >= BATTERY_LOG_S) logBattery(battery::kSample, 0);
  if (batteryFresh) {
    batteryFresh = false;
    if (batteryLow(lastBattery) && batteryEmpty()) powerOff(Off::Empty);
  }
  if (millis() - lastKeyMs > AUTO_OFF_MS) powerOff(Off::Idle);

  if (canRest())
    rest();
  else
    delay(input.isDebouncePending() ? 2 : 15);
}
