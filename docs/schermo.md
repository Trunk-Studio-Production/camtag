# Schermo: problemi aperti

## Negativo delle schermate precedenti a schermo fermo (risolto)

**Sintomo.** Sulla schermata filtri, qualche minuto dopo l'ultimo aggiornamento,
sul bianco affiora piano una grana grigia uniforme ("retinatura") che diventa
sempre più intensa. Non ha la forma di una schermata precedente: è uniforme su
tutto il bianco, con una striscia pulita sul lato destro (in verticale) e
qualche riga orizzontale appena visibile. È intermittente: dopo altri cambi di
filtri la stessa schermata è tornata pulita e stabile. Sulle altre pagine (lens
chart) l'immagine resta stabile; si vede solo un alone tenue della pagina
precedente, che non cresce.

**Cosa è escluso.**
- Non è un aggiornamento nostro: il registro della seriale mostra che dopo
  l'ultimo aggiornamento lo schermo non viene più toccato.
- Non è il pannello lasciato acceso: ogni aggiornamento (veloce, mezzo,
  completo) finisce con `X3_POF`, e anche il driver dell'SDK
  (`Uc8253X3Driver::display`) spegne il pannello quando `turnOff` è vero.
- Non è il blocco dei tasti (non usato) né il riposo: è successo con l'X3
  collegato alla USB.
- Non è ritenzione della schermata di spento (parapendio): la grana non ne ha la forma.

