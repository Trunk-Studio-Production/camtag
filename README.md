English · [Italiano](README.it.md)

# CamTag

**An e-paper badge for the camera department.** Lens chart, filters, crew and
notes on a small e-paper screen mounted on the camera: readable in full sun,
still there when it's off, and it turns with the camera.

<!-- LAUNCH VIDEO: when the 30–60 s on-set video is ready, drag the .mp4 into
the GitHub editor on this line (it becomes a user-attachments link that plays
inline) and keep the image below as the fallback. -->
![CamTag screens: lens chart, filters screen, crew](docs/img/screens.png)

**[▶ Try it in your browser](https://trunk-studio-production.github.io/camtag/)**:
no device needed. Build your cards in the web configurator and play with
them in the simulator, buttons included.

## What it does

- **Lens chart, built for you.** Pick your set from a database of 1,500+ cine
  and photo lenses (210 series), with T-stop, close focus and front diameter
  from the manufacturers' own data sheets. A rental unit that differs (a
  different front ring, say) is a local edit, marked with `*`.
- **Filters screen.** ND and diopter change with the rocker buttons, and the
  mattebox stages show what's in front of the lens, stage by stage.
- **Crew, notes, images.** Any other card you need on the camera.
- **Turns with the camera.** The built-in accelerometer rotates the content
  to any of four orientations, and you can lock it.
- **Wireless.** Send cards (and firmware updates) from the browser over
  Bluetooth, or copy them to the microSD.
- **Battery.** About two and a half days of real use on set, almost two weeks
  idle. When off, it keeps showing your off screen (camera name, logo).
- **English and Italian**, for the device, the cards and the web configurator.

## What you need

