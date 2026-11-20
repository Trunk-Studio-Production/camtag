#!/usr/bin/env python3
"""Prova guidata dell'X3 dal terminale, con riscontro dal vivo.

Legge la seriale di un firmware compilato con CAMTAG_DEBUG e mostra subito ogni
tasto premuto e i valori dell'accelerometro, un passo alla volta e senza limiti
di tempo. Se l'X3 va in sonno aspetta che torni.

    .venv/bin/python scripts/taratura.py tasti
    .venv/bin/python scripts/taratura.py versi [--carica] [--out risultato.json]
    .venv/bin/python scripts/taratura.py card
    .venv/bin/python scripts/taratura.py log [--file x3.log]

`card` prova con le card vere sopra: verso della card e direzione dei bilancieri
sulla lens chart nelle quattro posizioni, poi il blocco della rotazione.

`log` mostra dal vivo quello che l'X3 scrive (tasti, versi, card, ricezione) e
con `--file` lo salva anche, accelerometro compreso.

`--carica` compila e carica il firmware prima di cominciare.
"""
import argparse
import collections
import glob
import json
import re
import select
import statistics
import subprocess
import sys
import time
from pathlib import Path

import serial

REPO = Path(__file__).resolve().parent.parent
B, DIM, OK, BAD, WARN, X = '\033[1m', '\033[2m', '\033[32m', '\033[31m', '\033[33m', '\033[0m'

# Tasto logico dell'SDK -> tasto fisico secondo kKeyA…kKeyF in device/src/main.cpp.
KEY_NAMES = {0: 'A', 1: 'B', 2: 'C', 3: 'D', 4: 'F', 5: 'E', 6: 'power'}
KEY_STEPS = [
    ('A', 'bilanciere in basso a sinistra, capo sinistro'),
    ('B', 'bilanciere in basso a sinistra, capo destro'),
    ('C', 'bilanciere in basso a destra, capo sinistro'),
    ('D', 'bilanciere in basso a destra, capo destro'),
    ('F', 'tasto sul lato sinistro'),
    ('E', 'tasto sul lato destro'),
]
POSITIONS = [
    'verticale, tasti in basso',
    'un quarto di giro in senso orario: tasti a sinistra',
    'capovolto: tasti in alto',
    'un quarto di giro in senso antiorario dalla prima: tasti a destra',
]
READINGS = {
    '1': 'dritta',
    '2': 'capovolta',
    '3': 'di lato, cima delle lettere a sinistra',
    '4': 'di lato, cima delle lettere a destra',
}
FLAT_G = 0.85  # come kFlatG nel firmware


class X3:
    def __init__(self, port):
        self.fixed_port = port
        self.ser = None
        self.buf = b''
        self.imu = collections.deque(maxlen=4)
        self.verso = None
        self.keys = collections.deque()
        self.card = None    # (indice, nome del file) dell'ultima card disegnata
        self.boots = 0      # collegamenti: ognuno riavvia l'X3
        self.events = collections.deque()  # righe [camtag] rotation …
        self.on_line = None  # chiamata per ogni riga ricevuta (modalità log)

    def wait_port(self):
        told = False
        while True:
            found = [self.fixed_port] if self.fixed_port else glob.glob('/dev/cu.usbmodem*')
            if found and Path(found[0]).exists():
                return found[0]
            if not told:
                print(f'\r\033[K{WARN}X3 non visibile su USB: premi il power.{X}')
                told = True
            time.sleep(0.2)

    def connect(self):
        port = self.wait_port()
        time.sleep(0.5)
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = 115200
        self.ser.timeout = 0.05
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()
        self.buf = b''
        self.imu.clear()
        self.card = None  # dopo un riavvio la card a schermo può non essere ridisegnata
        self.boots += 1
        print(f'{DIM}collegato a {port}: aspetto che lo schermo si fermi…{X}')

    def close(self):
        if self.ser:
            try:
                self.ser.close()
            except OSError:
                pass
            self.ser = None

    def pump(self):
        """Legge quello che è arrivato. La porta sparisce quando l'X3 va in sonno."""
        try:
            self.buf += self.ser.read(4096)
        except (serial.SerialException, OSError):
            self.close()
            print(f'\r\033[K{WARN}X3 in sonno o scollegato.{X}')
            self.connect()
            return
        while b'\n' in self.buf:
            raw, self.buf = self.buf.split(b'\n', 1)
            line = raw.decode(errors='replace').strip()
            if self.on_line:
                self.on_line(line)
            m = re.match(r'\[imu\] ax=(-?[\d.]+) ay=(-?[\d.]+) az=(-?[\d.]+)', line)
            if m:
                self.imu.append(tuple(float(v) for v in m.groups()))
                continue
            m = re.match(r'\[camtag\] verso (\d)', line)
            if m:
                self.verso = int(m.group(1))
                continue
            m = re.match(r'\[camtag\] tasto logico (\d)', line)
            if m:
                self.keys.append(int(m.group(1)))
                continue
            m = re.match(r'\[camtag\] card (\d+) rot \d+: \S*/([^/]+)$', line)
            if m:
                self.card = (int(m.group(1)), m.group(2))
                continue
            m = re.match(r'\[camtag\] rotation (\w+)', line)
            if m:
                self.events.append(m.group(1))

    def sample(self):
        if not self.imu:
            return None
        return tuple(round(statistics.median(v[i] for v in self.imu), 2) for i in range(3))

    def wait_key(self, status):
        """Aspetta un tasto mostrando una riga di stato che si aggiorna da sola."""
        self.keys.clear()
        while not self.keys:
            self.pump()
            sys.stdout.write(f'\r\033[K   {status()}')
            sys.stdout.flush()
        sys.stdout.write('\r\033[K')
        return self.keys.popleft()


