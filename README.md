# Cobalt for 1010music bluebox desktop

**v1.2.0 - for bluebox desktop firmware 1.5.2**

Cobalt is an unofficial firmware mod for the bluebox desktop. It adds delay
modes, a new reverb, a per-track noise gate, master saturation, compressor
meters, an overdub recording fix, mixer scenes with MIDI recall, MIDI
controller sync and custom colour themes. The patcher runs locally: it takes your copy of the official
1.5.2 firmware and writes a new `BLUEBOX.BIN`. No firmware is distributed
here.

**Open the patcher: [sbsmnt.github.io/bluebox-cobalt-mod](https://sbsmnt.github.io/bluebox-cobalt-mod/)**

> Cobalt is not made, endorsed or supported by 1010music. Keep a copy of your
> working firmware and back up your projects before installing.

## New in v1.2

- **Phones Src AUTO:** MAIN → B → Phones Src has a third choice, AUTO. The
  phones play the cue mix while any track is cued and the main mix
  otherwise, like PFL on a DJ mixer.
- **Cue icons:** a small headphone marks every cued track on the track
  headers.
- **Unconfigured tracks:** tracks with no input selected are greyed out on
  the track headers.

Thanks to [Milan Wulf](https://github.com/milanwulf) for this feature ([pull request #1](https://github.com/sbsmnt/bluebox-cobalt-mod/pull/1)). Details in [Phones AUTO cue](#phones-auto-cue).

## New in v1.1

- **Scenes:** up to 16 snapshots per project of the mixer, master and aux
  buses, FX1/FX2 (including the effect type) and BPM. Recall is instant,
  with no project load, so playback and recording carry on.
- **Scene page:** select a project in PROJ and press A to load or save a
  scene and set the project's Bank #.
- **Quick save:** hold A for 2 s on MAIN to save the next empty scene.
- **MIDI recall:** Program Change recalls a scene; Bank Select loads the
  project with that Bank #. Channel and Bank CC in TOOLS > MIDI Settings.
- **Map CC Sync:** the bluebox sends the values of its mapped CCs to your
  controller on plug-in, project load and scene recall, so LED rings and
  motor faders show the bluebox values.
- **MIDI Out thru:** notes and CC from USB (or USB and MIDI in) are passed to
  the MIDI out.
- **Clock Src:** with clock on more than one input, one of them is followed
  (MIDI In first, or USB first).
- **PROJ list:** the loaded project is marked with `* `, and long names
  scroll.
- **MIXER sends with OUT3:** buttons read `O 2`, `O 3` and `NG` (gate) so
  all seven fit.

Details in [Scenes](#scenes), [MIDI](#midi) and the [CHANGELOG](CHANGELOG.md).

## Install

1. Open the patcher at
   [sbsmnt.github.io/bluebox-cobalt-mod](https://sbsmnt.github.io/bluebox-cobalt-mod/),
   or open `patch_bluebox.html` from this repository in any modern browser.
   Nothing is uploaded; patching happens in the page.
2. Drop the **original official desktop 1.5.2 `BLUEBOX.BIN`** on the drop
   zone (or click to browse). Other versions and already-patched files are
   refused.
3. Pick a theme preset or build your own, then **Verify and prepare
   download**.
4. Install the new `BLUEBOX.BIN` with the usual SD-card procedure.
5. Check that the middle TOOLS field shows **Cblt 1.2**. The official version
   field still shows 1.5.2.

To go back, tick **Restore the original** and choose your Cobalt
`BLUEBOX.BIN`: the page rebuilds the exact official 1.5.2 file.

The same patcher runs from the command line:

```sh
python3 patch_bluebox.py /path/to/BLUEBOX.BIN --check
python3 patch_bluebox.py /path/to/BLUEBOX.BIN --output /path/to/new/BLUEBOX.BIN
python3 patch_bluebox.py /path/to/BLUEBOX.BIN --theme themes/synthwave.json --output /path/to/new/BLUEBOX.BIN
python3 patch_bluebox.py /path/to/cobalt/BLUEBOX.BIN --restore --output /path/to/original/BLUEBOX.BIN
```

## Features

### FX1 delay modes

Open FX1 and press **B** to choose the mode. Each mode shows its own
controls on the FX1 panel; switching modes keeps every mode's settings.

#### Delay

The stock 1.5.2 delay, unchanged and bit-identical. New projects start here.

| Control | What it does |
|---|---|
| Time | Delay time in ms, or a note division (such as 1/4T) when BEAT is on |
| FEEDB | How much of each repeat is fed back (more repeats) |
| FILT | Turns the delay's filter on |
| CUTOF / WIDTH | Filter frequency and width |
| BEAT | Syncs Time to the project tempo |
| PING | Ping-pong repeats between left and right |
| QUAD | The stock quad delay layout |

#### Warp delay (panel title: Warp)

The stock delay with three extra controls:

| Control | What it does |
|---|---|
| Pitch | Shifts every repeat by −12 to +12 semitones (the toggle turns it on), so repeats climb or fall in pitch as they feed back |
| Drift | Tape-style wow: a slow 0.6 Hz wobble plus a random wander, up to about ±2.5 % pitch at full |
| To Rev | Sends the repeats into FX2, so the echoes are reverberated |

#### Granulr (Granular)

Records the last 2 seconds into a buffer and replays it as up to four
overlapping grains. When fully wet, the stock delay engine is paused to save
CPU.

| Control | What it does |
|---|---|
| Position | How far back in the 2-second buffer grains start: 0 % is the most recent audio, 100 % the oldest |
| Size ms | Grain length, 20–250 ms |
| Density | How many grains overlap, 1–4 (more is smoother and denser) |
| Pitch | Grain pitch, −12 to +12 semitones |
| Jitter | Randomises each grain's start position around Position |
| Feedback | Feeds grains back into the buffer (up to 95 %) for evolving clouds |
| To Rev | Send to the FX2 reverb |
| Freeze | Stops recording: grains keep playing from what is already in the buffer |
| Reverse | Grains play backwards |

With jitter or pitch the grains are mixed by power, so jittered settings stay
as loud as plain ones.

#### Tape

A tape echo: every repeat passes through saturation and filters inside the
feedback loop, so repeats get darker, thinner and more driven as they decay.

| Control | What it does |
|---|---|
| Time / FEEDB | Delay time and feedback, as on Delay |
| Drift | Wow and flutter, as on Warp |
| Drive | Saturation of each repeat (takes WIDTH's place). Quiet tails are not boosted, so drive adds grit without runaway feedback |
| Lo C | Low cut inside the loop: each repeat loses a little more low end. 20 Hz is Off |
| Hi C | High cut inside the loop, 500 Hz–16 kHz: each repeat gets a little darker |
| To Rev | Send to the FX2 reverb |
| BEAT / PING / QUAD | As on Delay |
| WILD | Adds pre-drive damping and extra regeneration in the loop, for a less polite, more unstable echo |

Tape bypasses the stock filter (no FILT), and your FILT setting is kept for
the other modes.

#### Ducked

A delay whose repeats duck while you play and swell back in the gaps.

| Control | What it does |
|---|---|
| Duck | How much the repeats are turned down while the FX1 input is loud (takes WIDTH's place) |
| Release ms | How quickly the repeats come back after the input stops, 20–2000 ms (takes Drift's place) |
| Pitch / To Rev | As on Warp |
| Time / FEEDB / FILT / CUTOF / BEAT / PING / QUAD | As on Delay |

Only what you hear is ducked: the feedback loop keeps running underneath, so
the echoes are all there when they swell back.

#### Reverse

Plays the input backwards in overlapping segments.

| Control | What it does |
|---|---|
| Segment ms | Length of each reversed slice, 40–600 ms (a note division when BEAT is on) |
| Feedback | Feeds reversed audio back for repeated reverse echoes |
| Tone | Low-pass on the reversed sound, from dark (200 Hz) to open (10 kHz) |
| Pitch | −12 to +12 semitones |
| To Rev | Send to the FX2 reverb |
| BEAT | Syncs Segment to the project tempo |

### FX2 Gazer reverb

FX2 → B → **Gazer**: a large, modulated shoegaze reverb with a fuzz stage in
front.

| Control | What it does |
|---|---|
| Saturate | Fuzz before the reverb: clean at 0, about 7 dB of drive at 35 %, pedal-style fuzz at the top. Output level stays constant |
| Size | Room size and decay, from about 3 s to about 40 s |
| Diffusion | How much the sound is smeared before it enters the reverb; higher is smoother |
| Mod Depth | Pitch modulation of the reverb tail (chorus-like shimmer) |
| Rate mHz | Modulation speed, 10–2000 mHz (0.01–2 Hz) |
| Hi C / Lo C | High and low cut on the sound going into the reverb |

### Noise gate

A gate on every track. Set it on TRACK (GATE knob) or in MIXER → Gate (`NG`
when OUT3 is present). The
threshold runs from −96 dB to −12 dB; the lowest setting is Off. It takes fine
steps when turned slowly and speeds up when spun. MIXER → Gate meters show
the level the gate listens to on the Gate fader's scale, so the meter crosses
the fader exactly when the gate opens.

### Phones AUTO cue

MAIN → B → **Phones Src** has a third choice, **AUTO**, next to CUE and MAIN.
It works like PFL on a DJ mixer: the phones play the cue mix while any track
has its cue on (CUE Style Toggle) or turned up (CUE Style Fader), and the
main mix otherwise. Only the cue setting counts, not whether the track is
playing or has an input, so a cued track that is stopped still gives you its
(silent) cue. After the last cue goes off, the phones return to Main within
0.1 s. CUE Send (Pre/Post Fader) works as before. CUE and MAIN are unchanged.

**Track headers** on every page with the track strip:

- A small headphone icon in the top-right corner marks each track with its
  cue on, in any Phones Src mode. It uses the theme's active button fill
  with a background-colour outline. On narrow headers it can cover the last
  letter of the name.
- Tracks with no input selected are greyed out with a stipple, so
  unconfigured tracks stand out. Tracks with a recorded clip are not greyed.

Both follow the track count, Single/Double Row and renamed tracks.

Thanks to [Milan Wulf](https://github.com/milanwulf) for this feature ([pull request #1](https://github.com/sbsmnt/bluebox-cobalt-mod/pull/1)).

### Master saturation

MAIN → B adds four rows after the compressor:

| Control | What it does |
|---|---|
| Saturate | Amount of saturation on the master. 0 is a true bypass |
| Sat Mode | **Soft** or **Tape** |
| Tape Tone | Tape mode only: the treble roll-off, from darker (about 5 kHz) to brighter (about 18 kHz). Default 50 % is about 11.5 kHz |
| Sat Out | Output level after saturation, −12 to +6 dB, to match levels when comparing. Has no effect while Saturate is 0 |

- **Soft** is a smooth, symmetric saturation (adapted from BlueLex). Drive is
  level-compensated, so turning it up adds harmonics rather than volume.
- **Tape** is a tape-style voicing, not a model of a specific machine. Like
  tape it boosts treble going in and cuts it coming out, so highs saturate
  first. Loud passages also lose a little more treble (the treble roll-off is
  linked across both channels). More Saturate adds a gentle low-end bump. Very
  low frequencies and DC are removed. **Tape Tone** sets where the treble
  starts to roll off. Turn it down for warm, darker tape; turn it up to keep
  the top end open.

### Meters

**CPU load bar.** The thin bar right of the clock shows how busy the audio
engine is. It is green up to 70 %, amber to 90 % and red above. A white mark
shows the recent peak.

**Compressor gain reduction.** When the master compressor is on, Cobalt
shows how much it is turning the level down:

- **Header bar:** the bar just left of the CPU bar. Half height is 10 dB of
  reduction; full height is 40 dB.
- **Thresh row (MAIN → B):** a strip along the bottom of the Thresh row. It
  fills from the left as gain reduction rises. Full width is 40 dB. The three
  scale markers are at **5 dB, 10 dB and 20 dB**.

Both use a square-root scale, so small amounts of compression are easy to
see. They show the larger of the two channels, held over about 20 ms. The
fill colour is set in the theme editor (Compressor GR meter).

### Overdub recording fix

Stock 1.5.2 can stop with an error while overdubbing long takes because the
storage task falls behind. Cobalt converts recorded and played-back audio in
word-sized bursts instead of byte by byte. The audio is bit-identical.

### Scenes

A scene is a snapshot of the mixer (track level, pregain, pan, sends, mute,
solo, compressor, EQ and gate), the master and aux buses, every FX1/FX2
parameter including the effect type, and the BPM. Each project holds up to
16, saved in the project file. Recalling a scene changes only the values
that differ, without loading anything, so playback and recording carry on.

- **Scene page:** select a project in PROJ and press A. Turn to pick a row
  and a number, then press Select:
  - **Load Scene:** recall that scene.
  - **Save Scene:** save the current state to that scene.
  - **Bank #:** give the project a number (None or 1–128) for MIDI Bank
    Select. A number another project has is refused ("BANK n IN USE").
  - **Crossfader:** opens the crossfader page (tap the row, or Select).
  - **X-Fade CC / X-Fade Ch:** the MIDI controller that moves the crossfader
    (Off or CC 1–119) and its channel (All or 1–16). LEARN on the crossfader
    page sets both. Saved in `settings.tml`.
  - **X-Fade CV** (eurorack): the CV input that moves the crossfader. Off, CV1–6
    +-5V (−5 V = A, +5 V = B) or CV1–6 0-5V (0 V = A, +5 V = B).
  Press A to leave. PROJ Save also applies the Bank # chosen there.
- **Crossfader page** (as on the Octatrack): scenes 1–8 on the left are side
  A, 9–16 on the right side B, the fader above them mixes the two. Tap a scene
  to pick it for its side; it is heard at once unless the fader is fully on
  the other side. Move the fader by touching the fader row, with the X-Fade
  CC or with the X-Fade CV. A CV moves the fader only while it changes, so
  touch and CC still work while the CV rests. Every number glides between A
  and B (levels, pan, sends, EQ frequency and Q, times, tempo); switches,
  modes and lists take A's setting below the middle and B's from it.
  - **Live scenes:** while you are in a scene (the fader at an end, the
    scene recalled with the fader off, or by Load Scene / Program Change),
    every change is kept in that scene and comes back with it. Nothing turns
    back. Changes made between the ends are only for the fade: the next fader
    move replaces them.
  - **Empty scenes** sound like the project as it was loaded (its own state,
    saved with the project). Change something in an empty scene and it
    becomes a scene of its own. **Hold a field for 1 s** to clear the scene:
    it is empty again ("SCENE n CLEARED").
  - So: fade to B, shape the sound, pick another scene on the A side, fade
    back, and so on. A goes back to the scene page.
  - **LEARN:** tap it, then move a MIDI controller or a CV source. The first CC
    becomes the X-Fade CC on its channel. On the eurorack, the first CV input
    that moves by about 1 V becomes the X-Fade CV, keeping the 0-5V choice.
    Tap LEARN again to cancel; it also stops by itself after 15 s.
  - **FADER:** switches the crossfader off and on. When it is off, a tap on a
    scene recalls it straight away, holding a field for 1 s clears the scene,
    and the fader, CC and CV do nothing. When it is on again, the sound follows the
    fader. The setting is saved in the project.
- **Quick save:** hold A for 2 s on MAIN. A band at the top shows progress,
  then saves the next empty scene. A new project starts at scene 1.
- **MIDI:** Program Change recalls a scene: program 0 (PC 1 on gear that
  counts from 1) is scene 1. Bank Select loads the project with that Bank #
  (value 0 = Bank 1); a Program Change after it waits for the load. TOOLS > **MIDI Settings** (tap it, or press B with it selected;
  B goes back) sets **Bank CC** (MSB - CC 0 or LSB - CC 32) and **PC Ch** (All, None or
  1–16).
- **PROJ list:** the loaded project is marked with `* `, and a long selected
  name scrolls.

### MIDI

Also in TOOLS > **MIDI Settings**:

- **MIDI Out:** what the MIDI out passes on, besides the bluebox's own MIDI
  output. `Default` passes nothing else (stock). `USB Thru` passes the
  messages (notes, CC, …) coming from USB (computer on USB-B, controller on
  USB-A). `All Thru` also passes MIDI in. Channels are kept. Clock and transport are never
  passed on, so a clocked setup never sees a second clock.
- **Map CC Sync:** where the values of the mapped CCs are sent, so a
  controller with LED rings or motor faders (OXI E16, MIDI Fighter Twister,
  Launch Control XL 3, Roto-Control, Faderfox EC4, …) shows them:
  - `Off`: never.
  - `USB` (default): to the controller on USB-A when it is plugged in, after
    a project load and after a scene recall.
  - `USB+MIDI Out`: as USB, plus the MIDI out after a project load or scene
    recall.
  - `MIDI Out`: the MIDI out only, after a project load or scene recall.
  The controller must listen on the channel and CC it sends (the usual
  default). If the values can't get out, a band says `MAP CC SYNC: USB OUT
  STUCK` or `MAP CC SYNC: MIDI OUT STUCK`.
- **Clock Src:** which input is followed when more than one sends clock.
  `MIDI In` (default): MIDI in, then the computer (USB-B), then the USB-A
  controller. `USB`: USB-B, USB-A, then MIDI in. The preferred input takes
  over at once; a lower one only after 300 ms without clock from the current
  one. Start, stop, continue and song position follow the same input. The
  band shows the order when the setting is changed.

### USB audio out (eurorack)

The bluebox eurorack sends the computer 18 channels over USB-C EXT. TOOLS >
**USB Audio Out** (tap it, or press B with it selected; B goes back) has a row
per channel, labelled with its number and source (**1 Track 1** to **12 Track
12**, **13 Main L**, **14 Main R**, **15 Out Bus** to **18 Out Bus**), each On or Off (all On by default, saved
in `settings.tml`). Only the channels left On are sent, in their order, and the
bluebox tells the computer it has that many: USB 1-8 On, the rest Off, gives an
8-channel device. After a change the computer sees the bluebox unplug and plug
back in (about a quarter of a second without USB audio); each channel set has
its own serial number, so the computer does not mix them up. All On, or all
Off, is the stock 18-channel device.

The first row, **Format** (`usbfmt`), picks the sample format: **24 bit** (stock,
3 bytes per sample) or **16 bit** (2 bytes per sample, the upper 16 bits of each
sample). Some USB hosts in instruments take only 16 bit (the Torso S-4 sends
16 bit itself); a computer takes both. 16 bit with all channels On is not the
stock device; the serial number then ends in `-16`.

USB 1-12 come from the 12 track channels, USB 13/14 is the main mix, USB 15-18
are the other four output buses (from the firmware's packing code; which
connector each of 15-18 is was not checked). USB audio from the computer
(**USB Rcv**) and MIDI are unchanged. Code: `targets/euro/usb_m7.c`.
### Audio to a device on the DEV port (eurorack, experimental)

The DEV port (USB-A, the port for MIDI controllers) is a USB host. With
**DEV Audio** on the USB page set to **Sine 1-2**, **Sine 3-4** or **Sine
all**, the bluebox also sends audio to a class-compliant USB audio device
plugged in there (for example a Torso S-4 as a device), next to MIDI. Stage 1
sends a 1 kHz test tone at -12 dBFS on the device's channels 1-2, 3-4 (the
S-4's track 1) or all of them, the other channels silent. **DEV State** shows Off, Waiting, No audio (no audio
output on the device), Starting, Sending, Error, No memory or Too big (more
than one full-speed packet), **DEV Ch** the device's channel count, **DEV Pkts** what happened to the
packets in the last second (OK, No SOF, Stuck, Not ready, NYET, Error, Stall;
with a feedback endpoint OK also means the device answers it near 48 frames,
that is, it runs the stream: "OK, no FB" packets out but no answer, "OK, FB
empty" answers of 0, "OK, FB 44.1k" / "OK, FB 96k" the device runs at that
rate, "OK, FB odd" any other value; 10.14 and 16.16 per frame or
microframe are understood). **DEV FB raw** shows the device's last feedback
answer as received, as a number (3 bytes little-endian: 786432 = 0x0C0000 is
48.0 frames in 10.14).
The three update while the page is open. The port is full speed: at most
1023 bytes per ms (10 channels of 16 bit fit). Like a computer, the bluebox
unmutes the device's input and sets it to 0 dB, and follows the device's clock
feedback (47-49 frames per ms) when it has one. **DEV Audio: Audio** sends the
bluebox itself: **DEV 1** to **DEV 10** pick for each of the device's first ten
channels a source (Off, Track 1-12, Main L/R, Bus 1-4 = the bluebox USB
channels 1-18; default Track 1-8, Main L, Main R). The M7 copies every audio
block, routed, into a ring in M4 memory (`targets/euro/dev_ring.h`, 256
frames, or 160 or 96 when the heap is short); the driver keeps it half full
and skips or doubles a frame now and then for the clock difference to the
device. With "No memory" DEV FB raw shows the free heap in bytes. **Ring test**
(DEV Audio) lets the M7 write a 1 kHz sine into the ring instead of the audio:
it checks the M7 -> M4 path on its own. In Audio and Ring test, DEV FB raw shows
the frames the M7 wrote in the last second (48000 at 48 kHz). DEV Pkts then also says "OK, no input" (nothing
from the M7) or "OK, gaps" (the ring ran empty). Default Off: the port then works as stock. Code:
`targets/euro/usbh_m4.c`.

`tools/usb_audio_desc.py` prints the USB audio descriptors (channels, format,
endpoints) of every audio device plugged into the computer (needs libusb and
pyusb).

### Themes

The patcher page has a theme editor with 18 presets and 22 colours, a live
preview of 27 device screens, hold-to-compare with stock and an RGB565
preview. Themes export and import as JSON (`themes/` holds the presets).

## Building from source

Requirements: Arm GNU Toolchain **13.2.Rel1** (other GCC versions produce
different payloads), Python 3 and the original firmware.

```sh
ARM_CC=/path/to/arm-none-eabi-gcc ARM_OBJCOPY=/path/to/arm-none-eabi-objcopy \
ARM_NM=/path/to/arm-none-eabi-nm sh build.sh /path/to/BLUEBOX.BIN
```

`build.sh` compiles the payloads into `build/` and regenerates
`patch_bluebox.html`, `patch_bluebox.py`, `patch_manifest.json`, `themes/`
and `docs/index.html`.

### Tests

The tests run the real patched firmware routines in the Unicorn emulator,
compile host-side DSP checks with sanitizers, and drive both patchers.

```sh
python3 -m pip install -r tests/requirements.txt
python3 tests/run_checks.py /path/to/BLUEBOX.BIN
```

They need Node.js and a host C compiler; add `--browser` to also run the
Playwright end-to-end test. Regression tests compare against earlier
development builds kept in `tests/references/` (patch data only, no
firmware). The tests cover correctness, not sound or CPU load on the device.

### Continuous integration and GitHub Pages

`.github/workflows/pages.yml` runs on every push and pull request. It checks
that no firmware image is committed, that the Python sources compile, that
the theme presets load, and that `docs/index.html` matches
`patch_bluebox.html`. It also runs the patcher page tests and the sanitized
DSP host tests. The emulator suite needs the original firmware, so run
`tests/run_checks.py` locally before pushing.

Pull requests go to the `dev` branch; `main` only receives tested releases
(see [CONTRIBUTING](CONTRIBUTING.md)). On `main`, the workflow then publishes `docs/` (the self-contained patcher
page) to GitHub Pages at [https://sbsmnt.github.io/bluebox-cobalt-mod/](https://sbsmnt.github.io/bluebox-cobalt-mod/).
Enable it once under **Settings → Pages → Build and
deployment → Source: GitHub Actions**.

## Layout

| Path | Contents |
|---|---|
| `src/` | Payload sources (M7 audio core, M4 UI core) and linker scripts |
| `tools/` | Release builder and patcher templates |
| `tests/` | Emulator, host and patcher tests |
| `themes/` | Theme presets as JSON |
| `docs/` | GitHub Pages copy of the patcher |
| `.github/workflows/` | Checks and Pages deployment |
| `patch_bluebox.html`, `patch_bluebox.py` | Ready-to-use patchers |

## Credits

Cobalt builds on **BlueLex**, the modded firmware for the 1010music Bluebox
Euro by lexnels: https://github.com/lexnels/bluelex-bluebox-euro. The CPU
load indicator, the MAIN → B compressor level and meter approach, the Soft
master saturation, and the delay pitch and send to reverb were adapted from
BlueLex, with every desktop address and hook traced independently.
[NOTICE.md](NOTICE.md) has the full attribution and disclaimer.

See [CHANGELOG.md](CHANGELOG.md) for release notes.
