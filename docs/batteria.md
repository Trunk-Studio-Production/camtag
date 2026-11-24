# Batteria: acceso, a riposo, spento

Come l'X3 gestisce l'energia e come si misura quanto consuma. I tempi sono
costanti in cima a `device/src/main.cpp`; il simulatore del configuratore web
segue lo stesso comportamento (`simPress`, `drawOffOverlay`).

Il comportamento è provato sull'X3 (20/11/2026). I consumi sono misurati dal
registro (21-24/11/2026): 25 mA da sveglio, 2,1 mA a riposo (1,8 mA con i
tasti bloccati), circa 10 mA con un uso vero. Da spento, prima del bianco,
**da 1,3 a 5 mA**, più del riposo: la 0.5.3 corregge due cause trovate nel
codice ma il consumo alto c'è ancora, una volta sì e una no: **causa da trovare** (vedi in fondo).

## Gli stati

| Stato | Cosa fa | A schermo | Come ci si arriva |
|---|---|---|---|
| **Acceso, sveglio** | chip a piena potenza, tasti letti ogni 5 ms | le card | un tasto, il power, un cambio di verso |
| **Acceso, a riposo** | sonno leggero; ogni 25 ms (`REST_POLL_MS`) si sveglia circa 1 ms per leggere i tasti | le card, uguale | 5 s senza tocchi (`AWAKE_MS`) |
| **Spento** | sonno profondo; lo sveglia solo il power | scheda di spento con la fascia, poi bianco | power tenuto 1 s, 24 h senza tasti, batteria scarica |

