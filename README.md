# RSMods-Min v1.0.0

First stable release for the Rocksmith Championship community.

## Features

- F8 song re-enumeration without restarting Rocksmith
- Real-time ASIO drop tuning
- Drop range from E Standard down to one octave below
- Alternate tuning reference from A420 through A461
- Small on-screen tuning display
- Two-player / multiplayer ASIO input support
- Minimal `xinput1_3.dll` proxy with no GUI or unrelated features

## Requirements

- Rocksmith 2014 Remastered
- RS_ASIO
- ASIO audio interface

RSMods-Min does not replace RS_ASIO.

## Install

Copy `xinput1_3.dll` into the Rocksmith 2014 root directory.

If another mod already supplies `xinput1_3.dll`, back it up or remove it first.

## Controls

- `F8` — Re-enumerate songs
- `,` — Drop one semitone
- `.` — Raise one semitone
- `;` — Reference frequency -1 Hz
- `'` — Reference frequency +1 Hz
- `\` — Reset reference to A440

## Status

This is a release candidate intended for community testing.

Pitch shifting may introduce subtle processing artifacts depending on the signal and amount of pitch shift.

## Credits

RSMods-Min builds on research and work from the Rocksmith modding community.

In particular:

- **RSMods** — inspiration and prior research around Rocksmith modding and song re-enumeration
- **RS_ASIO** — ASIO support for Rocksmith 2014
- **Stephan M. Bernsee** — `smbPitchShift` DSP algorithm, used under the Wide Open License

RSMods-Min's integration code, ASIO interception, multiplayer handling, tuning controls, OSD, and enumeration implementation are original to this project.

See `THIRD_PARTY-NOTICES.txt` for third-party licensing details.
