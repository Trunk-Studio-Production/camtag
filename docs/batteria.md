# Batteria: acceso, a riposo, spento

Come l'X3 gestisce l'energia e come si misura quanto consuma. I tempi sono
costanti in cima a `device/src/main.cpp`; il simulatore del configuratore web
segue lo stesso comportamento (`simPress`, `drawOffOverlay`).

Il comportamento è provato sull'X3 (20/11/2026). I consumi invece sono ancora
**stime e prime letture**, finché il registro non dà numeri misurati su più ore.

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
- Ogni 5 s (`REST_USB_MS`) chiede al fuel gauge se la batteria è in carica: se
  la carica è appena cominciata resta sveglio qualche secondo, il tempo perché
  la USB si faccia riconoscere.
- Con la USB collegata a un computer non va mai a riposo: nel sonno leggero la
  porta sparirebbe. Dopo ogni avvio resta sveglio almeno 15 s (`BOOT_AWAKE_MS`).
  Se dopo aver collegato il cavo la porta non compare, premi un tasto; se serve,
  stacca e riattacca.
- Non va a riposo durante la ricezione Bluetooth, la modifica degli stadi, un
  riordino in sospeso o un banner a schermo.
- La microSD resta alimentata (vedi "Da chiarire").

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
  secondo alla volta (il power lo sveglia da solo), quindi consuma meno.
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

Lo script divide il tempo tra una riga e l'altra in *sveglio*, *riposo*, *misto*
e *spento*, e per ognuno dà mAh scesi, corrente media e durata con una batteria
piena. Sotto 1 mA il gauge può sottostimare: accanto dà anche il calo di
tensione, come controllo.

## Da chiarire con le prime misure

- **GPIO13 sull'X3 stacca la batteria** (misurato il 20/11/2026): dopo uno
  spegnimento a batteria la riga `A` ha riavvio 1 (alimentazione tornata) e
  l'orologio riparte da zero. Quindi con la batteria staccata l'X3 non
  consuma, ma nessun timer può svegliarlo (da qui i due tempi dello spento), e
  GPIO13 non si può usare per spegnere la microSD a riposo.
- **Consumo nei tre giorni prima del bianco**: mai misurato. Con il chip
  alimentato l'orologio continua, quindi il registro lo dà (riga `S`, poi `A`
  con riavvio 8). Se è troppo, si accorcia `OFF_BLANK_S`.
- **Consumo vero a riposo.** Prime letture singole, non una misura: circa
  28 mA da sveglio, circa 2 mA a riposo (il gauge ha la risoluzione di 1 mA).
  Servono ore a batteria. Da capire quanto pesa la microSD alimentata.
- **Ricarica**: si ferma da sola a batteria piena (da +70 mA a zero, a circa
  4,35 V). Da collegato l'X3 continua a prendere corrente dalla batteria, e la
  ricarica riparte ricollegando il cavo; se riparta anche da sola quando la
  tensione scende non si è ancora visto.
- **Risvegli inutili dal riposo** (letture sbagliate dell'ADC, verso instabile):
  si vedono da `sveglio_s` che cresce senza tasti.
- **Pin nel sonno leggero**: il framework isola i pin; le uscite che devono
  restare ferme sono escluse in `keepOutputsInRest`. Riavvii inattesi a riposo
  comparirebbero come righe `A` senza una `S` prima.
