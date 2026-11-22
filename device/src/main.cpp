// CamTag MVP — mostra "card" BMP 1-bit dalla microSD (/camtag) su Xteink X3/X4.
//
// Ogni card può avere due file:  NOME.bmp   (orizzontale, 792x528)
//                                NOME.p.bmp (verticale,   528x792)
// L'accelerometro (solo X3) sceglie orientamento e variante.
//
// Tasti (come nel simulatore del configuratore web):
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
#include <esp_ota_ops.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <soc/gpio_periph.h>
#include <soc/usb_serial_jtag_reg.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Battery.h"
#include "BleUpload.h"
#include "Canvas.h"
#include "Filters.h"
#include "Lang.h"
#include "TinyFont.h"
#include "Version.h"

namespace {
using lang::tr;

constexpr const char* kCardDir = "/camtag";
// Scheda di spento: sta con le card ma non entra nel giro delle pagine.
constexpr const char* kOffCard = "off";
// Dopo l'ultimo tocco resta sveglio per questo tempo, poi va a riposo.
constexpr uint32_t AWAKE_MS = 5000;
// Dopo un avvio resta sveglio almeno così: il tempo che serve alla USB per farsi
// riconoscere dal computer (con la USB collegata a un computer non va mai a riposo).
constexpr uint32_t BOOT_AWAKE_MS = 15000;
// Il computer può mancare per un attimo anche con il cavo collegato (isPlugged() conta
// i pacchetti che manda ogni millisecondo): prima del riposo deve mancare da almeno così.
constexpr uint32_t USB_GONE_MS = 3000;
// A riposo guarda i tasti ogni REST_POLL_MS: una pressione più corta può sfuggire.
constexpr uint32_t REST_POLL_MS = 25;
constexpr uint32_t REST_IMU_MS = 1000;   // a riposo: ogni quanto guarda il verso
constexpr uint32_t REST_USB_MS = 5000;   // a riposo: ogni quanto guarda se è in carica
constexpr uint32_t REST_HOST_MS = 500;   // a riposo: ogni quanto sente se sul cavo c'è un computer
// Visto un computer sul cavo resta sveglio così, il tempo perché riconosca la USB
// (misurato: da 0,13 a 0,35 s). Se non la riconosce (computer che dorme, hub senza
// computer, caricatore che tiene basso il filo dei dati) torna a riposo e riprova
// dopo un minuto, poi ogni due, con un'attesa più corta: un hub lasciato attaccato
// mentre il portatile va e viene è un uso normale, e al ritorno non deve aspettare tanto.
constexpr uint32_t USB_TRY_MS = 2000;
constexpr uint32_t USB_RETRY_TRY_MS = 700;
constexpr uint32_t USB_RETRY_MIN_MS = 60000;
constexpr uint32_t USB_RETRY_MAX_MS = 120000;
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
// Aggiornamento del firmware: percentuale ridisegnata di rado (ogni aggiornamento
// dello schermo ferma i dati), e batteria minima se non è in carica.
constexpr uint32_t FW_REDRAW_MS = 8000;
constexpr uint8_t FW_MIN_BATTERY_PCT = 30;
constexpr int LOW_BATTERY_PERCENT = 20;
// Sopra questa corrente in entrata è "in carica" (sotto è rumore del gauge).
constexpr int16_t CHARGE_MIN_MA = 5;
constexpr uint32_t USB_POLL_MS = 1000;  // da sveglio: ogni quanto guarda USB e carica
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
uint32_t awakeUntilMs = 0;    // sveglio almeno fino a qui: dopo l'avvio, o aspettando il computer
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
uint32_t usbHostMs = 0;      // ultima volta che il computer c'era (usbHost); 0 = non da questo risveglio
bool usbLost = false;        // da allora è mancato almeno una volta
uint32_t hostTriedMs = 0;    // ultimo risveglio per un computer visto sul cavo
uint32_t hostRetryMs = 0;    // attesa prima del prossimo; 0 = subito (cavo staccato da allora)
bool hostTrying = false;     // sveglio per quel motivo, il computer non ha ancora risposto
bool gaugePending = true;    // capacità della batteria ancora da controllare nel gauge
Canvas::Rotation pendingRot = Canvas::ROT_0;
uint32_t pendingSinceMs = 0;
uint32_t bannerUntilMs = 0;
bool sdReady = false;

// Impostazioni del configuratore web (/camtag/settings.ini, docs in CLAUDE.md "Formato card").
// Senza il file: etichette dei tasti accese, nome CAMTAG solo senza scheda di spento.
constexpr const char* kSettingsFile = "/camtag/settings.ini";
bool keyLabels = true;  // keys=1: etichette dei tasti sulla schermata senza card e da spento
int8_t offLogo = -1;    // off.logo: 1 sì, 0 no, -1 solo se non c'è la scheda di spento

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

// Lasciato solo spento (POF), dopo un minuto o due il pannello fa affiorare il negativo
// delle schermate precedenti (docs/schermo.md); in sonno profondo no. Ci va
// DISPLAY_SLEEP_MS dopo l'ultimo aggiornamento, così i cambi ravvicinati dei filtri
// restano veloci. Risvegliarlo (begin) svuota il framebuffer, quindi si fa prima di
// disegnare, al primo canvas(); il primo aggiornamento dopo il risveglio non può
// essere veloce (la RAM del controller non ha più il frame di prima).
constexpr uint32_t DISPLAY_SLEEP_MS = 4000;
bool displayAsleep = false;
bool displayFresh = false;
uint32_t lastPresentMs = 0;

void wakeDisplay() {
  if (!displayAsleep) return;
  display->begin();
  display->skipInitialResync();
  displayAsleep = false;
  displayFresh = true;
  fltDrawn = false;  // framebuffer bianco: la schermata filtri va ricomposta tutta
}

void sleepDisplay() {
  display->deepSleep();
  displayAsleep = true;
  displayFresh = false;
}

Canvas canvas(Canvas::Rotation rot) {
  wakeDisplay();
  return Canvas(display->getFrameBuffer(), display->getDisplayWidth(), display->getDisplayHeight(),
                display->getDisplayWidthBytes(), rot);
}

// --- Elenco card --------------------------------------------------------------
void loadSettings();
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
  loadSettings();
}

// Lingua delle scritte del firmware: da NVS, da lang= in settings.ini o dalla
// schermata di scelta del primo avvio. Senza scelta resta l'inglese.
bool choosingLang = false;
void setLang(lang::Lang l) {
  if (l >= lang::kCount) return;
  lang::current = l;
  if (prefs.getUChar("lang", 255) != l) prefs.putUChar("lang", l);
}

