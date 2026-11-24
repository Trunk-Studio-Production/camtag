# Protocollo Bluetooth (v4)

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
| `GET <percorso>` | `FILE <byte> <crc32 hex> <percorso>`, poi i dati con notify su DATA | solo fuori da una sessione; `ERR ASSENTE` se il file non c'è. Il nome riservato `camtag-batteria.csv` (dal firmware 0.3.1) dà il registro dei consumi, che sta fuori da `/camtag` |
| `ACK <byte>` | — | durante un `GET`: byte ricevuti dal client |
| `FIRMWARE <byte> <crc32 hex> <versione>` | `ASK`, poi `OK` o `ERR RIFIUTATO`/`ERR BATTERIA` | dalla v4, solo fuori da una sessione: nuovo firmware (vedi sotto) |
| *(firmware su DATA)* | `ACK <byte>` ogni 2 KB, `DONE <byte>` alla fine | `DONE` solo se CRC e immagine tornano; poi l'X3 riparte |

La versione del firmware non cambia il protocollo: con la stessa versione del
protocollo (o una di `BLE.oldVersions`) firmware e configuratore web sono compatibili, e il configuratore web mostra la
versione dell'X3 senza avvisi (con un firmware più vecchio dice anche qual è
la più recente). Solo un protocollo diverso è un conflitto: l'invio si ferma
e chiede di aggiornarli insieme.

Errori: `ERR <motivo>`, con motivo `CRC`, `SD`, `OVERFLOW`, `STATO`, `DATI`,
`COMANDO`, `ASSENTE` (solo per `GET`, non interrompe nulla), e per `FIRMWARE`
`RIFIUTATO`, `BATTERIA`, `FLASH` o `IMMAGINE`. Dopo un errore il dispositivo cancella `/camtag.new` e torna in
attesa di `BEGIN`. Se il collegamento cade a metà, succede lo stesso.

- **Percorso**: relativo a `/camtag`, segmenti di `[A-Za-z0-9._-]` separati da
  `/`, al massimo 96 caratteri; niente segmenti che iniziano con `.`. Le
  sottocartelle (es. `03_filters/`) si creano da sole.
- **CRC**: CRC-32 standard (zlib), sul contenuto del file.
- **Dimensione massima** di un file: 2 MB.

## File di progetto

Insieme alle card il configuratore web manda `project.json`: lo stesso contenuto di
"Salva progetto" (schermate, lenti, kit, immagini, modifiche alle lenti usate)
più `savedAt`, la data dell'invio. Sta in `/camtag` e quindi cambia solo al
`COMMIT`, insieme alle card che descrive. Il firmware lo ignora (legge solo i
`.bmp`); "Importa dall'X3" lo rilegge con `GET project.json` e, dopo una
conferma, sostituisce il progetto del browser; se è uguale a quello del browser
a meno di `savedAt` (`sameProject`) lo dice e non chiede nulla. È anche nello zip: dalla microSD
si apre con "Importa progetto".

## Registro della batteria

"Registro batteria" nella finestra Bluetooth del configuratore web (solo in
modalità sviluppatore, `?dev`) chiede
`GET camtag-batteria.csv`: l'X3 manda `/camtag-batteria.csv` dalla radice della
microSD (docs/batteria.md) e il browser lo salva così com'è. Non lo cancella.
Un firmware precedente alla 0.3.1 risponde `ERR ASSENTE`, come un X3 senza registro.

## Controllo di flusso

