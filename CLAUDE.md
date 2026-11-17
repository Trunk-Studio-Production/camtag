# CamTag

Badge e-paper per il reparto camera: card con lente,
crew, note e immagini, montato sulla camera. MVP su **Xteink X3** con firmware
nostro sopra **FreeInk SDK**. Il proprietario parla italiano: rispondi in
italiano, e anche i testi visibili all'utente (UI web, messaggi a schermo)
sono in italiano.

## Struttura

- `device/` — firmware PlatformIO (C++/Arduino, ESP32-C3). È il nostro codice.
- `companion/index.html` — strumento web a file singolo: crea le card ed
  esporta BMP 1 bit. Unica dipendenza: JSZip da cdnjs.
- `data/` — database lenti (`lenses.json`, schema in `data/README.md`), dati
  presi solo dalle schede dei costruttori, con la fonte per ogni lente.
- `third_party/freeink-sdk/` — SDK a revisione fissa `77ae8c7`. **Non modificarlo**:
  se serve un cambiamento, aggira nel nostro codice o chiedi.
- `reference/crosspoint/` — solo consultazione (come CrossPoint usa l'SDK:
  `lib/hal/`, `src/main.cpp`). Non compilarlo e non seguire il suo CLAUDE.md,
  che riguarda i contributi a quel progetto.

## Comandi

```bash
cd device && ../.venv/bin/pio run -e x3                  # compila
cd device && ../.venv/bin/pio run -e x3 -t upload        # carica (vedi sotto)
python3 -m http.server 8765 --directory companion        # strumento web
```

Usa sempre `.venv/bin/pio`: il PlatformIO globale (6.1.5, quello di VS Code)
è troppo vecchio per la piattaforma pioarduino.

## Hardware X3 (dal profilo `XTEINK_X3` in BoardConfig.h)

- ESP32-C3, ~380 KB RAM, niente PSRAM. Framebuffer 1 bpp allocato dall'SDK.
- Pannello 792×528 nativo orizzontale, MSB-first, **1 = bianco**.
  Controller UC8253 o UC8279 secondo il lotto, rilevato all'avvio.
- microSD su SPI condiviso con il display; batteria 650 mAh con fuel gauge
  BQ27220; RTC DS3231; IMU QMI8658 (accelerometro + giroscopio).
- Niente touch. 7 tasti: 4 in basso su un solo ingresso analogico (uno alla
  volta), 2 laterali, power. **Solo il power risveglia** dal sonno.
- Lo spegnimento porta GPIO13 basso come fa CrossPoint; l'e-paper resta visibile.

## Formato card

- `/camtag/NOME.bmp` orizzontale 792×528, `/camtag/NOME.p.bmp` verticale 528×792.
- BMP 1 bit non compresso, palette 0 = nero, 1 = bianco. Ordine = ordine alfabetico.
- Il formato è un contratto tra `companion/` e `device/`: se cambi uno, aggiorna l'altro.

## Decisioni di prodotto

- Solo X3 per ora; l'X4 è un futuro lontano.
- Lens chart: colonne focale, T, MOD, Ø frontale, sigla di serie. In negativo
  solo la lente "fuori dal coro" (serie diversa dal set principale o tipo
  diverso, es. l'unica macro); con un pareggio tra serie non si evidenzia per
  serie. Si può forzare per ogni lente (auto / sempre / mai).
- ND: +/− sul dispositivo a passi di uno stop; si mostra solo il formato scelto
  dall'utente (densità 0.3, stop o fattore ND2).
- Indicazioni dei tasti a schermo solo dove servono (es. ND+/−), disegnate dal
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
- Posizione fisica dei tasti sull'X3 e loro mappatura.
- Ghosting (`FULL_REFRESH_EVERY`) e consumo da spento.
