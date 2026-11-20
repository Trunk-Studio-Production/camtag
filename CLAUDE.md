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
  `BleUpload.cpp` riceve le card via Bluetooth (NimBLE-Arduino, acceso solo in
  ricezione; i file uguali non viaggiano, li copia lui dalle card attuali: `KEEP`)
  e rimanda al telefono `project.json` (`GET`); `UploadProto.h` ha le parti provabili sull'host (`device/test/`).
  `Version.h` ha la versione del firmware (`CAMTAG_FW_VERSION`), scritta sulla
  schermata Bluetooth con i credits e mandata nella risposta a `HELLO`: lo
  strumento web la mostra accanto a `FW_VERSION`; si ferma solo se il protocollo è diverso. Si cambiano
  insieme, a ogni nuovo firmware.
  `Filters.cpp` è la schermata filtri provabile sull'host (lettura di
  `screen.ini`, regole degli stadi portate dal simulatore con gli stessi nomi,
  composizione dei pezzi, indicazioni); tasti, tempi e refresh sono in `main.cpp`,
  con riposo e spento (`rest`, `powerOff`). `Battery.cpp` legge il fuel gauge e
  scrive il registro dei consumi (`/camtag-batteria.csv`, specifica in `docs/batteria.md`).
  Problemi aperti dello schermo: `docs/schermo.md`.
- `companion/index.html` — strumento web a file singolo: lens chart e filtri
  generati in automatico più schede manuali (crew, note, lente singola, patch);
  esporta uno zip di BMP 1 bit o manda gli stessi file all'X3 via Web Bluetooth
  (`bleSend`; `bleGet` riprende il progetto dall'X3). Dipendenze esterne: JSZip da cdnjs (i font
  Inter sono nel repo). Legge
  `../data/*.json`, quindi va servito dalla radice del repo. Ha un
  **simulatore** (`simPress`/`renderSim`, solo da computer: sotto 700 px è nascosto) con tasti, rotazione, blocco e power:
  è la specifica di comportamento per il firmware, e i due devono restare allineati.
  Un solo tema colore, scuro (variabili su `:root`).
  Barra e anteprima restano fisse mentre l'editor scorre: tre colonne da 1100 px,
  due da 700 (striscia delle schermate e anteprima a sinistra), sotto i 700
  anteprima compatta in alto che si apre a tutto schermo; `fitStage` tiene
  anteprima e simulatore dentro lo schermo (il simulatore si scala). Nell'editor
  le sezioni richiudibili si fanno con `section()` e le spiegazioni stanno dietro
  una ⓘ (`info()`): in chiaro restano solo avvisi e stati vuoti. Le preferenze
  dell'interfaccia (sezioni aperte, orientamento, anteprima o simulatore) stanno
  nel localStorage `camtag.ui.v1`: sono del browser, non entrano in `project.json`.
- `companion/manuale/` — manuale d'uso (pulsante "Manuale" nella barra dello
  strumento): `index.html` disegna l'X3 in SVG attorno a PNG presi dal
  simulatore. I PNG non si aggiornano da soli: se cambiano le schermate, il
  disegno o i tasti, rigenerali con `schermate.js` (istruzioni in testa al
  file) e correggi il testo del manuale.
- `data/lenses.json` — database lenti (schema in `data/README.md`), dati solo
  dalle schede dei costruttori, con la fonte per ogni lente. `mounts` = attacchi
  offerti (sigle in `MOUNTS`, uguali in `data/validate.py`). Mai modificarlo per
  un esemplare: le differenze (es. Ø frontale del noleggio) sono modifiche
  locali per `id` nello strumento web (localStorage `camtag.lensOverrides.v1`,
  esportabili), applicate sopra il database quando si disegna. Un valore
  modificato porta un `*` accanto, sulla lens chart e nello strumento.
- `data/filters.json` — catalogo dei vetri (famiglie e gradazioni) **provvisorio, non verificato**.
- `third_party/freeink-sdk/` — sottomodulo git bloccato su `77ae8c7`. **Non
  modificarlo**: se serve un cambiamento, aggira nel nostro codice o chiedi.
