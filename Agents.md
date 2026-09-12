# AGENTS.md

## Project

RL-Mods is a lightweight Rocksmith 2014 Remastered mod implemented as a Win32
`xinput1_3.dll` proxy.

The project focuses on a small set of practical Championship-oriented features
while minimizing interference with Rocksmith and RS_ASIO. Keep the codebase
compact, readable, dependency-light, and easy to reason about.

The current release line is v1.4. The major v1.4 tuning work adds:

- runtime physical guitar tuning setup;
- bidirectional pitch shifting;
- physical-aware Auto tuning;
- independent Player 1 / Player 2 physical tuning state;
- immediate Auto recalculation after changing the declared guitar;
- persistence of each player's declared physical baseline.

Do not turn RL-Mods into a broad mod framework. Add the behavior the feature
needs and leave unrelated systems alone.

## Build

Primary build target:

- Visual Studio 2022
- MSVC v143
- C++17
- Release
- Win32
- Windows 10 SDK

Project file:

```text
src/RL-Mods.vcxproj
```

Output:

```text
src/Release/xinput1_3.dll
```

Command-line build:

```bat
msbuild src\RL-Mods.vcxproj /p:Configuration=Release /p:Platform=Win32
```

AppVeyor builds the same `Release|Win32` configuration.

The real MSVC/AppVeyor build is authoritative. Lightweight local syntax or stub
checks are useful, but they do not replace compiling the actual Windows project.

## Source of truth

When modifying behavior, use the current repository source as the primary source
of truth.

For Rocksmith memory behavior:

1. Prefer paths already proven in this project.
2. Use reproducible live observations or diagnostic output when needed.
3. Use external Rocksmith modding projects as research references, not as
   unquestioned authority.
4. Verify executable-version differences, pointer-chain semantics, and whether
   roots are absolute or module-relative before using an offset.
5. Prefer read-only state access when the required information already exists in
   Rocksmith memory.

Do not infer memory behavior from similar-looking roots or structures.

Do not introduce hooks, code patches, VEH/INT3 diagnostics, broad memory scans,
or other invasive mechanisms when a stable read-only data path already exists.

## Architecture

### `main.cpp`

Owns the XInput proxy lifecycle, worker thread, process-level polling, and
forwarding to the real system XInput DLL.

Keep it thin. Feature-specific state and behavior should normally live in the
module that owns that feature.

### `AsioPassthrough.*`

Owns RS_ASIO interception, player input routing, readiness/error state, and
delivery of per-player processing.

Audio callback work must remain bounded and allocation-free.

Player routes are independent. Do not collapse per-player state into a single
global audio target unless the feature explicitly requires synchronized state.

### `RealtimePitchShifter.*`

Owns pitch-shifting DSP.

Important invariants:

- neutral pitch must retain the true dry path;
- active pitch shifting may use the DSP/history path;
- positive and negative pitch ratios are supported by the DSP path;
- transitions between dry and shifted paths must avoid obvious discontinuities
  or dry/wet comb filtering;
- do not add unnecessary processing to the dry path.

The current shifted path adds approximately 768 samples of processing latency,
about 16 ms at 48 kHz. Neutral pitch/A440 remains the zero-DSP-latency path.

Do not "improve" the DSP casually. Pitch quality, latency, transient behavior,
and Rocksmith note detection all matter.

### `RocksmithTuning.*`

Owns Rocksmith tuning-related memory reads and tuning interpretation.

Keep version-specific memory knowledge localized here where practical.

Memory-reading code should fail safely on unreadable, null, invalid, or
out-of-range data rather than treating bad memory as valid state.

The current structural pre-song tuner reader is the preferred source of truth.
The proven object layout is:

```text
root -> +0x10 -> +0xD0 -> +0x94

container +0x10 = SP / P1 owner
container +0x18 = P2 owner

owner +0x38 = MIDI[6]
owner +0x50 = tuning offsets[6]
```

The MIDI values are validation data. The tuning offsets are the actual
Rocksmith tuner target.

The single-player tuner-text reader is only a compatibility fallback.

