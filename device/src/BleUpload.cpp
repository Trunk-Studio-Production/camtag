#include "BleUpload.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <SDCardManager.h>
#include <Update.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/stream_buffer.h>

#include "Battery.h"
#include "Lang.h"
#include "UploadProto.h"
#include "Version.h"

// Con CAMTAG_DEBUG: collegamento negoziato e tempi dei trasferimenti sulla seriale.
#ifdef CAMTAG_DEBUG
#define BLOG(...) Serial.printf(__VA_ARGS__)
#else
#define BLOG(...) \
  do {            \
  } while (0)
#endif

namespace bleupload {
namespace {

constexpr const char* kServiceUuid = "c4a70001-1b2e-4f3a-9d6e-3a7c5e0b2f10";
constexpr const char* kCtrlUuid = "c4a70002-1b2e-4f3a-9d6e-3a7c5e0b2f10";
constexpr const char* kDataUuid = "c4a70003-1b2e-4f3a-9d6e-3a7c5e0b2f10";

constexpr const char* kCardDir = "/camtag";
constexpr const char* kTmpDir = "/camtag.new";
constexpr const char* kOldDir = "/camtag.old";

// Il client tiene in volo al massimo 8 KB non confermati (docs/bluetooth.md):
// il buffer è il doppio, così non si riempie mai mentre il display aggiorna.
// Allocato solo in ricezione.
constexpr size_t kDataBufBytes = 16384;
constexpr uint32_t kAckEvery = 2048;
// Verso il telefono (GET) vale la stessa finestra, con i ruoli scambiati.
constexpr uint32_t kSendWindow = 8192;
constexpr int kSendBurst = 16;  // pacchetti al massimo per giro di loop
constexpr size_t kCmdLen = 160;
// FIRMWARE: tempo per confermare sull'X3, poi si rifiuta da solo.
constexpr uint32_t kFwAskMs = 60000;

QueueHandle_t cmdQueue = nullptr;
StreamBufferHandle_t dataBuf = nullptr;
NimBLECharacteristic* ctrlChr = nullptr;
NimBLECharacteristic* dataChr = nullptr;
volatile uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;

volatile bool isConnected = false;
volatile bool connChanged = false;
volatile bool overflow = false;
volatile uint16_t mtu = 23;
// Chrome manda un pacchetto per intervallo del collegamento, quindi la velocità
// dipende da questo. Le regole Apple accettano 15 ms esatti (minimo = massimo).
// Appena collegato il Mac regola i parametri per conto suo (una richiesta in quel
// momento si scontra con la sua e va persa); a metà dei dati invece blocca le
// scritture di Chrome. Quindi si chiede subito dopo lo scambio dell'MTU, l'ultima
// regolazione del Mac e prima di qualsiasi comando; se l'MTU non cambia, dopo
// kParamsDelayMs dal loop.
constexpr uint32_t kParamsDelayMs = 2000;
volatile uint32_t paramsDueMs = 0;

void requestFastInterval(uint16_t handle) {
  paramsDueMs = 0;
  NimBLEDevice::getServer()->updateConnParams(handle, 12, 12, 0, 400);
  BLOG("[ble] chiesto intervallo 15 ms\n");
}
bool running = false;

// FwAsk: FIRMWARE arrivato, si aspetta la conferma sui tasti; FwReceiving: i dati
// vanno nella partizione OTA libera.
enum class Phase { Idle, Session, Receiving, Sending, FwAsk, FwReceiving };
Phase phase = Phase::Idle;
FsFile file;
uint32_t fileSize = 0;
uint32_t fileCrc = 0;
uint32_t expectedCrc = 0;
uint32_t received = 0;
uint32_t lastAck = 0;
uint16_t files = 0;
uint16_t kept = 0;  // file della sessione copiati dalle card attuali (KEEP)
uint32_t sent = 0;   // GET: byte inviati
uint32_t acked = 0;  // GET: byte confermati dal client
uint32_t notifyFails = 0;  // GET: pacchetti che NimBLE non ha preso (solo per il registro)
uint32_t sessionStartMs = 0;  // BEGIN
uint32_t sessionBytes = 0;
uint32_t fileStartMs = 0;
uint32_t sdUs = 0;  // tempo passato a scrivere sulla microSD nella sessione
uint8_t pending[512];
size_t pendingLen = 0;  // pacchetto letto ma non ancora accettato da NimBLE
char error[32] = "";
char fwVersion[upload::kMaxFwVersion + 1] = "";
uint32_t fwAskMs = 0;
uint8_t fwHead[upload::kImageHeaderBytes];  // i primi byte, per controllare l'immagine

void reply(const char* msg) {
  const bool ok = ctrlChr && isConnected && ctrlChr->notify(reinterpret_cast<const uint8_t*>(msg), strlen(msg));
  if (!ok || strncmp(msg, "ACK", 3) != 0) BLOG("[ble] -> %s%s\n", msg, ok ? "" : "  (NON INVIATA)");
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    isConnected = true;
    connChanged = true;
    connHandle = info.getConnHandle();
    BLOG("[ble] collegato: intervallo %.2f ms, latenza %u, MTU %u\n", info.getConnInterval() * 1.25f,
         info.getConnLatency(), info.getMTU());
    paramsDueMs = millis() + kParamsDelayMs;
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
    BLOG("[ble] scollegato, motivo 0x%x\n", reason);
    isConnected = false;
    connChanged = true;
    connHandle = BLE_HS_CONN_HANDLE_NONE;
    NimBLEDevice::startAdvertising();
  }
  void onMTUChange(uint16_t value, NimBLEConnInfo& info) override {
    mtu = value;
    BLOG("[ble] MTU %u\n", value);
    if (paramsDueMs) requestFastInterval(info.getConnHandle());
  }
  void onConnParamsUpdate(NimBLEConnInfo& info) override {
    BLOG("[ble] nuovi parametri: intervallo %.2f ms, latenza %u\n", info.getConnInterval() * 1.25f,
         info.getConnLatency());
  }
  void onPhyUpdate(NimBLEConnInfo&, uint8_t tx, uint8_t rx) override { BLOG("[ble] PHY tx %u rx %u\n", tx, rx); }
};

class CtrlCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo&) override {
    const NimBLEAttValue v = chr->getValue();
    char line[kCmdLen] = {0};
    size_t n = std::min<size_t>(v.size(), kCmdLen - 1);
    memcpy(line, v.data(), n);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
    if (xQueueSend(cmdQueue, line, 0) != pdTRUE) overflow = true;
  }
};

class DataCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo&) override {
    const NimBLEAttValue v = chr->getValue();
    if (xStreamBufferSend(dataBuf, v.data(), v.size(), 0) != v.size()) overflow = true;
  }
};

ServerCallbacks serverCallbacks;
CtrlCallbacks ctrlCallbacks;
DataCallbacks dataCallbacks;

void setError(const char* msg) { strncpy(error, msg, sizeof(error) - 1); }

void cleanup() {
  if (file) file.close();
  // Firmware a metà: la partizione OTA si scarta, si riavvia quello di prima.
  if (Update.isRunning()) Update.abort();
  if (SdMan.exists(kTmpDir)) SdMan.removeDir(kTmpDir);
  phase = Phase::Idle;
  received = fileSize = 0;
  files = kept = 0;
  sent = acked = 0;
  pendingLen = 0;
  xStreamBufferReset(dataBuf);
}

Event fail(const char* reason, const char* screenText) {
  char msg[48];
  snprintf(msg, sizeof(msg), "ERR %s", reason);
  reply(msg);
  setError(screenText);
  cleanup();
  return Event::Failed;
}

// Sostituisce /camtag con la cartella ricevuta; se qualcosa va storto rimette
// a posto la vecchia.
bool swapIn() {
  if (SdMan.exists(kOldDir) && !SdMan.removeDir(kOldDir)) return false;
  const bool hadOld = SdMan.exists(kCardDir);
  if (hadOld && !SdMan.rename(kCardDir, kOldDir)) return false;
  if (!SdMan.rename(kTmpDir, kCardDir)) {
    if (hadOld) SdMan.rename(kOldDir, kCardDir);
    return false;
  }
  if (hadOld) SdMan.removeDir(kOldDir);
  return true;
}

// Apre in scrittura un file di /camtag.new, creando la sua cartella.
FsFile openNew(const char* rel) {
  const String path = String(kTmpDir) + "/" + rel;
  const int slash = path.lastIndexOf('/');
  if (slash > static_cast<int>(strlen(kTmpDir))) SdMan.mkdir(path.substring(0, slash).c_str());
  return SdMan.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
}