def upload(x3):
    port = x3.wait_port()
    print(f'{DIM}compilo e carico il firmware (lo schermo lampeggia)…{X}')
    run = subprocess.run([str(REPO / '.venv/bin/pio'), 'run', '-e', 'x3', '-t', 'upload', '--upload-port', port],
                         cwd=REPO / 'device', capture_output=True, text=True)
    if run.returncode:
        print(run.stdout[-1500:])
        sys.exit(f'{BAD}caricamento fallito{X}')
    print(f'{OK}firmware caricato{X}')
    time.sleep(2)


def ask(question, answers):
    while True:
        a = input(question).strip()
        if a in answers:
            return a
        print(f'   rispondi con uno tra: {", ".join(answers)}')


def test_keys(x3):
    print(f'\n{B}Prova dei tasti{X}: premi il tasto indicato, una pressione breve.\n')
    wrong = 0
    for i, (name, where) in enumerate(KEY_STEPS, 1):
        print(f'{B}{i}/6  Premi {name}{X} — {where}')
        got = x3.wait_key(lambda: f'{DIM}in attesa…{X}')
        seen = KEY_NAMES.get(got, '?')
        if seen == name:
            print(f'   {OK}✓ ricevuto {name}{X} (tasto logico {got})')
        else:
            wrong += 1
            print(f'   {BAD}✗ il firmware ha letto {seen}{X} (tasto logico {got}), mi aspettavo {name}')
    print(f'\n{OK}Tutti i tasti corrispondono.{X}' if not wrong else f'\n{BAD}{wrong} tasti da correggere.{X}')


def test_positions(x3, out):
    print(f'\n{B}Prova dei versi{X}: tieni l\'X3 dritto davanti a te, non appoggiato.')
    print('In ogni posizione aspetta che lo schermo si fermi, poi premi A.\n')
    results = []
    for i, where in enumerate(POSITIONS, 1):
        print(f'{B}{i}/4  {where}{X}')

        def status():
            s = x3.sample()
            if not s:
                return f'{DIM}aspetto l\'accelerometro…{X}'
            flat = f'  {WARN}troppo piatto: raddrizzalo{X}' if abs(s[2]) > FLAT_G else ''
            return f'ax {s[0]:+.2f}  ay {s[1]:+.2f}  az {s[2]:+.2f}   verso scelto {x3.verso}{flat}   {DIM}premi A{X}'

        while True:
            x3.wait_key(status)
            s = x3.sample()
            if s and abs(s[2]) <= FLAT_G:
                break
            print(f'   {WARN}era troppo piatto: raddrizzalo e premi di nuovo A{X}')
        print(f'   {OK}✓ letto{X}  ax {s[0]:+.2f}  ay {s[1]:+.2f}  az {s[2]:+.2f}   verso scelto {x3.verso}')
        for k, text in READINGS.items():
            print(f'      {k} = {text}')
        a = ask('   Come si legge la scritta sullo schermo? ', READINGS)
        results.append({'posizione': i, 'ax': s[0], 'ay': s[1], 'az': s[2], 'verso': x3.verso, 'scritta': READINGS[a]})
        print()
    print(f'{B}Riepilogo{X}')
    for r in results:
        print(f'   {r["posizione"]}  ax {r["ax"]:+.2f}  ay {r["ay"]:+.2f}  verso {r["verso"]}  scritta {r["scritta"]}')
    if out:
        Path(out).write_text(json.dumps(results, indent=1, ensure_ascii=False))
    print(f'\n{OK}Fatto.{X}')


