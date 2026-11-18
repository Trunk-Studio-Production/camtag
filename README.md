# CamTag — badge e-paper per il reparto camera (MVP su Xteink X3)

Un badge e-paper per il reparto camera: mostra card con lente, crew, note e
immagini, si sfoglia con i tasti e resta visibile anche da spento.

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
  scorrono solo questi), formato ND, kit dei filtri d'effetto (più gradazioni per famiglia in un colpo; sul dispositivo solo quelle segnate come montate).
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

### Prima di caricarlo sull'X3

1. **Fai un backup completo della flash originale.** Collega l'X3 via USB-C e:

   ```bash
   .venv/bin/python -m esptool --chip esp32c3 read_flash 0 0x1000000 backup-x3-originale.bin
   ```

   Conserva questo file: è il modo più sicuro per tornare allo stato di fabbrica.
   (`esptool` è incluso in PlatformIO; se manca: `.venv/bin/pip install esptool`.)
2. **Verifica che il caricamento via USB non sia bloccato** (unità da negozi terzi).
   Se il backup fallisce o il dispositivo non viene visto, fermati: non forzare.
3. Solo dopo, carica:

   ```bash
   cd device && ../.venv/bin/pio run -e x3 -t upload && ../.venv/bin/pio device monitor
   ```

### Ripristino

- Firmware originale Xteink o CrossPoint: dal flasher web su crosspointreader.com.
- Backup completo: `esptool --chip esp32c3 write_flash 0 backup-x3-originale.bin`.

La tabella partizioni è la stessa di CrossPoint, quindi i suoi strumenti di
ripristino restano compatibili.

## Da verificare sull'hardware

- [ ] Rilevamento X3 e controller del display (UC8253 o UC8279)
- [ ] Mappatura dei tasti: E ed F sono i tasti logici UP/DOWN dell'SDK, ma
      quale sia quale va verificato (`kKeyE`, `kKeyF` in `device/src/main.cpp`;
      con il monitor seriale aperto ogni pressione stampa il tasto logico)
- [ ] **Taratura assi IMU**: con il monitor seriale aperto stampa `ax/ay/az`
      ogni secondo. Regola `kImuSwapXY`, `kImuSignX` e `kImuSignY` in
      `device/src/main.cpp` finché i 4 versi corrispondono
- [ ] Soglie della rotazione (`kMinTiltG`, `kFlatG`, `ROTATE_STABLE_MS`) su camera vera
- [ ] Ghosting: ogni quanto serve un refresh completo (ora ogni 6 cambi)
- [ ] Riaccensione con il power e consumo da spento
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

Restano con le loro licenze:
- FreeInk SDK (`third_party/freeink-sdk/`): MIT;
- font Inter (`companion/fonts/`): SIL Open Font License 1.1.
