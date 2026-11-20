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
data sheet collegato senza una fonte più recente, il campo è `null`.
Eccezione: se il costruttore scrive accanto ai metri anche piedi o pollici e
questi danno ragione a una sola delle due fonti, vale quella fonte. In tutti
i casi `notes` riporta i valori in conflitto.

**Eccezione (2026-11-19):** per le serie vintage o solo a noleggio di cui il
costruttore non pubblica nulla (ZEISS Super Speed, Standard Speed, B Speed,
Variable Prime, LWZ.1; Tribe7 Blackwing7 17 e 23.7; Oppenheimer Zeiss Macro;
in parte Sony FE C) sono stati usati schede di noleggi o rivenditori. Ogni
voce lo dice in `notes` ("non dal costruttore") e cita la fonte in `sources`.
Per i rehousing il costruttore è chi fa il rehousing (Whitepoint, TLS,
Oppenheimer) e `notes` dice di chi è il vetro.

## Schema

```json
{ "version": 1, "lenses": [ { ...lente... } ] }
```

Ogni lente ha **tutti** i campi qui sotto (quelli non pertinenti valgono `null`).

| Campo | Tipo | Significato |
|---|---|---|
| `id` | stringa | Stabile, minuscolo, `marca-serie-focale` (zoom: `...-30-105`). Può contenere un punto nelle focali decimali (`canon-flex-zoom-31.5-95`). Non cambiarlo mai: lo strumento lo salva nelle produzioni. |
| `brand` | stringa | Come la scrive il costruttore: `ARRI`, `Cooke`, `ZEISS`, `Leitz`, `Sigma`, `Canon`, `DZOFilm`, `Atlas`, `Angénieux`, `Fujinon`, `Tokina`, `Laowa`, `Sirui`, `Blazar`, `Xeen`. |
| `series` | stringa | Nome della serie. |
| `seriesCode` | stringa | Sigla di 2–4 caratteri maiuscoli (inizia con una lettera), uguale per tutta la serie e unica tra le serie. Va sulla chart. |
| `name` | stringa | Nome commerciale della lente, per la ricerca. |
| `type` | stringa | `prime`, `macro`, `zoom`, `anamorphic`, `probe`, `specialty`. Serve per evidenziare la lente "fuori dal coro". |
| `focal` | numero, mm | Solo per le ottiche fisse; `null` per gli zoom. |
| `focalMin`, `focalMax` | numero, mm | Solo per gli zoom. |
| `tstop` | numero | Apertura massima (negli zoom: alla focale corta): T, oppure f se `stopType` è `f`. |
| `tstopLong` | numero | Solo per gli zoom con ramping: T alla focale lunga. `null` se l'apertura è costante. |
| `stopType` | stringa | `T` di norma; `f` quando il costruttore pubblica solo l'apertura f (Leitz M 0.8, tutte le ottiche fotografiche). Sulla lens chart il valore compare come `f1,4`. |
| `mod_m` | numero, m | Distanza minima di messa a fuoco **dal piano focale**, come pubblicata. |
| `front_mm` | numero, mm | Diametro esterno frontale, quello che serve per l'anello del mattebox. |
| `filterThread` | stringa | Filettatura filtri come pubblicata: `M82`, oppure con passo `M105x0.75`. `null` se non c'è o non è pubblicata. |
| `coverage` | stringa | Classe: `MFT`, `S35`, `S35+`, `FF`, `LF`, `65` (vedi convenzioni). |
| `imageCircle_mm` | numero, mm | Cerchio immagine se pubblicato in mm. |
| `squeeze` | numero | Anamorfiche fisse e zoom anamorfici (es. `2`); `null` sugli altri. Sulla lens chart va accanto al nome della serie (`×2`). |
| `mounts` | lista di sigle | Attacchi offerti dal costruttore, il nativo per primo: `PL`, `LPL`, `EF`, `EF-S`, `EF-M`, `RF`, `E` (Sony), `L`, `Z`, `X`, `G` (Fujifilm), `MFT`, `F` (Nikon), `M` (Leica), `SA`, `A` (Sony), `K` (Pentax), `DL` (DJI), `PV`, `SP70`, `PV65` (Panavision System 65), `B4`. `null` se non dichiarati. Con più attacchi lo strumento web chiede quale quando la lente entra nel kit. |
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
  - Vantage pubblica "CFD" (dal piano focale) e "MOD" (dal fronte lente): si
    usa la CFD, la MOD sta in `notes`.
  - Usate le unità metriche pubblicate. Dove la fonte dà solo piedi e pollici
    (Panavision in parte, TLS, serie vintage) il valore è convertito e
    `notes` riporta l'originale.
