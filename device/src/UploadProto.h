#pragma once
// Protocollo di caricamento delle card via Bluetooth LE: parti senza hardware
// (CRC, controllo dei percorsi, lettura dei comandi), così si provano sull'host.
// La specifica completa è in docs/bluetooth.md.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace upload {

constexpr int kProtocolVersion = 4;
constexpr size_t kMaxPath = 96;
constexpr uint32_t kMaxFileBytes = 2u * 1024u * 1024u;
// Nome riservato per GET: il registro dei consumi (/camtag-batteria.csv nella radice
// della microSD, docs/batteria.md) invece di un file di /camtag.
constexpr const char* kBatteryLogName = "camtag-batteria.csv";

// Aggiornamento del firmware (FIRMWARE, dal protocollo 4): al massimo quanto la
// partizione OTA (app0/app1 in device/partitions.csv), versione breve tipo "0.4.0".
constexpr uint32_t kMaxFirmwareBytes = 0x640000;
constexpr size_t kMaxFwVersion = 15;
// Intestazione di un'immagine ESP32 (esp_image_header_t): magic nel primo byte,
// chip_id (16 bit little endian) al byte 12. ESP32-C3 = 5.
constexpr uint8_t kImageMagic = 0xE9;
constexpr uint16_t kChipEsp32C3 = 5;
constexpr size_t kImageHeaderBytes = 24;

// CRC-32 standard (zlib, polinomio 0xEDB88320). Si concatena: crc32(crc32(0, a), b).
inline uint32_t crc32(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

// Percorso relativo dentro la cartella delle card: segmenti di [A-Za-z0-9._-]
// separati da "/", niente segmenti vuoti, "." o "..", niente "/" iniziale.
inline bool validPath(const char* p) {
  const size_t n = strlen(p);
  if (n == 0 || n > kMaxPath || p[0] == '/' || p[n - 1] == '/') return false;
  size_t segStart = 0;
  for (size_t i = 0; i <= n; i++) {
    const char c = p[i];
    if (c == '/' || c == '\0') {
      const size_t len = i - segStart;
      if (len == 0) return false;
      if (p[segStart] == '.') return false;  // ".", "..", file nascosti
      segStart = i + 1;
      continue;
    }
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                    c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

// I primi byte di un firmware per l'X3: un'immagine ESP32-C3.
inline bool firmwareHeaderOk(const uint8_t* b, size_t n) {
  if (n < kImageHeaderBytes || b[0] != kImageMagic) return false;
  return static_cast<uint16_t>(b[12] | (b[13] << 8)) == kChipEsp32C3;
}

// Versione del firmware nel comando FIRMWARE: [0-9A-Za-z.-], da 1 a 15 caratteri.
inline bool validFwVersion(const char* v) {
  const size_t n = strlen(v);
  if (n == 0 || n > kMaxFwVersion) return false;
  for (size_t i = 0; i < n; i++) {
    const char c = v[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
    if (!ok) return false;
  }
  return true;
}

enum class Cmd { Unknown, Hello, Begin, File, Keep, Commit, Abort, Get, Ack, Firmware };

struct Command {
  Cmd cmd = Cmd::Unknown;
  uint32_t size = 0;  // FILE, KEEP, FIRMWARE: byte del file; ACK: byte ricevuti dal client
  uint32_t crc = 0;
  char path[kMaxPath + 1] = {0};  // FIRMWARE: la versione
};

// "<byte> <crc32 esadecimale> <percorso>", la coda di FILE e KEEP.
inline bool parseFileSpec(const char* s, Command& c) {
  char* end = nullptr;
  const unsigned long size = strtoul(s, &end, 10);
  if (end == s || *end != ' ') return false;
  s = end + 1;
  const unsigned long crc = strtoul(s, &end, 16);
  if (end == s || *end != ' ') return false;
  s = end + 1;
  if (size > kMaxFileBytes || !validPath(s)) return false;
  c.size = static_cast<uint32_t>(size);
  c.crc = static_cast<uint32_t>(crc);
  strncpy(c.path, s, kMaxPath);
  return true;
}

// "HELLO" | "BEGIN" | "FILE <byte> <crc32 esadecimale> <percorso>" | "KEEP <come FILE>"
// | "COMMIT" | "ABORT" | "GET <percorso>" | "ACK <byte>" | "FIRMWARE <byte> <crc32 esadecimale> <versione>"
inline Command parse(const char* line) {
  Command c;
  if (strcmp(line, "HELLO") == 0) c.cmd = Cmd::Hello;
  else if (strcmp(line, "BEGIN") == 0) c.cmd = Cmd::Begin;
  else if (strcmp(line, "COMMIT") == 0) c.cmd = Cmd::Commit;
  else if (strcmp(line, "ABORT") == 0) c.cmd = Cmd::Abort;
  else if (strncmp(line, "GET ", 4) == 0) {
    if (!validPath(line + 4)) return c;
    c.cmd = Cmd::Get;
    strncpy(c.path, line + 4, kMaxPath);
  } else if (strncmp(line, "ACK ", 4) == 0) {
    char* end = nullptr;
    const unsigned long n = strtoul(line + 4, &end, 10);
    if (end == line + 4 || *end != '\0' || n > kMaxFileBytes) return c;
    c.cmd = Cmd::Ack;
    c.size = static_cast<uint32_t>(n);
  } else if (strncmp(line, "FILE ", 5) == 0) {
    if (parseFileSpec(line + 5, c)) c.cmd = Cmd::File;
  } else if (strncmp(line, "KEEP ", 5) == 0) {
    if (parseFileSpec(line + 5, c)) c.cmd = Cmd::Keep;
  } else if (strncmp(line, "FIRMWARE ", 9) == 0) {
    const char* s = line + 9;
    char* end = nullptr;
    const unsigned long size = strtoul(s, &end, 10);
    if (end == s || *end != ' ' || size < kImageHeaderBytes || size > kMaxFirmwareBytes) return c;
    s = end + 1;
    const unsigned long crc = strtoul(s, &end, 16);
    if (end == s || *end != ' ' || !validFwVersion(end + 1)) return c;
    c.cmd = Cmd::Firmware;
    c.size = static_cast<uint32_t>(size);
    c.crc = static_cast<uint32_t>(crc);
    strncpy(c.path, end + 1, kMaxPath);
  }
  return c;
}

}  // namespace upload
