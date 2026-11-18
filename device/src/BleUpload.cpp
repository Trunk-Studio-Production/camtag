#include "BleUpload.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <SDCardManager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/stream_buffer.h>

#include "UploadProto.h"

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
constexpr size_t kDataBufBytes = 16384;
constexpr uint32_t kAckEvery = 2048;
// Verso il telefono (GET) vale la stessa finestra, con i ruoli scambiati.
constexpr uint32_t kSendWindow = 8192;
constexpr int kSendBurst = 16;  // pacchetti al massimo per giro di loop
constexpr size_t kCmdLen = 160;

QueueHandle_t cmdQueue = nullptr;
StreamBufferHandle_t dataBuf = nullptr;
NimBLECharacteristic* ctrlChr = nullptr;
NimBLECharacteristic* dataChr = nullptr;
volatile uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;

volatile bool isConnected = false;
volatile bool connChanged = false;
volatile bool overflow = false;
volatile uint16_t mtu = 23;
bool running = false;

enum class Phase { Idle, Session, Receiving, Sending };
Phase phase = Phase::Idle;
FsFile file;
uint32_t fileSize = 0;
uint32_t fileCrc = 0;
uint32_t expectedCrc = 0;
uint32_t received = 0;
uint32_t lastAck = 0;
uint16_t files = 0;
uint32_t sent = 0;   // GET: byte inviati
uint32_t acked = 0;  // GET: byte confermati dal client
uint8_t pending[512];
size_t pendingLen = 0;  // pacchetto letto ma non ancora accettato da NimBLE
char error[32] = "";

void reply(const char* msg) {
  if (ctrlChr && isConnected) ctrlChr->notify(reinterpret_cast<const uint8_t*>(msg), strlen(msg));
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    isConnected = true;
    connChanged = true;
    connHandle = info.getConnHandle();
    // 15-30 ms: veloce ma dentro i limiti che iOS accetta.
    server->updateConnParams(info.getConnHandle(), 12, 24, 0, 400);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    isConnected = false;
    connChanged = true;
    connHandle = BLE_HS_CONN_HANDLE_NONE;
    NimBLEDevice::startAdvertising();
  }
  void onMTUChange(uint16_t value, NimBLEConnInfo&) override { mtu = value; }
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
  if (SdMan.exists(kTmpDir)) SdMan.removeDir(kTmpDir);
  phase = Phase::Idle;
  received = fileSize = 0;
  files = 0;
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

Event finishFile() {
  file.close();
  if (fileCrc != expectedCrc) return fail("CRC", "DATI ROVINATI");
  files++;
  char msg[24];
  snprintf(msg, sizeof(msg), "DONE %lu", static_cast<unsigned long>(received));
  reply(msg);
  phase = Phase::Session;
  return Event::Progress;
}

