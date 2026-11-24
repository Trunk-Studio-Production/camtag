#pragma once
// Fuel gauge dell'X3 (BQ27220) e registro dei consumi sulla microSD.
// Formato del registro e modo di leggerlo: docs/batteria.md.
#include <Print.h>

#include <cstdint>

namespace battery {

// Nella radice della microSD, fuori da /camtag: la ricezione via Bluetooth sostituisce
// tutta quella cartella. Si legge dalla seriale ("batt?") o via Bluetooth
// (GET di upload::kBatteryLogName).
constexpr const char* kLogPath = "/camtag-batteria.csv";

struct Reading {
  bool ok = false;       // false: nessun gauge (X4) o lettura fallita
  uint16_t mv = 0;
  int16_t ma = 0;        // + in carica, - in scarica; risoluzione 1 mA
  uint16_t mah = 0;      // capacità residua
  uint16_t fullMah = 0;  // capacità a batteria piena secondo il gauge
  uint16_t designMah = 0;
  uint8_t pct = 0;
};

// Contatori dall'ultimo avvio: ogni riga del registro li porta, così dalla differenza
// tra due righe si ricava quanto tempo è passato sveglio, a riposo e con quanti
// aggiornamenti dello schermo.
struct Counters {
  uint32_t awakeS = 0;
  uint32_t restS = 0;
  uint32_t screen = 0;
  uint32_t keys = 0;
  // Solo nella riga di avvio: tocchi brevi del power durante lo spento appena finito.
  uint32_t touches = 0;
  // Solo nella riga di avvio dopo uno spento: la corrente che il gauge aveva in memoria
  // al risveglio (wakeCurrent), cioè il consumo da spento; e la stessa lettura fatta al
  // primo tocco breve, prima che qualcosa potesse cambiare. Senza lettura la colonna
  // resta vuota.
  bool offMaOk = false, touchMaOk = false;
  int16_t offMa = 0, touchMa = 0;
};

// kDiag: un tratto della diagnosi da spento (0.5.6-0.5.11, docs/batteria.md); non si scrive più.
enum Event : char { kBoot = 'A', kSample = 'P', kOff = 'S', kDiag = 'D' };

void begin();
bool read(Reading& out);
// Solo la corrente: + in carica. false se non c'è il gauge.
bool current(int16_t& ma);
// Da chiamare per prima cosa in setup() dopo un sonno profondo: il gauge aggiorna la
// corrente ogni secondo (ogni 20 s quando è in SLEEP), quindi appena sveglio ha ancora
// la lettura fatta mentre il chip dormiva. Apre e richiude il bus I2C, con i pin
// dell'X3: a quel punto il dispositivo non è ancora stato riconosciuto.
bool wakeCurrent(int16_t& ma);

// Secondi di un orologio che attraversa sonno e riavvii (riparte solo togliendo la
// batteria): serve a misurare quanto è durato lo spento.
uint32_t nowS();

// Aggiunge una riga al registro. info: all'avvio motivo del riavvio * 100 + motivo
// del risveglio; allo spegnimento il motivo (0 power, 1 inattività, 2 batteria scarica).
bool log(Event ev, int info, const Reading& r, const Counters& c);
// Manda il registro riga per riga, ognuna con il prefisso "[b] ".
void dump(Print& out);
bool erase();

}  // namespace battery
