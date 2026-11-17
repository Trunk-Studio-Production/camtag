# Database lenti di CamTag

`lenses.json` è l'elenco delle lenti che lo strumento web (`companion/`) usa per
cercare, filtrare e generare la lens chart. Le lenti mancanti si inseriscono a
mano nello strumento; qui stanno solo quelle con dati verificati.

```bash
python3 data/validate.py        # controlla schema, unità, id e sigle
```

## Principio

Sul set un dato sbagliato toglie credibilità al badge: **meglio `null` che un
valore stimato**. Ogni valore viene dalle specifiche pubblicate dal costruttore
(pagina prodotto o PDF da lui pubblicato) e ogni lente cita in `sources` gli URL
da cui è stato letto. Non sono stati usati database di terzi (noleggi, rivenditori,
altri lens database). Se due fonti ufficiali dello stesso costruttore non
concordano, vale la **pagina prodotto attuale** sui PDF più vecchi (brochure
storiche, manuali); se il conflitto è dentro la stessa pagina o tra pagina e
data sheet collegato senza una fonte più recente, il campo è `null`. In tutti
i casi `notes` riporta i valori in conflitto.

## Schema

```json
{ "version": 1, "lenses": [ { ...lente... } ] }
```

Ogni lente ha **tutti** i campi qui sotto (quelli non pertinenti valgono `null`).

| Campo | Tipo | Significato |
|---|---|---|
| `id` | stringa | Stabile, minuscolo, `marca-serie-focale` (zoom: `...-30-105`). Può contenere un punto nelle focali decimali (`canon-flex-zoom-31.5-95`). Non cambiarlo mai: lo strumento lo salva nelle produzioni. |
| `brand` | stringa | Come la scrive il costruttore: `ARRI`, `Cooke`, `ZEISS`, `Leitz`, `Sigma`, `Canon`, `DZOFilm`, `Atlas`. |
| `series` | stringa | Nome della serie. |
| `seriesCode` | stringa | Sigla di 2–4 caratteri maiuscoli (inizia con una lettera), uguale per tutta la serie e unica tra le serie. Va sulla chart. |
| `name` | stringa | Nome commerciale della lente, per la ricerca. |
| `type` | stringa | `prime`, `macro`, `zoom`, `anamorphic`, `probe`, `specialty`. Serve per evidenziare la lente "fuori dal coro". |
| `focal` | numero, mm | Solo per le ottiche fisse; `null` per gli zoom. |
| `focalMin`, `focalMax` | numero, mm | Solo per gli zoom. |
| `tstop` | numero | T più aperta (negli zoom: alla focale corta). |
| `tstopLong` | numero | Solo per gli zoom con ramping: T alla focale lunga. `null` se l'apertura è costante. |
| `mod_m` | numero, m | Distanza minima di messa a fuoco **dal piano focale**, come pubblicata. |
| `front_mm` | numero, mm | Diametro esterno frontale, quello che serve per l'anello del mattebox. |
| `filterThread` | stringa | Filettatura filtri come pubblicata: `M82`, oppure con passo `M105x0.75`. `null` se non c'è o non è pubblicata. |
| `coverage` | stringa | Classe dichiarata: `S35`, `FF`, `LF` (vedi convenzioni). |
| `imageCircle_mm` | numero, mm | Cerchio immagine se pubblicato in mm. |
| `squeeze` | numero | Solo per le anamorfiche (es. `2`). |
| `sources` | lista di URL | Le pagine o i PDF del costruttore da cui sono stati letti i valori. |
| `checked` | data ISO | Giorno della verifica. |
| `notes` | stringa | Motivo dei `null`, conflitti tra fonti, varianti, stato di produzione. |

Rispetto allo schema iniziale ho aggiunto `name`, `imageCircle_mm` e `notes`, e
ho reso tutti i campi sempre presenti, così lo strumento web non deve gestire
chiavi mancanti.

## Convenzioni