- **Copertura.** È la classe dichiarata dal costruttore, ricondotta a sei
  valori: `MFT` (Micro Quattro Terzi, solo ottiche fotografiche); `S35` (anche
  l'APS-C delle ottiche fotografiche, classe approssimata); `S35+`, più grande del Super 35 ma non full frame (classe
  nostra: Tokina 25-75 "super35+", Blazar Cato 40 e 55 con formato 28,8×24);
  `FF` (fino a ~46,5 mm, compresi "FF+", "VV/FF", "Full Frame and beyond");
  `LF` (ARRI Large Format); `65` (formato 65 mm, cerchio ~60 mm: ZEISS
  Panoptes 65, Cooke Panchro 65/i, Leitz Thalia 65).
- **Macro dentro una serie.** Restano nella serie con `type: "macro"` (es.
  ARRI Master Macro 100, Cooke Panchro/i 65 mm, DZOFilm Vespid 90).
- **Varianti con la stessa ottica** non sono duplicate: ZEISS Supreme Prime
  Radiance (come Supreme Prime) e CP.3 XD (come CP.3). Lo dice `notes`.
- **Attacchi.** Se le specifiche cambiano con l'attacco, vale il PL (annotato).
- **Fuori produzione.** Le serie le cui schede sono ancora sul sito del
  costruttore restano, con "fuori produzione" in `notes`: ARRI Ultra Prime,
  Leitz Summicron-C, Sigma Cine FF Zoom 24-35, Canon CN-E14.5-60 e 15.5-47.
- **Zoom anamorfici** (Laowa Nanomorph Zoom, Proteus Zoom, Sunlight; Blazar
  Sentinel; Angénieux Optimo Anamorphic): `type: "zoom"` con `squeeze`. Anche i
  Laowa Probe Zoom sono `zoom`, non `probe`.
- **Solo mirrorless.** Sono incluse anche le ottiche cine (T-stop, ghiere a
  ingranaggi) senza attacco PL.
- **Attacchi** (`mounts`, 2026-11-19). Solo quelli che il costruttore vende o
  monta lui (kit IMS, baionette intercambiabili), non gli adattatori né i kit di
  terzi (ZEISS Nano Prime: solo E). I protocolli dati (/i, LDS, eXtended Data)
  non sono attacchi. Se cambiano da una focale all'altra la differenza è nella
  singola lente (Canon CN-E Zoom, Laowa Zero-D, Sirui Night Walker). La fonte
  dell'elenco è in `sources`. Casi da sapere:
  - Panavision non dichiara l'attacco sulle pagine prodotto: `null`, tranne le
    serie 65 e large format che lo scrivono (SP70, PV65). ZEISS B Speed:
    `null` (baionetta ARRI solo da un noleggio).
  - Vantage: dalla Vantage Lens Table 2023, "PL-Mount, unless stated
    otherwise" (LPL dove indicato).
  - Da schede di noleggio (come per gli altri dati di queste serie): ZEISS
    Super Speed, Standard Speed, Variable Prime, Oppenheimer Zeiss Macro.
  - Laowa: laowacine.com bloccava la lettura diretta; elenchi presi dalle sue
    pagine attraverso la ricerca, da ricontrollare.