- `reference/crosspoint/` — solo consultazione, escluso da git (come CrossPoint
  usa l'SDK: `lib/hal/`, `src/main.cpp`). Non compilarlo e non seguire il suo
  CLAUDE.md, che riguarda i contributi a quel progetto.

## Repo e comandi

Repo pubblico `Trunk-Studio-Production/camtag`, licenza GPL-3.0, branch `main`. Commit e push
solo quando l'utente lo chiede. Ogni push su `main` che tocca `companion/` o
`data/` pubblica lo strumento web su GitHub Pages
(`.github/workflows/pages.yml`): https://trunk-studio-production.github.io/camtag/

```bash
# dopo un clone: sottomodulo, PlatformIO isolato, riferimento CrossPoint
git submodule update --init
python3.13 -m venv .venv && .venv/bin/pip install platformio
git clone --depth 1 https://github.com/crosspoint-reader/crosspoint-reader reference/crosspoint

cd device && ../.venv/bin/pio run -e x3                  # compila
cd device && ../.venv/bin/pio run -e x3 -t upload        # carica (vedi sotto)
python3 -m http.server 8766    # dalla radice; poi apri http://localhost:8766/companion/
```

Sessioni cloud (environment su claude.ai/code): l'hook SessionStart in
`.claude/settings.json` lancia `scripts/cloud-setup.sh` (fa i passi qui sopra
e scarica i pacchetti del firmware) solo con `CLAUDE_CODE_REMOTE=true`. Nel
cloud niente X3 e niente browser integrato.

Nel browser integrato usa un server avviato da Bash: `preview_start` legge
ancora un `launch.json` vecchio e serve la cartella sbagliata.

Usa sempre `.venv/bin/pio`: il PlatformIO globale (6.1.5, quello di VS Code)
è troppo vecchio per la piattaforma pioarduino. `esptool` è
`~/.platformio/penv/bin/python -m esptool` (nel `.venv` non c'è).

L'X3 compare su USB (`/dev/cu.usbmodem*`) solo da acceso e sveglio. Collegato a
un computer resta sveglio (`usbHost`); se la porta non c'è fai premere un tasto,
o tenere il power se è spento, e se serve staccare e riattaccare il cavo.
`CAMTAG_DEBUG` (che oggi `x3` ha sempre) accende i messaggi e i comandi sulla
seriale. Aprire la seriale lo riavvia. Per le prove con l'utente usa
`scripts/taratura.py` nel suo terminale (`tasti`, `versi`, `card` con le card
sopra, `filtri` con la schermata filtri, `blocco` solo il blocco della rotazione,
`reattivita` pressioni sentite da `keyScan` (`[adc]`) contro quelle eseguite, con
gli aggiornamenti dello schermo (`[epd]`), `log` registro dal vivo, `batteria`
scarica il registro dei consumi in `misure/` e fa i conti; `--carica`):
guida passo per passo, mostra dal vivo tasti, versi, card e stato dei filtri
(righe `[filtri]` della seriale), e se l'X3 si spegne lo aspetta e rifà il passo.

## Hardware X3 (dal profilo `XTEINK_X3` in BoardConfig.h)

- ESP32-C3, ~380 KB RAM, niente PSRAM. Framebuffer 1 bpp allocato dall'SDK.
- Vetro 528×792 in verticale (area attiva 51,84 × 77,76 mm, ~259 ppi); il
  framebuffer dell'SDK è 792×528 (orizzontale), MSB-first, **1 = bianco**.
  Controller UC8253 o UC8279 secondo il lotto, rilevato all'avvio.
- Nello strumento web i canvas si creano con `newCanvas` (software,
  `willReadFrequently`): la stessa card deve uscire uguale byte per byte, se no
  l'invio Bluetooth la rimanda.
- Nello strumento web le immagini rimpicciolite vanno ammorbidite, non
  `pixelated` (che butta via righe); i pixel veri si vedono con "Pixel 1:1".
- Disegnando per l'e-paper, righe sottili su pixel interi e spesse almeno 2 px:
  una riga da 1 px a mezzo pixel diventa grigia e la soglia a 1 bit la cancella.
- Tipografia fissa, senza opzioni: Inter, soglia del testo 128 (`FONT`,
  `TEXT_T`). Inter 4.1 è incluso in `companion/fonts/` (variabile, licenza SIL
  OFL nel file accanto) e funziona offline. Il font va caricato
  (`document.fonts.load`) prima di disegnare, altrimenti il canvas usa il font
  di riserva.
- microSD su SPI condiviso con il display; batteria 650 mAh con fuel gauge
  BQ27220; RTC DS3231; IMU QMI8658 (accelerometro + giroscopio).
- Niente touch. Posizione naturale **verticale**: in basso due bilancieri,
  A|B a sinistra e C|D a destra; X sul lato sinistro, Y sul destro, power in
  alto a destra. I 4 in basso sono su un solo ingresso analogico (uno alla
  volta): dal sonno profondo li sveglierebbe solo in parte, quindi da spento
  **accende solo il power**, e da acceso i tasti si leggono a intervalli nel
  sonno leggero.
- Lo spegnimento porta GPIO13 basso come fa CrossPoint; l'e-paper resta visibile.
  Sull'X3 GPIO13 basso **stacca la batteria** (misurato: dopo lo spento il
  chip riparte da alimentazione tornata, con l'orologio a zero), al contrario
  di quanto dice il profilo dell'SDK. Con la batteria staccata non consuma,
  ma nessun timer può svegliarlo: per questo lo spento tiene GPIO13 alto fino
  al passaggio al bianco e lo abbassa solo dopo (`sleepOff`, che non usa
  `powerDownRailsForSleep` finché serve il timer). A riposo GPIO13 non si tocca.
- Nel sonno leggero il framework isola i pin: le uscite che devono restare
  ferme vanno escluse (`keepOutputsInRest`).
- Dopo ogni aggiornamento il pannello va spento (`displayBuffer(…, true)`) e,
  4 s dopo l'ultimo (`DISPLAY_SLEEP_MS`), il controller va in sonno profondo
  (`sleepDisplay`; `wakeDisplay` in `canvas()` lo risveglia e svuota il
  framebuffer): solo spento, in un paio di minuti affiora il negativo delle
  schermate precedenti (`docs/schermo.md`).

## Formato card

- `/camtag/NN_nome.bmp` orizzontale 792×528, `/camtag/NN_nome.p.bmp` verticale
  528×792. BMP 1 bit non compresso, palette 0 = nero, 1 = bianco. Ordine = nome.
- Nomi riservati: `NN_lenschart[-2…]` (pagine da 11 lenti al massimo) e `NN_filters`.
- `NN_filterlist` (facoltativa, `state.auto.filterlist`, spenta di partenza):
  elenco di tutti i vetri a disposizione (ND del kit, diottrie intere e split,
  famiglie con le gradazioni; le coppie di ND no), generato da `filterListRows`.
  Nome e gradazioni affiancati se ogni riga sta su una linea, se no nome sopra
  e gradazioni a 0,7 della sua misura (`VAL_STACK`);
  a capo solo se il testo cresce di oltre il 25%, in parti uguali (3 + 2).
  Nello strumento ha lo stesso editor dei filtri (`filtersEditor(ed, true)`).
  Per il firmware è una card qualsiasi.
- `/camtag/off.bmp`, `off.p.bmp` (facoltativa): schermata di spento, fuori dal
  giro delle pagine. Nello strumento la compone la pagina "Spento" (`state.off`:
  immagine di sfondo e scritta, `renderOff`), o è una scheda manuale con
  "Schermata di spento" accesa (`c.off`, una sola; `offCard`), che la sostituisce;
  senza nessuna delle due non si esporta. Il firmware ci disegna sopra il nome,
  una fascia in basso, la batteria e le etichette dei tasti (`drawOff`; nello
  strumento `drawOffOverlay`, con il font 5x7 del firmware copiato in `TINY`).
- `/camtag/settings.ini`: `keys=0|1` etichette dei tasti, `off.logo=0|1` nome
  CAMTAG da spento (al centro senza `off.bmp`, se no in un riquadro in alto).
  Senza il file: etichette sì, nome solo senza `off.bmp` (`loadSettings`).
- Etichette dei tasti (`kKeyTabs`/`drawKeyLabels`, nello strumento `KEY_TABS`,
  stesse frazioni lungo i bordi): linguette bianche con bordo nero e nome dritto
  per chi legge (A|B e C|D, X, Y, PWR, RESET, che è un tasto vero e riavvia),
  sulla schermata senza card e da spento (non sul bianco). La schermata senza
  card (`showMessage`, nel simulatore `drawMessageScreen`) ha il messaggio in un
  riquadro ben in vista al centro.
- `/camtag/project.json`: il progetto dello strumento web, inviato con le card
  (anche nello zip) e riletto con "Importa dall'X3". Il firmware lo ignora. Il
  lavoro nel browser sta solo nel localStorage: questo file è la copia che resta.
- Schermata filtri (specifica completa in `docs/filtri.md`): lo sfondo ha vuoti
  i riquadri dei valori e l'area degli stadi. La cartella `NN_filters/` contiene
  `screen.ini` v3 (riquadri e area stadi per orientamento, tasti e indicazioni
  per verso `keys.<gradi>`, `hints.<gradi>`, `edit.keys.<gradi>`,
  `edit.hints.<gradi>`, `edit.timeout`, vetri `kit.*` con `kit.slots`,
  `stages.count`, `stages.nd`, `stages.start`, `stages.settle`, `dio.slots`, e
  per l'ND `nd.glasses`, `nd.parts`) e i pezzi: `ndI`, `dioI` (I = passo, 0 =
  nessuno); `stN`/`stsN` colonna del numero dello stadio N, normale e
  selezionata; `fxG`/`fxsG` vetro G (indice nel kit, `n` = vuoto), normale e
  selezionato; `fxdK` il vetro ND K e `fxoI`/`fxsoI` la diottria al passo I nelle righe
  dei loro stadi. Tutti `.bmp`, con `.p.bmp` per il verticale. Una riga = `stN`
  + pezzo del vetro affiancati (`listNum` = larghezza del numero), altezza
  `listRow`; il pezzo di un vetro da due stadi è alto due righe e nella seconda
  non si disegna altro. In cima a ogni riga dopo la prima il firmware disegna
  un separatore da 2 px (nel secondo stadio di un vetro da due solo sotto il
  numero). Comporre i pezzi deve dare l'anteprima pixel per pixel (verificato,
  anche con un vetro da due stadi selezionato).
- Sulla schermata filtri il contenuto lascia libere le fasce sui lati corti
  (40 px in verticale, 64 in orizzontale) per le indicazioni dei tasti; la barra
  del titolo invece arriva ai bordi, e le indicazioni vi si disegnano sopra in
  un riquadro bianco con bordo nero.
- Bilancieri nella schermata filtri (regola unica in `rockerAction`): ogni
  bilanciere comanda il valore che gli sta accanto, quindi a 90° in senso orario
  A|B diventa ND e C|D diottria. "+" sta sempre sul capo in alto o a destra per
  chi guarda (a 90° e 180° è il primo tasto, A o C).
- Anche il protocollo Bluetooth (`docs/bluetooth.md`) è un contratto tra i due.
- Il formato è un contratto tra `companion/` e `device/`: se cambi uno, aggiorna
  l'altro.

## Decisioni di prodotto

- Solo X3 per ora; l'X4 è un futuro lontano.
- Decimali sulle schermate: sempre il punto (0.9), senza opzione.
- Lens chart: focale, T, CF (close focus), Ø frontale (più piccolo, dato secondario, si può
  nascondere: `state.lensFront`). Attacco (`state.lensMount`, spento di
  partenza): come le sigle, quello principale in basso (solo la sigla, "LPL", subito dopo la prima serie che lo usa), gli
  altri accanto alla focale. L'attacco è della lente nel progetto (`l.mount`,
  `lensMount`): se la serie ne offre più d'uno lo si chiede quando la lente
  entra nel kit (`addDbLenses`), salvo che il kit abbia già una lente della
  serie con un attacco valido; si cambia con ✎. Le lenti a focale fissa si chiamano "prime"
  anche nei testi in italiano, mai "fissi". Niente
  colonna serie: in basso un riquadro con il nome per esteso del set principale
  e "Serie (SIGLA)" per le altre; la sigla piccola compare accanto alla focale
  solo sulle lenti fuori dal set principale (su tutte se non c'è). In negativo
  solo la lente "fuori dal coro" (serie diversa dal set principale o tipo
  diverso, es. l'unica macro); con un pareggio tra serie non si evidenzia per
  serie. Si può forzare per ogni lente (auto / sempre / mai). Scala del testo
  in base al numero di lenti: con poche lenti ogni lente va su due righe grandi
  (focale sopra, sotto i valori: in orizzontale l'etichetta sta accanto al
  valore a 0,45 della sua misura, `LABEL`; in verticale, piccola, sopra), con
  tante torna la tabella; la misura è unica per tutte le pagine. La focale
  di un prime è sempre il testo più grande: T e CF stanno a 0,8 della sua misura
  (`VAL`), e in tabella le colonne prendono la larghezza del contenuto.
  Eccezione per gli zoom: la loro escursione può scendere fino a 0,6 della
  focale dei prime (`ZOOM`), così non rimpicciolisce tutta la tabella.
  In orizzontale la tabella si divide in due affiancate (prima metà a sinistra)
  quando la focale cresce almeno del 20% (`TWO_COLS_GAIN`): in pratica da 6
  lenti. Le pagine restano da 11 lenti in entrambi gli orientamenti.
  Gruppi di pagine (`lensGroups`), ognuno una lens chart a sé con le sue
  misure, la sua legenda e le sue lenti in negativo, su tre livelli uno dentro
  l'altro. Prima "Attacchi su pagine separate" (`state.lensByMount`, spento di
  partenza; `mountSplit`: vale solo con più di un attacco): un gruppo per
  attacco dal più numeroso, le lenti senza attacco in fondo (con un avviso
  nell'editor), così le pagine di una macchina da presa restano di fila.
  L'attacco del gruppo sta nella barra del titolo, in un riquadro bianco prima
  del numero di pagina, e "Mostra l'attacco" sparisce perché sarebbe un
  doppione. Poi per serie ("Serie":
  Insieme / Una per serie, dalla più numerosa / Principale a parte, il set
  principale e poi le altre mescolate; `state.lensSeries` = mix/each/main),
  poi dentro ognuno "Prime e zoom su pagine separate" (`state.lensSplit`).
  Ordine per focale (minima per gli zoom); "Zoom in fondo, dopo i prime"
  (`state.zoomsLast`, in `sortedLenses`) li mette dopo i prime sulla stessa
  lens chart (accesa di partenza), ed è nascosta quando le pagine sono separate.
- ND e diottrie: +/− sul dispositivo, scorrendo solo i valori del kit scelto
  nello strumento web. Nella scala dell'ND entrano anche i totali fatti con
  **due vetri** spuntati nello strumento (`ndCombos`, `ndComboList`: mai un
  totale che il kit ha già, coppia con il vetro più denso, che va nello stadio
  1); gli stadi si riordinano solo con ND e diottria fermi da 2 s (`simSettle`). Si
  mostra solo il formato ND scelto (0,9 / 3 stop / ND8). Tipo ND: ND, IRND o
  FSND (full spectrum).
- Gli altri vetri (diffusione, polarizzatori, colore…) seguono il mattebox
  (`docs/filtri.md`): un **kit** di vetri, uno per tipo, e da 1 a 6 **stadi**
  (`MAX_STAGES`; nello strumento il selettore mostra 1–3 e un `+`,
  `BOX_STAGES`, che apre anche 4, 5 e 6)
  numerati da 1 = lato lente; ogni stadio ha un vetro o è vuoto (`—`), e un
  vetro sta in un posto solo. Non esiste "montato": la schermata mostra sempre
  tutti gli stadi sotto ND e diottria. Un vetro occupa uno stadio, o due se la
  famiglia è segnata così nel kit (`e.slots`; la casella c'è solo per i vetri
  personalizzati e per quelli con `twoSlots` nel catalogo, oggi il Rota Pola: `canTwoSlots`): sulla schermata è una casella alta due righe. **L'ND, quando c'è,
  occupa i primi stadi** (uno, o due per una coppia) **e la diottria sta subito
  davanti all'ultimo vetro** (o all'ND; da sola negli stadi 1 e 2), davanti a
  tutto ma non per forza negli ultimi stadi (`dioStart`; due, o uno se segnata:
  `dioSlots`); gli stadi davanti a lei restano vuoti. I vetri lasciano sempre in
  fondo il posto per la diottria. Le righe dell'ND non si selezionano; quella
  della diottria sì, se può avanzare: un vetro messo lì la spinge avanti
  (`c.insert`, pezzi `fxsoI`). Se insieme non entrano, la diottria resta solo nel
  riquadro grande (`stageRange`). Una regola sola rimette a posto i vetri
  (`fitStages`): restano in ordine, scalano verso il fronte per l'ND e verso la
  lente per la diottria chiudendo i vuoti solo quanto serve, e se non c'è posto
  escono i più avanti; quando l'ND libera stadi tornano indietro di altrettanti
  verso la lente. Il simulatore tiene il suo
  stato (`sim.nd`, `sim.dio`, `sim.stages`) separato dai valori di partenza. Sul dispositivo si cambiano in
  **modifica** (`simEditPress`), sulla stessa schermata: tieni A o B per
  entrare; dopo 5 s senza tocchi la modifica è accettata e si esce da soli
  (subito con X o Y, o tenendo A o B); A/B scelgono lo stadio, C/D
  scorrono vuoto + i vetri liberi del kit, ricominciando in fondo. Selezione =
  riga in negativo, niente cursore lampeggiante; aggiornamenti parziali,
  completo all'uscita. Diottrie e ND restano su A|B e C|D fuori dalla modifica.
- Energia (`docs/batteria.md`): per chi lo usa gli stati sono due. **Acceso**
  mostra le card e sente i tasti in ogni momento: dopo 5 s senza tocchi va a
  riposo (sonno leggero, tasti letti ogni 25 ms, verso ogni secondo) senza che
  da fuori cambi nulla. **Spento** (sonno profondo) mostra la schermata di
  spento con la fascia "SPENTO - TIENI POWER PER ACCENDERE", e dopo 72 h il
  bianco con la sola fascia (fino ad allora il chip resta alimentato, poi la
  batteria si stacca; `taratura.py batteria --bianco N` accorcia l'attesa per provarlo). Power tenuto 1 s spegne e accende. Si spegne da
  solo dopo 24 h senza tasti e a batteria scarica (sotto 3,3 V, con l'avviso).
  Niente segni di "pausa" sulle card: se mostra una card, risponde.
- Stato del cavo sulle card (`drawStatus`, in alto a destra nel verso del
  contenuto, sotto il lucchetto se lo toccherebbe): batteria (sotto il 20% o in
  carica), fulmine in carica (corrente del gauge > `CHARGE_MIN_MA`), linguetta
  "USB" se un computer la riconosce (`usbHost`). L'X3 non ha un pin del cavo: su
  un caricatore a batteria piena non si vede nulla. Si ridisegna (`pollUsb`)
  dopo due letture uguali a 1 s; a riposo sveglia la carica che comincia o
  finisce. Non nel simulatore, che non mostra la batteria.
- **Power breve = tasti bloccati** (`keysLocked`, `toggleKeyLock`; nel
  simulatore `sim.keyLock`): la pagina resta, A–D, X e Y non fanno nulla, la rotazione
  continua. Il segno è un lucchetto sul bordo fisico sotto il power
  (`drawLock`), come le indicazioni dei tasti: segue la rotazione del
  dispositivo, dritto per chi legge. A riposo da bloccato il chip dorme un
  secondo alla volta. Spegnendo il blocco si toglie.
- Tasti: X (precedente) e Y (successiva) cambiano pagina in ciclo su tutte.
  Schermata filtri A/B = diottria −/+, C/D = ND −/+. Lens chart di più pagine:
  A–D sfogliano solo le sue pagine, in ciclo; i due tasti di un bilanciere fanno
  lo stesso, avanza quello a destra o in basso per chi guarda, arretra quello a
  sinistra o in alto (`lensPageDir`, `stepLensChart`). Altrove i bilancieri non
  fanno nulla. Y tenuto =
  blocco rotazione. X tenuto = ricezione via Bluetooth (X o Y escono). La
  pressione breve si legge al rilascio; quella lunga (0,8 s, anche A o B per la
  modifica) scatta allo scadere del tempo con il tasto ancora giù, e il
  rilascio che segue non conta (`holdFired` nel firmware, `simDown`/`simUp`).
  Nel firmware i tasti A–D, X e Y li legge `keyScan` (compito a parte, ogni 5 ms, anche
  durante gli aggiornamenti dello schermo) in una coda con l'ora: una pressione
  fatta mentre lo schermo si aggiorna si esegue dopo, breve o lunga secondo la
  durata vera. ADC condiviso con `input.update()` (power) sotto `adcLock`.
- Indicazioni dei tasti a schermo solo dove servono (filtri), disegnate dal
  firmware sul bordo fisico dei tasti, così seguono la rotazione. La rotazione
  bloccata ferma tutto: contenuto, indicazioni e funzione dei bilancieri (filtri
  e lens chart) restano quelli del verso bloccato (`simContentRot`; nel firmware
  `rotation`, mentre `physical` è la posizione vera): chi monta l'X3 a testa in
  giù vuole vedere tutto a testa in giù. Sbloccando il contenuto raggiunge
  subito la posizione vera.
- Refresh della schermata filtri: veloce (`FAST_REFRESH`, solo i pezzi
  cambiati) per ND, diottria e modifica; mezzo dopo `stages.settle` s fermi
  (riordino degli stadi), sempre se a schermo restano ombre dei veloci
  (`fastShadow`); completo all'uscita dalla modifica.

## Regole

- Unità di misura sempre minuscole ("CF m", "Ø mm"), anche nelle etichette
  in maiuscolo: mai passare le unità a `toUpperCase()`.
- Firmware piccolo: niente allocazioni grandi o librerie nuove senza motivo.
  `Canvas` disegna in coordinate logiche; la rotazione la gestisce lui.
- Dopo una modifica al firmware: compila. Se tocchi `Canvas`, `Filters` o
  l'esportazione dei filtri: `device/test/filters_test.sh` (stub di `SdFat.h`,
  confronto pixel per pixel con le anteprime dello strumento; se cambia
  l'esportazione rigenera `filters_fixture.zip` con `filters_fixture.js`).
  Per il protocollo Bluetooth: `device/test/upload_proto_test.cpp` (comando in `docs/bluetooth.md`).
- Dopo una modifica allo strumento web: aprilo nel browser integrato e
  controlla l'anteprima; verifica che i BMP esportati abbiano le dimensioni giuste.
- **Mai caricare firmware su un X3 nuovo senza i tre controlli del README**
  ("Rischi e controlli prima di caricare": compare sulla USB,
  `get-security-info` pulito, flash letta due volte uguale, che è anche il
  backup). Un'unità con la USB bloccata può restare inutilizzabile: se un
  controllo non passa, fermati. Il README deve dire sempre chiaro che il
  caricamento è a rischio di chi lo fa.
  Il caricamento lo fa l'utente, o lo fai tu solo se lo chiede esplicitamente.

## Manutenzione di questo file

Lo aggiorna Claude, senza aspettare che glielo si chieda: a fine lavoro, se è
cambiato qualcosa qui sopra (struttura, comandi, formato, decisioni, taratura),
correggilo. Tienilo corto: solo ciò che non si ricava dal codice; togli ciò che
non è più vero invece di aggiungere note.

## Da tarare sull'hardware (aperto)

Provati sull'X3: ricezione Bluetooth dal Mac (1,1 MB in ~40 s) e "Importa dall'X3", card vere, X/Y, rotazione e
blocco, bilancieri della lens chart nei 4 versi. Tasti, assi IMU e verso dello schermo sono tarati sull'X3 (`kKeyA`…`kKeyD`, `kKeyX`, `kKeyY`,
`kImuSwapXY`, `kImuSignX`, `kImuSignY`, `kQuarterTurns` in `device/src/main.cpp`:
la posizione naturale è `Canvas::ROT_270`).

- Soglie della rotazione (`kMinTiltG`, `kFlatG`, `ROTATE_STABLE_MS`) su camera vera.
- Ghosting (`FULL_REFRESH_EVERY`).
- Negativo a schermo fermo: corretto con il sonno profondo del controller
  (firmware 0.2.4), da confermare con ore d'uso. Aperto in `docs/schermo.md`:
  il riavvio da solo dopo il riposo e il riposo che scatta con la USB.
- Energia, provato sull'X3 (20/11/2026): riposo con tasti e rotazione, blocco
  dei tasti, spento e riaccensione con il power tenuto (prima e dopo il
  bianco), passaggio al bianco, registro. Il gauge ha già 642 mAh; la batteria
  si carica fino a circa 4,35 V e la ricarica si ferma da sola a piena.
  **I consumi non sono ancora misurati**: prime letture circa 25 mA da sveglio
  e 2 mA a riposo. Servono ore a batteria con `taratura.py batteria`: riposo,
  riposo con i tasti bloccati, primi tre giorni da spento. Le domande aperte
  sono in fondo a `docs/batteria.md`.
- Schermata filtri: provata sull'X3 (bilancieri, indicazioni, riordino,
  modifica, ombra tolta dal mezzo refresh, blocco che ferma tutto).
