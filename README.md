# RL-Mods v1.4

A lightweight Rocksmith 2014 Remastered mod focused on practical quality-of-life features without the large hook/protection layer used by broader mod suites.

## Features

- F8 song re-enumeration without restarting Rocksmith
- Real-time ASIO pitch shifting (includes support for Int32LSB and Float32LSB ASIO sample formats)
- Manual pitch shifting down or up
- Alternate tuning reference from A420 through A461
- Automatic song tuning from Rocksmith's pre-song tuner
- Runtime physical guitar tuning setup with F10
- Physical-aware bidirectional Auto tuning
- Immediate Auto recalculation after changing physical guitar setup
- Persistent Player 1 / Player 2 physical guitar baselines
- True dry bypass at neutral pitch / A440
- Single-player and two-player tuning OSD
- ASIO readiness and error reporting
- Automatic score screenshots on Rocksmith result screens
- Adjustable screenshot delay with persistent settings
- Optional screenshot diagnostics through `RLMods.ini`
- F4 in-game hotkey/help overlay
- Minimal `xinput1_3.dll` proxy with no external GUI

## Requirements

- Rocksmith 2014 Remastered
- RS_ASIO
- ASIO audio interface
- Steam screenshots enabled if using automatic score capture

RL-Mods does not replace RS_ASIO.

## Install

Copy `xinput1_3.dll` into the Rocksmith 2014 root directory.

If another mod already supplies `xinput1_3.dll`, back it up or remove it first. RL-Mods is intended to run as the XInput proxy and is not designed to be chained with RSMods, RSModsPlus, or another `xinput1_3.dll` proxy.

Create or update `RLMods.ini` in the Rocksmith directory:

```ini
[Rocksmith]
Version=2022

[Tuning]
Player1Physical=0
Player2Physical=0

[OSD]
DurationMs=5000

[Screenshot]
Enabled=1
DelayMs=10000
Debug=0
```

Use `Version=2022` for the September 2022 Remastered executable or `Version=2024` for the Learn & Play memory layout.

`Player1Physical` and `Player2Physical` are semitone offsets from E Standard:

- `0` = E Standard
- `-1` = Eb Standard
- `-2` = D Standard

If the tuning values are omitted, RL-Mods defaults to E Standard.

## Controls

### Normal Tuning Mode

- `F4` — Show RL-Mods hotkey help
- `F5` — Toggle automatic score screenshots
- `F6` — Reduce screenshot delay by 1 second
- `F7` — Increase screenshot delay by 1 second
- `F8` — Re-enumerate songs
- `F9` — Cycle tuning mode: Player 1 / Player 2 / Sync / Auto
- `F10` — Enter physical guitar Setup Mode
- `,` — Drop one semitone in manual mode
- `.` — Raise one semitone in manual mode
- `;` — Reference frequency -1 Hz
- `'` — Reference frequency +1 Hz
- `\` — Reset reference to A440

### Setup Mode

- `F10` — Exit Setup Mode
- `,` — Player 1 physical tuning down one semitone
- `.` — Player 1 physical tuning up one semitone
- `;` — Player 2 physical tuning down one semitone
- `'` — Player 2 physical tuning up one semitone

While Setup Mode is active, the tuning OSD stays visible and the current audio shift remains unchanged.

RL-Mods hotkeys only act while Rocksmith owns the foreground window. Key presses made while another application has focus are discarded rather than queued for later.

## Tuning Modes

### Player 1 / Player 2

Manual control of the selected player's pitch shift and reference frequency.

### Sync

Applies the same manual tuning controls to both players.

### Auto

Auto is the default tuning mode. Press `F9` to cycle into the manual Player 1 / Player 2 / Sync modes when needed.

Auto uses Rocksmith's pre-song tuner as the authority. The primary reader uses Rocksmith's six-string tuner target object directly, including custom tunings; the legacy single-player tuner text path is retained only as a compatibility fallback.

RL-Mods compares the song target to the physical guitar tuning currently known for that player and chooses a global virtual pitch shift that minimizes physical retuning.

The selection favors:

1. the fewest strings requiring physical retuning
2. the least total physical semitone movement
3. the smallest virtual pitch shift when otherwise tied

Examples:

- Guitar in E Standard → D Standard song: virtual shift `-2`
- Guitar in Eb Standard → E Standard song: virtual shift `+1`
- Guitar in Eb Standard → D Standard song: virtual shift `-1`
- Guitar in Eb Standard → Drop D song: virtual shift `+1`; only the low string needs to move physically
- Guitar in Drop D → E Standard song: virtual shift `0`; only the low string needs to return to E
- Guitar in Eb Standard → Eb Drop Db song: virtual shift `0`; only the low string needs to move physically

Any remaining non-uniform string changes are performed physically in Rocksmith's tuner.