Event handle(const char* line) {
  const upload::Command c = upload::parse(line);
  char msg[48];
  switch (c.cmd) {
    case upload::Cmd::Hello:
      snprintf(msg, sizeof(msg), "OK CAMTAG %d MTU %u", upload::kProtocolVersion, static_cast<unsigned>(mtu));
      reply(msg);
      return Event::None;

    case upload::Cmd::Begin:
      cleanup();
      if (!SdMan.mkdir(kTmpDir)) return fail("SD", "MICROSD NON SCRIVIBILE");
      phase = Phase::Session;
      reply("OK");
      return Event::Progress;

    case upload::Cmd::File: {
      if (phase != Phase::Session) return fail("STATO", "ERRORE DI PROTOCOLLO");
      String path = String(kTmpDir) + "/" + c.path;
      const int slash = path.lastIndexOf('/');
      if (slash > static_cast<int>(strlen(kTmpDir))) SdMan.mkdir(path.substring(0, slash).c_str());
      file = SdMan.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
      if (!file) return fail("SD", "MICROSD NON SCRIVIBILE");
      fileSize = c.size;
      expectedCrc = c.crc;
      fileCrc = 0;
      received = lastAck = 0;
      phase = Phase::Receiving;
      if (fileSize == 0) return finishFile();
      reply("OK");
      return Event::None;
    }

    case upload::Cmd::Commit:
      if (phase != Phase::Session || files == 0) return fail("STATO", "ERRORE DI PROTOCOLLO");
      if (!swapIn()) return fail("SD", "MICROSD NON SCRIVIBILE");
      phase = Phase::Idle;
      reply("OK");
      return Event::Done;

    // Manda al telefono un file di /camtag (es. project.json). Fuori da una
    // sessione di caricamento: le card non cambiano.
    case upload::Cmd::Get: {
      if (phase != Phase::Idle) return fail("STATO", "ERRORE DI PROTOCOLLO");
      const String path = String(kCardDir) + "/" + c.path;
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
      if (n < 0 || !file.seekSet(0)) return fail("SD", "MICROSD ILLEGGIBILE");
      char head[kCmdLen];
      snprintf(head, sizeof(head), "FILE %lu %lx %s", static_cast<unsigned long>(fileSize),
               static_cast<unsigned long>(fileCrc), c.path);
      sent = acked = 0;
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

    case upload::Cmd::Ack:
      if (phase != Phase::Sending) return Event::None;  // conferma in ritardo: si ignora
      if (c.size > acked) acked = std::min(c.size, sent);
      if (acked == fileSize) {
        file.close();
        phase = Phase::Idle;
        return Event::Sent;
      }
      return Event::None;

    case upload::Cmd::Abort:
      cleanup();
      reply("OK");
      return Event::Aborted;

    case upload::Cmd::Unknown:
      break;
  }
  return fail("COMANDO", "ERRORE DI PROTOCOLLO");
}

// GET: manda pacchetti finché la finestra lo permette. Se NimBLE non ha posto,
// il pacchetto resta in `pending` e riparte al giro dopo.
Event pumpSend() {
  if (phase != Phase::Sending || !isConnected) return Event::None;
  const size_t chunk = std::max<size_t>(20, std::min<size_t>(sizeof(pending), mtu - 3));
  for (int i = 0; i < kSendBurst && sent < fileSize && sent - acked < kSendWindow; i++) {
    if (!pendingLen) {
      const size_t want = std::min<size_t>(chunk, fileSize - sent);
      if (file.read(pending, want) != static_cast<int>(want)) return fail("SD", "MICROSD ILLEGGIBILE");
      pendingLen = want;
    }
    if (!dataChr->notify(pending, pendingLen, connHandle)) break;
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
    if (phase != Phase::Receiving || received + n > fileSize) return fail("DATI", "ERRORE DI PROTOCOLLO");
    if (file.write(buf, n) != n) return fail("SD", "MICROSD PIENA O GUASTA");
    fileCrc = upload::crc32(fileCrc, buf, n);
    received += n;
    if (received == fileSize) {
      ev = finishFile();
      if (ev == Event::Failed) return ev;
    } else if (received - lastAck >= kAckEvery) {
      lastAck = received;
      char msg[24];
      snprintf(msg, sizeof(msg), "ACK %lu", static_cast<unsigned long>(received));
      reply(msg);
    }
  }
  return ev;
}

}  // namespace

bool start(const char* deviceName) {
  if (running) return true;
  if (!cmdQueue) cmdQueue = xQueueCreate(8, kCmdLen);
  if (!dataBuf) dataBuf = xStreamBufferCreate(kDataBufBytes, 1);
  if (!cmdQueue || !dataBuf) return false;
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
  ctrlChr = service->createCharacteristic(kCtrlUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
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
  if (!running) NimBLEDevice::deinit(true);
  return running;
}

void stop() {
  if (!running) return;
  if (phase != Phase::Idle) cleanup();
  NimBLEDevice::deinit(true);
  ctrlChr = dataChr = nullptr;
  isConnected = false;
  running = false;
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
  if (overflow) {
    overflow = false;
    return fail("OVERFLOW", "TRASFERIMENTO TROPPO VELOCE");
  }
  char line[kCmdLen];
  Event ev = Event::None;
  // I dati del file in corso vanno scritti prima del comando successivo.
  ev = drainData();
  if (ev == Event::Failed) return ev;
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

bool connected() { return isConnected; }
bool transferring() { return phase != Phase::Idle; }
uint16_t filesReceived() { return files; }
const char* lastError() { return error; }

}  // namespace bleupload
