#include "Battery.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <SDCardManager.h>
#include <Wire.h>

#include <sys/time.h>

namespace battery {
namespace {

constexpr const char* kHeader = "t_s,evento,info,mV,mA,mAh,pieno_mAh,pct,sveglio_s,riposo_s,schermo,tasti,tocchi,spento_mA,tocco_mA\n";
// Mesi di righe: poi smette. Un po' sotto i 2 MB, il massimo di un file via Bluetooth.
constexpr uint32_t kMaxLogBytes = 2UL * 1024 * 1024 - 1024;

// Comandi standard del BQ27220 (16 bit, prima il byte basso).
constexpr uint8_t kVoltage = 0x08, kCurrent = 0x0C, kRemaining = 0x10, kFull = 0x12, kSoc = 0x2C, kDesign = 0x3C;

uint8_t gaugeAddr() {
  const auto& g = BoardConfig::ACTIVE.batteryGauge;
  return g.gaugeType == BoardConfig::GaugeType::Bq27220 ? g.gaugeAddr : 0;
}

bool reg16At(uint8_t addr, uint8_t reg, uint16_t& out) {
  if (!addr) return false;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, static_cast<uint8_t>(2), static_cast<uint8_t>(true)) < 2) return false;
  const uint8_t lo = Wire.read();
  const uint8_t hi = Wire.read();
  out = static_cast<uint16_t>(lo | (hi << 8));
  return true;
}

bool reg16(uint8_t reg, uint16_t& out) { return reg16At(gaugeAddr(), reg, out); }

}  // namespace

void begin() {
  const auto& g = BoardConfig::ACTIVE.batteryGauge;
  if (gaugeAddr()) Wire.begin(g.i2cSda, g.i2cScl, g.i2cHz);
}

bool read(Reading& out) {
  out = Reading();
  uint16_t mv, ma, mah, full, soc, design;
  if (!reg16(kVoltage, mv) || !reg16(kCurrent, ma) || !reg16(kRemaining, mah) || !reg16(kFull, full) ||
      !reg16(kSoc, soc) || !reg16(kDesign, design))
    return false;
  out.ok = true;
  out.mv = mv;
  out.ma = static_cast<int16_t>(ma);
  out.mah = mah;
  out.fullMah = full;
  out.designMah = design;
  out.pct = static_cast<uint8_t>(soc > 100 ? 100 : soc);
  return true;
}

bool current(int16_t& ma) {
  uint16_t raw;
  if (!reg16(kCurrent, raw)) return false;
  ma = static_cast<int16_t>(raw);
  return true;
}

// In cima a setup() il profilo attivo è ancora quello di partenza della build per X3 e
// X4, cioè l'X4, che non ha il gauge: qui si usano i pin e l'indirizzo dell'X3, come fa
// la sonda dell'SDK che li riconosce (stesso bus, stessa attesa breve). Su un X4 non
// risponde nessuno e la lettura manca.
bool wakeCurrent(int16_t& ma) {
#if FREEINK_DEVICE_X3
  const auto& g = BoardConfig::XTEINK_X3.batteryGauge;
  Wire.begin(g.i2cSda, g.i2cScl, g.i2cHz);
  Wire.setTimeOut(6);
  uint16_t raw = 0;
  bool ok = false;
  // Non è mai stato provato così presto dopo il risveglio: due riprove a 20 ms.
  for (int i = 0; i < 3 && !ok; i++) {
    if (i) delay(20);
    ok = reg16At(g.gaugeAddr, kCurrent, raw);
  }
  Wire.end();
  pinMode(g.i2cSda, INPUT);
  pinMode(g.i2cScl, INPUT);
  ma = static_cast<int16_t>(raw);
  return ok;
#else
  (void)ma;
  return false;
#endif
}

uint32_t nowS() {
  timeval tv;
  gettimeofday(&tv, nullptr);
  return static_cast<uint32_t>(tv.tv_sec);
}

bool log(Event ev, int info, const Reading& r, const Counters& c) {
  if (!SdMan.ready()) return false;
  FsFile f = SdMan.open(kLogPath, O_WRONLY | O_CREAT | O_APPEND);
  if (!f) return false;
  bool ok = f.fileSize() < kMaxLogBytes;
  if (ok && f.fileSize() == 0) ok = f.print(kHeader) > 0;
  if (ok) {
    char offMa[8] = "", touchMa[8] = "";
    if (c.offMaOk) snprintf(offMa, sizeof(offMa), "%d", c.offMa);
    if (c.touchMaOk) snprintf(touchMa, sizeof(touchMa), "%d", c.touchMa);
    char line[128];
    snprintf(line, sizeof(line), "%lu,%c,%d,%u,%d,%u,%u,%u,%lu,%lu,%lu,%lu,%lu,%s,%s\n",
             static_cast<unsigned long>(nowS()), static_cast<char>(ev), info, r.mv, r.ma, r.mah, r.fullMah, r.pct,
             static_cast<unsigned long>(c.awakeS), static_cast<unsigned long>(c.restS),
             static_cast<unsigned long>(c.screen), static_cast<unsigned long>(c.keys),
             static_cast<unsigned long>(c.touches), offMa, touchMa);
    ok = f.print(line) > 0;
  }
  f.close();
  return ok;
}

void dump(Print& out) {
  out.print("[b] inizio\n");
  FsFile f = SdMan.ready() ? SdMan.open(kLogPath) : FsFile();
  if (f) {
    char line[128] = "[b] ";
    int n = 4;
    for (int ch; (ch = f.read()) >= 0;) {
      if (n < static_cast<int>(sizeof(line)) - 2) line[n++] = static_cast<char>(ch);
      if (ch != '\n') continue;
      line[n] = 0;
      out.print(line);  // una riga per scrittura: non si mescola con le altre
      n = 4;
    }
    f.close();
  }
  out.print("[b] fine\n");
}

bool erase() { return SdMan.ready() && (!SdMan.exists(kLogPath) || SdMan.remove(kLogPath)); }

}  // namespace battery
