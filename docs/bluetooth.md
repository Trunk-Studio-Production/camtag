# Protocollo Bluetooth (v3)

Contratto tra `companion/index.html` (funzioni `bleSend` e `bleGet`) e il firmware
(`device/src/BleUpload.cpp`, `UploadProto.h`). Se cambi uno, aggiorna l'altro e
alza la versione.

## Servizio GATT

| | UUID | Proprietà |
|---|---|---|
| Servizio | `c4a70001-1b2e-4f3a-9d6e-3a7c5e0b2f10` | |
| CTRL | `c4a70002-1b2e-4f3a-9d6e-3a7c5e0b2f10` | write, write without response, notify |
| DATA | `c4a70003-1b2e-4f3a-9d6e-3a7c5e0b2f10` | write without response, write, notify |

L'X3 fa advertising solo in modalità ricezione (X tenuto premuto): l'UUID del
servizio sta nel pacchetto principale, il nome `CAMTAG-XXXX` (ultimi due byte
del MAC) nella risposta alla scansione.

## Messaggi

Comandi e risposte sono righe di testo ASCII sulla caratteristica CTRL: il
client scrive, il dispositivo risponde con una notify.

| Client → X3 | Risposta | Note |
|---|---|---|
| `HELLO` | `OK CAMTAG <versione> MTU <mtu> FW <firmware>` | il client usa pacchetti di MTU − 3 byte (da 20 a 512); `FW` (es. `0.1.0`, `CAMTAG_FW_VERSION` in `device/src/Version.h`) manca nei firmware precedenti alla 0.1.0 |
| `BEGIN` | `OK` | svuota e crea `/camtag.new` |
| `KEEP <byte> <crc32 hex> <percorso>` | `DONE <byte>` o `NO` | dalla v3: se `/camtag/<percorso>` ha quella dimensione e quel CRC lo copia in `/camtag.new` (`DONE`), se no `NO` e il client lo manda con `FILE` |
| `FILE <byte> <crc32 hex> <percorso>` | `OK`, o subito `DONE 0` per un file vuoto | apre il file in `/camtag.new` |
| *(dati su DATA)* | `ACK <byte>` ogni 2 KB, `DONE <byte>` alla fine | `DONE` solo se il CRC torna |
| `COMMIT` | `OK` | `/camtag.new` prende il posto di `/camtag` |
| `ABORT` | `OK` | cancella `/camtag.new`; `/camtag` resta com'era |
| `GET <percorso>` | `FILE <byte> <crc32 hex> <percorso>`, poi i dati con notify su DATA | solo fuori da una sessione; `ERR ASSENTE` se il file non c'è |
| `ACK <byte>` | — | durante un `GET`: byte ricevuti dal client |

La versione del firmware non cambia il protocollo: con la stessa versione del
protocollo (o una di `BLE.oldVersions`) firmware e strumento sono compatibili, e lo strumento mostra la
versione dell'X3 senza avvisi (con un firmware più vecchio dice anche qual è
la più recente). Solo un protocollo diverso è un conflitto: l'invio si ferma
e chiede di aggiornarli insieme.

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

Gli `ACK` del client durante un `GET` vanno scritti **senza risposta**: con la
scrittura con risposta, mentre il dispositivo invia, la risposta può mancare e
il browser dà "GATT operation failed for unknown reason" (Chrome sul Mac, con un
`project.json` da 100 KB). Il client ripete comunque un `ACK` fallito (fino a 3
volte): dice il totale ricevuto, quindi ripeterlo non cambia nulla. Con un
firmware senza "write without response" su CTRL il client torna alla scrittura
con risposta.

## Solo le pagine cambiate

Ogni invio è completo (le pagine tolte nello strumento spariscono dall'X3), ma
viaggiano solo i file cambiati: per ogni file il client chiede prima `KEEP`, e
manda i dati con `FILE` solo dopo un `NO`. L'X3 copia sulla microSD i file
uguali dalle card attuali, controllando il CRC mentre copia (se non torna
cancella la copia e risponde `NO`); `/camtag` non si tocca fino al `COMMIT`.
Niente `KEEP` per i file vuoti e per `project.json`, che cambia a ogni invio
(`savedAt`).

Perché funzioni, la stessa card deve uscire uguale byte per byte da
un'esportazione all'altra: lo strumento disegna su canvas in software
(`willReadFrequently` in `newCanvas`), perché Chromium passerebbe dalla GPU al
software a metà e l'antialiasing diverso sposterebbe qualche pixel dopo la soglia.

Con un X3 a protocollo 2 lo strumento non usa `KEEP` e manda tutto: è l'unica
versione precedente che accetta (`BLE.oldVersions`).

## Sostituzione delle card

Al `COMMIT`: `/camtag` diventa `/camtag.old`, `/camtag.new` diventa
`/camtag`, poi `/camtag.old` si cancella. Se il secondo passo fallisce,
`/camtag.old` torna al suo posto.

## Test

- Host, senza hardware: `c++ -std=c++17 -I device/src device/test/upload_proto_test.cpp -o /tmp/upt && /tmp/upt`
  (CRC, percorsi, lettura dei comandi, `KEEP`, `GET` e `ACK`).
- Lato client il flusso si prova nel browser sostituendo
  `navigator.bluetooth.requestDevice` con un dispositivo finto che segue questa
  tabella.
