# CamTag — badge e-paper per il reparto camera (MVP su Xteink X3)

Un badge e-paper per il reparto camera: mostra card con lente, crew, note e
immagini, si sfoglia con i tasti e resta visibile anche da spento.

> **⚠️ Firmware non ufficiale: lo carichi a tuo rischio.** CamTag sostituisce il
> firmware dell'X3 e, nel caso peggiore, può lasciarlo inutilizzabile senza
> rimedio. Non c'è alcuna garanzia. Prima di caricarlo leggi
> [Rischi e controlli prima di caricare](#rischi-e-controlli-prima-di-caricare).

## Struttura

| Cartella | Contenuto |
|---|---|
| `device/` | Firmware PlatformIO per Xteink X3 (e X4), sopra FreeInk SDK |
| `companion/` | Strumento web per creare le card ed esportarle in BMP 1 bit |
| `third_party/freeink-sdk/` | FreeInk SDK (driver display, tasti, batteria, SD), sottomodulo su `77ae8c7` |
| `reference/crosspoint/` | CrossPoint, solo come riferimento: non compilato e non nel repo |
| `.venv/` | PlatformIO recente, isolato dal resto del sistema |
| `data/` | Database lenti (in costruzione) |

### Dopo un clone

```bash
git submodule update --init
python3.13 -m venv .venv && .venv/bin/pip install platformio
git clone --depth 1 https://github.com/crosspoint-reader/crosspoint-reader reference/crosspoint
```

`.venv/` e `reference/` non sono nel repo: vanno ricreati così.

## Come funziona (v0.2)

1. Nello strumento web crei le card.
2. Le mandi all'X3 in uno di due modi:
   - **Bluetooth**: sull'X3 tieni premuto **X** finché compare BLUETOOTH, poi
     nello strumento web tocca **Bluetooth** e poi **Invia le card** (vedi sotto);
   - **microSD**: scarichi lo zip e copi la sua cartella `camtag/` nella radice
     della microSD.
3. Sul dispositivo:
   - **X** (lato sinistro): card precedente; **Y** (lato destro): successiva,
     in ciclo
   - **X tenuto premuto (0,8 s)**: ricezione delle card via Bluetooth
   - **Y tenuto premuto (0,8 s)**: blocca/sblocca la rotazione
   - le pressioni lunghe scattano mentre tieni premuto, allo scadere del
     tempo: non serve rilasciare per vedere l'effetto
   - A, B, C, D non cambiano card: sfogliano le pagine della lens chart e, sulla
     schermata filtri, cambiano ND e diottria (indicazioni accanto ai tasti)
   - **schermata filtri**: tieni **A** o **B** per modificare gli stadi del
     mattebox (A/B scelgono lo stadio, C/D il vetro); si esce con X o Y, tenendo
     di nuovo A o B, o da soli dopo 5 s senza tocchi (`docs/filtri.md`)
   - **power**: blocca e sblocca i tasti. La pagina resta com'è, con un
     lucchetto accanto al power
   - **power tenuto premuto (1 s)**: spegne; da spento, accende sull'ultima
     card
   - da acceso sente i tasti in ogni momento: tra una pressione e l'altra
     riposa, ma da fuori non si vede. Dimenticato acceso, si spegne dopo 24 ore
     senza tasti
   - da spento mostra la **schermata di spento** (una scheda scelta nello
     strumento web, o il solo nome) con una fascia che dice che è spento; dopo
     tre giorni lo schermo passa al bianco. Dettagli e consumi in
     [`docs/batteria.md`](docs/batteria.md)

### Invio via Bluetooth

Lo strumento web può mandare le card all'X3 senza togliere la microSD, con il
Bluetooth LE del browser (Web Bluetooth).

1. Sull'X3 tieni premuto **X** per 0,8 s: compare **BLUETOOTH** con il nome
   del dispositivo (es. `CAMTAG-1A2B`).
2. Nello strumento web tocca **Bluetooth**, poi **Invia le card** e
   scegli il dispositivo.
3. A fine invio l'X3 mostra la prima card con **CARD AGGIORNATE**.

Le card arrivano in una cartella temporanea e sostituiscono quelle di prima
solo a invio completo e controllato: se il collegamento cade o qualcosa va
storto, sull'X3 restano le card di prima. **X** o **Y** escono dalla
ricezione; dopo 3 minuti senza invii esce da sola. Il Bluetooth è acceso
solo in ricezione.

Con le card arriva sull'X3 anche il **progetto** (`project.json`): lo stesso
file di "Salva progetto". Il lavoro nello strumento web resta solo nel browser,
che può cancellarlo (Safari lo fa dopo una settimana senza visite). Per
riprenderlo, metti l'X3 in ricezione e tocca **Bluetooth** → **Importa
dall'X3**: le card non cambiano. In alternativa apri `camtag/project.json`
della microSD con **Importa progetto**.

Requisiti:
- la pagina va aperta in **HTTPS** (o da `localhost`): su `http://` il browser
  non dà il Bluetooth alle pagine;
- **Android, Mac, Windows, Linux**: Chrome, Edge o Brave (provato su Mac; in
  Brave il Bluetooth va acceso in `brave://flags`, voce "Web Bluetooth API");
- **iPhone e iPad**: Safari non ha Web Bluetooth; serve un browser che lo
  aggiunga, come **BLE Link** (provato) o Bluefy.

Il protocollo è descritto in [`docs/bluetooth.md`](docs/bluetooth.md).

### Rotazione automatica

L'X3 ha un IMU QMI8658 (accelerometro + giroscopio). Il firmware legge la
gravità e gira la card nei 4 versi, con un po' di isteresi: il cambio avviene
dopo circa 0,6 s di posizione stabile. Se il dispositivo è appoggiato in piano,
mantiene il verso attuale.

Ogni card è esportata in due file, accoppiati per nome:

| File | Formato |
|---|---|
| `01_lens_35mm.bmp` | orizzontale, 792×528 |
| `01_lens_35mm.p.bmp` | verticale, 528×792 |

Il firmware sceglie la variante adatta e la ruota di 0° o 180°. Se una card ha
solo una delle due varianti, la usa anche nell'altro orientamento, ruotata.

La rotazione funziona mentre il dispositivo è acceso, anche a riposo. Da spento
resta la schermata di spento nell'ultimo verso.

Il blocco è salvato e sopravvive allo spegnimento. L'X4 non ha IMU: lì la
rotazione resta fissa.

## Strumento web

Online: **https://trunk-studio-production.github.io/camtag/** (GitHub Pages,
aggiornato a ogni push su `main`). È anche l'indirizzo da aprire sul telefono
per l'invio via Bluetooth.