- **Ottiche fotografiche** (dal 2026-11-19: Sony, Panasonic LUMIX, Sigma,
  Voigtländer): catalogo attuale del costruttore, `stopType: "f"`, zoom ad
  apertura variabile con `tstopLong`. `front_mm` è il diametro massimo del
  barile pubblicato (non un anello per mattebox); `filterThread` è la
  filettatura frontale. La stessa ottica in più attacchi è una voce sola, con
  gli attacchi in `notes`. L'id aggiunge l'apertura (`sony-fe-50-f1.4-gm`)
  perché nella stessa serie ci sono più ottiche con la stessa focale.
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
| Cooke | S8/i FF | S8 | 12 | FF |
| Cooke | SP3 | SP3 | 6 | FF |
| Cooke | Anamorphic/i S35 | ANS | 10 | S35 |
| Cooke | Anamorphic/i FF | ANF | 8 | FF |
| Cooke | AP3 | AP3 | 3 | FF |
| Cooke | Macro/i FF | MAC | 3 | FF |
| Cooke | Panchro 65/i | P65 | 6 | 65 |
| Cooke | S4 FF/i | S4F | 6 | FF |
| Cooke | 5/i | C5 | 9 | S35 |
| Cooke | miniS4/i | MS4 | 10 | S35 |
| ZEISS | Supreme Prime | SUP | 14 | FF |
| ZEISS | CP.3 | CP3 | 10 | FF |
| ZEISS | Lightweight Zoom LWZ.3 | LWZ | 1 | S35 |
| ZEISS | CZ.2 | CZ2 | 3 | FF |
| ZEISS | Nano Prime | NP | 6 | FF |
| ZEISS | Aatma | AAT | 9 | FF |
| ZEISS | Horizon Anamorphic | HZA | 7 | FF |
| ZEISS | Panoptes 65 | PNP | 10 | 65 |
| ZEISS | Supreme Zoom Radiance | SZR | 3 | FF |
| Leitz | Summilux-C | SLX | 12 | S35 |
| Leitz | Summicron-C | SCR | 11 | S35 |
| Leitz | Leitz Zoom | LZ | 2 | FF |
| Leitz | Hugo | HUG | 14 | FF |
| Leitz | Elsie | ELS | 13 | FF |
| Leitz | Hektor | HEK | 6 | FF |
| Leitz | Leitz Prime | LPR | 13 | FF |
| Leitz | Thalia 65 | THA | 11 | 65 |
| Leitz | M 0.8 | M08 | 8 | FF |
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
| Angénieux | Optimo Prime | OPR | 12 | FF |
| Angénieux | Optimo Ultra 12x | U12 | 3 | S35/FF |
| Angénieux | Optimo Ultra Compact | OUC | 4 | S35/FF |
| Angénieux | Optimo | OPT | 4 | S35 |
| Angénieux | Optimo Style | OST | 3 | S35 |
| Angénieux | Optimo DP | ODP | 2 | S35 |
| Angénieux | Optimo Anamorphic | OAN | 2 | S35 |
| Fujinon | Premista | PRM | 3 | FF |
| Fujinon | HK Premier | HK | 4 | S35 |
| Fujinon | ZK Cabrio | ZK | 4 | S35 |
| Fujinon | XK Cabrio | XK | 1 | S35 |
| Fujinon | MK | MK | 2 | S35 |
| Tokina | Vista Prime | TVP | 11 | FF |
| Tokina | Cinema Zoom | TCZ | 4 | S35/S35+/FF |
| Tokina | Cinema ATX Macro | TCM | 1 | FF |
| Laowa | Nanomorph 1.5X Anamorphic (S35) | NMO | 6 | S35 |
| Laowa | Nanomorph LF 1.5X Anamorphic | NML | 4 | FF |
| Laowa | Nanomorph 1.5X Anamorphic Zoom | NMZ | 2 | S35 |
| Laowa | Proteus 2X Anamorphic | PRO | 7 | S35 |
| Laowa | Proteus 2X Anamorphic Zoom | PRZ | 2 | S35 |
| Laowa | Sunlight 2X FF Anamorphic Zoom | SUN | 2 | FF |
| Laowa | Ranger FF Compact Cine Zoom | RGF | 3 | FF |
| Laowa | Ranger S35 Compact Cine Zoom | RGS | 3 | S35 |
| Laowa | OOOM Cine Zoom | OOM | 2 | S35 |
| Laowa | Ultima Zoom | ULT | 3 | S35/FF |
| Laowa | Argus T1 Cine (S35) | AGS | 3 | S35 |
| Laowa | Argus T1 Cine (FF) | AGF | 3 | FF |
| Laowa | Sword Macro FF Cine | SWD | 6 | FF |
| Laowa | Wide Angle VV Cine | WVV | 4 | FF |
| Laowa | Zero-D Shift Cine | ZSH | 2 | FF |
| Laowa | FF Zoom Fisheye Cine | FEZ | 1 | FF |
| Laowa | Pro2be Probe | PRB | 1 | FF |
| Laowa | Probe Zoom | PBZ | 2 | FF |
| Laowa | 24mm T14 2X PeriProbe | PPR | 1 | FF |
| Laowa | 24mm T14 2X Macro Probe | MPB | 1 | FF |
| Laowa | Zero-D Cine | ZDC | 4 | S35/FF |
| Laowa | 2X Macro APO Cine | MAP | 1 | S35 |
| Sirui | Saturn | SAT | 3 | FF |
| Sirui | Saturn V2 | SA2 | 3 | FF |
| Sirui | Venus | VEN | 6 | FF |
| Sirui | Night Walker | NWK | 5 | S35 |
| Sirui | Vision Prime 1 | VP1 | 6 | FF |
| Sirui | Jupiter | JUP | 6 | FF |
| Sirui | IronStar | IRS | 6 | FF |
| Sirui | Astra | AST | 3 | FF |
| Sirui | Anamorfiche AF 1.33x S35 | A133 | 2 | S35 |
| Blazar | Remus 1.5X | REM | 8 | S35/FF |
| Blazar | Remus II 1.5X | RM2 | 1 | FF |
| Blazar | Cato 2X | CTO | 4 | S35+/FF |
| Blazar | Mantis 1.33X | MAN | 6 | FF |
| Blazar | Viper 1.5X | VIP | 3 | FF |
| Blazar | Talon 1.5X AF | TAL | 3 | FF |
| Blazar | Beetle 1.33X | BTL | 3 | FF |
| Blazar | Sentinel 1.33X Zoom | SEN | 1 | FF |
| Blazar | Apex 1.33X AF | APX | 2 | S35 |
| Xeen | Classic | XEN | 8 | FF |
| Xeen | CF | XCF | 6 | FF |
| Xeen | Meister | XMS | 5 | FF |
| Xeen | Anamorphic | XAN | 1 | FF |
| ZEISS | CP.2 | CP2 | 12 | FF/S35+ |
| ZEISS | CP.2 Super Speed | CPS | 3 | FF |
| ZEISS | Lightweight Zoom LWZ.1 | LW1 | 1 | S35 |
| ZEISS | Lightweight Zoom LWZ.2 | LW2 | 1 | S35 |
| ZEISS | Super Speed MKII | SS2 | 6 | S35 |
| ZEISS | Super Speed MKIII | SS3 | 6 | S35 |
| ZEISS | Standard Speed | STS | 15 | S35 |
| ZEISS | B Speed MKI | BSP | 5 | S35 |
| ZEISS | Variable Prime | VPR | 3 | S35 |
| Sigma | Aizu Prime | SAZ | 11 | LF |
| Sigma | FF Classic Prime | SCP | 11 | FF |
| Sigma | AF Cine | SAF | 2 | FF |
| Angénieux | Optimo DP | ODP | 2 | S35 |
| Laowa | 24mm T14 2X Macro Probe | MPB | 1 | FF |
| Sony | FE C | SFC | 1 | FF |
| NiSi | Athena Prime | NAT | 8 | FF |
| Tribe7 | Blackwing7 | BW7 | 10 | LF |
| Panavision | B Series | PVB | 5 | S35 |
| Panavision | C Series | PVC | 9 | S35 |
| Panavision | E Series | PVE | 8 | S35 |
| Panavision | G Series | PVG | 8 | S35 |
| Panavision | T Series | PVT | 10 | S35 |
| Panavision | Primo Anamorphic | PRA | 5 | S35 |
| Panavision | Macro Anamorphic Prime | PMA | 5 | S35 |
| Panavision | Anamorphic Zoom | PAZ | 6 | S35/S35+ |
| Panavision | Ultra Panatar | UPN | 9 | FF |
| Panavision | Ultra Panatar II | UP2 | 8 | FF |
| Panavision | Ultra Vista | UVS | 7 | FF |
| Panavision | VA | PVA | 11 | FF |
| Panavision | Primo | PRI | 14 | S35 |
| Panavision | Primo V | PRV | 9 | S35 |
| Panavision | Primo Zoom | PZM | 8 | S35 |
| Panavision | Primo 70 | PR70 | 13 | FF |
| Panavision | Primo 65 | PR65 | 12 | 65 |
| Panavision | Primo X | PRX | 3 | FF |
| Panavision | Primo Artiste | PAR | 11 | FF |
| Panavision | PVintage | PVN | 9 | S35 |
| Panavision | Sphero 65 | SPH | 9 | 65 |
| Panavision | System 65 | S65 | 9 | 65 |
| Panavision | 65 Vintage | V65 | 11 | 65 |
| Panavision | Super Speed | SSP | 5 | S35 |
| Panavision | Ultra Speed | USP | 12 | S35 |
| Panavision | Panaspeed | PSP | 12 | FF |
| Panavision | Macro | PVM | 8 | S35 |
| Panavision | H Series | HS | 18 | FF |
| Panavision | Large Format Zoom | LFZ | 6 | FF/65 |
| Panavision | Flare | FLR | 11 | S35 |
| Whitepoint Optics | HS | WHS | 7 | FF |
| Whitepoint Optics | TS70 | T70 | 7 | 65 |
| Whitepoint Optics | Nikon AI-S | WNK | 13 | FF |
| TLS | Petzval | PTZ | 4 | LF |
| TLS | Super Baltar | SBL | 8 | — |
| Schneider-Kreuznach | Cine-Xenar III | CX3 | 7 | S35 |
| Schneider-Kreuznach | Xenon FF-Prime | XFF | 6 | FF |
| P+S Technik | Evolution 2X | EVO | 6 | S35/FF |
| P+S Technik | Technovision 1.5X | TNV | 7 | FF |
| P+S Technik | Technovision 1.5X Zoom | TNZ | 2 | FF |
| P+S Technik | Technovision 1.5X AproXima | TAX | 5 | FF |
| Oppenheimer | Zeiss Macro | OZM | 1 | FF |
| Vantage | HAWK class-X | HCX | 9 | S35 |
| Vantage | HAWK class-X 2:1 Zoom | HXZ | 2 | S35 |
| Vantage | HAWK C-Series | HCS | 6 | S35 |
| Vantage | HAWK 3:1 Zoom | H3Z | 2 | S35 |
| Vantage | HAWK 5:1 Zoom | H5Z | 1 | S35 |
| Vantage | HAWK MHX | MHX | 10 | LF |
| Vantage | MiniHAWK | MHK | 9 | S35 |
| Vantage | HAWK65 | H65 | 11 | LF |
| Vantage | HAWK65 Vintage'74 | H6V | 11 | LF |
| Vantage | VANTAGE One | VON | 10 | S35 |
| Vantage | VANTAGE One4 | VO4 | 10 | LF |
| Vantage | VANTAGE LEITZ Macro | VLM | 10 | FF |
| Vantage | VANTAGE Lightweight Zoom | VLZ | 2 | FF |
| Vantage | HAWK V-Lite | HVL | 9 | S35 |
| Vantage | HAWK V-Lite Vintage'74 | HVLV | 9 | S35 |
| Vantage | HAWK V-Lite 1.3x | HVL3 | 11 | S35 |
| Vantage | HAWK V-Lite Vintage'74 1.3x | HLV3 | 11 | S35 |
| Vantage | HAWK V-Plus | HVP | 11 | S35 |
| Vantage | HAWK V-Plus Zoom | HVPZ | 2 | S35 |
| Vantage | HAWK V-Plus Vintage'74 Zoom | HPZV | 2 | S35 |
| Vantage | HAWK V-Plus 1.3x Zoom | HPZ3 | 3 | S35 |
| Vantage | HAWK V-Plus Vintage'74 1.3x Zoom | HZV3 | 3 | S35 |
| Vantage | HAWK V-Series | HVS | 13 | S35 |
| Sigma | Reflex (foto) | SRX | 44 | FF/S35 |
| Sigma | Art (reflex) (foto) | SRA | 19 | FF/S35 |
| Sigma | Sports (reflex) (foto) | SRS | 5 | FF |
| Sigma | Contemporary (reflex) (foto) | SRC | 5 | FF/S35 |
| Sigma | DG Art (foto) | SGA | 19 | FF |
| Sigma | DG Contemporary (foto) | SGC | 15 | FF |
| Sigma | DG Sports (foto) | SGS | 6 | FF |
| Sigma | DC Contemporary (foto) | SDC | 10 | S35 |
| Sigma | DC Art (foto) | SDA | 1 | S35 |
| Sony | FE (foto) | SFE | 58 | FF |
| Sony | E PZ (foto) | SEP | 6 | S35 |
| Sony | FE PZ (foto) | SPZ | 2 | FF |
| Panasonic | LUMIX G Leica DG (foto) | LDG | 15 | MFT |
| Panasonic | LUMIX G (foto) | LMG | 14 | MFT |
| Panasonic | LUMIX S PRO (foto) | LSP | 5 | FF |
| Panasonic | LUMIX S (foto) | LMS | 17 | FF |
| Voigtländer | Nokton (foto) | VNK | 33 | FF/S35/MFT |
| Voigtländer | Super Nokton (foto) | VSN | 1 | MFT |

