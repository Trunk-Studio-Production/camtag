#!/usr/bin/env python3
"""Prova guidata dell'X3 dal terminale, con riscontro dal vivo.

Legge la seriale di un firmware compilato con CAMTAG_DEBUG e mostra subito ogni
tasto premuto e i valori dell'accelerometro, un passo alla volta e senza limiti
di tempo. Se l'X3 va in sonno aspetta che torni.

    .venv/bin/python scripts/taratura.py tasti
    .venv/bin/python scripts/taratura.py versi [--carica] [--out risultato.json]

`--carica` compila e carica il firmware prima di cominciare.
"""
import argparse
import collections
import glob
import json
import re
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


def main():
    ap = argparse.ArgumentParser(description='Prova guidata dell\'X3 con riscontro dal vivo.')
    ap.add_argument('prova', choices=['tasti', 'versi'])
    ap.add_argument('--carica', action='store_true', help='compila e carica il firmware prima di cominciare')
    ap.add_argument('--out', help='salva il risultato della prova dei versi in questo file JSON')
    ap.add_argument('--porta', help='porta seriale (di solito si trova da sola)')
    args = ap.parse_args()

    x3 = X3(args.porta)
    try:
        if args.carica:
            upload(x3)
        x3.connect()
        if args.prova == 'tasti':
            test_keys(x3)
        else:
            test_positions(x3, args.out)
    except KeyboardInterrupt:
        print(f'\n{WARN}interrotto{X}')
    finally:
        x3.close()


if __name__ == '__main__':
    main()
