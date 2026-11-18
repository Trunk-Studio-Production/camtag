#pragma once
// Ricezione delle card via Bluetooth LE dallo strumento web (Web Bluetooth), e
// invio al telefono del file di progetto salvato con le card (GET).
// Il Bluetooth è acceso solo in modalità ricezione. I callback di NimBLE girano
// nel suo task e si limitano a mettere comandi e dati in coda; la microSD si
// scrive solo da poll(), nel loop, perché condivide l'SPI con il display.
// Protocollo: docs/bluetooth.md.
#include <cstdint>

namespace bleupload {

enum class Event { None, Connected, Disconnected, Progress, Done, Failed, Aborted, Sending, Sent };

bool start(const char* deviceName);
void stop();  // annulla un trasferimento in corso e spegne il Bluetooth
Event poll();

bool connected();
bool transferring();       // tra BEGIN e COMMIT, o durante un GET
uint16_t filesReceived();  // file completati nel trasferimento in corso
const char* lastError();   // dopo Event::Failed, in maiuscolo per lo schermo

}  // namespace bleupload