class Restart(Exception):
    """L'X3 si è riavviato (sonno o porta riaperta): si rifà il passo."""


def ask_live(x3, question, answers):
    """Come ask, ma intanto legge la seriale: se l'X3 va in sonno lo si vede subito."""
    boots = x3.boots
    while True:
        sys.stdout.write(question)
        sys.stdout.flush()
        while not select.select([sys.stdin], [], [], 0.02)[0]:
            x3.pump()
            if x3.boots != boots:
                raise Restart
        a = sys.stdin.readline().strip()
        if a in answers:
            return a
        print(f'   rispondi con uno tra: {", ".join(answers)}')


def wait_until(x3, done, status, restart=True):
    boots = x3.boots
    while not done():
        x3.pump()
        if restart and x3.boots != boots:
            sys.stdout.write('\r\033[K')
            raise Restart
        sys.stdout.write(f'\r\033[K   {status()}')
        sys.stdout.flush()
        time.sleep(0.02)
    sys.stdout.write('\r\033[K')


def is_lens(card):
    return card is not None and re.match(r'\d+_lenschart(-\d+)?\.', card[1]) is not None


# Spostamento sulla lens chart: +1 avanti, -1 indietro (anche girando in tondo).
def page_step(before, after):
    d = after - before
    return 0 if d == 0 else (1 if d == 1 or d < -1 else -1)


def press_rocker(x3, key, expected, where):
    before = x3.card
    print(f'   Premi {B}{key}{X}  {DIM}({where}){X}')
    x3.keys.clear()
    wait_until(x3, lambda: x3.card != before or bool(x3.keys and KEY_NAMES.get(x3.keys[-1]) != key),
               lambda: f'{DIM}in attesa di {key}…{X}')
    if x3.card == before:
        print(f'   {BAD}✗ nessun cambio di pagina{X}')
        return False
    got = page_step(before[0], x3.card[0])
    ok = got == expected
    word = {1: 'avanti', -1: 'indietro'}
    print(f'   {OK + "✓" if ok else BAD + "✗"} {key}: {word.get(got, "?")} → {x3.card[1]}{X}'
          + ('' if ok else f'  (doveva andare {word[expected]})'))
    return ok


# Posizioni come POSITIONS: verso atteso dal firmware e dove stanno i bilancieri per chi guarda.
# Specifica: lensPageDir nello strumento web (avanza il bilanciere a destra o in basso).
CARD_STEPS = [
    (3, 'A|B a sinistra, C|D a destra', -1, 1),
    (2, 'A|B in alto, C|D in basso', -1, 1),
    (1, 'A|B a destra, C|D a sinistra', 1, -1),
    (0, 'A|B in basso, C|D in alto', 1, -1),
]


def test_cards(x3, start=1):
    print(f'\n{B}Prova delle card{X}: tieni l\'X3 in mano, non appoggiato.\n')
    bad = []
    for i, ((verso, rockers, ab, cd), where) in enumerate(zip(CARD_STEPS, POSITIONS), 1):
        if i < start:
            continue
        while True:
            try:
                bad += card_position(x3, i, verso, rockers, ab, cd, where)
                break
            except Restart:
                print(f'   {WARN}l\'X3 si è riavviato: rifaccio la posizione {i}{X}')
        print()

    while True:
        try:
            bad += lock_test(x3)
            break
        except Restart:
            print(f'   {WARN}l\'X3 si è riavviato: rifaccio la prova del blocco{X}')
            # dopo il riavvio il blocco resta salvato: lo si toglie con E tenuto
            print(f'   Se lo schermo dice ROTAZIONE BLOCCATA, tieni E per sbloccare prima di ripartire.')
    print(f'\n{OK}Tutto come previsto.{X}' if not bad else f'\n{BAD}Da correggere:{X}\n   ' + '\n   '.join(bad))


def to_lens_page(x3):
    if not is_lens(x3.card):
        print('   Premi E o F fino a una pagina della lens chart.')
        wait_until(x3, lambda: is_lens(x3.card), lambda: f'{DIM}in attesa…{X}')


