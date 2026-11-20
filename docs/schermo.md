# Schermo: problemi aperti

## Retinatura che cresce a schermo fermo (aperto, 20/11/2026)

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

**Prova in corso.** Sulla lens chart, cambiare pagina con X/Y fino al refresh
completo (lampeggia in negativo, al massimo 6 cambi) e lasciarla ferma 5–10
minuti, guardando le zone bianche grandi.
- Se compare la grana: l'ipotesi è confermata. Correzione proposta: in
  `present()` dopo un completo fare anche un mezzo refresh dello stesso
  frame, così l'ultima forma d'onda non è mai quella veloce (l'SDK non si
  modifica). Costa circa un secondo in più sui completi.
- Se resta pulita: l'ipotesi è sbagliata. Ripartire da cosa si era fatto sui
  filtri subito prima che ricomparisse, con il registro dal vivo
  (`.venv/bin/python scripts/taratura.py log`, righe `[epd]` e `[filtri]`).

**Strada scartata.** CrossPoint sull'X3 non usa il mezzo refresh dell'SDK:
prima chiama `requestResync(1)`, che lo trasforma in un completo con un
passaggio di condizionamento (`reference/crosspoint/lib/hal/HalDisplay.cpp`).
Provato a farlo anche qui e poi annullato senza caricarlo: aggiunge proprio il
passaggio veloce finale sospettato, e la lens chart (mezzo refresh) è stabile.

## Riposo con la USB collegata (aperto)

Alle 18:36:51 l'X3 è andato a riposo anche collegato al Mac (`[riposo] inizio`)
e la seriale si è persa (`Device not configured`): da lì il registro non ha
visto più nulla. Secondo le regole dovrebbe restare sveglio con la USB
(`usbHost()` = `HWCDC::isPlugged()` in `canRest`). Da capire perché
`isPlugged()` era falso; finché non si risolve, per registrare a lungo premere
un tasto ogni tanto o ricollegare la seriale.
