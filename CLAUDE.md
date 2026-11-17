# CamTag

Badge e-paper per il reparto camera: lens chart, filtri, crew, note e
immagini, montato sulla camera. MVP su **Xteink X3** con firmware nostro sopra
**FreeInk SDK**. Il proprietario parla italiano: rispondi in italiano, e anche
i testi visibili all'utente (UI web, messaggi a schermo) sono in italiano.

Il repo potrà diventare pubblico e open source: nel codice, nella
documentazione e nei commit non citare prodotti o produttori concorrenti.
Hardware, SDK e progetti da cui dipendiamo (Xteink, FreeInk, CrossPoint) e i
costruttori di lenti e filtri nei dati vanno bene.

## Struttura

- `device/` — firmware PlatformIO (C++/Arduino, ESP32-C3). È il nostro codice.
- `companion/index.html` — strumento web a file singolo: lens chart e filtri
  generati in automatico più schede manuali (crew, note, lente singola, patch);
  esporta uno zip di BMP 1 bit. Unica dipendenza: JSZip da cdnjs. Legge
  `../data/*.json`, quindi va servito dalla radice del repo. Ha un
  **simulatore** (`simPress`/`renderSim`) con tasti, rotazione, blocco e power:
  è la specifica di comportamento per il firmware, e i due devono restare allineati.
- `data/lenses.json` — database lenti (schema in `data/README.md`), dati solo
  dalle schede dei costruttori, con la fonte per ogni lente. Mai modificarlo per
  un esemplare: le differenze (es. Ø frontale del noleggio) sono modifiche
  locali per `id` nello strumento web (localStorage `camtag.lensOverrides.v1`,
  esportabili), applicate sopra il database quando si disegna. Un valore
  modificato porta un `*` accanto, sulla lens chart e nello strumento.
- `data/filters.json` — catalogo filtri d'effetto **provvisorio, non verificato**.
- `third_party/freeink-sdk/` — sottomodulo git bloccato su `77ae8c7`. **Non
  modificarlo**: se serve un cambiamento, aggira nel nostro codice o chiedi.
