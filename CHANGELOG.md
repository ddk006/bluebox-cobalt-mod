# Changelog

## Unreleased

- **DEV port audio, stage 2 (eurorack):** DEV Audio **Audio** sends the
  bluebox to the device on the DEV port: **DEV 1**-**DEV 10** (tags
  0x2b1-0x2ba, keys `devout1`-`devout10`) route Track 1-12, Main L/R or
  Bus 1-4 (or Off) to its channels; default Track 1-8, Main L/R. The M7
  (usb_push) copies each block, routed to the device's channels 1-10, as 16
  bit into a ring in the M4 heap (`targets/euro/dev_ring.h`, 256 frames, 160
  or 128 when the heap is short; context and ring 7.4 KB, allocated once per
  boot) while the M4 driver's heartbeat runs. The driver reads it through a
  resampler (linear interpolation, PI control on the fill, up to 3 %): the
  M7 writes 48224 frames a second against the S-4's 48000, which the old
  skip-or-double correction (about 300 ppm) could not follow ("OK, gaps",
  distorted). DEV Audio **Audio sync**: no resampling, the frames per packet
  follow the M7 (for a DEV port USB clock that is off); DEV FB raw shows the
  USB clock source and the M7 ms per 10000 SOFs. "No memory" shows the free
  heap in DEV FB raw. DEV Audio "Ring test": the M7 writes a 1 kHz sine
  into the ring instead (checks that path alone). DEV Pkts "OK, no input" / "OK, gaps".
  Settings capacity 192, definition table 0x2bb records.
- **X-Fade CV against noise (eurorack):** the CV is low-pass filtered and
  moves the fader only once it leaves a hysteresis band of 1.5 steps; the
  outer 2 steps at each end give A or B. A resting, noisy CV no longer
  moves the fader, so an edit made between A and B (or at an end) stays
  until the CV really moves.
- **USB audio out channels (eurorack):** TOOLS > **USB Audio Out** switches
  each of the 18 USB channels On or Off (`usbch1`-`usbch18`). Only those On
  are sent and the device reports that many (UAC2 config descriptor edited in
  RAM, ring and DataIn for 3 bytes per channel, soft disconnect and reconnect
  on a change, serial number with the channel mask). Euro-only payload at
  0x080C8000 (`targets/euro/usb_m7.c`), inserted by the euro build; the M4
  passes the mask in backup SRAM 0x38800EE0. Settings capacity 176, the
  definition table 0x2ac records.
- **DEV port audio, stage 1 (eurorack, experimental):** **DEV Audio** (Off /
  Sine 1-2 / Sine 3-4 / Sine all), **DEV State**, **DEV Ch** and **DEV Pkts**
  (packet results per second) on the USB page; the page rebuilds its rows
  when they change (edit page flag +0xA3E2, as stock value events). Mute off
  and 0 dB on the stream's feature unit; the feedback endpoint (10.14 or
  16.16) sets 47-49 frames per packet, the sine phase runs on. DEV Pkts
  "OK, no FB" / "OK, FB odd" when the device does not answer its feedback
  endpoint near 48 (it does not run the stream); "OK, FB empty", "OK, FB
  44.1k" and "OK, FB 96k" tell why. Feedback in 16.16 per frame or per
  microframe (as some Linux gadgets send it) is understood too. **DEV FB
  raw** (tag 0x2b0, key `devfb`) shows the last answer as received; the
  definition table has 0x2b1 records. Two pipes in turn for the audio OUT
  and the feedback IN: the USB core sends an isochronous transfer in the
  frame after it is started, so one pipe served only every other 1 ms frame
  (the S-4 answered its feedback empty); the periodic TX FIFO holds two
  packets (RX 0x100, NPTX 0x80, PTX 0x200 words). The MIDI host class
  is wrapped (registration hook at 0x081056C6): with DEV Audio on it reads the
  device's config descriptor, selects its isochronous audio OUT stream
  (SET_INTERFACE, UAC2 SET_CUR 48 kHz), enlarges the periodic TX FIFO when
  needed and sends 48 frames per SOF with a 1 kHz sine on channels 1/2. MIDI
  keeps running. M4 payload at 0x080CC000; buffers from the FreeRTOS heap.
- **USB audio out format (eurorack):** **Format** on the same page: 24 bit
  (stock) or 16 bit (`usbfmt`). At 16 bit the descriptor says 2-byte subslots
  and 16-bit resolution and the upper two bytes of each sample are sent;
  the M4 passes it at 0x38800EE8. `tools/usb_audio_desc.py` reads the USB audio
  descriptors of the devices on the computer.
- **Crossfader morph and live scenes:** every number glides, a fader move
  rewrites only the items where A and B differ (round robin, so fast moves
  reach all of them); the heard scene keeps the live state; empty scenes sound
  like the project's own state (saved as `<scene n="0">`); holding a field
  clears a scene.
- **Crossfader page (Octatrack style):** a **Crossfader** row on the scene
  page opens it (tap, or Select with the row selected; A goes back). Side A
  holds one of scenes 1–8, side B one of 9–16, shown as two 2 × 4 fields of
  touch buttons with the fader above them. The fader (touch, or the
  **X-Fade CC** set on the scene page: Off or CC 1–119 on the PC Ch channel,
  `settings.tml` key `fadecc`) mixes A and B: dB, 0–1000, pan and wide
  numeric values such as BPM in proportion, switches, modes and lists from A
  below the middle and from B from it. A tap picks a scene for its side (heard
  at once unless the fader is fully on the other side); a tap on an empty
  field, or holding a field for 1 s, saves the current sound there. Values
  move only when the fader or a heard scene changes, so edits made in between
  stay. The chosen A/B scenes are saved in the project (`<scenes xa= xb=>`).
  At most 24 changes per main-loop pass. Load Scene and Program Change recall
  at once, as before. Touch hook: the UI loop's touch read (desktop
  0x0810d3a4, euro 0x0810d564); the definition table grows to 0x296 records.
