# Filtri: ND, diottria e stadi del mattebox

Specifica della schermata filtri. Vale per lo strumento web (`companion/`), per
il simulatore e per il firmware (`device/`), che devono restare allineati.

## Concetti

- **ND**: un valore solo, quello totale davanti alla lente. Si scorre fra i
  valori del kit ND e le coppie scelte (vedi sotto), nel formato scelto
  (densità, stop o fattore) e con il tipo scelto (ND, IRND, FSND).
- **Diottria**: un valore solo, fra quelli del kit diottrie (intere o split).
- **Kit dei vetri**: i vetri che ci sono nella valigetta, esclusi gli ND pieni.
  Ogni voce è una famiglia con una gradazione (es. "Black Pro-Mist 1/4"), o un
  vetro senza gradazione. Uno per tipo: non ci sono doppioni.
- **Stadi**: i cassetti del mattebox, da 1 a 4, numerati da **1 = lato lente**
  verso il fronte. Ogni stadio contiene un vetro del kit oppure è vuoto.

Non esiste più il concetto di "montato": un vetro è montato se sta in uno stadio.
Un vetro può stare in un solo stadio alla volta.

### L'ND occupa lo stadio 1

- Quando l'ND c'è (valore diverso da "nessuno"), sta nello **stadio 1**: la
  riga 1 mostra l'ND con il suo tipo (es. `IRND 0,6`) e gli altri vetri hanno a
  disposizione gli stadi dal 2 in poi.
- Quando l'ND è tolto, lo stadio 1 è libero e può ospitare un altro vetro.
- Quando l'ND rientra e lo stadio 1 è occupato, i vetri **scalano di uno stadio
  verso il fronte**: il vetro dello stadio 1 passa al 2, e così via finché uno
  trova uno stadio vuoto. Se non c'è posto, l'ultimo vetro esce dal mattebox.
- Quando l'ND è tolto, gli altri vetri **tornano indietro verso la lente** di
  tanti stadi quanti ne occupava l'ND (es. `ND 0,6 / BPM / Pola` diventa
  `BPM / Pola / —`).

### Due ND insieme

- Un totale che il kit non ha come vetro singolo si può fare con **due vetri**
  (es. kit 0,3 · 0,6 · 0,9 · 1,2 · 1,8: 1,5 = 1,2 + 0,3 e 2,1 = 1,8 + 0,3).
  Nello strumento web si spuntano i totali voluti; solo quelli entrano nella
  scala, in ordine di densità insieme ai vetri singoli.
- Se il kit ha già quel valore come vetro singolo, la coppia non si propone.
  Se un totale si fa in più modi vale la coppia con il vetro più denso. Mai
  più di due vetri.
- Una coppia occupa gli **stadi 1 e 2**: il vetro più denso nello stadio 1.
  Gli altri vetri scalano verso il fronte con la stessa regola di sopra.
- Passando **da due ND a uno**, gli altri vetri tornano indietro di uno stadio,
  così restano accanto all'ND (es. `ND 1,2 / ND 0,3 / BPM` diventa
  `ND 1,2 / BPM / —`). È la stessa regola di quando l'ND è tolto: i vetri
  tornano indietro di tanti stadi quanti l'ND ne libera, tutti insieme, quindi
  uno stadio lasciato vuoto fra due vetri resta vuoto.
- Il riquadro grande dell'ND cambia a ogni tocco. Gli stadi si riordinano solo
  quando il valore resta **fermo per 2 secondi** (o al primo tasto che non è
  ±ND): passare da una coppia mentre si scorre non sposta e non toglie nulla.

## Schermata sul dispositivo

- ND e diottria grandi, come valori principali.
- Sotto (a destra in orizzontale), una riga per stadio, sempre tutte: numero
  dello stadio, poi il vetro; `—` se lo stadio è vuoto. L'altezza delle righe
  dipende dal numero di stadi, così entrano sempre tutte senza scorrere. Il
  testo dei vetri ha una misura sola, la più grande che fa entrare tutti i
  nomi del kit.
- Se il kit dei vetri è vuoto la lista degli stadi non compare.

## Tasti nella schermata filtri

- A|B = diottria −/+, C|D = ND −/+. Ruotato di 90° in senso orario i due
  bilancieri si scambiano, così ognuno comanda il valore che gli sta accanto.
  "+" sta sempre sul capo in alto o a destra per chi guarda.
- E = pagina successiva, F = pagina precedente (in ciclo, su tutte le pagine),
  E tenuto = blocco rotazione. A e B non cambiano mai pagina.
- Le pressioni lunghe (0,8 s) scattano allo scadere del tempo, con il tasto
  ancora premuto: si vede subito l'effetto e il rilascio che segue non conta.
  Le pressioni brevi si leggono al rilascio.

## Modifica degli stadi

