#pragma once
// Schermata filtri: ND e diottria su A|B e C|D, stadi del mattebox, modifica.
// Specifica: docs/filtri.md. Il simulatore del configuratore web è il riferimento
// del comportamento: i nomi fra parentesi sono le sue funzioni.
//
// Qui c'è solo ciò che si prova sull'host (device/test/filters_test.cpp):
// lettura di screen.ini v3, regole degli stadi e composizione dei pezzi BMP.
// Tasti, tempi e refresh sono in main.cpp.
#include <SdFat.h>

#include <cstdint>

#include "Canvas.h"

namespace filters {

constexpr int kMaxSteps = 40;   // passi di ND o diottria
constexpr int kMaxKit = 64;     // vetri del kit
constexpr int kMaxStages = 6;   // MAX_STAGES nel configuratore web
constexpr int kHintLen = 8;     // testo di un'indicazione, terminatore compreso

enum Role : uint8_t { kNone = 0, kNd = 1, kDio = 2 };

struct Rect {
  int16_t x = 0, y = 0, w = 0, h = 0;
  bool ok() const { return w > 0 && h > 0; }
};

struct Layout {
  Rect nd, dio, list;
  int16_t row = 0, num = 0;  // listRow, listNum
};

// Il contenuto di screen.ini. Verso = quarti di giro in senso orario dalla
// posizione naturale (indice 0..3 = 0°, 90°, 180°, 270°).
struct Config {
  bool ok = false;
  uint32_t crc = 0;  // del file: se cambia, si riparte dai valori di partenza
  uint8_t ndCount = 0, ndStart = 0;
  uint8_t ndParts[kMaxSteps][2] = {};  // vetri ND (da 1) per ogni passo, 0 = nessuno
  uint8_t dioCount = 0, dioStart = 0;
  uint8_t dioSlots[kMaxSteps] = {};
  uint8_t kitCount = 0;
  uint8_t kitSlots[kMaxKit] = {};
  uint8_t stageCount = 0;
  int8_t stageStart[kMaxStages] = {};
  uint8_t settleS = 2, editTimeoutS = 5;
  Layout land, port;
  struct Key {
    Role role = kNone;
    int8_t dir = 0;
  } keys[4][4];                 // [verso][A..D]
  int8_t editRow[4][2] = {};    // [verso][A, B]: +1 = stadio successivo
  int8_t editVal[4][2] = {};    // [verso][C, D]: +1 = vetro successivo
  char hints[4][4][kHintLen] = {};
  char editHints[4][4][kHintLen] = {};

  const Layout& layout(bool portrait) const { return portrait ? port : land; }
};

// Legge screen.ini v3. false se manca o non è della versione giusta.
bool parse(FsFile& file, Config& c);

// Stato sul dispositivo: nd/dio nei riquadri grandi, ndShown/dioShown già
// applicati agli stadi (simSettle), stages[i] = vetro del kit che comincia
// nello stadio i, -1 = nessuno.
struct State {
  uint8_t nd = 0, dio = 0, ndShown = 0, dioShown = 0;
  int8_t stages[kMaxStages] = {-1, -1, -1, -1, -1, -1};
};
void startState(const Config& c, State& s);
bool validState(const Config& c, const State& s);

int ndUsed(const Config& c, int nd);
int dioUsed(const Config& c, int dio);
// Stadi per i vetri: [lo, hi) (stageRange).
void stageRange(const Config& c, int nd, int dio, int& lo, int& hi);
// Rimette i vetri in [lo, hi) (fitStages). true se qualcosa si è spostato.
bool fitStages(const Config& c, int8_t* stages, int lo, int hi, int back);
// Applica ND e diottria agli stadi (simSettle). true se è cambiato qualcosa.
bool settle(const Config& c, State& s);

enum CellKind : uint8_t { kCellNd, kCellDio, kCellGlass, kCellEmpty, kCellFront, kCellCont };
struct Cell {
  CellKind kind = kCellEmpty;
  uint8_t span = 1;
  int8_t glass = -1;    // kCellGlass: indice nel kit; kCellNd: vetro ND (da 1)
  bool insert = false;  // kCellDio: si può mettere un vetro al suo posto
};
// Cosa c'è in ogni stadio (stageCells).
void stageCells(const Config& c, const int8_t* stages, int nd, int dio, Cell* out);
// Stadi selezionabili in modifica (editableStages); restituisce quanti.
int editableStages(const Config& c, const int8_t* stages, int nd, int dio, int* out);
// Scelte per lo stadio i: -1 (vuoto) e i vetri liberi che ci stanno (stageOptions).
int stageOptions(const Config& c, const int8_t* stages, int i, int hi, int* out);

// Modifica: sposta la selezione (A/B) o cambia il vetro (C/D).
int editNav(const Config& c, const State& s, int sel, int step);
void editValue(const Config& c, State& s, int sel, int dir);
// La selezione dopo un cambio: resta, o va al primo stadio selezionabile prima (renderSim).
int editFix(const Config& c, const State& s, int sel);

// Composizione. dir = cartella dei pezzi ("/camtag/03_filters"); i file si aprono
// con filtersOpen, definita da chi usa il modulo (SD sul dispositivo, file sull'host).
enum Part : uint8_t { kPartNd = 1, kPartDio = 2, kPartList = 4, kPartAll = 7 };
bool drawParts(Canvas& c, const Config& cfg, const State& s, const char* dir, int sel, uint8_t parts);
// Indicazioni dei tasti sul bordo fisico, nel verso del contenuto: `phys` è la tela
// del contenuto (dritta per chi guarda, salvo con la rotazione bloccata), `natural`
// quella della posizione naturale (tasti in basso), q = quarti di giro del contenuto.
void drawHints(Canvas& phys, const Canvas& natural, const Config& cfg, int q, bool editing);

}  // namespace filters

FsFile filtersOpen(const char* path);