enum class KeepResult { Copied, Different, Failed };

// KEEP: copia /camtag/<percorso> in /camtag.new se ha la dimensione e il CRC
// dati. Il CRC si controlla mentre si copia; se non torna la copia si cancella.
// Le card attuali non si toccano: restano com'erano fino al COMMIT.
KeepResult keepFile(const upload::Command& c) {
  FsFile src = SdMan.open((String(kCardDir) + "/" + c.path).c_str(), O_RDONLY);
  if (!src || src.isDirectory() || src.size() != c.size) {
    if (src) src.close();
    return KeepResult::Different;
  }
  FsFile dst = openNew(c.path);
  if (!dst) {
    src.close();
    return KeepResult::Failed;
  }
  uint8_t buf[512];
  uint32_t crc = 0, copied = 0;
  bool ok = true;
  int n;
  while ((n = src.read(buf, sizeof(buf))) > 0) {
    if (dst.write(buf, n) != static_cast<size_t>(n)) {
      ok = false;
      break;
    }
    crc = upload::crc32(crc, buf, n);
    copied += n;
  }
  src.close();
  dst.close();
  if (!ok) return KeepResult::Failed;
  if (n < 0 || copied != c.size || crc != c.crc) {
    SdMan.remove((String(kTmpDir) + "/" + c.path).c_str());
    return KeepResult::Different;
  }
  return KeepResult::Copied;
}

Event finishFile() {
  const uint32_t t = micros();
  file.close();
  sdUs += micros() - t;
  if (fileCrc != expectedCrc) return fail("CRC", lang::tr(lang::ErrCrc));
  files++;
  sessionBytes += received;
  BLOG("[ble] file %lu B in %lu ms\n", static_cast<unsigned long>(received),
       static_cast<unsigned long>(millis() - fileStartMs));
  char msg[24];
  snprintf(msg, sizeof(msg), "DONE %lu", static_cast<unsigned long>(received));
  reply(msg);
  phase = Phase::Session;
  return Event::Progress;
}

// Ultimo byte del firmware: CRC, poi Update controlla l'immagine e la rende la
// partizione di avvio. Il rollback del bootloader la tiene in prova finché il
// nuovo firmware non si dichiara sano (main.cpp, markFirmwareValid).
Event finishFirmware() {
  if (fileCrc != expectedCrc) return fail("CRC", lang::tr(lang::ErrCrc));
  if (!Update.end()) {
    BLOG("[ble] firmware: %s\n", Update.errorString());
    return fail("FLASH", lang::tr(lang::ErrFwFlash));
  }
  BLOG("[ble] firmware %s: %lu B in %lu ms\n", fwVersion, static_cast<unsigned long>(received),
       static_cast<unsigned long>(millis() - fileStartMs));
  char msg[24];
  snprintf(msg, sizeof(msg), "DONE %lu", static_cast<unsigned long>(received));
  reply(msg);
  phase = Phase::Idle;
  return Event::FirmwareDone;
}

