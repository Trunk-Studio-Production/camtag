#pragma once
// Fuel gauge dell'X3 (BQ27220) e registro dei consumi sulla microSD.
// Formato del registro e modo di leggerlo: docs/batteria.md.
#include <Print.h>

#include <cstdint>

namespace battery {

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
};

enum Event : char { kBoot = 'A', kSample = 'P', kOff = 'S' };

void begin();
bool read(Reading& out);
// Solo la corrente: + in carica. false se non c'è il gauge.
bool current(int16_t& ma);

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
