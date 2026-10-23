// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#include "LFO.h"
#include "../../LFO.h"
#include "../../Synth.h"
#include "../../Voice.h"
#include "../../SIMDHelpers.h"
#include "../../Config.h"
#include "../../utility/Debug.h"

namespace sfz {

LFOSource::LFOSource(VoiceManager& manager)
    : voiceManager_(manager)
{
}

void LFOSource::init(const ModKey& sourceKey, NumericId<Voice> voiceId, unsigned delay)
{
    Voice* voice = voiceManager_.getVoiceById(voiceId);
    if (!voice) {
        ASSERTFALSE;
        return;
    }

    const Region* region = voice->getRegion();
    LFO* lfo = nullptr;
    const LFODescription* desc = nullptr;

    switch (sourceKey.id()) {
    case ModId::AmpLFO:
        lfo = voice->getAmplitudeLFO();
        desc = &*region->amplitudeLFO;
        break;
    case ModId::PitchLFO:
        lfo = voice->getPitchLFO();
        desc = &*region->pitchLFO;
        break;
    case ModId::FilLFO:
        lfo = voice->getFilterLFO();
        desc = &*region->filterLFO;
        break;
    case ModId::LFO:
        {
            unsigned lfoIndex = sourceKey.parameters().N;
            if (lfoIndex >= region->lfos.size()) {
                ASSERTFALSE;
                return;
            }
            lfo = voice->getLFO(lfoIndex);
            desc = &region->lfos[lfoIndex];
        }
        break;
    default:
        ASSERTFALSE;
        return;
    }

    lfo->configure(desc);
    lfo->start(delay);
}

LFO* LFOSource::find(const ModKey& sourceKey, NumericId<Voice> voiceId) const
{
    const unsigned lfoIndex = sourceKey.parameters().N;

    Voice* voice = voiceManager_.getVoiceById(voiceId);
    if (!voice) {
        ASSERTFALSE;
        return nullptr;
    }

    const Region* region = voice->getRegion();

    switch (sourceKey.id()) {
    case ModId::AmpLFO:
        return voice->getAmplitudeLFO();
    case ModId::PitchLFO:
        return voice->getPitchLFO();
    case ModId::FilLFO:
        return voice->getFilterLFO();
    case ModId::LFO:
        if (lfoIndex >= region->lfos.size()) {
            ASSERTFALSE;
            return nullptr;
        }
        return voice->getLFO(lfoIndex);
    default:
        ASSERTFALSE;
        return nullptr;
    }
}

void LFOSource::generate(const ModKey& sourceKey, NumericId<Voice> voiceId, absl::Span<float> buffer)
{
    LFO* lfo = find(sourceKey, voiceId);
    if (!lfo) {
        fill(buffer, 0.0f);
        return;
    }

    lfo->process(buffer);
}

void LFOSource::generateDiscarded(const ModKey& sourceKey, NumericId<Voice> voiceId, absl::Span<float> buffer)
{
    LFO* lfo = find(sourceKey, voiceId);
    if (!lfo)
        return;

    if (!lfo->advance(buffer.size()))
        lfo->process(buffer);
}

} // namespace sfz