I callback del Bluetooth mettono i dati in un buffer da 16 KB; il loop del
firmware li scrive sulla microSD (che condivide l'SPI con il display) e solo
dopo manda `ACK`. Il client tiene in volo al massimo **8 KB** non confermati
(byte inviati del file − ultimo `ACK`), così il buffer non si riempie anche
quando il display si sta aggiornando. Se si riempie comunque: `ERR OVERFLOW`.
Non alzare la finestra: con 24 KB il browser sul Mac intasa la sua coda di
scritture senza risposta, e l'invio rallenta fino a 1 KB/s o resta appeso.

Ogni pacchetto di dati è un `Uint8Array` con un buffer suo (`slice`, non
`subarray`). Bluefy su iPhone, con una vista su un buffer più grande, passa a
iOS tutto il buffer sotto (l'intero file) e non parte nulla: senza risposta la
scrittura risulta riuscita ma all'X3 non arriva un byte, con la risposta resta
appesa, mentre i comandi, che hanno un buffer loro, arrivano (registro dell'X3,
22/11/2026). Con `slice` va senza risposta a MTU − 3 byte come gli altri
browser (7 file e `project.json`, circa 75 KB/s per file).

Browser per iPhone provati (22/11/2026), invio, "Importa dall'X3" e
aggiornamento del firmware: Bluefy e BLE Link, utilizzabili tutti e due. BLE Link non ha `properties` sulle caratteristiche (l'`ACK` del `GET`
si scrive allora con la risposta); in Bluefy, dopo l'OK a `confirm()`, il progetto
non cambiava, con la domanda nella finestra Bluetooth (`bleAsk`) sì.

Il browser manda un pacchetto per intervallo del collegamento: la velocità
dipende quasi solo da quello. Il dispositivo chiede 15 ms esatti (le regole
Apple non danno di meno; con "15-30 ms" il Mac sceglie 30) subito dopo lo
scambio dell'MTU: appena collegato si scontrerebbe con la regolazione del
telefono o del computer e andrebbe persa, a metà dei dati bloccherebbe le
scritture del browser. Dal Mac con Brave (Chromium): 21-29 KB/s, 1,1 MB in 40-55 s. Altri
dispositivi Bluetooth in uso sul Mac (mouse, tastiera) rallentano.

**Collegamenti lentissimi dal Mac (24/11/2026): dipende dal Mac, non dall'X3.**
Ogni tanto, dal Mac con Brave, ogni scambio dura 3-15 s invece di 30-60 ms
(0,3 KB/s, `HELLO` dopo 48 s), per periodi da mezzo minuto a un paio di minuti;
poi torna veloce da solo, anche senza scollegarsi. Un collegamento che cade in
uno di questi periodi spesso finisce in timeout (sull'X3 `scollegato, motivo
0x208`). Per ora: "Annulla", aspettare un minuto e riprovare, tenendo fermo il
mouse Bluetooth durante l'invio.

Cosa succede: il controller Bluetooth del Mac smette di servire il collegamento
dell'X3 e lo tiene vivo con un solo scambio allo scadere di una soglia, che
`bluetoothd` imposta al 75% del timeout di supervisione (`connection threshold
to 200 events with interval 12`: 200 eventi da 15 ms, 3,0 s; a 45 ms sono 66
eventi, sempre 3 s). Nei periodi lenti le attese sono multipli esatti di 3,03 s.
Intanto il Mac serve gli altri suoi dispositivi (qui un mouse MX Master 3 in LE
a 7,5 ms e una Magic Keyboard classica a 15 ms) e le sue scansioni.

Prove fatte (firmware di prova con comandi per cambiare a collegamento aperto
intervallo e letture dei tasti, pagina che cronometra `HELLO`, registro
`/usr/bin/log show --info --predicate 'process == "bluetoothd"'`):

- **Spegnere il mouse sblocca**: in un collegamento lento dall'inizio, il Mac
  registra lo scollegamento del mouse alle 14:47:02.393 e la risposta a `HELLO`
  arriva alle 14:47:02.546; da lì 22-49 ms per scambio. Con il mouse in movimento
  al momento del collegamento i lenti sono 6 su 22, con il mouse fermo 5 su 60.
- Il mouse non è l'unica causa: un lento è ricomparso per 21 s a mouse spento,
  mentre il Mac cercava di ricollegarlo, e con la tastiera spenta ne sono usciti
  3 su 13 (con la tastiera accesa 1 su 40).
- **Non è l'X3**: con keyScan fermo e il power letto ogni 50 ms un lento resta
  lento; lento e veloce si alternano senza uscire dalla ricezione; tra 0.5.2 e
  0.5.5 non cambia nulla di ciò che gira in ricezione (il confronto diretto non
  è stato fatto: prima delle 12:53 erano veloci 6 su 6, compatibile con il caso).
- **Non è l'intervallo**: capita a 15, 16,25 e 45 ms, e uno stesso periodo lento
  attraversa più cambi di intervallo. Il Mac concede 16,25 e 25 ms (non 17,5 né
  20, e niente sotto i 15), ma a 16,25 ms uno scambio dura di solito 49 ms invece
  di 30: non conviene.
- Richiedere di nuovo i parametri dall'X3 a lento in corso non è un rimedio: a
  volte dopo 2-3 richieste torna veloce, ma con gli stessi tempi con cui passa
  da solo; due richieste sono state respinte con esito 0x22a (collisione con
  un'operazione del Mac) e in un caso il lento è cominciato proprio lì.
- Uguali nei lenti e nei veloci: MTU 517, PHY 2M, segnale (−49/−58 dBm), rumore,
  statistiche radio di mouse e tastiera, nessun audio Bluetooth, Wi-Fi a 5 GHz.

Nel registro del Mac un lento si riconosce così: dopo `LE Connection Parameters
Updated` passano 3 s, invece di 20 ms, prima di `Finding all characteristics`.

Il configuratore web se ne accorge: se dopo 4 s (`BLE.slowMs`) `HELLO` non ha
ancora risposto, o se durante un invio gli `ACK` mancano da altrettanto
(`link.watchSlow`: il lento può cominciare a collegamento già aperto), sotto il
messaggio spiega che il collegamento è lento e cosa fare, e un successivo "non
risponde" diventa "Il collegamento era troppo lento" (`link.slow`,
`link.slowHint`). Non rimedia: avvisa.

**Il configuratore usato non c'entra (24/11, 16:35).** Il dubbio era nato da
sei invii del firmware di fila lenti o falliti dal configuratore web in locale,
con in mezzo l'unico dal sito pubblicato andato bene. Rifatti a turno con la
seriale dell'X3 registrata: locale veloce (0.5.7 in 65 s), **sito lento** (caduto
dopo 24 KB in 70 s, "Il dispositivo non risponde"), locale veloce (41 s). Il
sito non ha l'avviso del collegamento lento, quindi nemmeno quello c'entra.
Sulla seriale, nel lento, l'X3 fa tutto come nel veloce: legge i tasti con 1 ms
di ritardo, ridisegna l'avanzamento ogni 9,1 s, risponde subito. È il canale
che non passa, nei due versi: `ASK` parte dall'X3 alle 16:36:28,2 e il Mac la
riceve alle 16:36:31,5; i dati arrivano all'X3 8 KB ogni 19 s; finisce con
`scollegato, motivo 0x208`. Quel lento è cominciato 2-4 s dopo il passaggio a
15 ms, a collegamento fermo in attesa della conferma, non all'inizio dei dati.
Da questi registri non si distingue chi dei due salta gli eventi radio: per
quello valgono le prove sopra (il mouse spento che sblocca in 0,15 s).

**Dopo il riavvio del Mac (24/11, 17:02): uguale.** 3 lenti su 38 collegamenti
con `scripts/ble-prova.html` (il 3°, il 5° e il 37°), contro 2 su 15 e 1 su 40
prima del riavvio: non era uno stato passeggero del Bluetooth del Mac. Guardati
nello stesso registro ed esclusi: il Wi-Fi (è a 5 GHz, canale 52) e il rumore
radio delle porte USB (`Desense Detection`: fondo tra -97 e -101 dBm, stato
`Off`, uguale nei lenti e nei veloci).

La pagina di prova si fermava sempre verso la fine con "Bluetooth Device is no
longer in range": è il browser, non l'X3. Chrome e Brave dimenticano un
dispositivo circa 3 minuti dopo la scelta (40 collegamenti durano 3 minuti);
ora la pagina chiede di sceglierlo di nuovo e riprende il conto. Il
configuratore web non ne risente: fa scegliere l'X3 a ogni operazione.

**Da iPhone non capita (24/11, 17:15-17:30).** Stesso X3 (0.5.7), stessa pagina
di prova aperta in BLE Link dall'indirizzo del Mac in rete: **0 lenti su 80** in
due giri da 40, il più tardo con `HELLO` 1,3 s dopo il collegamento. Dal Mac,
nello stesso pomeriggio, 6 su 93 con la stessa pagina: con quella frequenza 80
veloci di fila capitano per caso una volta su duecento. Dall'iPhone non c'è un
registro: vale solo il tempo misurato dalla pagina. BLE Link apre la pagina
anche da un indirizzo http della rete locale, Bluefy no. L'indagine si ferma qui:
l'uso sul set passa dal telefono, e dal Mac basta riprovare.

**Dal MacBook Pro non capita (nota del 24/11, sera).** Il proprietario ha fatto
molti trasferimenti da un MacBook Pro, anche il 24/11, e sono stati sempre
veloci: mai contati con la pagina di prova, ma abbastanza da dire che il
problema è del Mac Studio (o di quello che ha intorno: mouse, tastiera), non dei
Mac in genere. Un conto formale sul MacBook Pro non serve.

Differenza trovata tra i due: il MacBook Pro ha macOS 27, il Mac Studio l'ultima
macOS 26. Il 25/11 il Mac Studio passa al 27: se dopo l'aggiornamento i lenti
spariscono (contarli con `scripts/ble-prova.html`, ne servono una cinquantina
per dirlo: con uno ogni 15 la probabilità di zero per caso è circa 3%), la
causa era il Bluetooth di macOS 26. Se restano, l'indagine si chiude lo stesso:
da iPhone e dal MacBook Pro va sempre, ed è un solo computer, che basta far
riprovare. Le idee che seguono restano qui solo per chi incontrasse lo stesso
problema altrove. Che sia cominciato il 24/11
non è dimostrato: il registro del Mac parte dal 23/11 pomeriggio, e con un lento
ogni 10-20 collegamenti quattro veloci di fila sono normali. Idea non provata:
- un timeout di supervisione corto chiesto dall'X3. Nel lento passa uno scambio
  ogni 0,75 × timeout (3 s con i 4 s di oggi, a 15 come a 45 ms): con 0,5 s
  sarebbe uno ogni 0,4 s, cioè un invio lento ma che arriva in fondo. Da vedere
  se il Mac lo concede e se regge a distanza.

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

Ogni invio è completo (le pagine tolte nel configuratore web spariscono dall'X3), ma
viaggiano solo i file cambiati: per ogni file il client chiede prima `KEEP`, e
manda i dati con `FILE` solo dopo un `NO`. L'X3 copia sulla microSD i file
uguali dalle card attuali, controllando il CRC mentre copia (se non torna
cancella la copia e risponde `NO`); `/camtag` non si tocca fino al `COMMIT`.
Niente `KEEP` per i file vuoti e per `project.json`, che cambia a ogni invio
(`savedAt`).

Perché funzioni, la stessa card deve uscire uguale byte per byte da
un'esportazione all'altra: il configuratore web disegna su canvas in software
(`willReadFrequently` in `newCanvas`), perché Chromium passerebbe dalla GPU al
software a metà e l'antialiasing diverso sposterebbe qualche pixel dopo la soglia.

Con un X3 a protocollo 2 il configuratore web non usa `KEEP` e manda tutto. Le
versioni precedenti che accetta sono la 2 e la 3, che ha tutto tranne
`FIRMWARE` (`BLE.oldVersions`).

## Sostituzione delle card

Al `COMMIT`: `/camtag` diventa `/camtag.old`, `/camtag.new` diventa
`/camtag`, poi `/camtag.old` si cancella. Se il secondo passo fallisce,
`/camtag.old` torna al suo posto.

## Aggiornamento del firmware

`HELLO` e `FIRMWARE` sono il **contratto fisso**: le versioni future del
protocollo li tengono uguali, così un X3 dalla v4 in su si aggiorna anche
quando il resto del protocollo cambia. Il configuratore web, con un protocollo
diverso ma almeno 4, invece di fermarsi propone "Aggiorna firmware".

1. Il client manda `FIRMWARE <byte> <crc32> <versione>` (al massimo 6,25 MB,
   la partizione OTA; versione di `[0-9A-Za-z.-]`, fino a 15 caratteri).
2. L'X3 risponde `ASK` e chiede conferma sullo schermo: A, B, C o D installano,
   X o Y rifiutano (`ERR RIFIUTATO`), e dopo 60 s senza risposta rifiuta da
   solo. Con la batteria sotto il 30% e senza carica risponde subito
   `ERR BATTERIA`. Il client aspetta la conferma fino a 65 s.
3. Dopo `OK` i dati viaggiano su DATA come quelli di un file (ACK ogni 2 KB,
   finestra di 8 KB) e l'X3 li scrive nella partizione OTA libera (`Update`
   dell'Arduino). I primi 24 byte devono essere un'intestazione ESP32-C3 (magic
   `0xE9`, chip 5 al byte 12), se no `ERR IMMAGINE`.
4. All'ultimo byte controlla il CRC, `Update` verifica l'immagine e la sceglie
   per l'avvio; `DONE <byte>` e l'X3 riparte (quando il client si scollega, al
   massimo dopo 2 s).

Su qualsiasi errore, `ABORT` o collegamento caduto la partizione nuova si
scarta e l'X3 resta com'era. Il nuovo firmware parte **in prova** (rollback del
bootloader): si dichiara sano al primo giro di `loop()`; se riparte prima, il
bootloader torna a quello di prima.

Il client controlla il file prima di mandarlo: intestazione ESP32-C3 e la
scritta `CAMTAG_FW=<versione>` nel binario (`CAMTAG_FW_TAG` in
`device/src/Version.h`), da cui prende la versione. Il file è
`companion/firmware/camtag.bin`, compilato da GitHub Actions con il
configuratore web dello stesso commit (`.github/workflows/pages.yml`, che si
ferma se la versione non è uguale a `FW_VERSION`); in locale non c'è, e con
Maiusc premuto su "Aggiorna firmware" si sceglie un `.bin`.

Sull'X3 dal Mac con Brave: 864 KB in 26 s (circa 33 KB/s), più veloce delle
card perché non c'è la microSD di mezzo.

Non c'è una firma: protegge la conferma sui tasti dell'X3, che si aggiunge a X
tenuto per entrare in ricezione.

## Test

- Host, senza hardware: `c++ -std=c++17 -I device/src device/test/upload_proto_test.cpp -o /tmp/upt && /tmp/upt`
  (CRC, percorsi, lettura dei comandi, `KEEP`, `GET`, `ACK`, `FIRMWARE` e
  intestazione dell'immagine).
- Lato client il flusso si prova nel browser sostituendo
  `navigator.bluetooth.requestDevice` con un dispositivo finto che segue questa
  tabella.