**Ipotesi in prova.** La grana compare quando l'**ultimo aggiornamento è un
refresh completo**. L'SDK chiude ogni completo con un passaggio *veloce* di
assestamento (`Uc8253X3Driver::display`, blocco "First differential after a
full garbles on X3": `loadBankCdi(…, _cfg.fast)` + `triggerRefresh`). Se l'ultima
forma d'onda che il pannello riceve è quella veloce, il bianco potrebbe non
essere stabile. Il mezzo refresh non ha quel passaggio finale.

Coerente con quello che si è visto:
- quando è comparsa, l'ultimo aggiornamento era un completo (registro sotto);
- la lens chart si raggiunge con un mezzo refresh ed è stabile;
- sui filtri i completi sono frequenti: `present` ne fa uno ogni
  `FULL_REFRESH_EVERY` (6) cambi, e ogni riordino degli stadi è un cambio.

Registro della seriale (X3 sulla USB, schermata filtri):

```
18:35:07 [filtri] nd nd=1 …
18:35:08 [epd] veloce da 660584 a 661233
18:35:09 [filtri] riordino … / mezzo refresh (stadi)
18:35:11 [epd] mezzo da 662709 a 663634
18:35:11 [filtri] nd nd=2 …
18:35:11 [epd] veloce da 663747 a 664396
18:35:13 [filtri] riordino … / mezzo refresh (stadi)
18:35:15 [epd] completo da 665870 a 667961   <- full + passaggio veloce dell'SDK
(nessun altro aggiornamento; la grana cresce nei minuti dopo)
18:36:51 [riposo] inizio                     <- vedi "Riposo con la USB" sotto
```

**Prove del 20/11/2026 sera: l'ipotesi del passaggio veloce è smentita.**
Il negativo è quello delle schermate precedenti (Bluetooth, o un misto delle
pagine viste prima): il bianco si ingrigisce tranne dove prima c'era nero.
- 0.2.1, mezzo refresh dopo ogni completo: il negativo torna, sui filtri e
  sulla lens chart.
- 0.2.2, solo mezzi refresh (`kFullRefresh = false` in `present()`): torna lo
  stesso. Registro della seriale: uscita dal Bluetooth sui filtri alle 20:21:57
  (mezzo, `X3_POF` in fondo), negativo visibile verso le 20:23:20; in mezzo
  nessuna riga: niente comandi allo schermo, niente riposo, niente riavvio, X3
  sveglio sulla USB.

Quindi la causa non è il tipo di refresh né il riposo: dopo l'aggiornamento il
pannello scivola da solo, con il controller spento (`POF`) e nessun comando.
Sospetti: lo stato del controller dopo il `POF` (uscite non scaricate, la
carica che resta sui pixel), o le forme d'onda dell'SDK (non finiscono a
tensione zero). Leve senza toccare l'SDK: mettere il controller in sonno
profondo dopo ogni aggiornamento (`deepSleep`, poi `begin` +
`skipInitialResync` al successivo), oppure forme d'onda nostre con
`-DFREEINK_UC8253_X3_CONFIG` (vedi `Uc8253X3Driver.cpp`).

**Correzione (firmware 0.2.3–0.2.4).** Il controller va in sonno profondo
(`deepSleep`, comando 0x07 A5) dopo ogni aggiornamento: con la 0.2.3, subito,
il negativo non è più comparso. Risvegliarlo vuol dire `begin` +
`skipInitialResync`, che svuota il framebuffer, e il primo aggiornamento dopo
non può essere veloce: i cambi dei filtri diventavano lenti. Nella 0.2.4 il
sonno arriva `DISPLAY_SLEEP_MS` (4 s, scelti provando) dopo l'ultimo
aggiornamento, non durante la modifica né prima del riordino degli stadi, e
all'inizio del riposo (`wakeDisplay`/`sleepDisplay` in `main.cpp`).
Confermato sull'X3 il 21/11/2026, dopo ore d'uso: il negativo non torna, anche
con i 4 s di pannello solo spento.

**Riavvio da solo (spiegazione probabile).** Alle 20:19:23 l'X3 è andato a
riposo con il cavo attaccato; poco dopo la porta USB ha cambiato nome e lo
schermo ha fatto un completo (con la 0.2.2 solo un avvio lo fa), senza che il
cavo fosse toccato. Il motivo si è perso perché riaprire la seriale lo ha
riavviato di nuovo. Con ogni probabilità è la stessa cosa del riposo con la
USB qui sotto: a riposo la porta sparisce, al primo risveglio (allora il
controllo della carica, ogni 5 s) ricompare, con un altro nome se il vecchio è
ancora occupato, e `taratura.py` che la aspettava la riapre: aprire la seriale
riavvia l'X3. Non è un riavvio del firmware.

**Durante un'installazione o un ripristino via USB (visto una volta, 22/11/2026).**
Il negativo è ricomparso mentre si riscriveva la memoria per tornare dal
firmware CamTag a quello originale. Lì CamTag non gira: il chip è nel
caricatore della ROM, i pin dello schermo sono liberi e nessuno rimette il
controller in sonno profondo, quindi il pannello resta nello stato in cui il
negativo affiora. Spiegazione probabile, non verificata; caso limite, e il
primo aggiornamento del firmware che parte dopo lo toglie.

**Strada scartata.** CrossPoint sull'X3 non usa il mezzo refresh dell'SDK:
prima chiama `requestResync(1)`, che lo trasforma in un completo con un
passaggio di condizionamento (`reference/crosspoint/lib/hal/HalDisplay.cpp`).
Provato a farlo anche qui e poi annullato senza caricarlo: aggiunge proprio il
passaggio veloce finale sospettato, e la lens chart (mezzo refresh) è stabile.

## Riposo con la USB collegata (corretto nella 0.4.2, da confermare)

Alle 18:36:51 l'X3 è andato a riposo anche collegato al Mac (`[riposo] inizio`)
e la seriale si è persa (`Device not configured`): da lì il registro non ha
visto più nulla. Secondo le regole doveva restare sveglio con la USB
(`usbHost()` = `HWCDC::isPlugged()` in `canRest`).

**Perché `isPlugged()` può essere falso con il cavo collegato.** Non guarda il
cavo: è `usb_serial_jtag_is_connected()` del framework, che a ogni millisecondo
controlla se il computer ha mandato il suo pacchetto di cadenza (SOF) e diventa
falso dopo 3 ms senza. Qualunque pausa del computer lo spegne: un reset della
porta, un hub che esita, il computer che sospende la USB o va a dormire. E
`canRest` lo guardava a ogni giro di `loop()`: bastava una lettura falsa, dopo
5 s senza tasti, per andare a riposo, e a riposo la USB si spegne davvero. Da
lì l'X3 poteva non tornare più: la carica era già stata notata (`chargeSeen`),
quindi nessun risveglio fino al primo tasto.

**Correzione (0.4.2).**
- Prima del riposo il computer deve mancare da almeno 3 s (`USB_GONE_MS`): una
  pausa breve non conta. Ogni pausa vista finisce nel registro della seriale
  (`[usb] computer non sentito per N ms`), così si saprà quanto durano.
- Se va a riposo lo stesso (computer zitto per più di 3 s), a riposo sente il
  computer sul filo dei dati e si ripresenta da solo (`docs/batteria.md`, "Cavo
  USB a riposo"): subito la prima volta, poi sempre più di rado.

Che cosa abbia fermato il computer quella sera non si sa. Da confermare con un
registro lungo (`taratura.py log --file …`): non deve più comparire
`[riposo] inizio` con il cavo collegato, salvo dopo una riga
`computer non sentito` di più di 3 s.