Do not claim multiplayer lacks tuner text. Rocksmith visibly renders separate
P1/P2 tuning information. The accurate limitation is that the known
single-player tuner-text pointer path does not expose those multiplayer labels.

### `TuningControl.*`

Owns tuning-control modes, per-player tuning state, automatic tuning behavior,
physical guitar setup, persistence of physical baselines, and the tuning OSD.

Automatic tuning is driven by Rocksmith state and must preserve independent
player state in multiplayer.

#### F9 tuning modes

F9 cycles exactly:

```text
Player 1 -> Player 2 -> Sync -> Auto
```

Keep that cycle intact unless a requested feature explicitly changes it.

#### F10 Setup Mode

F10 Setup Mode is orthogonal to the F9 tuning mode. It is not a fifth F9 mode.

While Setup Mode is active:

- keep the tuning OSD visible;
- leave the current virtual audio shift untouched;
- pause normal Auto recomputation so it does not fight setup changes;
- `,` / `.` adjust Player 1 declared physical standard tuning;
- `;` / `'` adjust Player 2 declared physical standard tuning;
- F9 mode changes are suppressed until Setup Mode exits.

Setup changes the declared physical guitar baseline one uniform semitone at a
time.

On entering Setup Mode, preserve enough current Auto target context to allow a
correct recalculation on exit.

On exit:

- persist the declared P1/P2 physical baselines;
- if Auto has a valid effective/current target, immediately recalculate against
  the newly declared physical tuning;
- if no valid target exists, let the next normal tuner event perform the Auto
  calculation;
- in manual modes, do not silently replace the user's selected manual shift.

#### Persistent physical baselines

The declared physical baselines are stored in `RLMods.ini`:

```ini
[Tuning]
Player1Physical=0
Player2Physical=0
```

Values are uniform semitone offsets from E Standard:

```text
 0 = E Standard
-1 = Eb Standard
-2 = D Standard
```

If absent, default to E Standard.

Persistence represents the guitar the player expects to still be holding next
session.

Important distinction:

- F10 Setup changes the persistent declared baseline.
- Temporary physical state inferred because Rocksmith asked for a residual Drop
  or custom-string retune is session state and must not overwrite that saved
  baseline.

#### Bidirectional Auto tuning

Auto may choose a virtual shift in either direction.

For each candidate global shift `S`:

```text
requiredPhysical = target - S
```

In code this is represented with `RocksmithTuning::Shifted(target, -S)`.

Choose the candidate in this order:

1. fewest strings requiring physical retuning;
2. least total physical semitone movement;
3. smallest absolute virtual shift.

Do not revert to the old "highest target string" algorithm for v1.4 behavior.

Expected examples:

```text
Physical E  -> Target D Standard  = shift -2
Physical Eb -> Target E Standard  = shift +1
Physical Eb -> Target D Standard  = shift -1
Physical Eb -> Target Drop D      = shift +1, residual low-string retune
Physical Drop D -> Target E       = shift 0, residual low-string retune
Physical Eb -> Target Eb Drop Db  = shift 0, residual low-string retune
```

The goal is minimum real-world guitar retuning, not merely preferring downward
DSP shifts.

#### Auto tuner session behavior

The pre-song tuner remains Rocksmith's authority.

A normal Auto tuner event should:

1. capture the player's current physical state;
2. neutralize the virtual shift while Rocksmith checks the actual guitar;
3. read the Rocksmith target;
4. choose the best global virtual shift;
5. apply it;
6. let Rocksmith request any residual non-uniform physical string changes;
7. commit the resulting state when tuner -> gameplay succeeds.

If the tuner is cancelled/backed out of, restore the previous state.

Nonstop Play may skip the tuner. If it does, preserve the current shift rather
than inventing a new target.

### `ScreenshotControl.*`

Owns score-screen detection, screenshot settings, screenshot timing, and
screenshot/help overlays.

Keep screenshot behavior separate from tuning behavior.

Release/version text displayed by the help overlay must stay aligned with the
actual release version.

## Coding style

Match the existing codebase.

General expectations:

