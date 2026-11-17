# Protocollo Bluetooth (v1)

Contratto tra `companion/index.html` (funzione `bleSend`) e il firmware
(`device/src/BleUpload.cpp`, `UploadProto.h`). Se cambi uno, aggiorna l'altro e
alza la versione.

## Servizio GATT

| | UUID | Proprietà |
|---|---|---|
| Servizio | `c4a70001-1b2e-4f3a-9d6e-3a7c5e0b2f10` | |
| CTRL | `c4a70002-1b2e-4f3a-9d6e-3a7c5e0b2f10` | write, notify |
| DATA | `c4a70003-1b2e-4f3a-9d6e-3a7c5e0b2f10` | write without response, write |

L'X3 fa advertising solo in modalità ricezione (F tenuto premuto): l'UUID del
servizio sta nel pacchetto principale, il nome `CAMTAG-XXXX` (ultimi due byte
del MAC) nella risposta alla scansione.

## Messaggi

Comandi e risposte sono righe di testo ASCII sulla caratteristica CTRL: il
client scrive, il dispositivo risponde con una notify.

| Client → X3 | Risposta | Note |
|---|---|---|
| `HELLO` | `OK CAMTAG <versione> MTU <mtu>` | il client usa pacchetti di MTU − 3 byte (da 20 a 512) |
| `BEGIN` | `OK` | svuota e crea `/camtag.new` |
| `FILE <byte> <crc32 hex> <percorso>` | `OK`, o subito `DONE 0` per un file vuoto | apre il file in `/camtag.new` |
| *(dati su DATA)* | `ACK <byte>` ogni 2 KB, `DONE <byte>` alla fine | `DONE` solo se il CRC torna |
| `COMMIT` | `OK` | `/camtag.new` prende il posto di `/camtag` |
| `ABORT` | `OK` | cancella `/camtag.new`; `/camtag` resta com'era |

Errori: `ERR <motivo>`, con motivo `CRC`, `SD`, `OVERFLOW`, `STATO`, `DATI` o
`COMANDO`. Dopo un errore il dispositivo cancella `/camtag.new` e torna in
attesa di `BEGIN`. Se il collegamento cade a metà, succede lo stesso.

- **Percorso**: relativo a `/camtag`, segmenti di `[A-Za-z0-9._-]` separati da
  `/`, al massimo 96 caratteri; niente segmenti che iniziano con `.`. Le
  sottocartelle (es. `03_filters/`) si creano da sole.
- **CRC**: CRC-32 standard (zlib), sul contenuto del file.
- **Dimensione massima** di un file: 2 MB.

## Controllo di flusso

I callback del Bluetooth mettono i dati in un buffer da 16 KB; il loop del
firmware li scrive sulla microSD (che condivide l'SPI con il display) e solo
dopo manda `ACK`. Il client tiene in volo al massimo **8 KB** non confermati
(byte inviati del file − ultimo `ACK`), così il buffer non si riempie anche
quando il display si sta aggiornando. Se si riempie comunque: `ERR OVERFLOW`.

## Sostituzione delle card

Al `COMMIT`: `/camtag` diventa `/camtag.old`, `/camtag.new` diventa
`/camtag`, poi `/camtag.old` si cancella. Se il secondo passo fallisce,
`/camtag.old` torna al suo posto.

## Test

- Host, senza hardware: `c++ -std=c++17 -I device/src device/test/upload_proto_test.cpp -o /tmp/upt && /tmp/upt`
  (CRC, percorsi, lettura dei comandi).
- Lato client il flusso si prova nel browser sostituendo
  `navigator.bluetooth.requestDevice` con un dispositivo finto che segue questa
  tabella.