Event handle(const char* line) {
  if (strncmp(line, "ACK", 3) != 0) BLOG("[ble] <- %s\n", line);
  const upload::Command c = upload::parse(line);
  char msg[48];
  switch (c.cmd) {
    case upload::Cmd::Hello:
      snprintf(msg, sizeof(msg), "OK CAMTAG %d MTU %u FW %s", upload::kProtocolVersion, static_cast<unsigned>(mtu),
               CAMTAG_FW_VERSION);
      reply(msg);
      return Event::None;

    case upload::Cmd::Begin:
      cleanup();
      if (!SdMan.mkdir(kTmpDir)) return fail("SD", lang::tr(lang::ErrSdWrite));
      phase = Phase::Session;
      sessionStartMs = millis();
      sessionBytes = 0;
      sdUs = 0;
      reply("OK");
      return Event::Progress;

    case upload::Cmd::File: {
      if (phase != Phase::Session) return fail("STATO", lang::tr(lang::ErrProtocol));
      const uint32_t t = micros();
      file = openNew(c.path);
      sdUs += micros() - t;
      if (!file) return fail("SD", lang::tr(lang::ErrSdWrite));
      fileSize = c.size;
      expectedCrc = c.crc;
      fileCrc = 0;
      received = lastAck = 0;
      fileStartMs = millis();
      phase = Phase::Receiving;
      if (fileSize == 0) return finishFile();
      reply("OK");
      return Event::None;
    }

    // Il file è già sull'X3, uguale: lo si copia dalle card attuali invece di
    // riceverlo. "NO" se manca o è diverso, e allora il client lo manda con FILE.
    case upload::Cmd::Keep: {
      if (phase != Phase::Session) return fail("STATO", lang::tr(lang::ErrProtocol));
      const uint32_t t = micros();
      const KeepResult r = keepFile(c);
      sdUs += micros() - t;
      if (r == KeepResult::Failed) return fail("SD", lang::tr(lang::ErrSdWrite));
      if (r == KeepResult::Different) {
        reply("NO");
        return Event::None;
      }
      files++;
      kept++;
      snprintf(msg, sizeof(msg), "DONE %lu", static_cast<unsigned long>(c.size));
      reply(msg);
      return Event::Progress;
    }

    case upload::Cmd::Commit:
      if (phase != Phase::Session || files == 0) return fail("STATO", lang::tr(lang::ErrProtocol));
      {
        const uint32_t ms = millis() - sessionStartMs;
        BLOG("[ble] totale: %u file (%u uguali, copiati sull'X3), %lu B in %lu ms = %.1f KB/s; microSD %lu ms\n",
             files, kept, static_cast<unsigned long>(sessionBytes), static_cast<unsigned long>(ms),
             ms ? sessionBytes / 1.024f / ms : 0.0f, static_cast<unsigned long>(sdUs / 1000));
      }
      if (!swapIn()) return fail("SD", lang::tr(lang::ErrSdWrite));
      phase = Phase::Idle;
      reply("OK");
      return Event::Done;

    // Manda al telefono un file di /camtag (es. project.json), o il registro dei
    // consumi con il suo nome riservato. Fuori da una sessione di caricamento: le
    // card non cambiano.
    case upload::Cmd::Get: {
      if (phase != Phase::Idle) return fail("STATO", lang::tr(lang::ErrProtocol));
      const String path = strcmp(c.path, upload::kBatteryLogName) == 0 ? String(battery::kLogPath)
                                                                       : String(kCardDir) + "/" + c.path;
      file = SdMan.open(path.c_str(), O_RDONLY);
      if (!file || file.isDirectory() || file.size() > upload::kMaxFileBytes) {
        if (file) file.close();
        reply("ERR ASSENTE");
        return Event::None;
      }
      // Prima il CRC, poi da capo per l'invio.
      fileSize = static_cast<uint32_t>(file.size());
      fileCrc = 0;
      uint8_t buf[512];
      int n;
      while ((n = file.read(buf, sizeof(buf))) > 0) fileCrc = upload::crc32(fileCrc, buf, n);
      if (n < 0 || !file.seekSet(0)) return fail("SD", lang::tr(lang::ErrSdRead));
      char head[kCmdLen];
      snprintf(head, sizeof(head), "FILE %lu %lx %s", static_cast<unsigned long>(fileSize),
               static_cast<unsigned long>(fileCrc), c.path);
      sent = acked = 0;
      notifyFails = 0;
      pendingLen = 0;
      phase = Phase::Sending;
      reply(head);
      if (fileSize == 0) {
        file.close();
        phase = Phase::Idle;
        return Event::Sent;
      }
      return Event::Sending;
    }

    case upload::Cmd::Ack: {
      if (phase != Phase::Sending) return Event::None;  // conferma in ritardo: si ignora
      const uint32_t before = acked;
      if (c.size > acked) acked = std::min(c.size, sent);
      if (acked / 16384 != before / 16384 || acked == fileSize)
        BLOG("[ble] GET: confermati %lu, inviati %lu, pool libero %d, pacchetti rifiutati %lu\n",
             static_cast<unsigned long>(acked), static_cast<unsigned long>(sent), os_msys_num_free(),
             static_cast<unsigned long>(notifyFails));
      if (acked == fileSize) {
        file.close();
        phase = Phase::Idle;
        return Event::Sent;
      }
      return Event::None;
    }

    // Nuovo firmware: prima la conferma sull'X3 (main.cpp chiama answerFirmware).
    // "ASK" dice al client di aspettare la persona, poi "OK" o "ERR RIFIUTATO".
    case upload::Cmd::Firmware:
      if (phase != Phase::Idle) return fail("STATO", lang::tr(lang::ErrProtocol));
      fileSize = c.size;
      expectedCrc = c.crc;
      strncpy(fwVersion, c.path, sizeof(fwVersion) - 1);
      fwVersion[sizeof(fwVersion) - 1] = '\0';
      fwAskMs = millis();
      phase = Phase::FwAsk;
      reply("ASK");
      return Event::FirmwareAsk;

    case upload::Cmd::Abort:
      cleanup();
      reply("OK");
      return Event::Aborted;

    case upload::Cmd::Unknown:
      break;
  }
  return fail("COMANDO", lang::tr(lang::ErrProtocol));
}