- C++17 only.
- Favor straightforward code over abstraction for its own sake.
- Keep ownership of state obvious.
- Prefer small helpers with descriptive names.
- Avoid hidden global coupling between modules.
- Avoid new dependencies unless they provide clear value.
- Preserve existing behavior outside the requested change.
- Keep hot-path code predictable and allocation-free where required.
- Do not add defensive layers for hypothetical problems that the project does
  not actually have.
- Use enough validation to fail safely around external/process memory, but do
  not turn a tiny mod into enterprise infrastructure.

Windows headers can define macros such as `min` and `max`; be careful when using
similarly named standard-library functions.

## Making changes

Before editing:

- read the relevant implementation and header together;
- follow the state through its callers and consumers;
- identify whether the change crosses module boundaries;
- understand whether the code runs on the worker thread, UI path, or audio
  callback;
- inspect the current branch/source rather than relying on an older copy.

When editing:

- make the complete coherent change, not just the smallest textual change;
- keep declarations, definitions, and call sites synchronized;
- preserve F9/F10 context-sensitive hotkey behavior;
- preserve P1/P2 independence;
- avoid unrelated refactors unless they are necessary to make the requested
  change correct.

Before handing back code:

- check modified declarations and definitions;
- check all affected call sites;
- check for accidental duplicate or stale helper names;
- check headers and implementation files together;
- check README/help/version text when controls or release behavior changed;
- compile the actual project when possible.

A narrowly scoped change is good. A narrowly scoped change that misses an
interaction is not.

## Multiplayer

Treat these as separate concerns:

- Rocksmith multiplayer state;
- Player 2 ASIO route readiness;
- Player 2 tuning/setup state;
- Player 2 Rocksmith tuner target.

Do not assume that proving one proves the others.

Where Rocksmith exposes independent player arrangements or tuning state, keep
the resulting processing independent as well.

Do not force the user to manufacture arbitrary multiplayer target combinations;
Rocksmith songs/arrangements dictate the available P1/P2 combinations. Test with
real combinations that are available.

## OSD/UI

Overlay sizing and rendering should be based on the text actually being drawn,
using consistent font, width, wrapping, and padding between measurement and
paint.

UI code should reflect underlying state accurately. Do not hide a state or
logic failure by changing display text alone.

Setup Mode intentionally pins the tuning OSD while the user changes declared
physical tuning.

Avoid layout changes that cause unnecessary movement or clipping when state
changes.

## Diagnostics

Add diagnostics when they materially reduce uncertainty.

Prefer one broad, useful diagnostic capture when the unknown area is large over
repeated tiny guesses at adjacent memory.

Prefer targeted diagnostics that test a specific hypothesis once the relevant
area is understood.

Temporary diagnostic code should remain easy to identify and remove once the
underlying behavior is understood.

Do not leave experimental hooks or diagnostic hotkeys in production code.

## External reference projects

RSMods and related Rocksmith projects are useful references for prior research,
menu names, pointer chains, and behavior.

When borrowing ideas:

- verify the exact source/version;
- understand the code rather than copying it mechanically;
- adapt it to RL-Mods' smaller architecture;
- validate the result in this project.

## Delivery workflow

When preparing code for the project owner:

- provide complete replacement source files rather than patches unless a patch
  is explicitly requested;
- keep the replacement based on the current branch version of the file;
- if multiple files are required, package only the coherent replacement set;
- do not claim a GitHub write/commit occurred unless it actually succeeded;
- do not substitute a patch when an upload-ready replacement file was requested.

The project owner commonly uploads replacement files through the GitHub web UI,
so make deliverables directly usable that way.

## Release discipline

Keep the known-good release branch stable while an RC is being exercised.

For a feature release:

1. build and test on the feature branch;
2. cut an RC from that work when useful;
3. gather Championship group feedback;
4. fix issues on the feature branch;
5. merge only after the RC is proven.

Do not merge experimental work into the stable branch merely to create a test
release.

## Design principle

RL-Mods should remain small, dependable, and easy to reason about.

Prefer:

- correct behavior;
- low latency;
- clear ownership;
- safe failure modes;
- readable code;
- reproducible verification;
- real-world usability over theoretical purity.

Do not add complexity merely because another mod suite has it.