- **Crossfader controls:** **LEARN** on the crossfader page takes the first
  MIDI CC (1–119) as X-Fade CC together with its channel. The new **X-Fade Ch**
  row (`xfch`, All or 1–16) replaces the PC Ch filter for the fader. On the
  eurorack, LEARN also takes the first CV input that moves by about 1 V. The
  new **X-Fade CV** row (`xfcv`: Off, CV1–6 +-5V, CV1–6 0-5V) lets a CV input
  move the fader. The CV values come from the stock ADC path: centre minus
  reading of CV 1–6, 8 frames at engine+0x78c0. +-5 V is taken as ±2000; the
  real scale is not measured yet. **FADER** switches the crossfader off: a tap
  then recalls a scene, holding a field for 1 s saves, and fader, CC and CV do
  nothing. This is saved as `<scenes xo="1">`. New layout: buttons at the top,
  fader row, 2 × 4 fields of 74 × 36. The Crossfader row moves up to fourth
  place on the scene page. The definition table grows to 0x298 records. The
  PC map's read-only data moves to its own cave at 0x080DC000, because the
  code cave at 0x080BC000 was full.
- **Eurorack port** (branch `euro`): `tools/retarget.py euro BLUEEURO.BIN`
  builds Cobalt for the bluebox eurorack 1.5.1 (address map, RAM and object
  offsets with evidence in `targets/`).

## v1.2.0

Phones AUTO cue, for bluebox desktop firmware 1.5.2. Contributed by
[Milan Wulf](https://github.com/milanwulf) in
[pull request #1](https://github.com/sbsmnt/bluebox-cobalt-mod/pull/1).

- **Phones Src AUTO** (MAIN → B): the phones play the cue mix while any track
  has its cue on or turned up, and the main mix otherwise, like PFL on a DJ
  mixer. Works with CUE Style Fader and Toggle and both CUE Send settings;
  stopped tracks and tracks without an input count when cued. Back to Main
  0.1 s after the last cue goes off. CUE and MAIN are unchanged.
- **Cue icons:** a headphone in the top-right corner of every cued track
  header, in the theme's active button fill with a background outline.
- **Unconfigured tracks:** tracks without an input are greyed out on the
  track headers.
- TOOLS shows `Cblt 1.2`.

## v1.1.0

Scenes and MIDI, for bluebox desktop firmware 1.5.2.

- **Scenes:** up to 16 snapshots per project of the mixer (track level,
  pregain, pan, sends, mute, solo, compressor, EQ, gate), master and aux
  buses, every FX1/FX2 parameter including the effect type, and the BPM.
  Saved in the project file. Recall replays only the values that differ,
  without loading anything, so playback and recording carry on.
- **Scene page** (A on PROJ): Load Scene, Save Scene and Bank # rows with a
  Select button; A leaves. The project name shows at the top.
- **Quick save:** hold A for 2 s on MAIN to save the next empty scene. A new
  project starts at scene 1. Scene saves also save the project.
- **MIDI recall:** Program Change recalls a scene: program 0 (PC 1 on gear
  that counts from 1) is scene 1. Bank Select (CC 0, or CC 32, value 0 =
  Bank 1) loads the project with that Bank #.
- **MIDI Settings** in TOOLS (tap the row, or B on it; B goes back): Bank CC,
  PC Ch (All / None / 1–16), MIDI Out, Map CC Sync and Clock Src.
- **Map CC Sync** (Off / USB / USB+MIDI Out / MIDI Out): sends the mapped CC
  values to the controller on plug-in, project load and scene recall, so LED
  rings and motor faders follow the bluebox.
- **MIDI Out** (Default / USB Thru / All Thru): notes and CC from USB, or USB
  and MIDI in, are passed to the MIDI out with their channel. Clock and
  transport are never passed on.
- **Clock Src** (MIDI In / USB): with clock on more than one input, only one
  is followed; the preferred one takes over at once, another after 300 ms of
  silence. Changing it shows the order on the band.
- **Bank #:** None or 1–128 per project; a number another project already
  has is never taken. PROJ Save applies the chosen number.
- **PROJ list:** the loaded project is marked with `* `; long selected names
  scroll.
- **MIXER sends with OUT3:** the seven buttons read `O 2`, `O 3` and `NG`
  (gate) so they fit; the six-button layout is unchanged.
- Patcher page: a "Using scenes and MIDI" guide.
- TOOLS shows `Cblt 1.1`.

## v1.0.0

First public release, for bluebox desktop firmware 1.5.2.

- FX1 delay modes: Delay (stock), Warp delay (pitch, drift and send to
  reverb), Granulr, Tape, Ducked and Reverse.
- FX2 Gazer reverb.
- Per-track noise gate on TRACK and MIXER, with Gate meters in MIXER.
- Master saturation: Soft and Tape, with Tape Tone and Sat Out.
- CPU load bar and compressor gain-reduction meters.
- Overdub recording fix: faster, bit-identical PCM conversion so long
  overdubs no longer fall behind.
- Theme editor with 18 presets and 22 colours, and browser and Python
  patchers with exact restore of the original firmware.
- TOOLS shows `Cblt 1.0` in the middle header field.