When the tuner successfully advances into gameplay, the shift is latched for the song.

This also supports Nonstop Play: if Rocksmith presents another pre-song tuner, Auto processes the new target. If Rocksmith skips the tuner, RL-Mods leaves the current shift unchanged.

## Physical Guitar Setup

Press `F10` to tell RL-Mods what tuning the guitar in your hands is actually in.

Setup Mode changes the declared standard tuning for Player 1 and Player 2 independently without leaving Rocksmith or editing the INI manually.

Use Setup Mode when:

- starting with a guitar that is not in the saved tuning
- swapping to a differently tuned guitar
- manually changing the guitar tuning outside Rocksmith
- correcting RL-Mods' current physical tuning assumption

When Setup Mode is exited, the declared physical tuning is saved to `RLMods.ini` and becomes the starting baseline for the next Rocksmith session.

Temporary string-specific retuning inferred during normal Auto operation does not overwrite the saved baseline.

If Setup Mode is used while Auto already has a valid current song target, RL-Mods recalculates the required virtual shift when Setup Mode is exited.

## Tuning OSD

The tuning OSD shows the active tuning mode, guitar tuning, effective target, pitch shift, and reference frequency.

In Setup Mode, the OSD stays visible while physical guitar tuning is being changed.

The normal OSD hold time is configurable in `RLMods.ini`:

```ini
[OSD]
DurationMs=5000
```

`DurationMs` is clamped to 500–60000 ms. If omitted, RL-Mods keeps the previous 2200 ms default.

Single-player and multiplayer layouts are sized independently. Multiplayer display is based on Rocksmith's multiplayer state rather than merely detecting a second configured ASIO input.

ASIO setup failures are reported instead of silently accepting tuning commands that cannot be processed.

`ASIO: waiting for audio` is a normal transient startup state.

Common errors:

- `ASIO hook failed` — RL-Mods could not install its RS_ASIO interception.
- `ASIO: buffer setup failed` — the ASIO driver failed while creating or recreating its buffers.
- `ASIO: no input channel bound` — check the `Channel=` value in the relevant `[Asio.Input.N]` section of `RS_ASIO.ini`.
- `ASIO: unsupported input format` — the bound input is not using the supported 32-bit integer ASIO sample format.
- `ASIO: duplicate input Channel` — both player inputs are configured to the same ASIO channel.

RL-Mods matches `RS_ASIO.ini` `Channel=` directly to the driver's ASIO channel number.

## Screenshot Settings

Screenshot settings are stored in `RLMods.ini`.

```ini
[Screenshot]
Enabled=1
DelayMs=10000
Debug=0
```

- `Enabled=1` enables automatic score screenshots.
- `DelayMs` controls how long RL-Mods waits after detecting a score screen before triggering Steam screenshot capture.
- `Debug=1` enables the diagnostic screenshot overlay showing the detected Rocksmith menu and capture status.

Screenshot delay is limited to 3–20 seconds and persists between sessions.

## Automatic Score Capture

RL-Mods watches for result screens including:

- Learn a Song
- Score Attack
- Duet
- Head-to-Head

When one is detected, RL-Mods waits for the configured delay and triggers Steam screenshot capture once.

If Rocksmith loses focus before the delay expires, the capture remains armed but is not sent to the foreground application. If the same score screen is still active when Rocksmith regains focus, the screenshot is taken then.

The default delay is 10 seconds.

## Audio Latency

At neutral pitch / A440, RL-Mods uses a true dry path and adds no DSP latency.

When pitch shifting is active, the pitch shifter adds 768 samples of processing latency — approximately 16 ms at 48 kHz — in addition to normal interface and Rocksmith audio latency.

Tuning changes that cross the neutral boundary use a short output duck so the zero-latency dry path and delayed shifted path can switch without an audible hard cut or dry/wet comb filtering.

## Design

RL-Mods intentionally stays small and focused. It uses a minimal XInput proxy, intercepts the RS_ASIO driver path for tuning, and implements only the Rocksmith memory interactions needed by its features.

The audio callback performs bounded processing on preallocated buffers. UI polling, hotkeys, menu reads, and overlays run on RL-Mods' worker thread rather than the ASIO callback thread.

The goal is practical functionality with as little interference with Rocksmith as possible.

## Credits

RL-Mods builds on research and work from the Rocksmith modding community.

In particular:

- **RSMods** — prior Rocksmith memory research and auto-tuning behavior reference
- **RS_ASIO** — ASIO support for Rocksmith 2014
- **Stephan M. Bernsee** — `smbPitchShift` DSP algorithm, used under the Wide Open License

RL-Mods' integration code, ASIO interception, multiplayer handling, tuning controls, OSD, screenshot controls, and enumeration implementation are original to this project.

RL-Mods is open source under the MIT License. See `LICENSE` and `THIRD_PARTY-NOTICES.txt`.
