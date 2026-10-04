// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#include "VoiceManager.h"
#include "SisterVoiceRing.h"
#include "RegionSet.h"
#include <absl/algorithm/container.h>

namespace sfz {

void VoiceManager::onVoiceStateChanging(NumericId<Voice> id, Voice::State state)
{
    if (state == Voice::State::idle) {
        Voice* voice = getVoiceById(id);
        const Region* region = voice->getRegion();
        const uint32_t group = region->group;
        RegionSet::removeVoiceFromHierarchy(region, voice);
        swapAndPopFirst(activeVoices_, [voice](const Voice* v) { return v == voice; });
        ASSERT(polyphonyGroups_.contains(group));
        polyphonyGroups_[group].removeVoice(voice);
    } else if (state == Voice::State::playing) {
        Voice* voice = getVoiceById(id);
        const Region* region = voice->getRegion();
        const uint32_t group = region->group;
        activeVoices_.push_back(voice);
        RegionSet::registerVoiceInHierarchy(region, voice);
        ASSERT(polyphonyGroups_.contains(group));
        polyphonyGroups_[group].registerVoice(voice);
    }
}

const Voice* VoiceManager::getVoiceById(NumericId<Voice> id) const noexcept
{
    const size_t size = list_.size();

    if (size == 0 || !id.valid())
        return nullptr;

    // search a sequence of ordered identifiers with potential gaps
    size_t index = static_cast<size_t>(id.number());
    index = std::min(index, size - 1);

    while (index > 0 && list_[index].getId().number() > id.number())
        --index;

    return (list_[index].getId() == id) ? &list_[index] : nullptr;
}

Voice* VoiceManager::getVoiceById(NumericId<Voice> id) noexcept
{
    return const_cast<Voice*>(
        const_cast<const VoiceManager*>(this)->getVoiceById(id));
}

void VoiceManager::reset()
{
    for (auto& voice : list_)
        voice.reset();

    polyphonyGroups_.clear();
    polyphonyGroups_.emplace(0, PolyphonyGroup{});
    setStealingAlgorithm(StealingAlgorithm::Oldest);
}

bool VoiceManager::playingAttackVoice(const Region* releaseRegion) noexcept
{
    const auto compatibleVoice = [releaseRegion](const Voice& v) -> bool {
        const TriggerEvent& event = v.getTriggerEvent();
        return (
            !v.isFree()
            && event.type == TriggerEventType::NoteOn
            && releaseRegion->keyRange.containsWithEnd(event.number)
            && releaseRegion->velocityRange.containsWithEnd(event.value)
        );
    };

    if (absl::c_find_if(list_, compatibleVoice) == list_.end())
        return false;
    else
        return true;
}

void VoiceManager::ensureNumPolyphonyGroups(int groupIdx) noexcept
{
    if (!polyphonyGroups_.contains(groupIdx))
        polyphonyGroups_.emplace(groupIdx, PolyphonyGroup{});
}

void VoiceManager::setGroupPolyphony(int groupIdx, unsigned polyphony) noexcept
{
    ensureNumPolyphonyGroups(groupIdx);
    polyphonyGroups_[groupIdx].setPolyphonyLimit(polyphony);
}


const PolyphonyGroup* VoiceManager::getPolyphonyGroupView(int idx) noexcept
{
    if (!polyphonyGroups_.contains(idx))
        return {};

    return &polyphonyGroups_[idx];
}

std::vector<const Voice*> VoiceManager::getActiveVoices() const noexcept
{
    std::vector<const Voice*> returned;
    std::copy(activeVoices_.begin(), activeVoices_.end(), std::back_inserter(returned));
    return returned;
}


void VoiceManager::clear()
{
    for (auto& pg : polyphonyGroups_)
        pg.second.removeAllVoices();
    list_.clear();
    activeVoices_.clear();
}

void VoiceManager::setStealingAlgorithm(StealingAlgorithm algorithm)
{
    switch(algorithm){
    case StealingAlgorithm::First:
        for (auto& voice : list_)
            voice.disablePowerFollower();

        stealer_ = absl::make_unique<FirstStealer>();
        break;
    case StealingAlgorithm::Oldest:
        for (auto& voice : list_)
            voice.disablePowerFollower();

        stealer_ = absl::make_unique<OldestStealer>();
        break;
    case StealingAlgorithm::EnvelopeAndAge:
        for (auto& voice : list_)
            voice.enablePowerFollower();

        stealer_ = absl::make_unique<EnvelopeAndAgeStealer>();
        break;
    }
}

bool VoiceManager::checkPolyphony(const Region* region, int delay, const TriggerEvent& triggerEvent) noexcept
{
    checkNotePolyphony(region, delay, triggerEvent);
    checkRegionPolyphony(region, delay);
    checkGroupPolyphony(region, delay);
    checkSetPolyphony(region, delay);
    return checkEnginePolyphony(delay, triggerEvent.type == TriggerEventType::NoteOff);
}

Voice* VoiceManager::findFreeVoice() noexcept
{
    Voice* freeVoice = nullptr;
    for (auto& v: list_) {
        if (v.isFree()) {
            freeVoice = &v;
            break;
        }

        if (v.offedOrFree()) {
            if (freeVoice == nullptr || v.getAge() > freeVoice->getAge())
                freeVoice = &v;
        }
    };

    if (freeVoice != nullptr)
        return freeVoice;

    DBG("Engine hard polyphony reached");
    return {};
}

void VoiceManager::requireNumVoices(int numVoices, Resources& resources)
{
    numRequiredVoices_ = numVoices;
    const int numEffectiveVoices = std::min(int(config::maxVoices), numVoices +
            std::max(int(numVoices * config::overflowVoiceMultiplier), config::minOverflowVoices));

    clear();
    list_.reserve(numEffectiveVoices);
    temp_.reserve(numEffectiveVoices);
    activeVoices_.reserve(numEffectiveVoices);

    for (int i = 0; i < numEffectiveVoices; ++i) {
        list_.emplace_back(i, resources);
        Voice& lastVoice = list_.back();
        lastVoice.setStateListener(this);
    }
}

void VoiceManager::checkRegionPolyphony(const Region* region, int delay) noexcept
{
    Voice* candidate = stealer_->checkRegionPolyphony(region, absl::MakeSpan(activeVoices_));
    SisterVoiceRing::offAllSisters(candidate, delay);
}

void VoiceManager::checkNotePolyphony(const Region* region, int delay, const TriggerEvent& triggerEvent) noexcept
{
    if (!region->notePolyphony)
        return;

    unsigned notePolyphonyCounter { 0 };
    temp_.clear();

    for (Voice* voice : activeVoices_) {
        const TriggerEvent& voiceTriggerEvent = voice->getTriggerEvent();
        if (!voice->offedOrFree()
            && voice->getRegion()->group == region->group
            && voiceTriggerEvent.number == triggerEvent.number) {
            notePolyphonyCounter += 1;
            if (region->selfMask == SelfMask::dontMask || voiceTriggerEvent.value <= triggerEvent.value)
                temp_.push_back(voice);
        }
    }

    if (region->selfMask == SelfMask::mask) {
        absl::c_sort(temp_, [](const Voice* lhs, const Voice* rhs) {
            const auto lhsTrigger = lhs->getTriggerEvent();
            const auto rhsTrigger = rhs->getTriggerEvent();
            return lhsTrigger.value < rhsTrigger.value;
        });
    } else if (region->selfMask == SelfMask::dontMask) {
        absl::c_sort(temp_, [](const Voice* lhs, const Voice* rhs) {
            return lhs->getAge() > rhs->getAge();
        });
    } else {
        ASSERTFALSE;
    }

    auto it = temp_.begin();
    unsigned targetPolyphony { *region->notePolyphony - 1 };
    while (notePolyphonyCounter > targetPolyphony && it < temp_.end()) {
        Voice* voice = *it;
        if (!voice->offedOrFree())
            SisterVoiceRing::offAllSisters(voice, delay);

        notePolyphonyCounter--;
        it++;
    }
}

bool VoiceManager::withinValidTimerRange(const Region* region, unsigned timestampSamples, float sampleRate) const noexcept
{
    auto found = polyphonyGroups_.find(static_cast<int>(region->group));
    if (found != polyphonyGroups_.end()) {
        // convert timestamp to seconds
        return region->timerRange.contains((timestampSamples - found->second.getMostRecentStartTimestamp()) / sampleRate);
    }
    return true;
}

void VoiceManager::checkGroupPolyphony(const Region* region, int delay) noexcept
{
    auto& group = polyphonyGroups_[region->group];
    Voice* candidate = stealer_->checkPolyphony(
        absl::MakeSpan(group.getActiveVoices()), group.getPolyphonyLimit());
    SisterVoiceRing::offAllSisters(candidate, delay);
}

void VoiceManager::checkSetPolyphony(const Region* region, int delay) noexcept
{
    auto parent = region->parent;
    while (parent != nullptr) {
        Voice* candidate = stealer_->checkPolyphony(
            absl::MakeSpan(parent->getActiveVoices()), parent->getPolyphonyLimit());
        SisterVoiceRing::offAllSisters(candidate, delay);
        parent = parent->getParent();
    }
}

bool VoiceManager::checkEnginePolyphony(int delay, bool releaseVoice) noexcept
{
    if (releaseVoicesYield_) {
        // StageKeys: at the limit, the least audible voices make room first.
        //
        //   1. Release samples - resonance, key noise, anything a note-off
        //      starts: the most expendable audio in an instrument.
        //   2. Tails - notes the player has let go, fading out. Only a new note
        //      takes one: cutting a tail ends a note audibly, which a release
        //      sample is not worth.
        //   3. Held notes, by the configured stealer, as before.
        //
        // So a release sample only ever replaces an older release sample, and is
        // skipped when there is none. Without this, sfizz's oldest-voice stealer
        // takes a held note before a fading tail, and releasing a chord at the
        // cap lets its release samples silence the notes still held. Notes held
        // by the sustain pedal are not in release, so they keep a held key's
        // protection.
        unsigned playing = 0;
        Voice* oldestReleaseSample = nullptr;
        Voice* oldestTail = nullptr;
        for (Voice* voice : activeVoices_) {
            if (voice == nullptr || voice->offedOrFree())
                continue;
            ++playing;
            if (voice->getTriggerEvent().type == TriggerEventType::NoteOff) {
                if (oldestReleaseSample == nullptr || voice->getAge() > oldestReleaseSample->getAge())
                    oldestReleaseSample = voice;
            } else if (voice->releasing()) {
                // releasing(), not released(): a whole chord let go arrives in one
                // block, and its notes' releases are only scheduled until the next
                // render.
                if (oldestTail == nullptr || voice->getAge() > oldestTail->getAge())
                    oldestTail = voice;
            }
        }
        if (playing < static_cast<unsigned>(numRequiredVoices_))
            return true;
        if (oldestReleaseSample != nullptr) {
            SisterVoiceRing::offAllSisters(oldestReleaseSample, delay, true);
            return true;
        }
        if (releaseVoice)
            return false;  // never a tail or a held note for a release sample
        if (oldestTail != nullptr) {
            SisterVoiceRing::offAllSisters(oldestTail, delay, true);
            return true;
        }
    }
    Voice* candidate = stealer_->checkPolyphony(
        absl::MakeSpan(activeVoices_), numRequiredVoices_);
    SisterVoiceRing::offAllSisters(candidate, delay, true);
    return true;
}

} // namespace sfz