Totale: 1544 lenti, 210 serie. Verificate il 2026-11-17 le prime 32 serie, il 2026-11-18 le altre fino a Xeen, il 2026-11-19 le serie aggiunte in fondo alla tabella (cine, poi le ottiche fotografiche).

Gli Angénieux EZ cambiano focale e T secondo il blocco posteriore montato
(S35 o FF/VistaVision): ogni zoom compare due volte, una voce per
configurazione (es. EZ-1 S35 30-90 T2 e EZ-1 FF 45-135 T3).

## Valori `null` da conoscere

**MOD**: oggi nessuno. Atlas Orion 40 ha 0.6 m per decisione del
proprietario (2026-11-17), perché la scheda Atlas pubblica "2.0 ft / .56 m",
valori che non coincidono tra loro.

Conflitti risolti con la pagina prodotto (2026-11-17): ARRI Ultra Prime 16, 28
e 40 (contro la brochure del 2003); ZEISS Supreme Prime 21 e 135 e CZ.2 70-200
(contro il manuale per il cambio attacco). Leitz Summilux-C 18, 21, 25, 35, 40,
65, 75 e 100 (2026-11-18): usati i metri della pagina prodotto, confermati dai
piedi scritti su entrambe le fonti (il data sheet arrotonda per eccesso, es.
0.5 m contro 0.45 m e 1'6"). Il 16 ha 0.45 m su entrambe, ma 1'4" ≈ 0,41 m:
tenuto 0.45. Cooke 5/i 135 e miniS4/i 40: usato il PDF, confermato dai piedi.

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
  manuale EZ-1/EZ-2), per la gamma Optimo pagine prodotto e schede PDF; per
  l'Optimo DP, non più sul sito, le schede PDF del 2013 nella copia di
  web.archive.org