Per chi lo usa gli stati sono due: **acceso** (mostra le card e risponde
sempre, salvo con i tasti bloccati, e allora c'è il lucchetto) e **spento**
(mostra una schermata che non è una card). Il riposo non
si vede: la prima pressione arriva come le altre, con al più 25 ms di ritardo;
una pressione più corta di così può sfuggire.

### A riposo

- Ogni secondo (`REST_IMU_MS`) legge l'accelerometro: se il verso è cambiato si
  sveglia e la rotazione procede come sempre.
- L'IMU tiene acceso solo l'accelerometro, in basso consumo a 21 Hz
  (`imuAccelOnly`): il giroscopio che l'SDK accende non serve.
- Ogni mezzo secondo (`REST_HOST_MS`) sente se all'altro capo del cavo c'è un
  computer, e in quel caso si sveglia per farsi riconoscere (vedi "Cavo USB a
  riposo").
- Ogni 5 s (`REST_USB_MS`) chiede al fuel gauge se la batteria è in carica: se
  la carica è appena cominciata o finita si sveglia per aggiornare il fulmine
  sulla card.
- Con la USB collegata a un computer non va mai a riposo: nel sonno leggero la
  porta sparirebbe. Il computer deve mancare da almeno 3 s (`USB_GONE_MS`), e
  dopo ogni avvio resta sveglio almeno 15 s (`BOOT_AWAKE_MS`).
- Non va a riposo durante la ricezione Bluetooth, la modifica degli stadi, un
  riordino in sospeso o un banner a schermo.
- La microSD resta alimentata (vedi "Da chiarire").

### Cavo USB a riposo

L'X3 non ha un pin che senta il cavo, e nel sonno leggero la USB del chip non
funziona: il suo orologio è fermo. Fino alla 0.4.1 un cavo collegato a riposo
si notava solo dalla corrente di carica, guardata ogni 5 s: a batteria piena
mai, e nemmeno passando da un caricatore al computer (la carica era già stata
notata). In più, tra un sonno e l'altro il chip riaccendeva il pad USB per un
millisecondo ogni 25: il computer poteva vedere un dispositivo che si presenta
e non risponde, e rinunciare finché il cavo non veniva staccato.

Dalla 0.4.2:

- **A riposo il pad USB è spento** e senza resistenze (`usbPad(false)`): per
  il computer è un cavo senza niente attaccato. All'uscita dal riposo si
  riaccende, e il computer vede un dispositivo appena collegato.
- **Ogni mezzo secondo si sente il filo dei dati** (`usbHostPull`): chi ospita
  una USB, computer o hub, tiene D+ e D- a massa con 15 kΩ. Il firmware porta
  D- a 3,3 V per 10 µs, lo lascia libero e lo legge dopo 60 µs: con un computer
  è tornato a zero, a cavo staccato o su un caricatore è rimasto alto. Non
  serve corrente di carica, quindi **vale anche a batteria piena**. Con i tasti
  bloccati il chip si sveglia una volta al secondo, e la lettura segue.
- **Sentito un computer si sveglia** (`[riposo] fine: usb`) e gli lascia 2 s
  (`USB_TRY_MS`) per riconoscere la USB: misurati da 0,13 a 0,35 s, poi resta
  sveglio finché il computer c'è.
- **Se il computer non risponde** (dorme, è un hub senza computer, o un
  caricatore che tiene basso il filo dei dati) torna a riposo e riprova dopo
  1 minuto, poi ogni 2 (`USB_RETRY_MIN_MS`, `USB_RETRY_MAX_MS`), aspettando
  0,7 s (`USB_RETRY_TRY_MS`). Un hub senza computer dietro si legge come un
  computer: l'X3 lasciato attaccato all'hub mentre il portatile va e viene è
  un uso normale, e al ritorno aspetta al più 2 minuti. Vale anche per un
  computer che lo riconosce e poi tace
  per più di 3 s: non lo sveglia di continuo. Staccato il cavo si riparte da
  capo: al prossimo computer si sveglia subito. Un tasto fa sempre da scorciatoia: da sveglio la
  USB è accesa.
- Su un caricatore non cambia nulla: la carica si vede dal gauge come prima.

**Consumi.** La lettura dura circa un decimo di millisecondo e cade in un
risveglio che c'è già (quello dei tasti): un decimo di millisecondo ogni mezzo
secondo a 25 mA sono circa 0,005 mA, lo 0,2% dei 2,1 mA a riposo e sotto quello
che il gauge distingue (passi di 1 mAh, tratti che già variano tra 1,9 e
2,2 mA). Per questo il riposo non è stato rimisurato apposta: lo conferma il
prossimo registro (`taratura.py batteria`), dove `sveglio_s` che cresce senza
tasti e senza cavo vorrebbe dire letture sbagliate. Costa solo restare
attaccati a un computer che non risponde o a un hub senza computer: 0,7 s
svegli ogni 2 minuti, circa 0,15 mA di media in più.

**Prova.** `taratura.py usb` fa staccare e riattaccare il cavo dieci volte con
l'X3 a riposo, misura dopo quanto compare la porta e legge dall'X3 gli appunti
sull'ultimo riposo, che il firmware tiene in memoria attraverso il riavvio
della seriale e stampa all'avvio:

```
[usb] ultimo riposo: 21 s, fine usb, cavo 42 letture (1 con computer), computer dopo 310 ms
```

Più di una lettura "con computer" prima del risveglio vuol dire un computer
che non rispondeva, o letture sbagliate a cavo staccato.

**Provato sull'X3 (22/11/2026)**, attraverso una catena di hub, batteria al
100% (a volte con +55 mA di carica, a volte senza):

| Prova | Porta dopo | Cavo sentito | Computer dopo il risveglio |
|---|---|---|---|
| 1 | 2,8 s | 1 su 46 | 141 ms |
| 2 | 3,4 s | 1 su 35 | 142 ms |
| 3 | 4,6 s | 1 su 38 | 126 ms |
| 4, hub staccato dal Mac | 16,1 s | 44 su 46 | 262 ms |
| 5 | 2,6 s | 1 su 37 | 217 ms |
| 6 | 4,1 s | 1 su 38 | 172 ms |
| 7, altra presa dell'hub | 6,7 s | 1 su 31 | 353 ms |
| 8, 9, 10 | riuscite come le altre (tempi non registrati) | | |

Nove su dieci entro 10 s; l'unica fuori tempo è la 4, fatta apposta in modo
diverso. I secondi comprendono la mano che attacca il cavo. A cavo staccato nessuna
lettura sbagliata; a batteria piena e senza corrente di carica funziona
uguale. Nella prova 4 il cavo è rimasto attaccato all'hub e si è staccato
l'hub dal computer: l'hub da solo si legge come un computer (44 letture su
46), l'X3 ha provato, non ha avuto risposta ed è tornato a riposo; si è
risvegliato prima della riprova grazie a due letture libere, forse l'hub che
si riaccende. Da lì le riprove più fitte di adesso. Rifatto con quelle, hub
staccato per più di 90 s (quindi dopo almeno una riprova a vuoto): al
ricollegamento la linguetta USB è comparsa in pochi secondi.

### Tasti bloccati

- **Power breve** blocca e sblocca i tasti A–D, X e Y: la pagina resta com'è, per non
  cambiare un valore con un urto. La rotazione continua a seguire il verso.
- Il segno è un **lucchetto** in un riquadro di 36 px sul bordo fisico, sotto
  il power che sblocca (`drawLock`; nel simulatore `drawLockIcon`): come le
  indicazioni dei tasti segue la rotazione del dispositivo e resta dritto per
  chi legge. Compare con un aggiornamento veloce, sparisce con un mezzo refresh.
- Bloccando si chiude la modifica degli stadi e si applica il riordino in
  sospeso. Non si blocca durante la ricezione Bluetooth.
- A riposo, con i tasti bloccati, il chip non ha tasti da guardare: dorme un
  secondo alla volta (il power lo sveglia da solo), quindi consuma meno:
  1,80 mA invece di 2,1 (misurato su una notte, vedi in fondo).
- Il blocco è salvato (`keylock` in NVS) e sopravvive a un riavvio; spegnendo
  si toglie.

### Spento

- **Power tenuto 1 s** (`POWER_HOLD_MS`) spegne.
- **Da spento, power tenuto** (circa 1 s in tutto, `POWER_ON_HOLD_MS` dopo
  l'avvio del chip) accende sull'ultima card. Un tocco breve non accende e non
  cambia lo schermo: in una borsa non si accende da solo.
- **Dimenticato acceso** si spegne dopo 24 h senza tasti (`AUTO_OFF_MS`).
- **Batteria scarica**: sotto 3,3 V (`EMPTY_MV`), non in carica, si spegne con
  l'avviso "BATTERIA SCARICA - METTI IN CARICA" e non si riaccende finché non
  è in carica. Si guarda la tensione e non la percentuale, che dipende da come
  è configurato il gauge.
- Dopo un caricamento del firmware o l'apertura della seriale si accende da
  solo.

### Schermata di spento

- Se in `/camtag` ci sono `off.bmp` (792×528) e/o `off.p.bmp` (528×792) il
  firmware mostra quella, nella variante adatta al verso. Stesso formato delle
  card, ma fuori dal giro delle pagine. Nel configuratore web la compone la pagina
  "Spento" (immagine di sfondo e scritta, `state.off`), oppure è una scheda
  manuale con "Schermata di spento" accesa (`c.off`, una sola), che la sostituisce.
- Senza, mostra il nome CAMTAG al centro.
- Sopra, il firmware disegna (`drawOff`): il nome CAMTAG in un riquadro a un
  quarto dell'altezza se `off.logo=1` in `settings.ini` (senza il file, il nome
  c'è solo senza `off.bmp`); una fascia bianca in basso (36 px, riga nera da 2 px
  in cima) con "SPENTO - TIENI POWER PER ACCENDERE"; la batteria al momento dello
  spegnimento, in alto a destra; con `keys=1` (o senza il file) le **etichette
  dei tasti** sul bordo fisico di ogni tasto (`drawKeyLabels`), la fascia sopra
  le etichette in basso e la batteria al centro in alto.
- Dopo 72 h da spento (`OFF_BLANK_S`) lo schermo passa al **bianco con la sola
  fascia**: un'immagine ferma per settimane può lasciare un'ombra.
- Lo spento ha quindi due tempi (`sleepOff`). Con la scheda di spento a schermo
  il chip resta **alimentato in sonno profondo** (GPIO13 alto), perché serve il
  timer che lo sveglia per il bianco: in quei tre giorni consuma poco, ma non
  zero. Dopo il bianco GPIO13 va basso e **la batteria è staccata**: non
  consuma più nulla. A batteria scarica si stacca subito, senza bianco.
- Per provarlo senza aspettare tre giorni:
  `taratura.py batteria --bianco 120` (poi `--bianco 0` per rimettere i tre giorni).
- Nei tre giorni alimentati tutto deve restare a dormire e fermo (dalla 0.5.3):
  - **I comandi del bus sono tenuti alti** (`holdBus`: CS della microSD e del
    pannello, clock, MOSI, DC), come RESET del pannello e GPIO13. Nel sonno
    profondo un pin non tenuto resta in aria, e con microSD e pannello
    alimentati un ingresso a mezza tensione consuma. All'avvio `releaseBus` li
    libera prima di riconoscere il dispositivo, che altrimenti leggerebbe un bus
    bloccato.
  - **Un tocco breve del power non sveglia il pannello.** Il chip si sveglia,
    vede che il power non è tenuto e torna a dormire *prima* di riconoscere il
    dispositivo (`setup`): la sonda del pannello gli dà un impulso di RESET che
    lo toglie dal sonno profondo, e fino alla 0.5.2 da lì restava sveglio fino
    alla prossima accensione. I tocchi si contano (`offNote`) e finiscono nella
    colonna `tocchi` del registro.
  - **Alimentazione tornata con la scheda di spento a schermo** (RESET, poi un
    tocco del power o il cavo): le periferiche sono appena accese, non a
    dormire. Il firmware le avvia e le rimette a dormire (`sleepAll`) prima di
    tornare spento.

In NVS: `off` = 0 acceso, 1 scheda di spento a schermo, 2 bianco, 3 avviso di
batteria scarica; `offAt` = ora dello spegnimento.

## Registro dei consumi

Il fuel gauge dell'X3 (BQ27220) misura la corrente della batteria e conta i
mAh. Il firmware scrive `/camtag-batteria.csv` nella radice della microSD
(fuori da `/camtag`, che la ricezione Bluetooth sostituisce): una riga ogni
5 minuti, una all'avvio, una allo spegnimento. Si ferma a 2 MB.

| Colonna | Significato |
|---|---|
| `t_s` | secondi di un orologio che attraversa sonno e riavvii (riparte solo senza alimentazione) |
| `evento` | `A` avvio, `P` riga periodica, `S` spegnimento |
| `info` | `A`: motivo del riavvio × 100 + motivo del risveglio (`esp_reset_reason`, `esp_sleep_get_wakeup_cause`); `S`: 0 power, 1 dimenticato acceso, 2 batteria scarica |
| `mV`, `mA` | tensione e corrente; `mA` positivo = in carica, risoluzione 1 mA |
| `mAh`, `pieno_mAh`, `pct` | capacità residua, capacità a pieno e percentuale secondo il gauge |
| `sveglio_s`, `riposo_s` | secondi passati sveglio e a riposo dall'ultimo avvio |
| `schermo`, `tasti` | aggiornamenti dello schermo e pressioni dall'ultimo avvio |
| `tocchi` | dalla 0.5.3, solo nelle righe `A`: tocchi brevi del power durante lo spento appena finito (troppo corti per accendere) |
| `spento_mA` | dalla 0.5.5 (nella 0.5.4 la colonna c'è ma resta vuota), solo nelle righe `A` dopo uno spento: la corrente che il gauge aveva in memoria al risveglio, cioè il consumo da spento (negativa = consumo); vuota se non c'è lettura |
| `tocco_mA` | dalla 0.5.5: la stessa lettura fatta al primo tocco breve, prima che il tocco potesse cambiare qualcosa; vuota senza tocchi |

Dalla 0.5.6 dopo una riga `A` possono esserci righe `D` della diagnosi da spento (vedi "Da chiarire"): non sono tratti di consumo.

Un registro cominciato con un firmware più vecchio tiene la sua intestazione
senza `tocchi`: le righe nuove hanno comunque la colonna in più, e lo script
legge le une e le altre. Per un file pulito, `--azzera` dopo l'aggiornamento.

All'avvio il firmware controlla anche la capacità scritta nel gauge
(`BatteryMonitor::loadDesignCapacity`): se è rimasta al valore di fabbrica
(3000 mAh) mette quella vera, 650 mAh. Senza, percentuale e mAh sono sbagliati.
Sull'X3 di prova non è servito: il gauge aveva già 642 mAh.

### Come si misura

```bash
.venv/bin/python scripts/taratura.py batteria            # scarica, salva in misure/, fa i conti
.venv/bin/python scripts/taratura.py batteria --azzera   # e poi cancella il registro sull'X3
.venv/bin/python scripts/taratura.py batteria --leggi misure/batteria-….csv
```

1. Carica l'X3, poi **stacca la USB**: con il cavo la batteria si carica e la
   misura non vale (lo script scarta da solo i tratti in carica).
2. Lascialo nello stato da misurare: acceso e fermo per il riposo, spento per
   lo spento. Qualche ora almeno: il gauge conta a passi di 1 mAh.
3. Ricollega la USB e lancia `batteria`. La seriale riavvia l'X3: è normale.

Senza cavo, via Bluetooth: sull'X3 tieni premuto X, nel configuratore web apri
la finestra Bluetooth e scegli "Registro batteria" (si vede in modalità
sviluppatore: pagina aperta con `?dev`); il CSV si scarica nel
browser e i conti li fa `taratura.py batteria --leggi <file>`. Così si legge a
metà prova senza metterlo in carica. La ricezione lo tiene sveglio per quei
secondi: finisce nella colonna `sveglio_s` della riga dopo, come un uso normale.

Lo script divide il tempo tra una riga e l'altra in *sveglio*, *riposo*, *misto*
e *spento*, e per ognuno dà mAh scesi, corrente media e durata con una batteria
piena. Sotto, ogni spento di almeno mezz'ora ha la sua corrente e i suoi tocchi
brevi del power: se da uno all'altro cambia, si vede lì e non nella media. Sotto 1 mA il gauge può sottostimare: accanto dà anche il calo di
tensione, come controllo.

## Da chiarire con le prime misure

- **GPIO13 sull'X3 stacca la batteria** (misurato il 20/11/2026): dopo uno
  spegnimento a batteria la riga `A` ha riavvio 1 (alimentazione tornata) e
  l'orologio riparte da zero. Quindi con la batteria staccata l'X3 non
  consuma, ma nessun timer può svegliarlo (da qui i due tempi dello spento), e
  GPIO13 non si può usare per spegnere la microSD a riposo.
- **Consumo nei tre giorni prima del bianco: troppo, e non sempre uguale;
  la 0.5.3 non l'ha tolto, causa da trovare.**
  Registro del 22-24/11/2026 (0.5.2, letto via Bluetooth il 24/11), tratti tra
  una riga `S` e la `A` con riavvio 8 che segue:

  | Spento | Durata | mAh | Media | Tensione |
  |---|---|---|---|---|
  | 22/11, pomeriggio | 3,4 h | 18 | 5,3 mA | 4200 → 4177 mV |
  | 23/11 | 3,1 h | 4 | 1,3 mA | 4217 → 4225 mV |
  | 23/11 | 1,6 h | 2 | ~1,3 mA | |
  | 23/11 | 0,9 h | 2 | ~2 mA | |
  | notte 23-24/11 | 14,2 h | 69 | 4,9 mA | 4265 → 4155 mV |

  In tutto 23,7 h, 98 mAh, 4,1 mA: **da spento consumava il doppio che a
  riposo**. La tensione conferma il gauge (circa 1,7 mV per mAh sia a riposo
  sia nella notte da spento). A 4,9 mA le 72 h prima del bianco costano il 55%
  della batteria; a 1,3 mA il 15%.

  Nel sonno profondo il chip consuma decine di µA: il resto sono le
  periferiche, che restano alimentate. Nel codice c'erano due cause possibili,
  e il registro non dice quale contava (un tocco breve non lasciava righe):

  1. un tocco breve del power, o un rimbalzo del tasto al rilascio dopo lo
     spegnimento, lasciava il pannello fuori dal sonno profondo;
  2. i comandi del bus restavano in aria, mentre a riposo
     `keepOutputsInRest` li tiene tutti e lì il consumo è stabile.

  La 0.5.3 le toglie tutte e due (vedi "Schermata di spento") e conta i tocchi.
  **Prima misura (24/11/2026, 0.5.3): 2,1 h da spento con un tocco breve circa
  10 s dopo lo spegnimento (`tocchi` = 1), 11 mAh, 5,3 mA** (4317 → 4285 mV).
  Lo stesso numero di prima: quelle due cause non erano il consumo, o non da
  sole. Il conto dei tocchi funziona, e dopo il tocco non affiora il negativo.
  Non è un errore del gauge dopo la carica: il 22/11 i 5,3 mA sono venuti al
  90%, dopo 18 ore a riposo.

  I valori sono due, circa 1,3 e circa 5 mA, senza vie di mezzo: qualcosa che
  ogni volta resta in uno stato o nell'altro. Non si sa ancora se a deciderlo è
  il tocco (nella 0.5.3 il risveglio breve non tocca più pannello, microSD e
  IMU: restano il chip e `holdPowerRails`) o l'ingresso nello spento (microSD
  dopo `SdMan.shutdown()`, pannello che non prende il comando di sonno profondo
  subito dopo l'aggiornamento, `imu.sleep()` che non riesce).

  **Come trovarla senza aspettare ore (0.5.5)**: appena sveglio il gauge ha
  ancora in `Current` l'ultima lettura fatta mentre il chip dormiva (nelle
  righe `P` a riposo dà −2 mA 560 volte su 700, cioè i 2,1 mA del riposo).
  `battery::wakeCurrent` lo legge per prima cosa in `setup()` e la riga `A` lo
  porta in `spento_mA`, con `tocco_mA` per la lettura al primo tocco breve:
  basta un minuto da spento invece di tre ore (il gauge in SLEEP misura ogni
  20 s, quindi non meno). `taratura.py batteria` lo scrive accanto a ogni
  spento ("al risveglio 5 mA"). La 0.5.4 lo leggeva con il profilo attivo, che
  in cima a `setup()` è ancora quello dell'X4 senza gauge, e la colonna restava
  vuota: la 0.5.5 usa i pin dell'X3.

  **Provato sull'X3 (24/11/2026, 0.5.5): la lettura vale**, e bastano 2-3
  minuti da spento. Cinque spegnimenti di fila, tutti senza tocchi:

  | Acceso prima | Spento | `spento_mA` |
  |---|---|---|
  | 62 s (27 sveglio, 35 a riposo) | 169 s | −2 |
  | 150 s (in Bluetooth) | 905 s | −2 |
  | 10 s | 147 s | −2 |
  | 7 s | 165 s | −5 |
  | 9 s | 216 s | −5 |

  Quindi: i due stati ci sono anche a pochi minuti dallo spegnimento (−2 è lo
  stato da 1,3 mA, −5 quello da 5), **il consumo alto viene senza nessun tocco
  del power**, e si decide all'ingresso nello spento. I due casi alti sono
  venuti dopo accensioni di pochi secondi, ma anche uno dei bassi (10 s): con
  cinque casi non è una regola. Resta da trovare quale periferica resta
  accesa: IMU, microSD o pannello (la diagnosi qui sotto dice microSD).

  **Diagnosi da spento (0.5.6), 24/11/2026.** Con `diag` acceso, nei primi
  minuti dello spento il timer sveglia l'X3 ogni 60 s (`kDiagStepS`): legge dal
  gauge il consumo del tratto appena finito e cambia una cosa sola per il
  tratto dopo. Uno spento, partito alto:

  | Tratto | Prima del tratto | Consumo | IMU |
  |---|---|---|---|
  | 1 | niente: lo spento com'è venuto | 5 mA | spento |
  | 2 | un risveglio a vuoto | 5 mA | spento |
  | 3 | tutto riavviato e rimesso a dormire | 5 mA | spento |
  | 4 | come il 3, IMU lasciato sveglio (accelerometro) | 2 mA | acceso |
  | 5 | come il 3, pannello non in sonno profondo | 2 mA | spento |
  | 6 | come il 3, microSD "non spenta" | 5 mA | spento |
  | 7 | tutto rimesso a dormire | 2 mA | |

  Quindi **IMU e pannello non c'entrano**: lasciati svegli stanno a 2 mA, e
  con 5 mA l'IMU era spento. Un risveglio a vuoto non cambia lo stato; rifare
  tutto lo cambia a caso (5, poi 2). Resta la microSD, e nell'SDK c'è il
  motivo: su queste schede `SdMan.shutdown()` scrive solo quello che è in
  sospeso, **non ferma la microSD**, perché dà per scontato che lo spento le
  tolga l'alimentazione. Qui invece resta alimentata fino al bianco, nello
  stato in cui l'ha lasciata l'ultima operazione: a volte ferma, a volte no.
  (I tratti 3 e 6 fanno quindi la stessa cosa.)

  **Prova del rimedio (0.5.7)**: `sdGoIdle` aspetta che la microSD finisca,
  la riporta allo stato di partenza con CMD0 e le dà i clock per fermarsi. La
  diagnosi ora fa dieci tratti da un minuto, riavviando tutto ogni volta: nei
  pari (2, 4, 6, 8, 10) con `sdGoIdle`, nei dispari no. Se i pari stanno tutti a
  2 mA e tra i dispari escono dei 5, il rimedio vale e va in `sleepAll` per
  tutti (oggi lo fa solo la diagnosi). Righe `D` del registro, dopo la riga
  `A`: `info` = 100000 + (tratto − 1) × 1000 + 254 se la microSD è lasciata
  com'è, se no la risposta a CMD0 (1 = a riposo, 255 = non ha risposto);
  `spento_mA` = la lettura. Nella 0.5.6 `info` era (tratto − 1) × 1000 + stato
  dell'IMU. `taratura.py batteria` legge tutte e due.

  La diagnosi si accende e si spegne dalla seriale (`diag=1`, `diag=0`, cioè
  `taratura.py batteria --diagnosi 1`) e resta in NVS; di partenza è spenta.
  Per un firmware che la ha già accesa, da installare via Bluetooth senza cavo:
  `PLATFORMIO_BUILD_FLAGS="-DCAMTAG_OFF_DIAG_DEFAULT=1" ../.venv/bin/pio run -e x3`
  (senza `-v`, che fa fallire l'ultimo passo). Quella build porta `-diag` in
  coda alla versione (`0.5.7-diag`, `Version.h`): lo si legge sulla schermata
  Bluetooth dell'X3 e nel configuratore web, che la tratta come più vecchia
  della pubblicata. Un tocco breve durante la
  diagnosi fa ripartire il minuto in corso; accendendo prima della fine nel
  registro vanno i tratti fatti. Sotto il minuto da spento la lettura non
  vale (il gauge non ha ancora misurato: −12, −28, −33 mA dopo pochi secondi).

  Resta un limite che il firmware non toglie: il caso migliore da spento è
  1,3 mA, poco meno del riposo con i tasti bloccati (1,8 mA). È la scheda
  alimentata (la microSD in attesa è la prima indiziata), e si azzera solo
  staccando la batteria con GPIO13. Dopo la misura va deciso quanto restare
  alimentati prima del bianco (`OFF_BLANK_S`): a 1,3 mA 72 h costano 94 mAh,
  24 h ne costano 31 (5%).
- **Consumi: misurati** (registro del 20-22/11/2026 letto via Bluetooth, 39 h,
  tratti in carica esclusi):

  | Stato | Tempo | mAh | Media | Con 642 mAh |
  |---|---|---|---|---|
  | sveglio | 14 min | 6 | 25 mA | 25,7 h |
  | riposo | 21,7 h | 46 | 2,12 mA | 12,6 giorni |
  | riposo, tasti bloccati | 12,25 h | 22 | 1,80 mA | 14,9 giorni |
  | misto (uso vero) | 2,5 h | 24 | 9,5 mA | 67,4 h |

  Il registro del 22-24/11 (0.5.x, 52 h a riposo, 25 min svegli, 6 h di uso
  vero) dà gli stessi numeri: 2,12 mA a riposo, 26 mA da sveglio, 10 mA in uso.

  Il riposo è stabile nei tratti lunghi (notte 2,23 mA su 10,3 h, mattina
  1,9 mA su 3,75 h, pomeriggio 2,24 mA su 6,25 h con qualche tasto) e conferma
  la prima notte (2,26 mA su 11,9 h). Con i tasti bloccati (notte del
  21-22/11, nessun risveglio) il chip dorme un secondo alla volta invece di
  guardare i tasti ogni 25 ms: circa 0,4 mA in meno, il resto (microSD, IMU,
  gauge, pannello) non cambia. Basta così: non si ottimizza a scapito d'altro
  (prontezza dei tasti, microSD), si continua solo a guardare il registro.
- **Ricarica**: si ferma da sola a batteria piena (da +70/+110 mA a zero, a circa
  4,35 V; il gauge torna a 642 mAh). Da collegato l'X3 continua a prendere corrente dalla batteria, e la
  ricarica riparte ricollegando il cavo; se riparta anche da sola quando la
  tensione scende non si è ancora visto.
- **Risvegli inutili dal riposo**: nessuno nelle 39 h del 21-22/11 (`sveglio_s`
  cresce solo insieme ai tasti); si vedono da `sveglio_s` che cresce senza tasti.
  Con la 0.4.2 e seguenti (registro del 22-24/11) qualche secondo sveglio
  senza tasti, a gruppi di 5 s (il verso che cambia e si riassesta, probabilmente
  l'X3 maneggiato) e una volta 130 s in 40 min dopo un caricamento via USB
  (riprove del cavo): pochi secondi, nessun costo visibile sul riposo.
- **Computer che dorme con l'X3 collegato**: l'X3 va a riposo e riprova a
  presentarsi ogni 2 minuti. Se questo svegli il computer non è stato provato,
  per scelta: il cavo serve ai dati di rado. Se capita, si allungano i tempi o
  si tolgono le riprove (`USB_RETRY_*`).
- **Pin nel sonno leggero**: il framework isola i pin; le uscite che devono
  restare ferme sono escluse in `keepOutputsInRest`. Riavvii inattesi a riposo
  comparirebbero come righe `A` senza una `S` prima. Nel registro del 21-22/11
  nessuno: tutte le 46 righe `A` sono USB, power o i riavvii dopo gli
  aggiornamenti del firmware via Bluetooth (motivo 3, anche a batteria).
