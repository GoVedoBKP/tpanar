# <div align="center"><img src="imgs/tpanar.png" alt="TPanar logo" width="192"></div>

# TPanar

**TPanar** is a drum-focused digital audio workstation with a tracker interface. It combines multitrack audio recording, linked note tracks, tracker editing, sample/instrument work, and mixer routing in a wxWidgets desktop application.

## What it is for

TPanar is built around a practical drum-production workflow:

1. Record one or more audio tracks at the same time.
2. Analyze the recorded takes into linked note tracks.
3. Quantize the note tracks to tracker rows or metronome clicks.
4. Refine timing and velocity in the tracker.
5. Edit audio ranges in the timeline and optionally retrigger/stretch them against the quantized notes.

## Current workflow

- **Project tab** for project metadata, paired audio/note track creation, analysis, quantization, track outputs, and export.
- **Tracker tab** for the tempo track plus note tracks only, with up to three visible note sub-columns per track.
- **Tracks tab** for audio and note timeline editing, multitrack selection, group edit operations, and retrigger stretch actions.
- **Instrument tab** for sampler, SoundFont, and SFZ instruments, including direct sample recording into sampler slots.
- **Mixer tab** for track and bus levels, pan, DSP chains, and analysis tools.
- **Settings tab** for audio backend, MIDI, theme, key bindings, GUI settings, recording count-in, and export lead-in.

## Main features

- Multitrack audio recording with per-track arming
- Tempo track and linked audio/note track model
- Drum-hit analysis from audio into note tracks
- Note-track quantization, including alignment to metronome clicks
- Audio retrigger/time-stretch workflow driven by note tracks
- Sample editing operations such as cut, copy, paste, crop, normalize, silence insertion, resampling, and time-stretching
- SoundFont and SFZ playback for note tracks
- Separate-track WAV export and click-track export
- Project archives with deduplicated embedded SoundFonts

## File formats

- **`.tp`** — TPanar project archive
- **`.tpi`** — TPanar sample instrument archive
- **WAV / AIFF / FLAC / OGG / MP3** — imported or edited audio
- **SF2 / SF3** — SoundFont instruments
- **`.sfz`** — SFZ instruments

Older **`.dg`** project files and **`.dgi`** instrument archives remain loadable for compatibility.

## Building

TPanar uses **autotools** and builds a native desktop application.

### Main dependencies

- C++17 compiler
- `wxWidgets`
- `jack`
- `nlohmann_json`
- `libsndfile`
- `libsamplerate`
- `soundtouch`
- `fftw3`
- `libarchive`
- `fluidsynth`
- `espeak-ng`
- `alsa`

### Build steps

```sh
autoreconf -fi
./configure
make
```

The executable is built as:

```sh
./src/tpanar
```

## Platform notes

- JACK is the preferred backend for live routing and recording.
- OSS is available as a playback-oriented backend.
- On FreeBSD, the project prefers **clang** for correct wxWidgets runtime behavior.

## Status

TPanar is under active development. The codebase already contains a working tracker, recording path, audio editor, project archiving, and drum-oriented editing workflow, while routing/plugin refinement and broader export workflows are still evolving.

## License

TPanar is released under the **GNU General Public License v3.0 or later**.