- `reference/crosspoint/` — solo consultazione, escluso da git (come CrossPoint
  usa l'SDK: `lib/hal/`, `src/main.cpp`). Non compilarlo e non seguire il suo
  CLAUDE.md, che riguarda i contributi a quel progetto.

## Repo e comandi

Repo privato `Trunk-Studio-Production/camtag`, branch `main`. Commit e push
solo quando l'utente lo chiede.

```bash
# dopo un clone: sottomodulo, PlatformIO isolato, riferimento CrossPoint
git submodule update --init
python3.13 -m venv .venv && .venv/bin/pip install platformio
git clone --depth 1 https://github.com/crosspoint-reader/crosspoint-reader reference/crosspoint

cd device && ../.venv/bin/pio run -e x3                  # compila
cd device && ../.venv/bin/pio run -e x3 -t upload        # carica (vedi sotto)
python3 -m http.server 8766    # dalla radice; poi apri http://localhost:8766/companion/
```

Nel browser integrato usa un server avviato da Bash: `preview_start` legge
ancora un `launch.json` vecchio e serve la cartella sbagliata.

Usa sempre `.venv/bin/pio`: il PlatformIO globale (6.1.5, quello di VS Code)
è troppo vecchio per la piattaforma pioarduino.

## Hardware X3 (dal profilo `XTEINK_X3` in BoardConfig.h)

- ESP32-C3, ~380 KB RAM, niente PSRAM. Framebuffer 1 bpp allocato dall'SDK.
- Vetro 528×792 in verticale (area attiva 51,84 × 77,76 mm, ~259 ppi); il
  framebuffer dell'SDK è 792×528 (orizzontale), MSB-first, **1 = bianco**.
  Controller UC8253 o UC8279 secondo il lotto, rilevato all'avvio.
- Nello strumento web le immagini rimpicciolite vanno ammorbidite, non
  `pixelated` (che butta via righe); i pixel veri si vedono con "Pixel 1:1".
- Disegnando per l'e-paper, righe sottili su pixel interi e spesse almeno 2 px:
  una riga da 1 px a mezzo pixel diventa grigia e la soglia a 1 bit la cancella.
- microSD su SPI condiviso con il display; batteria 650 mAh con fuel gauge
  BQ27220; RTC DS3231; IMU QMI8658 (accelerometro + giroscopio).
- Niente touch. Posizione naturale **verticale**: in basso due bilancieri,
  A|B a sinistra e C|D a destra; F sul lato sinistro, E sul destro, power in
  alto a destra. I 4 in basso sono su un solo ingresso analogico (uno alla
  volta). **Solo il power risveglia** dal sonno.
- Lo spegnimento porta GPIO13 basso come fa CrossPoint; l'e-paper resta visibile.

## Formato card

- `/camtag/NN_nome.bmp` orizzontale 792×528, `/camtag/NN_nome.p.bmp` verticale
  528×792. BMP 1 bit non compresso, palette 0 = nero, 1 = bianco. Ordine = nome.
- Nomi riservati: `NN_lenschart[-2…]` (pagine da 11 righe) e `NN_filters`.
- Schermata filtri: lo sfondo ha i riquadri dei valori vuoti; la cartella
  `NN_filters/` contiene `screen.ini` (riquadri per orientamento, tasti e
  indicazioni per ogni verso `keys.<gradi>`/`hints.<gradi>`, valori iniziali)
  e i riquadri `ndI[.p].bmp`,
  `dioI[.p].bmp` (I = passo; 0 = nessuno). Il firmware compone sfondo + riquadri.
- Sulla schermata filtri il contenuto lascia libere le fasce sui lati corti
  (40 px in verticale, 64 in orizzontale) per le indicazioni dei tasti; la barra
  del titolo invece arriva ai bordi, e le indicazioni vi si disegnano sopra in
  un riquadro bianco con bordo nero.
- Bilancieri nella schermata filtri (regola unica in `rockerAction`): ogni
  bilanciere comanda il valore che gli sta accanto, quindi a 90° in senso orario
  A|B diventa ND e C|D diottria. "+" sta sempre sul capo in alto o a destra per
  chi guarda (a 90° e 180° è il primo tasto, A o C).
- Il formato è un contratto tra `companion/` e `device/`: se cambi uno, aggiorna
  l'altro. **Il firmware non gestisce ancora la schermata filtri.**

## Decisioni di prodotto

- Solo X3 per ora; l'X4 è un futuro lontano.
- Lens chart: focale, T, MOD, Ø frontale (più piccolo, dato secondario). Niente
  colonna serie: in basso un riquadro con il nome per esteso del set principale
  e "Serie (SIGLA)" per le altre; la sigla piccola compare accanto alla focale
  solo sulle lenti fuori dal set principale (su tutte se non c'è). In negativo
  solo la lente "fuori dal coro" (serie diversa dal set principale o tipo
  diverso, es. l'unica macro); con un pareggio tra serie non si evidenzia per
  serie. Si può forzare per ogni lente (auto / sempre / mai). Scala del testo
  in base al numero di lenti: con poche lenti ogni lente va su due righe grandi,
  con tante torna la tabella; la misura è unica per tutte le pagine.
- ND e diottrie: +/− sul dispositivo, scorrendo solo i valori del kit scelto
  nello strumento web; si mostra solo il formato ND scelto (0,9 / 3 stop / ND8).
  Tipo ND: ND, IRND o FSND (full spectrum).
- Filtri d'effetto (diffusione, contrasto, colore…): kit per famiglia con più
  gradazioni; ognuna è "montata" o no, e la schermata mostra solo le montate.
- Tasti: schermata filtri A/B = diottria −/+, C/D = ND −/+; altrove A/B =
  schermata precedente/successiva. E = salto ai filtri (dai filtri: schermata
  successiva), F = lens chart (pagina successiva), E tenuto = blocco rotazione.
- Indicazioni dei tasti a schermo solo dove servono (filtri), disegnate dal
  firmware sul bordo fisico dei tasti, così seguono la rotazione.

## Regole

- Firmware piccolo: niente allocazioni grandi o librerie nuove senza motivo.
  `Canvas` disegna in coordinate logiche; la rotazione la gestisce lui.
- Dopo una modifica al firmware: compila. Per la logica di disegno c'è un test
  host (stub di `SdFat.h` + `Canvas.cpp` compilati con `c++`): rifallo se tocchi `Canvas`.
- Dopo una modifica allo strumento web: aprilo nel browser integrato e
  controlla l'anteprima; verifica che i BMP esportati abbiano le dimensioni giuste.
- **Mai caricare firmware sull'X3 senza un backup completo della flash** (vedi README).
  Il caricamento lo fa l'utente, o lo fai tu solo se lo chiede esplicitamente.

## Manutenzione di questo file

Lo aggiorna Claude, senza aspettare che glielo si chieda: a fine lavoro, se è
cambiato qualcosa qui sopra (struttura, comandi, formato, decisioni, taratura),
correggilo. Tienilo corto: solo ciò che non si ricava dal codice; togli ciò che
non è più vero invece di aggiungere note.

## Da tarare sull'hardware (aperto)

- Assi IMU: `kImuSwapXY`, `kImuSignX`, `kImuSignY` in `device/src/main.cpp`.
- Quale tasto logico dell'SDK (BACK/CONFIRM/LEFT/RIGHT/UP/DOWN) è A–F.
- Ghosting (`FULL_REFRESH_EVERY`) e consumo da spento.