// Legge settings.ini: righe "chiave=valore", il resto si ignora.
void loadSettings() {
  keyLabels = true;
  offLogo = -1;
  if (!sdReady) return;
  FsFile f = SdMan.open(kSettingsFile);
  if (!f) return;
  char line[64];
  int n = 0;
  for (;;) {
    const int ch = f.read();
    if (ch < 0 || ch == '\n') {
      line[n] = 0;
      if (n && line[n - 1] == '\r') line[--n] = 0;
      const char* eq = strchr(line, '=');
      if (eq && line[0] != '#') {
        const String key(line, eq - line);
        const int v = atoi(eq + 1);
        if (key == "keys") keyLabels = v != 0;
        else if (key == "off.logo") offLogo = v ? 1 : 0;
        else if (key == "lang") setLang(lang::parse(eq + 1));
      }
      n = 0;
      if (ch < 0) break;
    } else if (n < static_cast<int>(sizeof(line)) - 1) {
      line[n++] = static_cast<char>(ch);
    }
  }
  f.close();
  LOGF("[camtag] impostazioni: tasti %d, nome %d, lingua %d\n", keyLabels, offLogo, lang::current);
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

// --- Cavo USB ---------------------------------------------------------------------
// L'X3 non ha un pin che dica se c'è il cavo: "dati" vuol dire che un computer ha
// riconosciuto la USB, "in carica" che il gauge vede entrare corrente. Su un
// caricatore senza dati, a batteria piena la carica si ferma e non si vede nulla.
bool usbHost() { return HWCDC::isPlugged(); }

// Appunti sull'ultimo riposo per le prove del cavo (scripts/taratura.py usb): aprire
// la seriale riavvia l'X3, e questi restano in memoria fino all'avvio dopo, che li stampa.
struct RestNote {
  uint32_t magic;
  uint32_t secs;    // durata del riposo
  uint32_t probes;  // letture del cavo
  uint32_t host;    // quante con un computer
  int32_t hostMs;   // dal risveglio al computer che riconosce la USB; -1 = non l'ha fatto
  char why[16];
};
constexpr uint32_t kRestNoteMagic = 0x43544147;
RTC_NOINIT_ATTR RestNote restNote;

// A riposo la USB non può funzionare (nel sonno leggero il suo orologio è fermo), e
// un dispositivo che si presenta e poi non risponde fa rinunciare il computer finché
// non si stacca il cavo. Quindi a riposo il pad USB è spento e senza resistenze, come
// a cavo staccato, e all'uscita si riaccende: per il computer è un dispositivo appena
// collegato, e lo riconosce da capo.
constexpr auto kUsbDm = static_cast<gpio_num_t>(USB_INT_PHY0_DM_GPIO_NUM);
uint32_t usbDmMux = 0;  // configurazione del pin D- da rimettere all'uscita dal riposo

void usbPad(bool on) {
  constexpr uint32_t kPulls = USB_SERIAL_JTAG_DP_PULLUP | USB_SERIAL_JTAG_DP_PULLDOWN | USB_SERIAL_JTAG_DM_PULLUP |
                              USB_SERIAL_JTAG_DM_PULLDOWN;
  uint32_t v =
      REG_READ(USB_SERIAL_JTAG_CONF0_REG) & ~(kPulls | USB_SERIAL_JTAG_PAD_PULL_OVERRIDE | USB_SERIAL_JTAG_USB_PAD_ENABLE);
  if (on) {
    gpio_set_direction(kUsbDm, GPIO_MODE_DISABLE);
    gpio_set_level(kUsbDm, 0);
    REG_WRITE(GPIO_PIN_MUX_REG[kUsbDm], usbDmMux);
    // Come HWCDC::begin: resistenze decise dalla periferica, pull-up su D+, pad acceso.
    v |= USB_SERIAL_JTAG_DP_PULLUP | USB_SERIAL_JTAG_USB_PAD_ENABLE;
  } else {
    usbDmMux = REG_READ(GPIO_PIN_MUX_REG[kUsbDm]);
    v |= USB_SERIAL_JTAG_PAD_PULL_OVERRIDE;
  }
  REG_WRITE(USB_SERIAL_JTAG_CONF0_REG, v);
}

// Con il pad spento: c'è un computer (o un hub) all'altro capo del cavo? Chi ospita
// una USB tiene i due fili dei dati a massa con 15 kΩ. Si porta D- a 3,3 V per un
// attimo e lo si lascia libero: con quella resistenza torna a zero in pochi
// microsecondi, a cavo staccato o su un caricatore resta dov'è. Non serve corrente
// di carica, quindi vale anche a batteria piena; per il computer D- alto per qualche
// microsecondo non è un dispositivo (ne aspetta 100 ms fermi).
bool usbHostPull() {
  gpio_set_level(kUsbDm, 1);
  gpio_set_direction(kUsbDm, GPIO_MODE_OUTPUT);
  esp_rom_delay_us(10);
  gpio_set_direction(kUsbDm, GPIO_MODE_INPUT);
  esp_rom_delay_us(60);
  const bool low = gpio_get_level(kUsbDm) == 0;
  gpio_set_direction(kUsbDm, GPIO_MODE_DISABLE);  // ingresso spento: il filo libero non consuma
  return low;
}

bool chargingNow() {
  int16_t ma = 0;
  return battery::current(ma) && ma > CHARGE_MIN_MA;
}

struct UsbState {
  bool data = false;
  bool charging = false;
  bool operator==(const UsbState& o) const { return data == o.data && charging == o.charging; }
  bool operator!=(const UsbState& o) const { return !(*this == o); }
};
UsbState usbShown;         // com'è sulla card a schermo
UsbState usbSeen;          // lettura diversa, da confermare al giro dopo
bool usbPending = false;
uint32_t usbPollMs = 0;

UsbState readUsb() { return {usbHost(), chargingNow()}; }

// Fulmine 14x24 per la carica, riempito pixel per pixel dentro il poligono.
void drawBolt(Canvas& c, int x, int y) {
  static constexpr int8_t kPx[] = {9, 1, 6, 4, 13, 8, 11};
  static constexpr int8_t kPy[] = {0, 13, 13, 24, 10, 10, 0};
  constexpr int n = sizeof(kPx);
  for (int py = 0; py < 24; py++)
    for (int px = 0; px < 14; px++) {
      const float fx = px + 0.5f, fy = py + 0.5f;
      bool in = false;
      for (int i = 0, j = n - 1; i < n; j = i++)
        if ((kPy[i] > fy) != (kPy[j] > fy) &&
            fx < (kPx[j] - kPx[i]) * (fy - kPy[i]) / static_cast<float>(kPy[j] - kPy[i]) + kPx[i])
          in = !in;
      if (in) c.fillRect(x + px, y + py, 1, 1, true);
    }
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
  wakeDisplay();
  const bool full = forceFull || changesSinceFull >= FULL_REFRESH_EVERY;
  // Pannello spento subito dopo ogni aggiornamento: lasciato alimentato,
  // l'immagine scivola in pochi minuti verso quella di prima (fantasma).
  EPD_BEGIN();
  display->displayBuffer(full ? EInkDisplay::FULL_REFRESH : EInkDisplay::HALF_REFRESH, true);
  lastPresentMs = millis();
  displayFresh = false;
  EPD_END(full ? "completo" : "mezzo");
  screenUpdates++;
  changesSinceFull = full ? 0 : changesSinceFull + 1;
  fastShadow = false;  // mezzo e completo portano ogni pixel al suo valore
}

// Aggiornamento veloce (schermata filtri): anche qui il pannello si spegne subito.
// Lascia ombra: la toglie il mezzo refresh quando ND e diottria restano fermi.
void presentFast() {
  wakeDisplay();
  if (displayFresh) {
    present(false);
    return;
  }
  EPD_BEGIN();
  display->displayBuffer(EInkDisplay::FAST_REFRESH, true);
  lastPresentMs = millis();
  displayFresh = false;
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

// --- Etichette dei tasti ----------------------------------------------------------
// Linguette bianche con bordo nero sul bordo fisico di ogni tasto, con il nome dritto
// per chi legge il contenuto: sulla schermata senza card e da spento, per chi non
// conosce ancora i tasti (keys=0 in settings.ini le toglie). Posizioni lungo il bordo
// nella posizione naturale (verticale, power in alto a destra), in frazioni: le stesse
// di KEY_TABS nel configuratore web.
struct KeyTab {
  uint8_t edge;          // bordo naturale: 0 alto, 1 destra, 2 basso, 3 sinistra
  float from, to;        // estensione lungo il bordo
  const char* label[2];  // uno o due nomi (bilanciere)
  float at[2];           // centro di ogni nome
  float divider;         // separatore tra i due tasti di un bilanciere; 0 = nessuno
  uint8_t scale;
};
constexpr KeyTab kKeyTabs[] = {
    {0, 0.05f, 0.23f, {"RESET", nullptr}, {0.14f, 0}, 0, 2},
    {0, 0.76f, 0.94f, {"PWR", nullptr}, {0.85f, 0}, 0, 3},
    {3, 0.13f, 0.35f, {"X", nullptr}, {0.24f, 0}, 0, 3},
    {1, 0.13f, 0.35f, {"Y", nullptr}, {0.24f, 0}, 0, 3},
    {2, 0.10f, 0.47f, {"A", "B"}, {0.14f, 0.44f}, 0.285f, 3},
    {2, 0.53f, 0.90f, {"C", "D"}, {0.57f, 0.86f}, 0.715f, 3},
};
constexpr int kTabPad = 5;
constexpr int kTabRadius = 8;  // spigoli arrotondati delle linguette

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool overlaps(const Rect& o) const { return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h; }
};
struct Insets {
  int top = 0, right = 0, bottom = 0, left = 0;
};

// Punto del bordo naturale `edge`, a `along` (frazione) e `depth` px verso l'interno.
void edgePoint(const Canvas& n, int edge, float along, int depth, int& x, int& y) {
  const int W = n.width(), H = n.height();
  switch (edge) {
    case 0: x = static_cast<int>(along * W + 0.5f); y = depth; break;
    case 1: x = W - 1 - depth; y = static_cast<int>(along * H + 0.5f); break;
    case 2: x = static_cast<int>(along * W + 0.5f); y = H - 1 - depth; break;
    default: x = depth; y = static_cast<int>(along * H + 0.5f); break;
  }
}
// Dal canvas naturale a quello del contenuto.
void toContent(const Canvas& n, const Canvas& c, int x, int y, int& lx, int& ly) {
  int px, py;
  n.toPanel(x, y, px, py);
  c.fromPanel(px, py, lx, ly);
}
// Rettangolo tra due punti del canvas naturale, nel canvas del contenuto.
Rect contentRect(const Canvas& n, const Canvas& c, int x0, int y0, int x1, int y1) {
  int ax, ay, bx, by;
  toContent(n, c, x0, y0, ax, ay);
  toContent(n, c, x1, y1, bx, by);
  Rect r;
  r.x = std::min(ax, bx);
  r.y = std::min(ay, by);
  r.w = std::abs(bx - ax) + 1;
  r.h = std::abs(by - ay) + 1;
  return r;
}

// Profondità della linguetta: il testo corre sempre in orizzontale per chi legge,
// quindi su un bordo che il contenuto vede verticale conta la sua larghezza.
int tabDepth(const Canvas& n, const Canvas& c, const KeyTab& t) {
  int x0, y0, x1, y1, ax, ay, bx, by;
  edgePoint(n, t.edge, t.from, 0, x0, y0);
  edgePoint(n, t.edge, t.to, 0, x1, y1);
  toContent(n, c, x0, y0, ax, ay);
  toContent(n, c, x1, y1, bx, by);
  const bool across = std::abs(bx - ax) > std::abs(by - ay);  // bordo orizzontale per chi legge
  int w = 0;
  for (const char* l : t.label)
    if (l) w = std::max(w, tinyfont::textWidth(l, t.scale));
  return (across ? 7 * t.scale : w) + 2 * kTabPad;
}

Rect tabRect(const Canvas& n, const Canvas& c, const KeyTab& t) {
  const int d = tabDepth(n, c, t);
  int x0, y0, x1, y1;
  edgePoint(n, t.edge, t.from, -4, x0, y0);  // oltre il bordo: da quel lato niente riga
  edgePoint(n, t.edge, t.to, d - 1, x1, y1);
  return contentRect(n, c, x0, y0, x1, y1);
}

// Spazio che le etichette prendono su ogni bordo del contenuto.
Insets keyInsets(const Canvas& c) {
  Insets in;
  if (!keyLabels) return in;
  const Canvas n = canvas(kNaturalRot);
  for (const KeyTab& t : kKeyTabs) {
    const Rect r = tabRect(n, c, t);
    if (r.y <= 0) in.top = std::max(in.top, r.y + r.h);
    if (r.y + r.h >= c.height()) in.bottom = std::max(in.bottom, c.height() - r.y);
    if (r.x <= 0) in.left = std::max(in.left, r.x + r.w);
    if (r.x + r.w >= c.width()) in.right = std::max(in.right, c.width() - r.x);
  }
  return in;
}

bool hitsKeyLabel(const Canvas& c, const Rect& box) {
  if (!keyLabels) return false;
  const Canvas n = canvas(kNaturalRot);
  for (const KeyTab& t : kKeyTabs)
    if (tabRect(n, c, t).overlaps(box)) return true;
  return false;
}

// Il pixel (px, py) sta dentro il rettangolo con gli spigoli arrotondati di raggio rad?
bool inRound(int px, int py, const Rect& r, int rad) {
  const int cx = std::min(std::max(px, r.x + rad), r.x + r.w - 1 - rad);
  const int cy = std::min(std::max(py, r.y + rad), r.y + r.h - 1 - rad);
  const int dx = px - cx, dy = py - cy;
  return dx * dx + dy * dy <= rad * rad + rad;
}
// Riquadro bianco con bordo nero di `t` px e spigoli arrotondati.
void roundBox(Canvas& c, const Rect& r, int rad, int t) {
  const Rect in{r.x + t, r.y + t, r.w - 2 * t, r.h - 2 * t};
  for (int y = r.y; y < r.y + r.h; y++)
    for (int x = r.x; x < r.x + r.w; x++)
      if (inRound(x, y, r, rad)) c.setPixel(x, y, !inRound(x, y, in, rad - t));
}

void drawKeyLabels(Canvas& c) {
  if (!keyLabels) return;
  const Canvas n = canvas(kNaturalRot);
  for (const KeyTab& t : kKeyTabs) {
    const int d = tabDepth(n, c, t);
    const Rect r = tabRect(n, c, t);
    roundBox(c, r, kTabRadius, 2);
    if (t.divider > 0) {
      int x0, y0, x1, y1;
      edgePoint(n, t.edge, t.divider, 0, x0, y0);
      edgePoint(n, t.edge, t.divider, d - 1, x1, y1);
      const Rect l = contentRect(n, c, x0, y0, x1, y1);
      c.fillRect(l.x - (l.w == 1), l.y - (l.h == 1), std::max(l.w, 2), std::max(l.h, 2), true);
    }
    for (int i = 0; i < 2; i++) {
      if (!t.label[i]) continue;
      int x, y, lx, ly;
      edgePoint(n, t.edge, t.at[i], d / 2, x, y);
      toContent(n, c, x, y, lx, ly);
      c.text(lx - tinyfont::textWidth(t.label[i], t.scale) / 2, ly - 7 * t.scale / 2, t.label[i], t.scale);
    }
  }
}

// La misura più grande, fino a `scale`, con cui il testo sta in `w` px.
int fitScale(const char* s, int scale, int w) {
  while (scale > 1 && tinyfont::textWidth(s, scale) > w) scale--;
  return scale;
}

// Schermata senza card (e gli altri avvisi): il nome, sotto un riquadro con il
// messaggio ben in vista, e le etichette dei tasti.
void showMessage(const char* line1, const char* line2) {
  Canvas c = canvas(rotation);
  c.clear();
  const bool port = Canvas::isPortrait(rotation);
  const int W = c.width(), mid = c.height() / 2;
  c.textCentered(mid - (port ? 150 : 120), "CAMTAG", port ? 6 : 8);
  constexpr int kPadX = 28, kPadY = 24, kGap = 20;
  const int maxW = W - 2 * kPadX - 80;
  const int s1 = fitScale(line1, port ? 4 : 5, maxW);
  const int s2 = line2 ? fitScale(line2, 3, maxW) : 0;
  const int tw = std::max(tinyfont::textWidth(line1, s1), line2 ? tinyfont::textWidth(line2, s2) : 0);
  const int bw = tw + 2 * kPadX, bh = 7 * s1 + (line2 ? kGap + 7 * s2 : 0) + 2 * kPadY;
  const int bx = (W - bw) / 2, by = mid - (port ? 40 : 30);
  c.strokeRect(bx, by, bw, bh, 4, true);
  c.textCentered(by + kPadY, line1, s1);
  if (line2) c.textCentered(by + kPadY + 7 * s1 + kGap, line2, s2);
  drawKeyLabels(c);
  present(true);
  prefs.putString("shown", "");  // al prossimo avvio ridisegna
}

// Scelta della lingua al primo avvio (finché in NVS non c'è "lang"): A o B
// inglese, C o D italiano, con le etichette dei tasti per trovarli. Bilingue.
void showLangChoice() {
  Canvas c = canvas(rotation);
  c.clear();
  const bool port = Canvas::isPortrait(rotation);
  const int W = c.width(), mid = c.height() / 2;
  c.textCentered(mid - (port ? 170 : 150), "CAMTAG", port ? 6 : 8);
  c.textCentered(mid - (port ? 80 : 70), "LANGUAGE - LINGUA", 3);
  const int s = port ? 4 : 5, lh = 7 * s, gap = 24, pad = 26;
  const char* l1 = "A / B  ENGLISH";
  const char* l2 = "C / D  ITALIANO";
  const int bw = std::max(tinyfont::textWidth(l1, s), tinyfont::textWidth(l2, s)) + 2 * pad;
  const int bh = 2 * lh + gap + 2 * pad, bx = (W - bw) / 2, by = mid - (port ? 30 : 30);
  c.strokeRect(bx, by, bw, bh, 4, true);
  c.text(bx + pad, by + pad, l1, s);
  c.text(bx + pad, by + pad + lh + gap, l2, s);
  const bool saved = keyLabels;
  keyLabels = true;
  drawKeyLabels(c);
  keyLabels = saved;
  present(true);
  prefs.putString("shown", "");
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
constexpr int kLockBox = 36;

// Angolo in alto a sinistra del lucchetto, nelle coordinate di c.
void lockOrigin(const Canvas& c, int& x, int& y) {
  constexpr float kPowerPos = 0.85f;  // centro del power lungo il bordo in alto
  const Canvas natural = canvas(kNaturalRot);
  int px, py, lx, ly;
  natural.toPanel(static_cast<int>(kPowerPos * natural.width() + 0.5f), kLockBox / 2 + 4, px, py);
  c.fromPanel(px, py, lx, ly);
  x = lx - kLockBox / 2;
  y = ly - kLockBox / 2;
}

void drawLock(Canvas& c) {
  constexpr int kBox = kLockBox;
  int x, y;
  lockOrigin(c, x, y);
  c.fillRect(x, y, kBox, kBox, false);
  c.strokeRect(x, y, kBox, kBox, 2, true);
  c.fillRect(x + 12, y + 7, 12, 3, true);  // arco
  c.fillRect(x + 12, y + 7, 3, 10, true);
  c.fillRect(x + 21, y + 7, 3, 10, true);
  c.fillRect(x + 9, y + 16, 18, 13, true);  // corpo
  c.fillRect(x + 17, y + 20, 2, 5, false);  // serratura
}

// In alto a destra nel verso del contenuto, da destra: batteria (scarica o in carica),
// fulmine se è in carica, linguetta USB se un computer la vede.
// Con i tasti bloccati scende sotto il lucchetto se gli finirebbe sopra.
void drawStatus(Canvas& c, const UsbState& s) {
  const int pct = batteryPercent();
  const bool showBattery = s.charging || (pct >= 0 && pct < LOW_BATTERY_PERCENT);
  const int usbW = tinyfont::textWidth("USB", 2) + 14;
  const int width = (showBattery ? 48 : 0) + (s.charging ? 22 : 0) + (s.data ? usbW + 4 : 0);
  if (!width) return;
  int right = c.width() - 10;  // bordo destro libero del prossimo segno, aloni compresi
  int y = 12;
  if (keysLocked) {
    int lx, ly;
    lockOrigin(c, lx, ly);
    if (lx < right && lx + kLockBox > right - width && ly < y + 22 && ly + kLockBox > y - 2) y = ly + kLockBox + 8;
  }
  if (showBattery) {
    c.battery(right - 42, y, pct < 0 ? 0 : pct);
    right -= 44 + 4;
  }
  if (s.charging) {
    c.fillRect(right - 18, y - 2, 18, 24, false);
    drawBolt(c, right - 16, y - 2);
    right -= 18 + 4;
  }
  if (s.data) {
    const int w = usbW, h = 24;
    c.fillRect(right - w, y - 2, w, h, false);
    c.strokeRect(right - w, y - 2, w, h, 2, true);
    c.text(right - w + 7, y + 3, "USB", 2);
  }
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
    showMessage(tr(lang::BadBmp), card.base.c_str());
    return;
  }
  fltDrawn = filtersOn();
  if (fltDrawn) {
    if (!filters::drawParts(c, fltCfg, flt, fltDir.c_str(), fltSel, filters::kPartAll))
      LOGF("[filtri] mancano dei pezzi in %s\n", fltDir.c_str());
    drawHints();
  }

  usbShown = readUsb();
  usbPending = false;
  drawStatus(c, usbShown);
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

// Tipo di una card per i tasti: la parola dopo "NN_" nel nome, fino al primo "_" o "-"
// (NN_lens_35mm-…, NN_lenschart-2, NN_notes_…); le pagine della lens chart sono del
// tipo delle lenti. Una card con un nome diverso è un tipo a sé, salvo che la parola
// coincida. Specifica: navType nel configuratore web.
String navType(const Card& c) {
  const int us = c.base.indexOf('_');
  if (us < 0) return c.base;
  int end = us + 1;
  while (end < static_cast<int>(c.base.length()) && c.base[end] != '_' && c.base[end] != '-') end++;
  const String w = c.base.substring(us + 1, end);
  return w == "lenschart" ? String("lens") : w;
}

// X e Y: prima pagina del tipo precedente o successivo, in ciclo, nell'ordine dei nomi.
void stepType(int dir) {
  std::vector<String> types;
  for (const Card& c : cards) {
    const String t = navType(c);
    if (std::find(types.begin(), types.end(), t) == types.end()) types.push_back(t);
  }
  const int n = static_cast<int>(types.size());
  const int at = static_cast<int>(std::find(types.begin(), types.end(), navType(cards[current])) - types.begin());
  const String next = types[((at + dir) % n + n) % n];
  for (int i = 0; i < static_cast<int>(cards.size()); i++)
    if (navType(cards[i]) == next) {
      bannerUntilMs = 0;
      showCard(i, false);
      return;
    }
}

// Su un tipo di più pagine A–D sfogliano solo quelle, in ciclo. I due tasti di un
// bilanciere fanno la stessa cosa: avanza il bilanciere che chi guarda vede a destra
// o in basso, arretra quello a sinistra o in alto (A|B: sinistra a 0°, in alto a 90°,
// a destra a 180°, in basso a 270°). Specifica: lensPageDir nel configuratore web.
void stepPage(bool rockerAB, Canvas::Rotation rot) {
  const String type = navType(cards[current]);
  std::vector<int> pages;
  for (int i = 0; i < static_cast<int>(cards.size()); i++)
    if (navType(cards[i]) == type) pages.push_back(i);
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
  if (!fltDrawn || displayAsleep || bannerUntilMs || drawnRot != rotationFor(cards[current], rotation)) {
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
  showCard(current, false, tr(rotationLocked ? lang::RotLocked : lang::RotAuto));
  bannerUntilMs = millis() + BANNER_MS;
}

// --- Ricezione via Bluetooth ----------------------------------------------------
// Testo centrato che resta dentro la cornice della ricezione (bordo da 16 a 20 px):
// troppo largo va su due righe se c'è " - " ("FIRMWARE AGGIORNATO - RIAVVIO"), se no si
// rimpicciolisce. Restituisce il numero di righe usate.
int textFit(Canvas& c, int y, const char* s, int scale) {
  const int maxW = c.width() - 2 * 22;  // 2 px dal bordo: ci sta "AGGIORNAMENTO FIRMWARE 100%"
  if (tinyfont::textWidth(s, scale) <= maxW) { c.textCentered(y, s, scale); return 1; }
  const char* dash = strstr(s, " - ");
  if (dash) {
    char first[64];
    const size_t n = std::min(static_cast<size_t>(dash - s), sizeof(first) - 1);
    memcpy(first, s, n);
    first[n] = '\0';
    const char* second = dash + 3;
    if (tinyfont::textWidth(first, scale) <= maxW && tinyfont::textWidth(second, scale) <= maxW) {
      c.textCentered(y, first, scale);
      c.textCentered(y + 10 * scale, second, scale);
      return 2;
    }
  }
  while (scale > 1 && tinyfont::textWidth(s, scale) > maxW) scale--;
  c.textCentered(y, s, scale);
  return 1;
}

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
  // Con lo stato su due righe dettaglio e uscita scendono di una riga.
  const int down = textFit(c, mid + 20, status, port ? 3 : 4) > 1 ? (port ? 30 : 40) : 0;
  if (detail) textFit(c, mid + 80 + down, detail, 2);
  textFit(c, mid + 130 + down, tr(lang::ExitHint), 2);
  // In piccolo in fondo: versione del firmware e credits.
  const int h = c.height();
  // Il marchio CAMTAG_FW=… va usato da qualche parte, se no il linker lo toglie.
  static const char kTag[] = CAMTAG_FW_TAG;
  char fw[32];
  snprintf(fw, sizeof(fw), "FIRMWARE %s", kTag + sizeof("CAMTAG_FW=") - 1);
  c.textCentered(h - 100, fw, 2);
  c.textCentered(h - 76, tr(lang::Credits), 2);
  c.textCentered(h - 52, "TRUNK STUDIO - TRUNKSTUDIO.IT", 2);
  present(full);
  lastReceiveDrawMs = millis();
  LOGF("[camtag] schermo ricezione: %lu ms\n", static_cast<unsigned long>(lastReceiveDrawMs - t0));
  prefs.putString("shown", "");  // al prossimo avvio ridisegna la card
}

// Aggiornamento del firmware via Bluetooth: conferma sui tasti (A-D sì, X o Y no).
void drawFirmwareAsk() {
  Canvas c = canvas(rotation);
  const bool port = Canvas::isPortrait(rotation);
  const int mid = c.height() / 2;
  c.clear();
  c.textCentered(mid - 170, "FIRMWARE", port ? 5 : 7);
  textFit(c, mid - 80, tr(lang::FwAsk), port ? 3 : 4);
  char line[48];
  snprintf(line, sizeof(line), tr(lang::FwFromTo), CAMTAG_FW_VERSION, bleupload::firmwareVersion());
  textFit(c, mid - 20, line, port ? 3 : 4);
  textFit(c, mid + 60, tr(lang::FwYes), port ? 2 : 3);
  textFit(c, mid + 100, tr(lang::FwNo), port ? 2 : 3);
  drawKeyLabels(c);
  present(true);
  lastReceiveDrawMs = millis();
  prefs.putString("shown", "");
}

void drawFirmwareProgress() {
  char line[40];
  snprintf(line, sizeof(line), tr(lang::FwUpdating), static_cast<unsigned>(bleupload::firmwarePercent()));
  drawReceive(line, tr(lang::FwKeepOn), false);
}

// Risposta alla conferma (tasti o batteria) e schermata che ne segue.
void answerFirmware(bool yes, const char* reason) {
  lastActivityMs = millis();
  if (bleupload::answerFirmware(yes, reason) == bleupload::Event::Failed)
    drawReceive(tr(lang::Error), bleupload::lastError(), true);
  else
    drawFirmwareProgress();
}

// Firmware nuovo scritto: lascia arrivare il DONE al telefono e riparte da quello.
[[noreturn]] void firmwareDone() {
  drawReceive(tr(lang::FwDone), bleupload::firmwareVersion(), true);
  for (uint32_t t0 = millis(); bleupload::connected() && millis() - t0 < 2000;) delay(20);
  bleupload::stop();
  LOGF("[camtag] riavvio con il firmware nuovo\n");
  delay(100);
  ESP.restart();
  for (;;) {}
}

void showCurrentOrEmpty() {
  if (choosingLang)
    showLangChoice();
  else if (!sdReady)
    showMessage(tr(lang::NoSd), nullptr);
  else if (cards.empty())
    showMessage(tr(lang::NoCards), tr(lang::HoldX));
  else
    showCard(current, true);
}

void enterReceive() {
  if (!sdReady) {
    showMessage(tr(lang::NoSd), nullptr);
    return;
  }
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(bleName, sizeof(bleName), "CAMTAG-%02X%02X", static_cast<unsigned>((mac >> 32) & 0xFF),
           static_cast<unsigned>((mac >> 40) & 0xFF));
  bannerUntilMs = 0;
  if (!bleupload::start(bleName)) {
    showMessage(tr(lang::BleUnavailable), nullptr);
    return;
  }
  LOGF("[camtag] ricezione: %s\n", bleName);
  receiving = true;
  drawnFiles = 0;
  lastActivityMs = millis();
  drawReceive(tr(lang::Waiting), tr(lang::OpenPhone), true);
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
    showMessage(tr(lang::NoCards), tr(lang::HoldX));
    return;
  }
  showCard(0, true, tr(lang::CardsUpdated));
  bannerUntilMs = millis() + BANNER_MS;
}

void loopReceive() {
  if (!receiving) return;  // X o Y l'hanno appena chiusa (shortPress)

  char line[24];
  switch (bleupload::poll()) {
    case bleupload::Event::Connected:
      lastActivityMs = millis();
      drawReceive(tr(lang::Connected), nullptr, false);
      break;
    case bleupload::Event::Disconnected:
      lastActivityMs = millis();
      drawReceive(tr(lang::Waiting), tr(lang::LinkClosed), false);
      break;
    case bleupload::Event::Progress:
      lastActivityMs = millis();
      if (bleupload::filesReceived() != drawnFiles && millis() - lastReceiveDrawMs > RECEIVE_REDRAW_MS) {
        drawnFiles = bleupload::filesReceived();
        snprintf(line, sizeof(line), tr(lang::ReceivedN), static_cast<unsigned>(drawnFiles));
        drawReceive(line, nullptr, false);
      }
      break;
    case bleupload::Event::Done:
      receiveDone();
      return;
    case bleupload::Event::Failed:
      lastActivityMs = millis();
      drawReceive(tr(lang::Error), bleupload::lastError(), false);
      break;
    case bleupload::Event::Sending:
      lastActivityMs = millis();
      drawReceive(tr(lang::SendingProject), nullptr, false);
      break;
    case bleupload::Event::FirmwareAsk: {
      lastActivityMs = millis();
      // Con poca batteria e senza carica niente aggiornamento: spegnersi a metà
      // non rovina nulla (il vecchio firmware resta), ma lo si rifà da capo.
      battery::Reading r;
      battery::read(r);
      if (r.ok && r.ma <= CHARGE_MIN_MA && r.pct < FW_MIN_BATTERY_PCT)
        answerFirmware(false, "BATTERIA");
      else
        drawFirmwareAsk();
      break;
    }
    case bleupload::Event::FirmwareDone:
      firmwareDone();
    case bleupload::Event::Sent:
      lastActivityMs = millis();
      drawReceive(tr(lang::ProjectSent), tr(lang::CardsUnchanged), false);
      break;
    case bleupload::Event::Aborted:
      lastActivityMs = millis();
      drawReceive(tr(lang::Cancelled), tr(lang::CardsUnchanged), false);
      break;
    case bleupload::Event::None:
      break;
  }
  if (bleupload::firmwareReceiving() && millis() - lastReceiveDrawMs > FW_REDRAW_MS) drawFirmwareProgress();
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
// La scheda di spento, se c'è, e il nome CAMTAG (al centro senza scheda, in alto in
// un riquadro sopra la scheda, se off.logo lo chiede), con una fascia in basso che
// dice che è spento e come si riaccende, la batteria e le etichette dei tasti.
// Blank: solo la fascia su bianco.
void drawOff(Off why) {
  loadSettings();  // anche prima dell'elenco card (batteria scarica all'avvio)
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
  const bool port = Canvas::isPortrait(rot), labels = keyLabels && why != Off::Blank;
  if (!drawn) c.clear();
  if (why != Off::Blank && (drawn ? offLogo == 1 : offLogo != 0)) {
    const int scale = port ? 6 : 8, tw = tinyfont::textWidth("CAMTAG", scale);
    if (!drawn) {
      c.textCentered(c.height() / 2 - 40, "CAMTAG", scale);
    } else {
      // In un riquadro bianco a un quarto dell'altezza, sopra l'immagine.
      const int bw = tw + 48, bh = 7 * scale + 36, bx = (c.width() - bw) / 2, by = c.height() / 4 - bh / 2;
      c.fillRect(bx, by, bw, bh, false);
      c.strokeRect(bx, by, bw, bh, 3, true);
      c.textCentered(by + 18, "CAMTAG", scale);
    }
  }
  const bool saved = keyLabels;
  keyLabels = labels;
  const Insets in = keyInsets(c);
  constexpr int kScale = 2, kH = 7 * kScale + 22;
  constexpr int kGap = 10;  // tra la fascia e le etichette in basso
  const int fy = c.height() - in.bottom - (in.bottom ? kGap : 0) - kH;
  c.fillRect(0, fy, c.width(), kH, false);
  c.fillRect(0, fy, c.width(), 2, true);
  c.textCentered(fy + 13, tr(why == Off::Empty ? lang::OffEmpty : lang::OffHold),
                 kScale);
  if (why != Off::Blank) {
    // Quanta batteria aveva quando si è spento: in alto a destra, o al centro in alto
    // con le etichette dei tasti (sotto, se lì c'è un'etichetta). Il riquadro bianco
    // tocca il bordo.
    const int pct = batteryPercent();
    if (pct >= 0) {
      char label[6];
      snprintf(label, sizeof(label), "%d%%", pct);
      const int w = tinyfont::textWidth(label, kScale);
      Rect box{c.width() - 62 - w, 0, w + 56, 30};
      if (labels) {
        box.x = (c.width() - box.w) / 2;
        if (hitsKeyLabel(c, box)) box.y = in.top + 8;
      }
      c.fillRect(box.x, box.y, box.w, box.h, false);
      c.text(box.x + 4, box.y + 8, label, kScale);
      c.battery(box.x + 10 + w, box.y + 5, pct);
    }
  }
  drawKeyLabels(c);
  keyLabels = saved;
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

bool canRest() {
  const uint32_t now = millis();
  if (usbHost()) {
    if (hostTrying) {
      restNote.hostMs = static_cast<int32_t>(now - hostTriedMs);
      LOGF("[usb] computer dopo %ld ms\n", static_cast<long>(restNote.hostMs));
    } else if (usbLost && usbHostMs) {
      // Per capire quanto dura quando manca con il cavo collegato (docs/schermo.md).
      LOGF("[usb] computer non sentito per %lu ms\n", static_cast<unsigned long>(now - usbHostMs));
    }
    usbLost = hostTrying = false;
    usbHostMs = now;
    return false;
  }
  usbLost = true;
  return !receiving && fltSel < 0 && !fltSettleAt && !bannerUntilMs && !pendingSinceMs && !gaugePending &&
         !powerDownAt && !usbPending && uxQueueMessagesWaiting(keyQueue) == 0 && now - lastActivityMs > AWAKE_MS &&
         static_cast<int32_t>(now - awakeUntilMs) >= 0 && now - usbHostMs > USB_GONE_MS;
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

// Sonno leggero finché non succede qualcosa: un tasto, il power, un altro verso, un
// computer sul cavo, la carica che comincia, la riga del registro, lo spegnimento da
// dimenticato. Il chip dorme e si sveglia ogni REST_POLL_MS per circa un millisecondo.
void rest() {
  const auto powerPin = static_cast<gpio_num_t>(BoardConfig::ACTIVE.input.power);
  const int powerDown = BoardConfig::ACTIVE.input.powerActiveHigh ? HIGH : LOW;
  LOGF("[riposo] inizio\n");
  if (!displayAsleep) sleepDisplay();
  // keyScan fermo: a riposo l'ADC lo legge solo questo ciclo. Preso con adcLock, così
  // non resta sospeso a metà lettura.
  xSemaphoreTake(adcLock, portMAX_DELAY);
  vTaskSuspend(keyScanTask);
  keepOutputsInRest();
  usbPad(false);
  hostTrying = false;
  usbHostMs = 0;
  restNote = {kRestNoteMagic, 0, 0, 0, -1, ""};
  gpio_wakeup_enable(powerPin, powerDown == LOW ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
  esp_sleep_enable_gpio_wakeup();

  restStartUs = static_cast<uint64_t>(esp_timer_get_time());
  uint32_t imuAt = millis(), usbAt = imuAt, hostAt = imuAt - REST_HOST_MS;
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
    if (now - hostAt >= REST_HOST_MS) {
      hostAt = now;
      restNote.probes++;
      if (!usbHostPull()) {
        // Cavo staccato (o caricatore): al prossimo computer subito. Solo qui: un computer
        // che riconosce la USB e poi tace non deve farlo svegliare di continuo.
        hostRetryMs = 0;
      } else {
        restNote.host++;
        if (!hostRetryMs || now - hostTriedMs >= hostRetryMs) {
          why = "usb";
          break;
        }
      }
    }
    if (now - usbAt >= REST_USB_MS) {
      usbAt = now;
      // Carica cominciata: sveglio, così la card mostra il fulmine.
      const bool charging = chargingNow();
      if (charging && !chargeSeen) {
        chargeSeen = true;
        why = "carica";
        break;
      }
      if (!charging) chargeSeen = false;
      // Carica finita: il fulmine sulla card va tolto.
      if (!charging && usbShown.charging) {
        why = "carica finita";
        break;
      }
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
  const uint64_t restBeganUs = restStartUs;
  restUs += static_cast<uint64_t>(esp_timer_get_time()) - restStartUs;
  restStartUs = 0;

  gpio_wakeup_disable(powerPin);
  // gpio_wakeup_disable lascia il tipo LOW_LEVEL: come interrupt scatterebbe di continuo.
  gpio_set_intr_type(powerPin, GPIO_INTR_DISABLE);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  usbPad(true);
  vTaskResume(keyScanTask);
  xSemaphoreGive(adcLock);
  const uint32_t now = millis();
  if (!strcmp(why, "usb")) {
    // Non è un tocco: sveglio solo il tempo che serve al computer, e se non risponde
    // la prossima prova è più in là.
    hostTrying = true;
    hostTriedMs = now;
    awakeUntilMs = now + (hostRetryMs ? USB_RETRY_TRY_MS : USB_TRY_MS);
    hostRetryMs = hostRetryMs ? std::min(hostRetryMs * 2, USB_RETRY_MAX_MS) : USB_RETRY_MIN_MS;
  } else {
    lastActivityMs = now;
  }
  restNote.secs = static_cast<uint32_t>((static_cast<uint64_t>(esp_timer_get_time()) - restBeganUs) / 1000000);
  strlcpy(restNote.why, why, sizeof(restNote.why));
  LOGF("[riposo] fine: %s\n", why);
}

// Cavo attaccato o staccato, carica cominciata o finita: ridisegna la card quando
// due letture di fila, a un secondo, sono d'accordo.
void pollUsb() {
  if (choosingLang || cards.empty() || fltSel >= 0 || bannerUntilMs || millis() - usbPollMs < USB_POLL_MS) return;
  usbPollMs = millis();
  const UsbState s = readUsb();
  if (s == usbShown) {
    usbPending = false;
    return;
  }
  if (!usbPending || s != usbSeen) {
    usbSeen = s;
    usbPending = true;
    return;
  }
  LOGF("[usb] dati %d, carica %d\n", s.data, s.charging);
  showCard(current, Refresh::Half);  // aggiorna usbShown e usbPending
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
  if (choosingLang || cards.empty()) {
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

// --- Firmware nuovo in prova --------------------------------------------------------
// Il bootloader ha il rollback: un firmware appena arrivato via Bluetooth parte "in
// prova" e, se riparte prima di essersi dichiarato sano, il bootloader torna a quello
// di prima. Il framework lo dichiarerebbe sano subito all'avvio; così lo fa
// markFirmwareValid al primo giro di loop(), a setup() finito. Va fatto presto: lo
// spento (GPIO13) e il sonno profondo passano dal bootloader, che lo boccerebbe.
extern "C" bool verifyRollbackLater() { return true; }

bool firmwareChecked = false;

void markFirmwareValid() {
  firmwareChecked = true;
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  if (!running || esp_ota_get_state_partition(running, &st) != ESP_OK) return;
  LOGF("[camtag] partizione %s, stato %d\n", running->label, static_cast<int>(st));
  if (st == ESP_OTA_IMG_PENDING_VERIFY && esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
    LOGF("[camtag] firmware nuovo confermato\n");
}

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
  LOGF("[camtag] firmware " CAMTAG_FW_VERSION "\n");
  LOGF("[camtag] device: %s riavvio=%d risveglio=%d\n", BoardConfig::ACTIVE.name, static_cast<int>(reset),
       static_cast<int>(wake));
  if (restNote.magic == kRestNoteMagic) {
    restNote.magic = 0;
    restNote.why[sizeof(restNote.why) - 1] = 0;
    LOGF("[usb] ultimo riposo: %lu s, fine %s, cavo %lu letture (%lu con computer), computer dopo %ld ms\n",
         static_cast<unsigned long>(restNote.secs), restNote.why, static_cast<unsigned long>(restNote.probes),
         static_cast<unsigned long>(restNote.host), static_cast<long>(restNote.hostMs));
  }

  prefs.begin("camtag", false);
  const uint8_t savedLang = prefs.getUChar("lang", 255);
  if (savedLang < lang::kCount) lang::current = static_cast<lang::Lang>(savedLang);
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
  awakeUntilMs = millis() + BOOT_AWAKE_MS;
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

  if (sdReady) {
    loadCardList();  // con settings.ini, che può dare la lingua
    loadFilters();
  }
  if (!cards.empty())
    current = std::min(std::max(static_cast<int>(prefs.getInt("idx", 0)), 0), static_cast<int>(cards.size()) - 1);
  if (prefs.getUChar("lang", 255) >= lang::kCount) {
    choosingLang = true;
    keysLocked = false;
    prefs.putBool("keylock", false);
    showLangChoice();
    return;
  }
  if (!sdReady) {
    showMessage(tr(lang::NoSd), nullptr);
    return;
  }
  if (cards.empty()) {
    showMessage(tr(lang::NoCards), tr(lang::HoldX));
    return;
  }

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
    if (bleupload::firmwareAsking())
      answerFirmware(k >= 'A' && k <= 'D', nullptr);
    else if (k == 'X' || k == 'Y')
      exitReceive();
    return;
  }
  if (cards.empty()) return;
  if (filtersOn() && filtersPress(k)) {
    // fatto dalla schermata filtri
  } else if (k == 'X' || k == 'Y') {
    stepType(k == 'Y' ? 1 : -1);
  } else if (!isFilters(cards[current])) {
    // Con la rotazione bloccata vale il verso del contenuto, come per i filtri.
    stepPage(k == 'A' || k == 'B', rotation);
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
    if (choosingLang) {
      holdFired = false;
      if (k >= 'A' && k <= 'D') {
        setLang(k <= 'B' ? lang::En : lang::It);
        choosingLang = false;
        LOGF("[camtag] lingua scelta: %d\n", lang::current);
        showCurrentOrEmpty();
      }
      continue;
    }
    if (holdFired) {
      holdFired = false;
    } else if (!receiving && ev.t - keyHeldAt >= LONG_PRESS_MS && hasLongPress(k)) {
      longPress(k);
    } else {
      shortPress(k);
    }
  }
  if (keyHeld && !holdFired && !receiving && !choosingLang && millis() - keyHeldAt >= LONG_PRESS_MS && hasLongPress(keyHeld)) {
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
  if (receiving || choosingLang || cards.empty()) return;
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
  if (!firmwareChecked) markFirmwareValid();
  readPower();
  handlePower();
  handleKeys();
#ifdef CAMTAG_DEBUG
  serialCommands();
#endif
  if (!displayAsleep && fltSel < 0 && !fltSettleAt && millis() - lastPresentMs >= DISPLAY_SLEEP_MS) sleepDisplay();
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
  pollUsb();

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
