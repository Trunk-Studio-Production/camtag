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

1. Nello strumento web crei le card e scarichi lo zip.
2. Copi la cartella `camtag/` dello zip nella radice della microSD dell'X3.
3. Sul dispositivo:
   - **destra / giù / conferma (breve)**: card successiva
   - **sinistra / su / indietro**: card precedente
   - **conferma tenuto premuto (0,8 s)**: blocca/sblocca la rotazione
   - **power**: spegne subito
   - dopo 20 s senza tasti si spegne da solo; l'e-paper continua a mostrare la card
   - il power lo riaccende sull'ultima card; ridisegna solo se nel frattempo
     l'hai girato

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
cliccabili (anche da tastiera: A–F, P = power). Si può ruotare, bloccare la
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
- [ ] Mappatura dei tasti (quale fisico corrisponde a "destra", "su"…)
- [ ] **Taratura assi IMU**: con il monitor seriale aperto stampa `ax/ay/az`
      ogni secondo. Regola `kImuSwapXY`, `kImuSignX` e `kImuSignY` in
      `device/src/main.cpp` finché i 4 versi corrispondono
- [ ] Soglie della rotazione (`kMinTiltG`, `kFlatG`, `ROTATE_STABLE_MS`) su camera vera
- [ ] Ghosting: ogni quanto serve un refresh completo (ora ogni 6 cambi)
- [ ] Riaccensione con il power e consumo da spento
- [ ] Lettura batteria (fuel gauge BQ27220): l'icona appare sotto il 20%

## Prossimi passi

1. **Caricamento senza microSD**: Wi-Fi in modalità access point con pagina di upload,
   oppure Bluetooth LE dallo strumento web (Web Bluetooth).
2. **Database lenti** nello strumento web.
3. **Rotazione anche da spento**: risveglio a timer (es. ogni 10 s) per leggere
   l'IMU e ridisegnare solo se è cambiato il verso. Da valutare dopo aver
   misurato i consumi reali.
4. **Supporto** stampato in 3D per il montaggio sulla camera.
