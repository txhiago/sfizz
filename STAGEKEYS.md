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
| `fix: never pass memcpy a null pointer from an empty Buffer` | `Buffer::resize()` copied its old contents with `memcpy` even on a first allocation, when the old pointer is null and the size zero; the copy constructor and copy assignment did the same from an empty buffer. Nothing is read, but `memcpy`'s pointers must be valid even for zero bytes, and glibc declares them nonnull, so UBSan stops a Linux run there before anything plays. Found when StageKeys #98 put its sampler self-test under ASan and UBSan. Each copy is now skipped when there is nothing to copy |
| `feat: count the voices stolen at the polyphony limit` | StageKeys #12 shows the musician how many voices were cut at the cap since the counters were reset, beside the voices rendering. sfizz counted nothing. `getNumStolenVoices()` is monotonic since the synth was built and counts each voice cut to make room at the engine limit, a stereo pair's two voices included, and only voices still sounding: the instrument's own choking (region, group, set and note polyphony) is authored content, not stealing, and is not counted, nor is a release sample that never started because `setReleaseVoicesYield()` found no room for it. A relaxed atomic add on the rendering thread; reading it allocates nothing |
| `feat: budget the release-triggered voices that start in one block` | StageKeys #156. Releasing a chord of Salamander starts its release layer (string resonance, hammer noise: `trigger=release`) all at once, two or three voices a note, and on the phone's cores that burst is the last thing that drops out (6 dropouts in 16 releases of 32-88 notes at a cap of 96). `Sfizz::setReleaseStartBudget(perBlock, maxBlocksLate)` places a note-off's voices on a fixed schedule: the first `perBlock` start where the note-off arrived, the next `perBlock` one block later (the frames the host last rendered - not the `samplesPerBlock` it prepared with, which on Android is the stream's buffer capacity - through the voice's start delay, which #168 made exact in any block), and so on; one that would start more than `maxBlocksLate` blocks late is skipped (`getNumSkippedReleaseVoices()`). The schedule is a function of the notes played, never of the load, so the same lifted pedal sounds the same every time (principle #7). A slot is claimed only by a voice that starts, so a release that yields at the polyphony limit (the patch above) or finds no free voice takes none; the polyphony checks still run at the note-off's own time, so no held note is lost to the delay. `VoiceManager` holds the schedule, `Synth::Impl::startVoice` applies it and `renderBlock` moves it on a block, and `allSoundOff()` clears it. Default is unchanged: 0 per block, no budget |
| `fix: count a voice's age from its trigger, not from zero again`, then `fix: start a voice on its sample's first frame, whichever block it starts in` | `Voice::renderBlock` clamped a voice's age with `min(age - triggerDelay, 0)` where `max` was meant, so every voice read age 0 again after its first block, and `fillWithData()` took age 0 to mean "the voice just started": **a voice started at once held its playhead one frame one block in** (5.3 ms at a 256-frame callback), and one a region `delay=` started two or more blocks in **skipped its sample's first frame**. Now the age counts the frames since the trigger, and the start is a flag of its own, set when a voice starts and cleared by the first fill that renders a frame: every voice begins on its sample's first frame and steps evenly from there, at once or delayed into any block. Found in StageKeys #168 on a ramp sample; the one-frame hold's size is the waveform's step at that frame - −70 dB re the note's peak at worst on Salamander and ED GERMAN LUV, whose sound has barely begun 5.3 ms in, −41 dB on an ED organ, −15 dB on a bright ED saw. Ages also stop running a block behind in the stealers' oldest-first comparisons |
| `fix: never render a voice whose start failed` | StageKeys #121. sfizz queues a background load for every voice it starts on a sample longer than its preload, in a queue of 256 (`config::maxVoices`) that a dispatcher thread empties; a start that finds the queue full (or a sample not preloaded) gets no file promise and `Voice::startVoice()` returns false. It had already taken the new region and saves its modulation targets last, so the voice was left waiting for its cleanup with the region's id and the targets of whatever it played before - another region's, or none - and `Synth::renderBlock` rendered it once: `ModMatrix::getModulation()` refuses a per-voice target of another region, and the amplitude envelope copied from the null pointer. A segfault on the rendering thread, found on the phone when #87's check rendered a block per note in a second sampler while the dispatcher fell behind; reproduced on the desktop by thousands of note-ons between two blocks with the dispatcher starved. `Synth::Impl::startVoice` now resets a voice whose start failed, so it is never rendered and is free for the next start; the amplitude envelope also silences and lets go a voice whose target reads null rather than copying from it, the one modulation read that was not already optional. `getNumFailedVoiceStarts()` counts the starts that failed for want of sample data (a disabled region also fails to start, but never rendered anything and is the file's own choice: freed, not counted): the notes that did not sound. Behavior is otherwise unchanged: a failed start was already a note that did not sound |
| `perf: generate an LFO's phase without a dependency between frames`, then `perf: leave alone an LFO every connection of which is at zero depth` | StageKeys #163 writes SF2's default mod wheel vibrato into every region an SF2 import makes (`lfo1_pitch_oncc1=50`), so every voice runs an LFO; on the Moto G 06 that cost 9-11% of each block with 64 notes of a piano held, and on the desktop `LFO::generatePhase` was the costliest function of the render: each frame's phase was the last one plus the increment, wrapped by a float-to-int conversion, in series. The phase is now the block's first plus `i` increments, wrapped in the offset loop that already wraps every frame, so both loops vectorize. And `ModMatrix::getModulation` skips a connection to an *additive* target whose depth is zero for the whole cycle (its fixed depth zero, its depth controllers at rest - the wheel down, nearly all of a service); a source nothing else reads is then moved on by `generateDiscarded()` at the voice's end, which the LFO source now overrides with `LFO::advance()`: the delay, the fade-in and each subwaveform's phase run as `process()` would, without the wave, so a wheel moved two seconds into a note finds the vibrato past its delay and at the phase it would have reached (0.41 degrees from the unpatched render, the first patch's rounding, where a frozen phase would be 127 degrees off). `advance()` declines where the phase is not a function of the frames alone - rate, beats or phase modulation, a playing beat clock, a sample-and-hold - and the cycle is processed in full. On a multiplicative target a depth of zero is a gain of zero, so nothing is skipped there. Measured on the Moto, 15 interleaved pairs: +3.6% a block with the wheel at rest and +4.9% with it at 127, against +11.0% unpatched |

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
