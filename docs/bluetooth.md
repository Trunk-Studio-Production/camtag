# Protocollo Bluetooth (v2)

Contratto tra `companion/index.html` (funzioni `bleSend` e `bleGet`) e il firmware
(`device/src/BleUpload.cpp`, `UploadProto.h`). Se cambi uno, aggiorna l'altro e
alza la versione.

## Servizio GATT

| | UUID | Proprietà |
|---|---|---|
| Servizio | `c4a70001-1b2e-4f3a-9d6e-3a7c5e0b2f10` | |
| CTRL | `c4a70002-1b2e-4f3a-9d6e-3a7c5e0b2f10` | write, notify |
| DATA | `c4a70003-1b2e-4f3a-9d6e-3a7c5e0b2f10` | write without response, write, notify |

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
| `GET <percorso>` | `FILE <byte> <crc32 hex> <percorso>`, poi i dati con notify su DATA | solo fuori da una sessione; `ERR ASSENTE` se il file non c'è |
| `ACK <byte>` | — | durante un `GET`: byte ricevuti dal client |

Errori: `ERR <motivo>`, con motivo `CRC`, `SD`, `OVERFLOW`, `STATO`, `DATI`,
`COMANDO` o `ASSENTE` (solo per `GET`, non interrompe nulla). Dopo un errore il dispositivo cancella `/camtag.new` e torna in
attesa di `BEGIN`. Se il collegamento cade a metà, succede lo stesso.

- **Percorso**: relativo a `/camtag`, segmenti di `[A-Za-z0-9._-]` separati da
  `/`, al massimo 96 caratteri; niente segmenti che iniziano con `.`. Le
  sottocartelle (es. `03_filters/`) si creano da sole.
- **CRC**: CRC-32 standard (zlib), sul contenuto del file.
- **Dimensione massima** di un file: 2 MB.

## File di progetto

Insieme alle card lo strumento manda `project.json`: lo stesso contenuto di
"Salva progetto" (schermate, lenti, kit, immagini, modifiche alle lenti usate)
più `savedAt`, la data dell'invio. Sta in `/camtag` e quindi cambia solo al
`COMMIT`, insieme alle card che descrive. Il firmware lo ignora (legge solo i
`.bmp`); "Importa dall'X3" lo rilegge con `GET project.json` e, dopo una
conferma, sostituisce il progetto del browser. È anche nello zip: dalla microSD
si apre con "Importa progetto".

## Controllo di flusso

I callback del Bluetooth mettono i dati in un buffer da 16 KB; il loop del
firmware li scrive sulla microSD (che condivide l'SPI con il display) e solo
dopo manda `ACK`. Il client tiene in volo al massimo **8 KB** non confermati
(byte inviati del file − ultimo `ACK`), così il buffer non si riempie anche
quando il display si sta aggiornando. Se si riempie comunque: `ERR OVERFLOW`.
Non alzare la finestra: con 24 KB il browser sul Mac intasa la sua coda di
scritture senza risposta, e l'invio rallenta fino a 1 KB/s o resta appeso.

Il browser manda un pacchetto per intervallo del collegamento: la velocità
dipende quasi solo da quello. Il dispositivo chiede 15 ms esatti (le regole
Apple non danno di meno; con "15-30 ms" il Mac sceglie 30) subito dopo lo
scambio dell'MTU: appena collegato si scontrerebbe con la regolazione del
telefono o del computer e andrebbe persa, a metà dei dati bloccherebbe le
scritture del browser. Dal Mac con Brave (Chromium): 21-29 KB/s, 1,1 MB in 40-55 s. Altri
dispositivi Bluetooth in uso sul Mac (mouse, tastiera) rallentano.

Con `GET` i ruoli si scambiano: il dispositivo manda pacchetti di MTU − 3 byte
(al massimo 512) e non tiene più di 8 KB non confermati; il client risponde
`ACK <byte ricevuti>` ogni 2 KB e alla fine, poi controlla il CRC. Quando l'ACK
arriva al totale il dispositivo chiude il file. Le card non cambiano e il
dispositivo resta in ricezione.

## Sostituzione delle card

Al `COMMIT`: `/camtag` diventa `/camtag.old`, `/camtag.new` diventa
`/camtag`, poi `/camtag.old` si cancella. Se il secondo passo fallisce,
`/camtag.old` torna al suo posto.

## Test

- Host, senza hardware: `c++ -std=c++17 -I device/src device/test/upload_proto_test.cpp -o /tmp/upt && /tmp/upt`
  (CRC, percorsi, lettura dei comandi, `GET` e `ACK`).
- Lato client il flusso si prova nel browser sostituendo
  `navigator.bluetooth.requestDevice` con un dispositivo finto che segue questa
  tabella.