- **MOD.** I valori sono misurati dal piano focale. Dove il costruttore pubblica
  la distanza dal fronte lente (Cooke sulle pagine web, ARRI in aggiunta per le
  Signature), quel valore non è usato.
  - Cooke: è la "Min. marked object distance" dei PDF, cioè la distanza minima
    segnata sulla scala. Il PDF non scrive esplicitamente il riferimento.
  - Leitz, Canon CN-E L F, Canon Flex Zoom, Atlas Orion: il costruttore non
    dichiara il riferimento. I valori sono tenuti, con una nota, secondo la
    convenzione cine (piano focale).
  - Usate solo unità metriche pubblicate; nessuna conversione da piedi.
- **Copertura.** È la classe dichiarata dal costruttore, ricondotta a tre valori:
  `S35`, `FF` (fino a ~46,5 mm, compresi "FF+", "VV/FF", "Full Frame and
  beyond") e `LF` (ARRI Large Format).
- **Macro dentro una serie.** Restano nella serie con `type: "macro"` (es.
  ARRI Master Macro 100, Cooke Panchro/i 65 mm, DZOFilm Vespid 90).
- **Varianti con la stessa ottica** non sono duplicate: ZEISS Supreme Prime
  Radiance (come Supreme Prime) e CP.3 XD (come CP.3). Lo dice `notes`.
- **Attacchi.** Se le specifiche cambiano con l'attacco, vale il PL (annotato).
- **Fuori produzione.** Le serie le cui schede sono ancora sul sito del
  costruttore restano, con "fuori produzione" in `notes`: ARRI Ultra Prime,
  Leitz Summicron-C, Sigma Cine FF Zoom 24-35, Canon CN-E14.5-60 e 15.5-47.
- **Ordine** nel file: raggruppato per marca e serie. L'ordine di
  visualizzazione lo decide lo strumento web.

## Serie e sigle

| Marca | Serie | Sigla | Lenti | Copertura |
|---|---|---|---|---|
| ARRI | Signature Prime | SIG | 16 | LF |
| ARRI | Signature Zoom | SIZ | 4 | LF |
| ARRI | Master Prime (con Master Macro 100) | MP | 17 | S35 |
| ARRI | Ultra Prime (standard e LDS, fuori produzione) | UP | 29 | S35 |
| Cooke | S4/i | S4 | 18 | S35 |
| Cooke | Panchro/i Classic | PAN | 12 | S35 |
| Cooke | Panchro/i Classic FF | PFF | 12 | FF |
| Cooke | S7/i | S7 | 14 | FF |
| Cooke | Varotal/i FF | VAR | 3 | FF |
| ZEISS | Supreme Prime | SUP | 14 | FF |
| ZEISS | CP.3 | CP3 | 10 | FF |
| ZEISS | Lightweight Zoom LWZ.3 | LWZ | 1 | S35 |
| ZEISS | CZ.2 | CZ2 | 3 | FF |
| Leitz | Summilux-C | SLX | 12 | S35 |
| Leitz | Summicron-C | SCR | 11 | S35 |
| Leitz | Leitz Zoom | LZ | 2 | FF |
| Sigma | Cine FF High Speed Prime | SHS | 11 | FF |
| Sigma | Cine High Speed Zoom | SHZ | 2 | S35 |
| Sigma | Cine FF Zoom | SFZ | 1 | FF |
| Canon | CN-E Prime (L F, EF) | CNE | 7 | FF |
| Canon | Sumire Prime (PL) | SUM | 7 | FF |
| Canon | CN-E Zoom | CNZ | 6 | S35 |
| Canon | Flex Zoom | FLX | 4 | S35/FF |
| DZOFilm | Vespid Prime | VSP | 11 | FF |
| DZOFilm | Vespid Prime 2 | VS2 | 6 | FF |
| DZOFilm | Arles Prime | ARL | 11 | FF |
| DZOFilm | Gnosis Macro | GNO | 4 | FF |
| DZOFilm | Pictor Zoom | PIC | 4 | S35 |
| DZOFilm | Catta Zoom | CAT | 3 | FF |
| DZOFilm | Catta Ace | CTA | 3 | FF |
| Atlas | Orion Series (anamorfiche 2×) | ORN | 12 | S35 |
| Angénieux | Type EZ (EZ-1, EZ-2, EZ-3) | EZ | 6 | S35/FF |

Totale: 276 lenti, 32 serie, verificate il 2026-11-17.

Gli Angénieux EZ cambiano focale e T secondo il blocco posteriore montato
(S35 o FF/VistaVision): ogni zoom compare due volte, una voce per
configurazione (es. EZ-1 S35 30-90 T2 e EZ-1 FF 45-135 T3).

## Valori `null` da conoscere

**MOD**: oggi nessuno. Atlas Orion 40 ha 0.6 m per decisione del
proprietario (2026-11-17), perché la scheda Atlas pubblica "2.0 ft / .56 m",
valori che non coincidono tra loro.

Conflitti risolti con la pagina prodotto (2026-11-17): ARRI Ultra Prime 16, 28
e 40 (contro la brochure del 2003); ZEISS Supreme Prime 21 e 135 e CZ.2 70-200
(contro il manuale per il cambio attacco). Leitz Summilux-C 16, 18, 21, 25, 35,
40, 65, 75 e 100: usati i metri del data sheet PDF, che arrotonda per eccesso
(es. 0.5 m contro 0.45 m della pagina prodotto).

**filterThread** è `null` dove il costruttore non pubblica una filettatura.
Succede per ARRI, ZEISS, Summicron-C, alcune grandangolari e le Sumire (per
queste le tabelle Canon USA e UK sono incoerenti).

**imageCircle_mm** è `null` dove il costruttore dichiara solo la classe:
Canon, ZEISS Supreme e CZ.2, ARRI Ultra Prime standard.

## Fonti

Domini da cui sono stati letti i dati, tutti del costruttore o da lui collegati
come scheda tecnica ufficiale:

- ARRI: www.arri.com (pagine "Technical Data", brochure, Configuration Overview)
- Cooke: cookeoptics.com (pagine prodotto e PDF delle schede)
- ZEISS: www.zeiss.com (pagine prodotto, brochure, manuali per il cambio attacco)
- Leitz: www.leitz-cine.com. I data sheet PDF stanno su api.knots.io, il CDN
  usato da Leitz, linkati come "Data Sheet" dalle pagine prodotto.
- Sigma: www.sigma-global.com, www.sigmaphoto.com
- Canon: www.usa.canon.com, www.canon.co.uk, www.canon-europe.com,
  downloads.canon.com
- DZOFilm: dzofilm.com
- Atlas: atlaslensco.com. Il PDF di specifiche sta su cdn.shopify.com, il CDN
  del loro negozio.
- Angénieux: www.angenieux.com (pagina Type EZ Series, brochure EZ 2025,
  manuale EZ-1/EZ-2)

Gli URL esatti sono in `sources` per ogni lente.

## Lenti modificate dal noleggio o dall'operatore

`lenses.json` contiene solo le specifiche del costruttore e non va modificato
per adattare un esemplare (es. il Sigma 18-35 di Trunk portato a Ø frontale
114 mm). Queste differenze appartengono all'utente, non al database: lo
strumento web le tiene da parte come modifiche locali, legate all'`id` della
lente, nel formato

```json
{ "version": 1,
  "overrides": {
    "sigma-high-speed-zoom-18-35": { "front_mm": 114, "note": "Anello frontale 114 mm (Trunk)" }
  } }
```

- Campi modificabili: `tstop`, `tstopLong`, `mod_m`, `front_mm`,
  `filterThread`, `squeeze`, più `note`. Nello strumento web stanno in
  localStorage (`camtag.lensOverrides.v1`), si esportano e importano, e il file
  di progetto include le modifiche delle sue lenti.
- Lo strumento mostra il valore modificato e segnala che differisce dalla
  scheda del costruttore. Così un aggiornamento del database non cancella le
  modifiche, e si può sempre tornare al valore originale.

## Aggiungere o correggere una lente

1. Usa solo la scheda del costruttore e metti l'URL in `sources`.
2. Aggiorna `checked`; se lasci un `null`, spiega in `notes` il perché.
3. Per una serie nuova scegli una sigla non ancora usata e aggiungila alla
   tabella sopra.
4. `python3 data/validate.py` deve dare 0 errori.