- **Tieni A o B** per entrare. La riga dello stadio selezionato va in negativo,
  sulla stessa schermata (ND e diottria restano visibili). Si parte dal primo
  stadio selezionabile. Niente cursori lampeggianti.
- **A/B** spostano la selezione fra gli stadi (si ferma al primo e all'ultimo).
  Gli stadi occupati dall'ND non si selezionano: si parte dal primo libero. Se
  l'ND li occupa tutti, la modifica non si apre.
- **C/D** cambiano il vetro dello stadio selezionato. L'ordine è: vuoto, poi
  tutti i vetri del kit nell'ordine del kit (per famiglia), saltando quelli già
  in un altro stadio. Arrivati in fondo si ricomincia dall'inizio.
- **E o F** per uscire, con un tocco solo; vale anche tenere di nuovo A o B.
  In modifica E e F fanno soltanto questo: non cambiano schermata e non
  bloccano la rotazione.
- Dopo **5 secondi** senza tocchi la modifica è accettata e si esce da soli:
  uscire a mano non è necessario.
- In modifica ND e diottria non si cambiano.
- Le indicazioni dei tasti diventano ▲ ▼ per A|B e − + per C|D ("+" in alto o
  a destra, come fuori dalla modifica).
- Aggiornamenti parziali della riga mentre si modifica, completo all'uscita.
- Il dispositivo ricorda il contenuto degli stadi, come ND e diottria.

## Strumento web

- **ND** e **Diottrie**: kit, formato, tipo e valori iniziali. Sotto il kit ND,
  "Combinazioni di due ND": i totali possibili con i loro due vetri, da spuntare.
- **Vetri nel kit**: famiglie dal catalogo (`data/filters.json`) o
  personalizzate, con le loro gradazioni. Ogni gradazione e ogni famiglia ha
  una ✕ per toglierla. Togliere un vetro che sta in uno stadio svuota lo stadio.
- **Mattebox**: numero di stadi e, per ogni stadio, il vetro di partenza scelto
  da un menu (i vetri già in un altro stadio non sono selezionabili). Gli stadi
  occupati dall'ND di partenza (uno, o due per una coppia) non si scelgono.
- Il simulatore tiene il suo stato (ND, diottria, stadi), come il dispositivo:
  quello che cambi con i tasti non tocca i valori di partenza. Riparte da
  quelli quando cambi i filtri nello strumento.
- I progetti salvati prima di questa versione vengono convertiti: i vetri che
  erano "montati" finiscono negli stadi, in ordine.

## Esportazione (contratto con il firmware)

Cartella `NN_filters/`, accanto allo sfondo `NN_filters.bmp` / `.p.bmp`. Lo
sfondo ha vuoti i riquadri di ND e diottria e l'area degli stadi.

`screen.ini` (versione 2):

- `nd.*`, `dio.*`, `keys.<gradi>`, `hints.<gradi>`: ND, diottria e tasti fuori
  dalla modifica. `nd.labels` è la scala (passo 0 = nessuno), coppie comprese.
  `nd.glasses` sono i vetri ND del kit; `nd.parts` dice per ogni passo quali
  vetri lo fanno (indici da 1 in `nd.glasses`, `4+1` per una coppia con il
  primo nello stadio 1, `-` = nessuno). `nd.settle`: secondi di valore fermo
  prima di riordinare gli stadi.
- `kit.count`, `kit.labels`: i vetri, nell'ordine in cui si scorrono.
- `stages.count`, `stages.start`: numero di stadi e vetro di partenza di ognuno
  (indice nel kit, `-` = vuoto). `stages.nd`: il primo stadio dell'ND (1); gli
  stadi occupati dall'ND di partenza sono vuoti in `stages.start`.
- `<verso>.list` (x, y, larghezza, altezza dell'area), `<verso>.listRow`
  (altezza di una riga), `<verso>.listNum` (larghezza della colonna del numero).
- `edit.keys.<gradi>`, `edit.hints.<gradi>`, `edit.timeout` (secondi).

Pezzi, tutti `.bmp` con `.p.bmp` per il verticale:

- `ndI`, `dioI`: riquadri dei valori (I = passo).
- `stN`, `stsN`: colonna del numero dello stadio N (da 1), normale e selezionata.
- `fxG`, `fxsG`: vetro G (indice nel kit, `n` = vuoto), normale e selezionato.
- `fxdK`: il vetro ND K (da 1, come in `nd.glasses`) per le righe degli stadi
  dell'ND; solo normale, perché quelle righe non si selezionano.

Una riga si compone con `stN` a sinistra e `fxG` (o `fxdK`) subito a destra; fra una riga
e la successiva il firmware disegna un separatore nero da 2 px, in cima alla
riga. Comporre i pezzi deve dare l'anteprima dello strumento web pixel per pixel.
