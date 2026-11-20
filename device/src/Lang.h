#pragma once
// Lingua delle scritte del firmware (messaggi, schermata Bluetooth, spento, errori).
// Si sceglie al primo avvio sull'X3 o con lang= in settings.ini; resta in NVS
// ("lang"). Le card hanno la lingua scelta nello strumento web: i due possono
// essere diversi. Solo i caratteri di TinyFont (maiuscole, cifre, ./:-%!_+).
// Le stesse frasi sono nel simulatore dello strumento web (`DEV_TEXT`).
#include <cstdint>

namespace lang {

enum Lang : uint8_t { En = 0, It = 1, kCount };

enum Str : uint8_t {
  NoSd,
  NoCards,
  HoldX,
  BadBmp,
  RotLocked,
  RotAuto,
  ExitHint,
  Credits,
  BleUnavailable,
  Waiting,
  OpenPhone,
  CardsUpdated,
  Connected,
  LinkClosed,
  ReceivedN,  // formato printf con %u
  Error,
  SendingProject,
  ProjectSent,
  CardsUnchanged,
  Cancelled,
  OffEmpty,
  OffHold,
  ErrCrc,
  ErrSdWrite,
  ErrProtocol,
  ErrSdRead,
  ErrSdFull,
  ErrTooFast,
  kStrCount
};

// [frase][lingua]: inglese, italiano. Gli errori stanno in 31 caratteri.
constexpr const char* kText[kStrCount][kCount] = {
    {"NO MICROSD", "NESSUNA MICROSD"},
    {"NO CARDS", "NESSUNA CARD"},
    {"HOLD X TO RECEIVE THEM", "TIENI X PER RICEVERLE"},
    {"INVALID BMP", "BMP NON VALIDO"},
    {"ROTATION LOCKED", "ROTAZIONE BLOCCATA"},
    {"AUTO ROTATION", "ROTAZIONE AUTOMATICA"},
    {"X OR Y TO EXIT", "X O Y PER USCIRE"},
    {"DEVELOPED BY IACOPO BOCCALARI", "SVILUPPATO DA IACOPO BOCCALARI"},
    {"BLUETOOTH UNAVAILABLE", "BLUETOOTH NON DISPONIBILE"},
    {"WAITING", "IN ATTESA"},
    {"OPEN CAMTAG ON YOUR PHONE", "APRI CAMTAG SUL TELEFONO"},
    {"CARDS UPDATED", "CARD AGGIORNATE"},
    {"CONNECTED", "CONNESSO"},
    {"CONNECTION CLOSED", "COLLEGAMENTO CHIUSO"},
    {"RECEIVED %u FILES", "RICEVUTI %u FILE"},
    {"ERROR", "ERRORE"},
    {"SENDING THE PROJECT", "INVIO DEL PROGETTO"},
    {"PROJECT SENT", "PROGETTO INVIATO"},
    {"CARDS UNCHANGED", "CARD INVARIATE"},
    {"CANCELLED", "ANNULLATO"},
    {"BATTERY EMPTY - PLEASE CHARGE", "BATTERIA SCARICA - METTI IN CARICA"},
    {"OFF - HOLD POWER TO TURN ON", "SPENTO - TIENI POWER PER ACCENDERE"},
    {"CORRUPTED DATA", "DATI ROVINATI"},
    {"MICROSD NOT WRITABLE", "MICROSD NON SCRIVIBILE"},
    {"PROTOCOL ERROR", "ERRORE DI PROTOCOLLO"},
    {"MICROSD UNREADABLE", "MICROSD ILLEGGIBILE"},
    {"MICROSD FULL OR FAULTY", "MICROSD PIENA O GUASTA"},
    {"TRANSFER TOO FAST", "TRASFERIMENTO TROPPO VELOCE"},
};

// Lingua in uso: inglese finché non se ne sceglie un'altra.
inline Lang current = En;

inline const char* tr(Str s) { return kText[s][current]; }

// "en" / "it" (settings.ini); kCount se non è nessuna delle due.
inline Lang parse(const char* s) {
  if ((s[0] == 'e' || s[0] == 'E') && (s[1] == 'n' || s[1] == 'N') && !s[2]) return En;
  if ((s[0] == 'i' || s[0] == 'I') && (s[1] == 't' || s[1] == 'T') && !s[2]) return It;
  return kCount;
}

}  // namespace lang
