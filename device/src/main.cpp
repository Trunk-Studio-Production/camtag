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
//         A B C D (bilancieri)      = nessuna funzione qui: mai cambio pagina
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

namespace {

constexpr const char* kCardDir = "/camtag";
constexpr uint32_t IDLE_SLEEP_MS = 20000;
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
// UP/DOWN. Quale dei due laterali è E è DA TARARE sull'X3: con CAMTAG_DEBUG il
// monitor seriale stampa il tasto logico di ogni pressione.
constexpr uint8_t kKeyE = InputManager::BTN_DOWN;
constexpr uint8_t kKeyF = InputManager::BTN_UP;

// --- Orientamento -----------------------------------------------------------
// Mappatura assi IMU -> pannello. DA TARARE sull'X3: con CAMTAG_DEBUG il monitor
// seriale stampa ax/ay/az; tieni il dispositivo dritto in orizzontale (testo
// leggibile) e regola finché risulta ROT_0, poi verifica gli altri tre lati.
constexpr bool kImuSwapXY = false;
constexpr float kImuSignX = 1.0f;
constexpr float kImuSignY = 1.0f;
constexpr float kMinTiltG = 0.5f;     // sotto questa inclinazione non decide
constexpr float kMarginG = 0.25f;     // isteresi tra i due assi
constexpr float kFlatG = 0.85f;       // appoggiato piatto: tiene l'orientamento
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
Canvas::Rotation drawnRot = Canvas::ROT_0;  // rotazione usata per la card a schermo
bool rotationLocked = false;
uint8_t changesSinceFull = 0;
uint32_t lastActivityMs = 0;
uint32_t lastImuPollMs = 0;
Canvas::Rotation pendingRot = Canvas::ROT_0;
uint32_t pendingSinceMs = 0;
uint32_t bannerUntilMs = 0;
bool sdReady = false;

// Modalità ricezione via Bluetooth.
bool receiving = false;
char bleName[16] = "";
uint16_t drawnFiles = 0;
uint32_t lastReceiveDrawMs = 0;

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
  if (millis() - lastLog > 1000) {
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

void present(bool forceFull) {
  const bool full = forceFull || changesSinceFull >= FULL_REFRESH_EVERY;
  display->displayBuffer(full ? EInkDisplay::FULL_REFRESH : EInkDisplay::HALF_REFRESH);
  changesSinceFull = full ? 0 : changesSinceFull + 1;
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

void showCard(int index, bool forceFull, const char* banner = nullptr) {
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

  const int pct = batteryPercent();
  if (pct >= 0 && pct < LOW_BATTERY_PERCENT) c.battery(c.width() - 52, 12, pct);
  if (banner) c.banner(banner);

  present(forceFull);
  drawnRot = rot;
  prefs.putInt("idx", current);
  prefs.putString("shown", card.base);
  prefs.putUChar("shownRot", rot);
}

void toggleRotationLock() {
  rotationLocked = !rotationLocked;
  prefs.putBool("lock", rotationLocked);
  prefs.putUChar("rot", rotation);
  LOGF("[camtag] rotation %s\n", rotationLocked ? "locked" : "auto");
  showCard(current, false, rotationLocked ? "ROTAZIONE BLOCCATA" : "ROTAZIONE AUTOMATICA");
  bannerUntilMs = millis() + BANNER_MS;
}

// --- Ricezione via Bluetooth ----------------------------------------------------
void drawReceive(const char* status, const char* detail, bool full) {
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
  if (input.wasPressed(InputManager::BTN_POWER)) goToSleep();
  if (input.wasReleased(kKeyE) || input.wasReleased(kKeyF)) {
    exitReceive();
    return;
  }

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
  if (bannerUntilMs) showCard(current, false);  // non lasciare il banner a schermo
  prefs.putUChar("rot", rotation);
  imu.sleep();
  display->deepSleep();
  SdMan.shutdown();
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
  if (rotationLocked || !imu.present() || millis() - lastImuPollMs < IMU_POLL_MS) return;
  lastImuPollMs = millis();
  const Canvas::Rotation seen = readOrientation(rotation);
  if (seen == rotation) {
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
  rotation = seen;
  lastActivityMs = millis();
  if (cards.empty()) return;
  if (rotationFor(cards[current], rotation) != drawnRot) showCard(current, false);
}

}  // namespace

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

  static EInkDisplay einkDisplay(pins.sclk, pins.mosi, pins.cs, pins.dc, pins.rst, pins.busy);
  display = &einkDisplay;
  if (isX3) display->setDisplayX3();
  display->begin();

  prefs.begin("camtag", false);
  rotationLocked = prefs.getBool("lock", false);
  rotation = static_cast<Canvas::Rotation>(prefs.getUChar("rot", Canvas::ROT_0) & 3);
  if (imu.begin()) {
    if (!rotationLocked) rotation = settleOrientation(rotation);
  } else {
    LOGF("[camtag] nessun IMU: rotazione fissa\n");
  }
  pendingRot = rotation;
  lastActivityMs = millis();

  sdReady = SdMan.begin();
  if (!sdReady) {
    showMessage("NESSUNA MICROSD", nullptr);
    return;
  }
  loadCardList();
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

void loop() {
  input.update();

  if (receiving) {
    loopReceive();
    if (input.wasAnyPressed() || input.wasAnyReleased()) lastActivityMs = millis();
    // Corto: i dati arrivano in coda e vanno scritti in fretta sulla microSD.
    delay(2);
    return;
  }

  if (input.wasPressed(InputManager::BTN_POWER)) goToSleep();

  // F si legge al rilascio, come E: tenuto premuto apre la ricezione.
  if (input.wasReleased(kKeyF) && input.getHeldTime() >= LONG_PRESS_MS) {
    enterReceive();
  } else if (!cards.empty()) {
    int step = 0;
    if (input.wasReleased(kKeyF))
      step = -1;
    else if (input.wasReleased(kKeyE)) {
      if (input.getHeldTime() >= LONG_PRESS_MS)
        toggleRotationLock();
      else
        step = 1;
    }
    if (step != 0) {
      bannerUntilMs = 0;
      showCard(current + step, false);
    }
  }
  if (input.wasAnyPressed() || input.wasAnyReleased()) lastActivityMs = millis();
#ifdef CAMTAG_DEBUG
  for (uint8_t b = 0; b <= InputManager::BTN_POWER; b++)
    if (input.wasPressed(b)) LOGF("[camtag] tasto logico %u\n", b);
#endif

  if (bannerUntilMs && millis() > bannerUntilMs) {
    bannerUntilMs = 0;
    showCard(current, false);
  }
  pollOrientation();

  if (millis() - lastActivityMs > IDLE_SLEEP_MS) goToSleep();
  delay(input.isDebouncePending() ? 2 : 15);
}
