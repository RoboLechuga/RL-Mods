# RSMods-Min

A lightweight, purpose-built utility for **Rocksmith 2014 Remastered**.

RSMods-Min keeps a few useful functions from larger Rocksmith mod projects, trims away the rest, and packages them into a tiny `xinput1_3.dll` proxy.

The goal is simple:

**Do a few things well, with as little interference as possible.**

## Features

### Song Re-enumeration

Press **F8** to force Rocksmith to re-enumerate the song library.

Useful after adding or removing CDLC without restarting the game.

### Drop Tuning

Pitch-shift the incoming guitar signal before Rocksmith processes it.

Controls:

| Key | Function |
|---|---|
| `,` | Lower tuning one semitone |
| `.` | Raise tuning one semitone |

The on-screen display shows the resulting tuning:

`E` → `Eb` → `D` → `C#` → `C` → etc.

### Alternate Tuning Reference

Adjust the tuning reference independently of the semitone shift.

| Key | Function |
|---|---|
| `;` | Reference -1 Hz |
| `'` | Reference +1 Hz |
| `\` | Reset reference to A440 |

Supported range:

**A420 through A461**

Example:

`Drop: Eb    Ref: A445`

This is useful for recordings mastered slightly sharp or flat relative to A440.

### Multiplayer

Both Rocksmith players are supported when using two inputs from the same ASIO interface.

Both players use the same selected drop tuning and reference frequency, while each input has its own independent pitch-shifter processing state.

### On-Screen Display

Tuning changes briefly display in a small overlay.

The tuning controls themselves continue to work even if the overlay cannot be displayed.

## Requirements

- Rocksmith 2014 Remastered
- Windows
- RS_ASIO
- An ASIO audio interface

RSMods-Min does **not** replace RS_ASIO.

Your existing RS_ASIO files and configuration remain in place.

## Installation

1. Download `xinput1_3.dll` from the latest RSMods-Min release.
2. Copy it into the Rocksmith 2014 root folder.
3. If another mod already provides `xinput1_3.dll`, remove or replace that file first.
4. Leave your existing RS_ASIO installation and `RS_ASIO.ini` in place.
5. Start Rocksmith normally.

To uninstall RSMods-Min, remove its `xinput1_3.dll`.

## Controls

| Key | Function |
|---|---|
| `F8` | Force song re-enumeration |
| `,` | Lower pitch one semitone |
| `.` | Raise pitch one semitone |
| `;` | Lower tuning reference 1 Hz |
| `'` | Raise tuning reference 1 Hz |
| `\` | Reset tuning reference to A440 |

## How It Works

RSMods-Min is a small 32-bit `xinput1_3.dll` proxy.

It forwards the normal XInput exports to the Windows system XInput library while adding two narrowly targeted Rocksmith functions.

### Song Enumeration

Rather than installing a permanent Rocksmith code hook, RSMods-Min resolves Rocksmith's enumeration state when **F8** is pressed and sets the required enumeration flags directly.

### Pitch Shifting

With RS_ASIO, RSMods-Min intercepts the ASIO input buffers before Rocksmith receives them.

The pitch shifter uses a short rolling delay line with period detection, waveform-aligned jumps, and crossfades to alter pitch while keeping additional latency small.

Each multiplayer input is processed independently.

## Philosophy

RSMods-Min is intentionally **not** a general Rocksmith mod framework.

There is:

- no configuration GUI
- no Direct3D mod overlay
- no MIDI subsystem
- no Crowd Control
- no gameplay modification framework
- no background logging system
- no large collection of unrelated patches

It exists to perform a small set of useful operations and otherwise stay out of Rocksmith's way.

## Building

Visual Studio 2022:

- Configuration: `Release`
- Platform: `Win32`
- Project: `src/RSMods-Min.vcxproj`

Output:

`src/Release/xinput1_3.dll`

AppVeyor is configured to build the `develop` branch.

## Development

`main` contains release-ready code.

Development takes place on `develop` and is merged into `main` through pull requests.

The repository is provided publicly so the implementation can be inspected and learned from.

If you want to modify, extend, or experiment with RSMods-Min, please **fork the repository** and work from your own fork.

## Credits

RSMods-Min builds on work done by members of the Rocksmith modding community.

In particular:

- **RSMods** — Rocksmith modding techniques and song re-enumeration research
- **RSModsPlus** — ASIO input interception and delay-line pitch-shifting work
- **RS_ASIO** — ASIO support for Rocksmith 2014

RSMods-Min would not exist without the work of those projects and their contributors.

## Notes

Pitch shifting is performed in real time and may introduce a small amount of additional latency or occasional low-level splice artifacts depending on the input signal and tuning shift.

RSMods-Min is an independent community project and is not affiliated with Ubisoft.
