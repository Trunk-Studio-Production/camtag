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
   - **Bluetooth**: sull'X3 tieni premuto **F** finché compare BLUETOOTH, poi
     nello strumento web tocca **Bluetooth** e poi **Invia le card** (vedi sotto);
   - **microSD**: scarichi lo zip e copi la sua cartella `camtag/` nella radice
     della microSD.
3. Sul dispositivo:
   - **E** (lato destro): card successiva; **F** (lato sinistro): precedente,
     in ciclo
   - **E tenuto premuto (0,8 s)**: blocca/sblocca la rotazione
   - **F tenuto premuto (0,8 s)**: ricezione delle card via Bluetooth
   - le pressioni lunghe scattano mentre tieni premuto, allo scadere del
     tempo: non serve rilasciare per vedere l'effetto
   - A, B, C, D non cambiano card
   - **power**: spegne subito
   - dopo 20 s senza tasti si spegne da solo; l'e-paper continua a mostrare la card
   - il power lo riaccende sull'ultima card; ridisegna solo se nel frattempo
     l'hai girato

### Invio via Bluetooth

Lo strumento web può mandare le card all'X3 senza togliere la microSD, con il
Bluetooth LE del browser (Web Bluetooth).

1. Sull'X3 tieni premuto **F** per 0,8 s: compare **BLUETOOTH** con il nome
   del dispositivo (es. `CAMTAG-1A2B`).
2. Nello strumento web tocca **Bluetooth**, poi **Invia le card** e
   scegli il dispositivo.
3. A fine invio l'X3 mostra la prima card con **CARD AGGIORNATE**.

Le card arrivano in una cartella temporanea e sostituiscono quelle di prima
solo a invio completo e controllato: se il collegamento cade o qualcosa va
storto, sull'X3 restano le card di prima. **E** o **F** escono dalla
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
- **Android, Mac, Windows, Linux**: Chrome o Edge;
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

La rotazione funziona mentre il dispositivo è acceso. Da spento l'immagine
resta quella dell'ultimo verso: girandolo da spento, si aggiorna alla prossima
pressione del power.

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
cliccabili (anche da tastiera: A–F, P = power). Le pagine si sfogliano
in ciclo con E (successiva) e F (precedente); A e B non cambiano mai pagina. Si può ruotare, bloccare la
rotazione (E tenuto premuto) e spegnere, e si comporta come è previsto per il
firmware. Le schermate sono a risoluzione piena (792×528 e 528×792); il
simulatore le mostra a circa metà scala, vicino alla dimensione reale.

## Firmware

Compila:

```bash
cd device && ../.venv/bin/pio run -e x3
```

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

Servono un cavo USB-C dati e l'X3 **acceso e sveglio**: in sonno non compare
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

Entrambe le strade passano dalla USB, con l'X3 sveglio (con CamTag: premi il
power, hai 20 secondi prima che torni in sonno). Se la USB non risponde più,
resta solo riscrivere la memoria con un programmatore esterno aprendo il
dispositivo, come descrive
[CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader/blob/HEAD/docs/fix-bricked-xteink.md):
un'operazione delicata, che può finire di romperlo.

## Da verificare sull'hardware

Tasti e versi si provano dal terminale con `scripts/taratura.py`, che mostra
subito ogni tasto premuto e i valori dell'accelerometro:

```bash
.venv/bin/python scripts/taratura.py tasti
.venv/bin/python scripts/taratura.py versi --carica   # compila e carica prima
```

- [x] Rilevamento dell'X3, display e microSD
- [x] Mappatura dei tasti (`kKeyA`…`kKeyF` in `device/src/main.cpp`)
- [x] Assi IMU e verso dello schermo (`kImuSwapXY`, `kImuSignX`, `kImuSignY`,
      `kQuarterTurns`): la scritta resta dritta nei 4 versi
- [x] Riaccensione con il power
- [ ] Soglie della rotazione (`kMinTiltG`, `kFlatG`, `ROTATE_STABLE_MS`) su camera vera
- [ ] Ghosting: ogni quanto serve un refresh completo (ora ogni 6 cambi)
- [ ] Consumo da spento
- [ ] Lettura batteria (fuel gauge BQ27220): l'icona appare sotto il 20%
- [ ] Ricezione via Bluetooth: velocità reale, Android (Chrome) e iPhone
      (BLE Link), consumo con il Bluetooth acceso

## Prossimi passi

1. **Database lenti** nello strumento web.
2. **Rotazione anche da spento**: risveglio a timer (es. ogni 10 s) per leggere
   l'IMU e ridisegnare solo se è cambiato il verso. Da valutare dopo aver
   misurato i consumi reali.
3. **Supporto** stampato in 3D per il montaggio sulla camera.

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