// GET: manda pacchetti finché la finestra lo permette. Se NimBLE non ha posto,
// il pacchetto resta in `pending` e riparte al giro dopo.
Event pumpSend() {
  if (phase != Phase::Sending || !isConnected) return Event::None;
  const size_t chunk = std::max<size_t>(20, std::min<size_t>(sizeof(pending), mtu - 3));
  for (int i = 0; i < kSendBurst && sent < fileSize && sent - acked < kSendWindow; i++) {
    if (!pendingLen) {
      const size_t want = std::min<size_t>(chunk, fileSize - sent);
      if (file.read(pending, want) != static_cast<int>(want)) return fail("SD", lang::tr(lang::ErrSdRead));
      pendingLen = want;
    }
    if (!dataChr->notify(pending, pendingLen, connHandle)) {
      notifyFails++;
      break;
    }
    sent += pendingLen;
    pendingLen = 0;
  }
  return Event::None;
}

Event drainData() {
  uint8_t buf[512];
  Event ev = Event::None;
  size_t n;
  while ((n = xStreamBufferReceive(dataBuf, buf, sizeof(buf), 0)) > 0) {
    const bool fw = phase == Phase::FwReceiving;
    if ((phase != Phase::Receiving && !fw) || received + n > fileSize) return fail("DATI", lang::tr(lang::ErrProtocol));
    if (fw) {
      // Le prime righe dell'immagine: deve essere un firmware per ESP32-C3. Update
      // tiene i dati in un buffer da 4 KB, quindi la flash non è ancora toccata.
      if (received < sizeof(fwHead)) {
        const size_t k = std::min<size_t>(n, sizeof(fwHead) - received);
        memcpy(fwHead + received, buf, k);
        if (received + k == sizeof(fwHead) && !upload::firmwareHeaderOk(fwHead, sizeof(fwHead)))
          return fail("IMMAGINE", lang::tr(lang::ErrFwImage));
      }
      if (Update.write(buf, n) != n) {
        BLOG("[ble] firmware: %s\n", Update.errorString());
        return fail("FLASH", lang::tr(lang::ErrFwFlash));
      }
    } else {
      const uint32_t t = micros();
      if (file.write(buf, n) != n) return fail("SD", lang::tr(lang::ErrSdFull));
      sdUs += micros() - t;
    }
    fileCrc = upload::crc32(fileCrc, buf, n);
    received += n;
    if (received == fileSize) {
      ev = fw ? finishFirmware() : finishFile();
      if (ev == Event::Failed) return ev;
    } else if (received - lastAck >= kAckEvery) {
      if (received / 8192 != lastAck / 8192) BLOG("[ble] ricevuti %lu B\n", static_cast<unsigned long>(received));
      lastAck = received;
      char msg[24];
      snprintf(msg, sizeof(msg), "ACK %lu", static_cast<unsigned long>(received));
      reply(msg);
    }
  }
  return ev;
}

// La RAM del Bluetooth torna libera fuori dalla ricezione.
void freeBuffers() {
  if (dataBuf) vStreamBufferDelete(dataBuf);
  if (cmdQueue) vQueueDelete(cmdQueue);
  dataBuf = nullptr;
  cmdQueue = nullptr;
}

}  // namespace