- An **[Xteink X3](https://www.xteink.com)**: a small e-paper reader, the
  hardware CamTag runs on. CamTag replaces its firmware.
- A **USB-C data cable and a computer** for the first install, from the
  command line. After that, cards and updates travel over Bluetooth.
- A way to mount it on the camera: we don't ship a mount yet.

> **⚠️ Unofficial firmware: you flash it at your own risk.** CamTag replaces the
> X3's firmware and, in the worst case, can leave it unusable beyond repair.
> There is no warranty of any kind. Before flashing, read
> [Risks and checks before flashing](#risks-and-checks-before-flashing).

## Getting started

1. Make your cards in the [web configurator](https://trunk-studio-production.github.io/camtag/).
2. Run the [three checks](#risks-and-checks-before-flashing), keep the backup,
   and flash CamTag on the X3.
3. Send the cards over Bluetooth (hold **X** on the X3) or via microSD.

The [user manual](https://trunk-studio-production.github.io/camtag/companion/manual/)
explains every button and screen.

## Contribute

- **A lens or filter set is missing?**
  [Add a lens set](https://github.com/Trunk-Studio-Production/camtag/issues/new?template=lens-set.yml)
  or [add a filter set](https://github.com/Trunk-Studio-Production/camtag/issues/new?template=filter-set.yml):
  no code or JSON needed, just the values and a link to the manufacturer's
  page or data sheet. We check them against it and add the whole set.
- **Using CamTag on a production?** We'd love a photo of it on the camera:
  [send it](https://github.com/Trunk-Studio-Production/camtag/issues/new?template=on-set.yml).
  <!-- INSTAGRAM: add the account here when it exists ("or tag us on Instagram"). -->
- **Bugs and ideas**: [open an issue](https://github.com/Trunk-Studio-Production/camtag/issues/new/choose).

Made in Italy by [Trunk Studio Production](https://github.com/Trunk-Studio-Production),
tested on set by working camera assistants. Free software under the GPL v3.

## How it works

1. Create the cards in the web configurator.
2. Send them to the X3 in one of two ways:
   - **Bluetooth**: on the X3 hold **X** until BLUETOOTH appears, then in the
     web configurator tap **Bluetooth** and then **Send cards** (see below);
   - **microSD**: download the zip and copy its `camtag/` folder to the root
     of the microSD.
3. On the device:
   - **X** (left side): previous card; **Y** (right side): next, in a loop
   - **X held (0.8 s)**: receive cards via Bluetooth
   - **Y held (0.8 s)**: lock/unlock rotation
   - long presses fire while you are still holding, when the time runs out:
     you don't need to release to see the effect
   - A, B, C, D don't change card: they flip through the pages of the lens
     chart and, on the filters screen, change ND and diopter (hints next to
     the buttons)
   - **filters screen**: hold **A** or **B** to edit the mattebox stages
     (A/B pick the stage, C/D the glass); you exit with X or Y, by holding A
     or B again, or automatically after 5 s without touches (`docs/filtri.md`)
   - **power**: locks and unlocks the buttons. The page stays as it is, with a
     padlock next to the power button
   - **power held (1 s)**: turns off; when off, turns on at the last card
   - when on, it hears the buttons at any moment: between presses it rests,
     but from the outside you can't tell. If left on, it turns itself off
     after 24 hours without button presses
   - when off it shows the **off screen** (a card chosen in the web configurator, or
     just the name) with a strip saying it is off; after three days the screen
     goes white. Details and power consumption in
     [`docs/batteria.md`](docs/batteria.md)

### Sending via Bluetooth

The web configurator can send the cards to the X3 without removing the microSD, using
the browser's Bluetooth LE (Web Bluetooth).

1. On the X3 hold **X** for 0.8 s: **BLUETOOTH** appears with the device
   name (e.g. `CAMTAG-1A2B`).
2. In the web configurator tap **Bluetooth**, then **Send cards** and choose the
   device.
3. When the transfer is done the X3 shows the first card with **CARDS
   UPDATED**.

The cards arrive in a temporary folder and replace the previous ones only once
the transfer is complete and checked: if the connection drops or something
goes wrong, the previous cards stay on the X3. **X** or **Y** exit receive
mode; after 3 minutes without transfers it exits by itself. Bluetooth is on
only while receiving.

Along with the cards, the **project** (`project.json`) also reaches the X3:
the same file as "Save project". Your work in the web configurator lives only in the
browser, which may delete it (Safari does so after a week without visits). To
get it back, put the X3 in receive mode and tap **Bluetooth** → **Import from
X3**: the cards don't change. Alternatively, open `camtag/project.json` from
the microSD with **Import project**.

Requirements:
- the page must be opened over **HTTPS** (or from `localhost`): on `http://`
  the browser doesn't give pages access to Bluetooth;
- **Android, Mac, Windows, Linux**: Chrome, Edge or Brave (tested on Mac; in
  Brave, Bluetooth must be enabled in `brave://flags`, entry "Web Bluetooth
  API");
- **iPhone and iPad**: Safari has no Web Bluetooth; you need a browser that
  adds it, such as **[BLE Link](https://apps.apple.com/app/id6468414672)**
  (tested) or [Bluefy](https://apps.apple.com/app/id1492822055).

The protocol is described in [`docs/bluetooth.md`](docs/bluetooth.md).

### Automatic rotation

The X3 has a QMI8658 IMU (accelerometer + gyroscope). The firmware reads
gravity and turns the card to any of the 4 orientations, with some hysteresis:
the change happens after about 0.6 s in a stable position. If the device is
lying flat, it keeps the current orientation.

Each card is exported as two files, paired by name:

| File | Format |
|---|---|
| `01_lens_35mm.bmp` | landscape, 792×528 |
| `01_lens_35mm.p.bmp` | portrait, 528×792 |

The firmware picks the right variant and rotates it by 0° or 180°. If a card
has only one of the two variants, it uses it for the other orientation too,
rotated.

Rotation works while the device is on, including at rest. When off, the off
screen stays in the last orientation.

The lock is saved and survives power-off. The X4 has no IMU: there the
rotation stays fixed.

### Languages

The X3 firmware's own messages (no-card notice, Bluetooth screen, off strip,
errors) come in English or Italian. On first boot the X3 asks for the
language (**A** or **B** = English, **C** or **D** = Italian) and keeps asking
at every boot until one is chosen. It can also be set from the web configurator, which
writes `lang=en|it` in `/camtag/settings.ini` with the next card upload
("Don't change" writes nothing).

The web configurator has three independent language choices, in the EN/IT button of
its top bar:
- **interface**: per browser; defaults to Italian if the system language is
  Italian, otherwise English;
- **cards**: per project, saved in `project.json`;
- **X3 messages**.

Cards and X3 in different languages is fine: nothing checks it. The user
manual exists in English (`companion/manual/`) and Italian
(`companion/manuale/`).

## Web configurator

Online: **https://trunk-studio-production.github.io/camtag/** (GitHub Pages,
updated on every push to `main`). It is also the address to open on your phone
to send via Bluetooth.

The screens use the **Inter** font (© The Inter Project Authors, SIL Open Font
License 1.1), included in `companion/fonts/` together with its license.

From the project root (`data/`, with the lens database, is needed too):

```bash
python3 -m http.server 8766
```

Then open http://localhost:8766/companion/. Data stays in the browser;
"Save project" exports a JSON file to import again later.

Screens:
- **Lens chart** (automatic): search the database and add lenses, one by one
  or a whole series; missing lenses can be entered by hand. The lens that
  "stands out" is shown in negative; this can be forced for each lens.
- **Filters** (automatic): ND and diopter kits (the device's +/− buttons
  cycle only through these), ND format, glass kit (several grades per family
  at once) and mattebox stages, from 1 to 3 (`+` opens 4, 5 and 6, for bigger
  matte boxes or a glass mounted outside; see `docs/filtri.md`).
- **Manual cards**: crew, notes, single lens, patch (image).

"Download .zip" creates `camtag.zip`: copy the `camtag/` folder to the root of
the microSD.

**Simulator** (button above the preview): the X3 drawn with all buttons
clickable (also from the keyboard: A–D, X and Y, P = power). Pages cycle with
X (previous) and Y (next); A and B never change page. You can rotate it, lock
rotation (Y held) and turn it off (power held), and it behaves as the firmware
is meant to. Screens are at full resolution (792×528 and 528×792); the
simulator shows them at about half scale, close to the real size.

## Risks and checks before flashing

**Flashing CamTag can make the X3 unusable.** Read this whole section before
plugging in the cable.

### What can go wrong

- CamTag is unofficial firmware, with no connection to Xteink. Flashing
  replaces the original firmware, the bootloader and the partition table, and
  may void the device's warranty.
- **The first flash always goes through USB.** From firmware 0.4.0 CamTag
  also updates over Bluetooth ("Update firmware" in the web configurator,
  confirmed on the X3's buttons), but only to other CamTag versions: going
  back to the original firmware or to CrossPoint needs USB. If USB doesn't
  work on your X3, don't flash CamTag on it.
- **Some units leave the factory with USB flashing locked**, in particular
  those sold by third-party shops rather than xteink.com (documented by
  [CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader#usb-locked-devices-xteink-unlocker)).
  On a locked unit, firmware not supported by their unlock tool can leave the
  device dead for good, and CamTag is not among the supported ones. **Do not
  flash CamTag on a locked unit.**
- We don't know how that lock works. The checks below tell with reasonable
  confidence whether an X3 is at risk, but they are not a guarantee.
- So far CamTag has been tested on a single X3: bought from a third-party
  shop, with original firmware V6.1.15, and not locked.

### Checks: if even one fails, do not flash

You need a USB-C data cable and the X3 **on**: when off it doesn't show up on
USB. `esptool` is the one PlatformIO installs on the first build (if missing:
`.venv/bin/pip install esptool`, then `.venv/bin/python -m esptool`).
Below, `esptool` stands for `~/.platformio/penv/bin/python -m esptool --chip esp32c3`.

1. **The X3 shows up on USB.** With the original firmware running:

   ```bash
   ls /dev/cu.usbmodem*        # macOS; on Linux /dev/ttyACM*
   ```

   If it doesn't show up, try another cable and another port. If it still
   doesn't show up, that is the sign of a locked unit: stop.

2. **The chip has no protections enabled.**

   ```bash
   esptool get-security-info
   ```

   You must read `Flags: 0x00000000`, `Secure Boot: Disabled` and
   `Flash Encryption: Disabled`, and before that `Stub flasher running`. With
   different values, or if esptool can't connect, stop.

3. **The whole flash reads, twice identical.** This is also the backup:

   ```bash
   esptool read-flash 0 0x1000000 backup-x3-originale.bin
   esptool read-flash 0 0x1000000 backup-x3-rilettura.bin
   shasum -a 256 backup-x3-originale.bin backup-x3-rilettura.bin
   ```

   Each read takes about a minute and must yield 16777216 bytes; the two
   hashes must be identical. If a read fails or the hashes differ, stop.

After every command esptool restarts the X3, and the original firmware goes
back to sleep: press power and check that it starts up as before.

**Keep `backup-x3-originale.bin` in at least two places.** It is the exact copy
of your device and the only way back to the factory state. It contains your
unit's data: do not publish it.

### Flashing

Only with all three checks passed and the backup safe, from a clone of this
repository set up as in [After cloning](#after-cloning):

```bash
cd device && ../.venv/bin/pio run -e x3 -t upload && ../.venv/bin/pio device monitor
```

Keep the X3 awake and connected until flashing has finished.

### Restore

- Full backup, which puts the X3 back exactly as it was:
  `~/.platformio/penv/bin/python -m esptool --chip esp32c3 write-flash 0 backup-x3-originale.bin`.
- Original Xteink firmware or CrossPoint: from the web flasher at
  crosspointreader.com. CamTag's partition table is the same as CrossPoint's.

Both routes go through USB, with the X3 on (with CamTag: if it is off, hold
power; connected to a computer it stays awake). If USB no longer responds,
the only option left is rewriting the memory with an external programmer by
opening the device, as described by
[CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader/blob/HEAD/docs/fix-bricked-xteink.md):
a delicate operation, which may finish breaking it.

## For developers

### Structure

| Folder | Contents |
|---|---|
| `device/` | PlatformIO firmware for Xteink X3 (and X4), on top of FreeInk SDK |
| `companion/` | Web configurator to create the cards and export them as 1-bit BMP |
| `third_party/freeink-sdk/` | FreeInk SDK (display driver, buttons, battery, SD), submodule at `77ae8c7` |
| `reference/crosspoint/` | CrossPoint, for reference only: not compiled and not in the repo |
| `.venv/` | Recent PlatformIO, isolated from the rest of the system |
| `data/` | Lens database and filter catalog |

#### After cloning

```bash
git submodule update --init
python3.13 -m venv .venv && .venv/bin/pip install platformio
git clone --depth 1 https://github.com/crosspoint-reader/crosspoint-reader reference/crosspoint
```

`.venv/` and `reference/` are not in the repo: recreate them this way.

### Firmware

Build:

```bash
cd device && ../.venv/bin/pio run -e x3
```

Host tests, without an X3:

```bash
device/test/filters_test.sh      # filters screen: identical to the web configurator preview
```

`filters_test.sh` composes the pieces exported by the web configurator the way the
firmware does and compares them pixel by pixel with the previews, along with
the stage rules. The data is in `device/test/filters_fixture.zip`: if the
filters export changes, regenerate it with `device/test/filters_fixture.js`
(instructions in the file).

### To verify on hardware

Tests are run from the terminal with `scripts/taratura.py` (firmware built
with `CAMTAG_DEBUG`), which immediately shows each button pressed, the
orientation and the card on screen, and waits for the X3 if it turns off:

```bash
.venv/bin/python scripts/taratura.py tasti
.venv/bin/python scripts/taratura.py versi --carica   # build and flash first
.venv/bin/python scripts/taratura.py card             # with the cards on it
.venv/bin/python scripts/taratura.py filtri           # with the filters screen on it
.venv/bin/python scripts/taratura.py log              # live log
.venv/bin/python scripts/taratura.py batteria         # measured consumption (docs/batteria.md)
```

- [x] X3 detection, display and microSD
- [x] Button mapping (`kKeyA`…`kKeyD`, `kKeyX`, `kKeyY` in `device/src/main.cpp`)
- [x] IMU axes and screen orientation (`kImuSwapXY`, `kImuSignX`, `kImuSignY`,
      `kQuarterTurns`): text stays upright in all 4 orientations
- [x] Turning on with power
- [x] Always listening when on (rest in light sleep), buttons locked with a
      short power press, off and back on with power held, off screen and
      switch to white
- [ ] Consumption measured with `taratura.py batteria`: at rest, at rest with
      buttons locked, during the first three days off (first readings: about
      25 mA awake, 2 mA at rest)
- [x] Bluetooth receive from Brave on the Mac: creates `/camtag` even the
      first time and shows how many files it received; "Import from X3"
      brings the project back into the browser and leaves the cards as they are
- [x] Real cards in portrait and landscape, X/Y in a loop, automatic rotation
      and lock (Y held)
- [x] Rockers on a multi-page lens chart in all 4 orientations (like
      `lensPageDir` in the simulator)
- [x] Receive speed from the Mac: 98 files, 1.1 MB in about 40 s (was 100 s)
      by requesting the 15 ms interval right after the MTU exchange;
      Bluetooth mouse and keyboard in use on the Mac slow it down
- [x] Panel powered off after every update: even with the X3 awake for a long
      time, the previous screen doesn't resurface
- [ ] Rotation thresholds (`kMinTiltG`, `kFlatG`, `ROTATE_STABLE_MS`) on a real camera
- [ ] Ghosting: how often a full refresh is needed (now every 6 changes)
- [ ] Consumption when off
- [ ] Battery reading (BQ27220 fuel gauge): the icon appears below 20%
- [ ] Bluetooth receive from Android (Chrome) and iPhone (BLE Link),
      consumption with Bluetooth on
- [x] Filters screen (`taratura.py filtri`): reads the web configurator's cards as they
      are; ND and diopter with the rockers and button hints in all 4
      orientations, stage reordering after 2 s, stage editing (entering,
      choosing stage and glass, exiting by itself after 5 s and with Y); the
      half refresh after 2 s idle removes the shadow of fast ND and diopter
      changes
- [x] Locked rotation freezes everything: turning the X3, filters screen,
      hints and rockers stay those of the locked orientation
- [x] No lost button presses: before, they were all dropped during a screen
      update (`taratura.py reattivita`); now a task reads them even then and
      queues them, and they run as soon as the screen is done

### Next steps

1. **Measured consumption** with the log on the microSD (`docs/batteria.md`),
   and from there the choices still open: how much the powered microSD costs
   at rest, timings for rest, for going white when off (`OFF_BLANK_S`) and for
   turning off when forgotten. A build configuration without `CAMTAG_DEBUG`
   is also needed: today `x3` always has it on.
2. **Screen care** (`docs/schermo.md`): confirm with hours of use that the
   negative image on a static screen doesn't come back, understand the
   spontaneous restart after rest and the rest that kicks in with USB, tune
   how often a full refresh is needed (`FULL_REFRESH_EVERY`). Heat above about
   50 °C and direct sunlight for months damage the panel: the mount matters.
3. **Filter catalog** (`data/filters.json`) verified against the
   manufacturers' data sheets: today it is provisional.
4. **3D-printed mount** for attaching it to the camera.

## License

CamTag (firmware and web configurator) is free software, distributed under the
**GNU GPL v3.0** license: full text in [`LICENSE`](LICENSE). You may use,
study, modify and redistribute it; anyone who distributes a modified version
must make its source code available under the same license.

**No warranty.** As sections 15 and 16 of the license state, the software is
provided "as is": whoever uses it does so at their own risk, and the authors
are not liable for damage to devices, data or anything else, including an X3
made unusable by flashing the firmware.

These keep their own licenses:
- FreeInk SDK (`third_party/freeink-sdk/`): MIT;
- Inter font (`companion/fonts/`): SIL Open Font License 1.1.