- Fujinon: www.fujifilm.com, www.fujifilm-x.com, asset.fujifilm.com (manuali)
- Tokina: tokinalens.com, tokinacinema.com
- Laowa: laowacine.com, www.venuslens.net. La tabella del Nanomorph Zoom è
  un'immagine nella pagina laowacine.com ospitata su c1.iggcdn.com.
- Sirui: store.sirui.com (il negozio è il sito prodotti di Sirui)
- Blazar: blazarlens.com
- Xeen: www.xeenglobal.com (pagine delle serie e manuali PDF linkati)
- Panavision: www.panavision.com (pagine prodotto con la tabella della serie;
  copie del sito 2019-2020 su web.archive.org solo come conferma)
- Vantage: www.vantagefilm.com (pagine prodotto, data sheet PDF, Lens Table)
- Sigma (Aizu, Classic, AF Cine e ottiche foto, anche fuori produzione): www.sigma-global.com
- Sony: www.sony.jp (schede spec.html)
- Panasonic: panasonic.jp (LUMIX S), panasonic.com UK e Canada (LUMIX G)
- Voigtländer: www.cosina.co.jp/voigtlander/en/
- NiSi: en.nisioptics.com e la brochure NiSi 2024
- Whitepoint Optics, TLS, Schneider-Kreuznach, P+S Technik: siti dei
  rispettivi costruttori (brochure Cine-Xenar III dalla copia su lensrentals.com)
- ZEISS CP.2, CP.2 Super Speed, LWZ.2: pagine e brochure ZEISS archiviate
- Fonti non del costruttore (vedi l'eccezione sopra): abelcine.com,
  keslowcamera.com, truelens.co.uk, camaleonrental.com, cbm-cine.com

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