def card_position(x3, i, verso, rockers, ab, cd, where):
    bad = []
    print(f'{B}{i}/4  {where}{X}')
    wait_until(x3, lambda: x3.verso == verso,
               lambda: f'{DIM}aspetto che la card giri (ora verso {x3.verso}){X}')
    to_lens_page(x3)
    time.sleep(1.5)  # lascia finire il disegno
    x3.pump()
    if ask_live(x3, f'   La card {x3.card[1]} si legge dritta e intera? [s/n] ', ['s', 'n']) == 'n':
        bad.append(f'{where}: card storta o tagliata')
    if not press_rocker(x3, 'A', ab, rockers):
        bad.append(f'{where}: A nel verso sbagliato')
    if not press_rocker(x3, 'C', cd, rockers):
        bad.append(f'{where}: C nel verso sbagliato')
    return bad


def lock_test(x3):
    bad = []
    print(f'{B}Blocco della rotazione{X}')
    print('   Rimetti l\'X3 verticale con i tasti in basso.')
    wait_until(x3, lambda: x3.verso == 3, lambda: f'{DIM}aspetto il verso verticale (ora {x3.verso}){X}')
    print(f'   Tieni premuto {B}E{X} finché compare ROTAZIONE BLOCCATA.')
    x3.events.clear()
    wait_until(x3, lambda: 'locked' in x3.events, lambda: f'{DIM}in attesa…{X}')
    print(f'   {OK}✓ bloccata{X}')
    print('   Gira l\'X3 di un quarto in senso orario (tasti a sinistra) e aspetta un paio di secondi.')
    if ask_live(x3, '   La card è rimasta com\'era, senza girare? [s/n] ', ['s', 'n']) == 'n':
        bad.append('blocco: la card gira lo stesso')
    to_lens_page(x3)
    print(f'   {DIM}Con la rotazione bloccata i bilancieri seguono comunque la posizione vera.{X}')
    if not press_rocker(x3, 'A', -1, 'A|B ora è in alto'):
        bad.append('blocco: A nel verso sbagliato')
    print(f'   Tieni premuto {B}E{X} per sbloccare.')
    wait_until(x3, lambda: 'auto' in x3.events, lambda: f'{DIM}in attesa…{X}')
    x3.verso = None
    wait_until(x3, lambda: x3.verso == 2, lambda: f'{DIM}aspetto che la card giri…{X}')
    print(f'   {OK}✓ sbloccata, la card ha seguito il verso{X}')

    return bad


# Canvas::Rotation -> posizione fisica, secondo kQuarterTurns in device/src/main.cpp.
VERSI = {0: 'tasti a destra', 1: 'capovolto, tasti in alto', 2: 'tasti a sinistra', 3: 'verticale, tasti in basso'}


def show_log(x3, path):
    log = open(path, 'a', buffering=1) if path else None
    print(f'\n{B}Registro dell\'X3{X} dal vivo (ctrl-C per finire){f": anche in {path}" if path else ""}.\n')

    def on_line(line):
        stamp = time.strftime('%H:%M:%S')
        if log:
            log.write(f'{stamp} {line}\n')
        if not line or line.startswith('[imu]'):
            return
        m = re.match(r'\[camtag\] tasto logico (\d)', line)
        if m:
            line = f'{B}tasto {KEY_NAMES.get(int(m.group(1)), "?")}{X}'
        m = re.match(r'\[camtag\] verso (\d)', line)
        if m:
            line = f'{B}verso {m.group(1)}{X}: {VERSI.get(int(m.group(1)), "?")}'
        print(f'{DIM}{stamp}{X} {line}')

    x3.on_line = on_line
    while True:
        x3.pump()
        time.sleep(0.02)


def main():
    ap = argparse.ArgumentParser(description='Prova guidata dell\'X3 con riscontro dal vivo.')
    ap.add_argument('prova', choices=['tasti', 'versi', 'card', 'log'])
    ap.add_argument('--carica', action='store_true', help='compila e carica il firmware prima di cominciare')
    ap.add_argument('--out', help='salva il risultato della prova dei versi in questo file JSON')
    ap.add_argument('--da', type=int, default=1, help='prova card: riparti da questa posizione (1-4)')
    ap.add_argument('--file', help='modalità log: salva anche in questo file')
    ap.add_argument('--porta', help='porta seriale (di solito si trova da sola)')
    args = ap.parse_args()

    x3 = X3(args.porta)
    try:
        if args.carica:
            upload(x3)
        x3.connect()
        if args.prova == 'tasti':
            test_keys(x3)
        elif args.prova == 'card':
            test_cards(x3, args.da)
        elif args.prova == 'log':
            show_log(x3, args.file)
        else:
            test_positions(x3, args.out)
    except KeyboardInterrupt:
        print(f'\n{WARN}interrotto{X}')
    finally:
        x3.close()


if __name__ == '__main__':
    main()