bool start(const char* deviceName) {
  if (running) return true;
  if (!cmdQueue) cmdQueue = xQueueCreate(8, kCmdLen);
  if (!dataBuf) dataBuf = xStreamBufferCreate(kDataBufBytes, 1);
  if (!cmdQueue || !dataBuf) {
    freeBuffers();
    return false;
  }
  xQueueReset(cmdQueue);
  xStreamBufferReset(dataBuf);
  overflow = false;
  isConnected = false;
  connChanged = false;
  mtu = 23;
  phase = Phase::Idle;
  error[0] = '\0';

  if (!NimBLEDevice::init(deviceName)) return false;
  NimBLEDevice::setMTU(517);
  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks, false);
  NimBLEService* service = server->createService(kServiceUuid);
  // WRITE_NR: gli ACK di un GET arrivano senza risposta (docs/bluetooth.md).
  ctrlChr = service->createCharacteristic(kCtrlUuid,
                                         NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
  ctrlChr->setCallbacks(&ctrlCallbacks);
  dataChr = service->createCharacteristic(kDataUuid,
                                         NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
  dataChr->setCallbacks(&dataCallbacks);
  // Il server GATT parte da solo con l'advertising.

  // Servizio nel pacchetto principale, nome nella risposta alla scansione: insieme
  // superano i 31 byte.
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advData.addServiceUUID(kServiceUuid);
  adv->setAdvertisementData(advData);
  NimBLEAdvertisementData scanData;
  scanData.setName(deviceName);
  adv->setScanResponseData(scanData);
  running = adv->start();
  if (!running) {
    NimBLEDevice::deinit(true);
    freeBuffers();
  }
  BLOG("[ble] RAM libera con il Bluetooth acceso: %lu B\n", static_cast<unsigned long>(ESP.getFreeHeap()));
  return running;
}

void stop() {
  if (!running) return;
  if (phase != Phase::Idle) cleanup();
  NimBLEDevice::deinit(true);
  ctrlChr = dataChr = nullptr;
  isConnected = false;
  running = false;
  freeBuffers();
}

Event poll() {
  if (!running) return Event::None;
  if (connChanged) {
    connChanged = false;
    if (!isConnected) {
      if (phase != Phase::Idle) cleanup();
      xQueueReset(cmdQueue);
      return Event::Disconnected;
    }
    return Event::Connected;
  }
  if (paramsDueMs && isConnected && static_cast<int32_t>(millis() - paramsDueMs) >= 0) requestFastInterval(connHandle);
  if (overflow) {
    overflow = false;
    return fail("OVERFLOW", lang::tr(lang::ErrTooFast));
  }
  if (phase == Phase::FwAsk && millis() - fwAskMs > kFwAskMs) return fail("RIFIUTATO", lang::tr(lang::FwRefused));
  char line[kCmdLen];
  Event ev = Event::None;
  // I dati del file in corso vanno scritti prima del comando successivo.
  ev = drainData();
  if (ev == Event::Failed || ev == Event::FirmwareDone) return ev;
  // Più comandi per giro: durante un GET arrivano gli ACK del client.
  for (int i = 0; i < 8 && xQueueReceive(cmdQueue, line, 0) == pdTRUE; i++) {
    const Event cmdEv = handle(line);
    if (cmdEv != Event::None) ev = cmdEv;
    if (cmdEv == Event::Failed || cmdEv == Event::Done) return ev;
    if (phase == Phase::Receiving) break;  // i dati del FILE prima del comando dopo
  }
  if (pumpSend() == Event::Failed) return Event::Failed;
  return ev;
}

Event answerFirmware(bool yes, const char* reason) {
  if (phase != Phase::FwAsk) return Event::None;
  if (!yes) {
    const bool battery = reason && strcmp(reason, "BATTERIA") == 0;
    return fail(reason ? reason : "RIFIUTATO", lang::tr(battery ? lang::ErrFwBattery : lang::FwRefused));
  }
  xStreamBufferReset(dataBuf);
  if (!Update.begin(fileSize, U_FLASH)) {
    BLOG("[ble] firmware: %s\n", Update.errorString());
    return fail("FLASH", lang::tr(lang::ErrFwFlash));
  }
  fileCrc = 0;
  received = lastAck = 0;
  fileStartMs = millis();
  phase = Phase::FwReceiving;
  reply("OK");
  return Event::Progress;
}

bool firmwareAsking() { return phase == Phase::FwAsk; }
bool firmwareReceiving() { return phase == Phase::FwReceiving; }
const char* firmwareVersion() { return fwVersion; }
uint8_t firmwarePercent() { return fileSize ? static_cast<uint8_t>(uint64_t(received) * 100 / fileSize) : 0; }

bool connected() { return isConnected; }
bool transferring() { return phase != Phase::Idle; }
uint16_t filesReceived() { return files; }
const char* lastError() { return error; }

}  // namespace bleupload
