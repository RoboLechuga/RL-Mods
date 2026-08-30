# RL-Mods v1.2

A lightweight Rocksmith 2014 Remastered mod focused on practical quality-of-life features without the large hook/protection layer used by broader mod suites.

## Features

- F8 song re-enumeration without restarting Rocksmith
- Real-time ASIO drop tuning
- Drop range from E Standard down to one octave below
- Alternate tuning reference from A420 through A461
- True dry bypass at E Standard / A440
- Small on-screen tuning display
- Two-player / multiplayer ASIO input support
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

## Controls

- `F4` — Show RL-Mods hotkey help
- `F5` — Toggle automatic score screenshots
- `F6` — Reduce screenshot delay by 1 second
- `F7` — Increase screenshot delay by 1 second
- `F8` — Re-enumerate songs
- `F12` — Steam screenshot (native Rocksmith / Steam behavior)
- `,` — Drop one semitone
- `.` — Raise one semitone
- `;` — Reference frequency -1 Hz
- `'` — Reference frequency +1 Hz
- `\` — Reset reference to A440

Screenshot delay is limited to 3–20 seconds.

## Screenshot Settings

Screenshot settings are stored in `RLMods.ini` in the Rocksmith directory.

Example:

```ini
[Screenshot]
Enabled=1
DelayMs=10000
Debug=0
```

- `Enabled=1` enables automatic score screenshots.
- `DelayMs` controls how long RL-Mods waits after detecting a score screen before triggering Steam's screenshot key.
- `Debug=1` enables the diagnostic screenshot overlay showing the detected Rocksmith menu and capture status.

The screenshot settings persist between Rocksmith sessions.

## Automatic Score Capture

RL-Mods watches for Rocksmith result screens including:

- Learn a Song
- Score Attack
- Duet
- Head-to-Head

When one is detected, RL-Mods waits for the configured delay and triggers the normal Steam `F12` screenshot action once.

The default delay is 10 seconds.

## Status

RL-Mods v1.2 is the current stable release.

Pitch shifting may introduce subtle processing artifacts depending on the signal and amount of pitch shift.

## Design

RL-Mods intentionally stays small and focused. It uses a minimal XInput proxy, intercepts the RS_ASIO driver path for tuning, and implements only the Rocksmith memory interactions needed by its features.

The goal is practical functionality with as little interference with Rocksmith as possible.

## Credits

RL-Mods builds on research and work from the Rocksmith modding community.

In particular:

- **RSMods** — inspiration and prior research around Rocksmith modding and song re-enumeration
- **RS_ASIO** — ASIO support for Rocksmith 2014
- **Stephan M. Bernsee** — `smbPitchShift` DSP algorithm, used under the Wide Open License

RL-Mods' integration code, ASIO interception, multiplayer handling, tuning controls, OSD, screenshot controls, and enumeration implementation are original to this project.

See `THIRD_PARTY-NOTICES.txt` for third-party licensing details.
