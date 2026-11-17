# Filtri: ND, diottria e stadi del mattebox

Specifica della schermata filtri. Vale per lo strumento web (`companion/`), per
il simulatore e per il firmware (`device/`), che devono restare allineati.

## Concetti

- **ND**: un valore solo, quello totale davanti alla lente. Si scorre fra i
  valori del kit ND, nel formato scelto (densità, stop o fattore) e con il tipo
  scelto (ND, IRND, FSND).
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
- Togliere l'ND non sposta gli altri vetri.

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
- E = schermata successiva, F = lens chart, E tenuto = blocco rotazione.

## Modifica degli stadi

- **Tieni A o B** per entrare. La riga dello stadio selezionato va in negativo,
  sulla stessa schermata (ND e diottria restano visibili). Si parte dal primo
  stadio selezionabile. Niente cursori lampeggianti.
- **A/B** spostano la selezione fra gli stadi (si ferma al primo e all'ultimo).
  Con l'ND montato lo stadio 1 non si seleziona: si parte dal 2. Se il mattebox
  ha un solo stadio ed è dell'ND, la modifica non si apre.
- **C/D** cambiano il vetro dello stadio selezionato. L'ordine è: vuoto, poi
  tutti i vetri del kit nell'ordine del kit (per famiglia), saltando quelli già
  in un altro stadio. Arrivati in fondo si ricomincia dall'inizio.
- **E o F** per uscire, con un tocco solo; vale anche tenere di nuovo A o B.
  In modifica E e F fanno soltanto questo: non cambiano schermata e non
  bloccano la rotazione. Si esce da soli dopo **30 secondi** senza tocchi.
- In modifica ND e diottria non si cambiano.
- Le indicazioni dei tasti diventano ▲ ▼ per A|B e − + per C|D ("+" in alto o
  a destra, come fuori dalla modifica).
- Aggiornamenti parziali della riga mentre si modifica, completo all'uscita.
- Il dispositivo ricorda il contenuto degli stadi, come ND e diottria.

## Strumento web

- **ND** e **Diottrie**: kit, formato, tipo e valori iniziali.
- **Vetri nel kit**: famiglie dal catalogo (`data/filters.json`) o
  personalizzate, con le loro gradazioni. Ogni gradazione e ogni famiglia ha
  una ✕ per toglierla. Togliere un vetro che sta in uno stadio svuota lo stadio.
- **Mattebox**: numero di stadi e, per ogni stadio, il vetro di partenza scelto
  da un menu (i vetri già in un altro stadio non sono selezionabili). Se l'ND
  di partenza non è "nessuno", lo stadio 1 è suo e non si sceglie.
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
  dalla modifica.
- `kit.count`, `kit.labels`: i vetri, nell'ordine in cui si scorrono.
- `stages.count`, `stages.start`: numero di stadi e vetro di partenza di ognuno
  (indice nel kit, `-` = vuoto). `stages.nd`: lo stadio dell'ND (1); se
  `nd.start` non è 0 quello stadio parte vuoto in `stages.start`.
- `<verso>.list` (x, y, larghezza, altezza dell'area), `<verso>.listRow`
  (altezza di una riga), `<verso>.listNum` (larghezza della colonna del numero).
- `edit.keys.<gradi>`, `edit.hints.<gradi>`, `edit.timeout` (secondi).

Pezzi, tutti `.bmp` con `.p.bmp` per il verticale:

- `ndI`, `dioI`: riquadri dei valori (I = passo).
- `stN`, `stsN`: colonna del numero dello stadio N (da 1), normale e selezionata.
- `fxG`, `fxsG`: vetro G (indice nel kit, `n` = vuoto), normale e selezionato.
- `fxdI`: l'ND al passo I (da 1) per la riga dello stadio 1; solo normale,
  perché quella riga non si seleziona mentre c'è l'ND.

Una riga si compone con `stN` a sinistra e `fxG` (o `fxdI`) subito a destra; fra una riga
e la successiva il firmware disegna un separatore nero da 2 px, in cima alla
riga. Comporre i pezzi deve dare l'anteprima dello strumento web pixel per pixel.
