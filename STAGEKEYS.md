# Why this fork exists

[StageKeys](https://github.com/txhiago/stagekeys) uses sfizz as its only audio
engine. **sfizz upstream was archived in March 2025** and is read-only, so there
is nowhere to send a patch. This fork is where StageKeys' patches live.

**Pin to the `stagekeys` branch, not `develop` and not a release tag.**

## What branch to build

| Branch | What it is |
|---|---|
| `stagekeys` | **Use this.** `develop` plus the patches below |
| `develop` | Upstream's final state, untouched, for diffing |
| `master` | Upstream's last release line, untouched |

## Why `develop` and not the 1.2.3 release

1.2.3 was tagged 2024-01-14. Upstream then committed for **fourteen more
months** — 39 commits, the last of them 2025-03-17. None of it was released, and
none of it ever will be.

Some of those commits matter to a stage instrument specifically:

| Commit | Why it matters |
|---|---|
| `40555826` | Fixes stuck notes, and a crash when using a loop |
| `f8fc628b` | Improved note-on performance |
| `d885ee50` | Stops `-mfpu=neon` / `-mfloat-abi=hard` being passed on processors reporting `arm64`, where clang rejects them |
| `7f501666` | Fixes a use-after-free |
| `698e27f6` | C++20 support |

A stuck note during a service is the exact failure StageKeys' first principle is
about. Pinning to the last *release* would leave that fix on the floor for no
reason, since there is no later release to wait for.

The last of those also halves the patch burden: building 1.2.3 needed two
patches, and `d885ee50` is one of them, already fixed here.

## The patches

| Patch | Why |
|---|---|
| `fix: drop the template disambiguator atomic_queue cannot use` | Four call sites in the vendored `atomic_queue` write `Base::template do_pop_any(...)` with no argument list. That is ill-formed C++, and clang promoted the diagnostic to a default error after 1.2.3 shipped. Fixed in the header rather than suppressed project-wide |
| `feat: let the host skip <effect> headers at load` | StageKeys' Engine owns effects, not the instrument file, and V1 has none. `Sfizz::setEffectsEnabled(false)` makes the next load skip every `<effect>` header before any bus is created, so no effect and no `fxN` bus exist and every region plays dry through main whatever its `effectN` sends say. `getNumIgnoredEffects()` reports how many were skipped. Default is unchanged: enabled |
| `feat: make room at the polyphony limit from the least audible voices` | Measured in StageKeys #45: releasing a seven-note chord at the cap had its fourteen release samples (`trigger=release` resonance and key noise) steal all five notes still held, and a new note at the cap took a held note before any fading tail - the default stealer takes the oldest voice, and held notes are older. `Sfizz::setReleaseVoicesYield(true)` sets the order room is made in at the engine limit, oldest first within each: release samples; then, for a note-on only, tails of notes let go (not while the pedal holds them); then held notes, by the stealer. A release sample only replaces an older release sample and is skipped otherwise - it never cuts a tail short or steals a held note. `Voice::releasing()` counts a release that is scheduled but not yet rendered, since a chord let go arrives in one block. Region, group and set polyphony are unchanged. Default is unchanged: off |
| `feat: report the voices rendering, not only the capped count` | `getNumActiveVoices()` is clamped to the cap, while the voice pool holds the cap plus a 1.5x overflow for stolen voices fading out - so a host could not see the real load, nor test that an instrument's own `polyphony` opcodes cannot raise the cap. `getNumRenderingVoices()` returns the unclamped count. It reads a size; nothing is allocated |

## Two notes for anyone embedding this

**`renderBlock`'s `numOutputs` counts stereo pairs, not channels.** Two buffers
means `numOutputs = 1`. Passing `2` writes past the end of the array and
segfaults somewhere unrelated.

**Never call `enableFreeWheeling()` on the audio thread.** It blocks the render
call until the background sample loader has data. Measured here, one block
overran a 256-frame deadline by **324%** — so reaching for it to cure dropouts
guarantees them instead. It is for offline rendering only, which is also why
rendering flat out is not a valid benchmark: a loop that produces 2.7 s of audio
in 12 ms starves the loader, voices die of it, and the render cost that comes
back is for far fewer voices than were played.