Le schermate usano il font **Inter** (© The Inter Project Authors, licenza SIL
Open Font License 1.1), incluso in `companion/fonts/` insieme alla sua licenza.

Dalla radice del progetto (serve anche `data/`, con il database lenti):

```bash
python3 -m http.server 8766
```

Poi apri http://localhost:8766/companion/. I dati restano nel browser;
"Salva progetto" esporta un JSON da reimportare.

Schermate:
- **Lens chart** (automatica): cerchi nel database e aggiungi le lenti, una
  per una o tutta la serie; le lenti mancanti si inseriscono a mano. La lente
  "fuori dal coro" va in negativo; si può forzare per ogni lente.
- **Filtri** (automatica): kit ND e diottrie (i tasti +/− del dispositivo
  scorrono solo questi), formato ND, kit dei vetri (più gradazioni per famiglia in un colpo) e stadi del mattebox, da 1 a 4 o fino a 6 per un vetro montato fuori (vedi `docs/filtri.md`).
- **Schede manuali**: crew, note, lente singola, patch (immagine).

"Scarica tutto" crea `camtag.zip`: la cartella `camtag/` va copiata nella
radice della microSD.

**Simulatore** (pulsante sopra l'anteprima): l'X3 disegnato con tutti i tasti
cliccabili (anche da tastiera: A–D, X e Y, P = power). Le pagine si sfogliano
in ciclo con X (precedente) e Y (successiva); A e B non cambiano mai pagina. Si può ruotare, bloccare la
rotazione (Y tenuto premuto) e spegnere (power tenuto premuto), e si comporta
come è previsto per il firmware. Le schermate sono a risoluzione piena (792×528 e 528×792); il
simulatore le mostra a circa metà scala, vicino alla dimensione reale.

## Firmware

Compila:

```bash
cd device && ../.venv/bin/pio run -e x3
```

Test sull'host, senza X3:

```bash
device/test/filters_test.sh      # schermata filtri: uguale all'anteprima dello strumento web
```

`filters_test.sh` compone i pezzi esportati dallo strumento web come fa il
firmware e li confronta pixel per pixel con le anteprime, insieme alle regole
degli stadi. I dati stanno in `device/test/filters_fixture.zip`: se cambia
l'esportazione dei filtri va rigenerato con `device/test/filters_fixture.js`
(istruzioni nel file).

### Rischi e controlli prima di caricare

**Caricare CamTag può rendere l'X3 inutilizzabile.** Leggi questa parte per
intero prima di collegare il cavo.

#### Cosa può andare storto

- CamTag è un firmware non ufficiale, senza alcun legame con Xteink. Il
  caricamento sostituisce il firmware originale, il bootloader e la tabella
  partizioni, e può far decadere la garanzia del dispositivo.
- **CamTag non si aggiorna via rete.** L'unica strada per caricare altro, o per
  tornare indietro, è la USB. Se sul tuo X3 la USB non funziona, resti con
  quello che c'è sopra.
- **Alcune unità escono di fabbrica con il caricamento USB bloccato**, in
  particolare quelle vendute da negozi terzi e non da xteink.com (lo documenta
  [CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader#usb-locked-devices-xteink-unlocker)).
  Su un'unità bloccata un firmware non previsto dal loro strumento di sblocco
  può lasciare il dispositivo fermo per sempre, e CamTag non è tra quelli
  previsti. **Su un'unità bloccata non caricare CamTag.**
- Come funzioni quel blocco non lo sappiamo. I controlli qui sotto dicono con
  ragionevole sicurezza se un X3 è a rischio, ma non sono una garanzia.
- Finora CamTag è stato provato su un solo X3: comprato da un negozio terzo,
  con firmware originale V6.1.15, e non bloccato.

#### Controlli: se anche uno solo non passa, non caricare

Servono un cavo USB-C dati e l'X3 **acceso**: da spento non compare
sulla USB. `esptool` è quello che PlatformIO installa alla prima compilazione
(se manca: `.venv/bin/pip install esptool`, poi `.venv/bin/python -m esptool`).
Qui sotto `esptool` sta per `~/.platformio/penv/bin/python -m esptool --chip esp32c3`.

1. **L'X3 compare sulla USB.** Con il firmware originale in funzione:

   ```bash
   ls /dev/cu.usbmodem*        # macOS; su Linux /dev/ttyACM*
   ```

   Se non compare, prova un altro cavo e un'altra porta. Se continua a non
   comparire, è il segno di un'unità bloccata: fermati.

2. **Il chip non ha protezioni attive.**

   ```bash
   esptool get-security-info
   ```

   Devi leggere `Flags: 0x00000000`, `Secure Boot: Disabled` e
   `Flash Encryption: Disabled`, e prima `Stub flasher running`. Con valori
   diversi, o se esptool non riesce a collegarsi, fermati.

3. **Tutta la flash si legge, due volte uguale.** È anche il backup:

   ```bash
   esptool read-flash 0 0x1000000 backup-x3-originale.bin
   esptool read-flash 0 0x1000000 backup-x3-rilettura.bin
   shasum -a 256 backup-x3-originale.bin backup-x3-rilettura.bin
   ```

   Ogni lettura dura circa un minuto e deve dare 16777216 byte; le due impronte
   devono essere identiche. Se una lettura fallisce o le impronte differiscono,
   fermati.

Dopo ogni comando esptool riavvia l'X3, e il firmware originale torna in sonno:
premi il power e controlla che riparta come prima.

**Conserva `backup-x3-originale.bin` in almeno due posti.** È la copia esatta del
tuo dispositivo e l'unico modo per tornare allo stato di fabbrica. Contiene i
dati della tua unità: non pubblicarlo.

#### Caricare

Solo con i tre controlli passati e il backup al sicuro:

```bash
cd device && ../.venv/bin/pio run -e x3 -t upload && ../.venv/bin/pio device monitor
```

Tieni l'X3 sveglio e collegato finché il caricamento non ha finito.

### Ripristino

- Backup completo, che riporta l'X3 esattamente com'era:
  `~/.platformio/penv/bin/python -m esptool --chip esp32c3 write-flash 0 backup-x3-originale.bin`.
- Firmware originale Xteink o CrossPoint: dal flasher web su crosspointreader.com.
  La tabella partizioni di CamTag è la stessa di CrossPoint.

Entrambe le strade passano dalla USB, con l'X3 acceso (con CamTag: se è spento
tieni premuto il power; collegato a un computer resta sveglio). Se la USB non risponde più,
resta solo riscrivere la memoria con un programmatore esterno aprendo il
dispositivo, come descrive
[CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader/blob/HEAD/docs/fix-bricked-xteink.md):
un'operazione delicata, che può finire di romperlo.

## Da verificare sull'hardware

Le prove si fanno dal terminale con `scripts/taratura.py` (firmware compilato
con `CAMTAG_DEBUG`), che mostra subito ogni tasto premuto, il verso e la card a
schermo, e aspetta l'X3 se si spegne:

```bash
.venv/bin/python scripts/taratura.py tasti
.venv/bin/python scripts/taratura.py versi --carica   # compila e carica prima
.venv/bin/python scripts/taratura.py card             # con le card sopra
.venv/bin/python scripts/taratura.py filtri           # con la schermata filtri sopra
.venv/bin/python scripts/taratura.py log              # registro dal vivo
.venv/bin/python scripts/taratura.py batteria         # consumi misurati (docs/batteria.md)
```

- [x] Rilevamento dell'X3, display e microSD
- [x] Mappatura dei tasti (`kKeyA`…`kKeyD`, `kKeyX`, `kKeyY` in `device/src/main.cpp`)
- [x] Assi IMU e verso dello schermo (`kImuSwapXY`, `kImuSignX`, `kImuSignY`,
      `kQuarterTurns`): la scritta resta dritta nei 4 versi
- [x] Riaccensione con il power
- [x] Acceso sempre in ascolto (riposo in sonno leggero), tasti bloccati con
      il power breve, spento e riaccensione con il power tenuto, schermata di
      spento e passaggio al bianco
- [ ] Consumi misurati con `taratura.py batteria`: a riposo, a riposo con i
      tasti bloccati, nei primi tre giorni da spento (prime letture: circa
      25 mA da sveglio, 2 mA a riposo)
- [x] Ricezione via Bluetooth da Brave sul Mac: crea `/camtag` anche la prima
      volta e mostra quanti file ha ricevuto; "Importa dall'X3" riporta il
      progetto nel browser e lascia le card come sono
- [x] Card vere in verticale e in orizzontale, X/Y in ciclo, rotazione
      automatica e blocco (Y tenuto)
- [x] Bilancieri sulla lens chart di più pagine nei 4 versi (come
      `lensPageDir` nel simulatore)
- [x] Velocità di ricezione dal Mac: 98 file, 1,1 MB in circa 40 s (erano
      100 s) chiedendo l'intervallo da 15 ms subito dopo lo scambio dell'MTU;
      mouse e tastiera Bluetooth in uso sul Mac rallentano
- [x] Pannello spento dopo ogni aggiornamento: anche con l'X3 sveglio a lungo
      non riaffiora la schermata precedente
- [ ] Soglie della rotazione (`kMinTiltG`, `kFlatG`, `ROTATE_STABLE_MS`) su camera vera
- [ ] Ghosting: ogni quanto serve un refresh completo (ora ogni 6 cambi)
- [ ] Consumo da spento
- [ ] Lettura batteria (fuel gauge BQ27220): l'icona appare sotto il 20%
- [ ] Ricezione via Bluetooth da Android (Chrome) e iPhone (BLE Link), consumo
      con il Bluetooth acceso
- [x] Schermata filtri (`taratura.py filtri`): legge le card dello strumento
      così come sono; ND e diottria con i bilancieri e indicazioni dei tasti nei
      4 versi, riordino degli stadi dopo 2 s, modifica degli stadi (entrata,
      scelta di stadio e vetro, uscita da sola dopo 5 s e con Y); il mezzo
      refresh dopo 2 s fermi toglie l'ombra dei cambi veloci di ND e diottria
- [x] Rotazione bloccata che ferma tutto: girando l'X3 schermata filtri,
      indicazioni e bilancieri restano quelli del verso bloccato
- [x] Nessun tasto perso: prima cadevano tutti durante un aggiornamento dello
      schermo (`taratura.py reattivita`); ora un compito li legge anche lì e li
      mette in coda, e si eseguono appena lo schermo ha finito

## Prossimi passi

1. **Consumi misurati** con il registro sulla microSD (`docs/batteria.md`), e
   da lì le scelte ancora aperte: quanto pesa la microSD alimentata a riposo,
   tempi del riposo, del bianco da spento
   (`OFF_BLANK_S`) e dello spegnimento da dimenticato. Serve anche una
   configurazione di compilazione senza `CAMTAG_DEBUG`: oggi `x3` lo ha sempre
   acceso.
2. **Cura dello schermo** (`docs/schermo.md`): confermare con ore d'uso che il
   negativo a schermo fermo non torna, capire il riavvio da solo dopo il riposo
   e il riposo che scatta con la USB, tarare ogni quanto serve il refresh
   completo (`FULL_REFRESH_EVERY`). Caldo oltre i 50 °C circa e sole diretto
   per mesi rovinano il pannello: conta il supporto.
3. **Catalogo dei filtri** (`data/filters.json`) verificato sulle schede dei
   costruttori: oggi è provvisorio.
4. **Supporto** stampato in 3D per il montaggio sulla camera.

## Licenza

CamTag (firmware e strumento web) è software libero, distribuito con la
licenza **GNU GPL v3.0**: testo completo in [`LICENSE`](LICENSE). Puoi usarlo,
studiarlo, modificarlo e ridistribuirlo; chi distribuisce una versione
modificata deve renderne disponibile il codice con la stessa licenza.

**Nessuna garanzia.** Come dicono gli articoli 15 e 16 della licenza, il
software è fornito "così com'è": chi lo usa lo fa a proprio rischio, e gli
autori non rispondono di danni a dispositivi, dati o altro, compreso un X3 reso
inutilizzabile dal caricamento del firmware.

Restano con le loro licenze:
- FreeInk SDK (`third_party/freeink-sdk/`): MIT;
- font Inter (`companion/fonts/`): SIL Open Font License 1.1.
