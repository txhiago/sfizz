// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#pragma once

#include "absl/container/flat_hash_map.h"
#include "Config.h"
#include "PolyphonyGroup.h"
#include "Region.h"
#include "Resources.h"
#include "Voice.h"
#include "VoiceStealing.h"
#include <atomic>
#include <cstdint>
#include <vector>

namespace sfz {

struct VoiceManager final : public Voice::StateListener
{
    /**
     * @brief The voice callback which is called during a change of state.
     */
    void onVoiceStateChanging(NumericId<Voice> id, Voice::State state) final;

    /**
     * @brief Find the voice which is associated with the given identifier.
     *
     * @param id
     * @return const Voice*
     */
    const Voice* getVoiceById(NumericId<Voice> id) const noexcept;

    /**
     * @brief Find the voice which is associated with the given identifier.
     *
     * @param id
     * @return Voice*
     */
    Voice* getVoiceById(NumericId<Voice> id) noexcept;

    /**
     * @brief Reset all voices and clear the polyphony groups
     */
    void reset();

    /**
     * @brief Check if a compatible attack voice is playing for the release region.
     *
     * @param releaseRegion
     * @return true
     * @return false
     */
    bool playingAttackVoice(const Region* releaseRegion) noexcept;

    /**
     * @brief Ensures that the polyphony groups are at least this size.
     * Call this each time a new `group=N` is given in an sfz file.
     *
     * @param groupIdx
     */
    void ensureNumPolyphonyGroups(int groupIdx) noexcept;

    /**
     * @brief Set the polyphony for a given group
     * If the number of polyphony groups is too small, it will
     * be increased.
     *
     * @param groupIdx
     * @param polyphony
     */
    void setGroupPolyphony(int groupIdx, unsigned polyphony) noexcept;

    /**
     * @brief Get a view into a given polyphony group
     *
     * @param idx
     * @return const PolyphonyGroup*
     */
    const PolyphonyGroup* getPolyphonyGroupView(int idx) noexcept;

    /**
     * @brief Get the actives voices as a view
     *
     * @return std::vector<const Voice*>
     */
    std::vector<const Voice*> getActiveVoices() const noexcept;

    /**
     * @brief Clear all voices and polyphony groups.
     * Also resets the stealing algorithm to default.
     */
    void clear();

    /**
     * @brief Set the stealing algorithm
     *
     * @param algorithm
     */
    void setStealingAlgorithm(StealingAlgorithm algorithm);

    /**
     * @brief Off voices as necessary depending on the trigger event and started region
     *
     * @param region
     * @param delay
     * @param triggerEvent
     */
    bool checkPolyphony(const Region* region, int delay, const TriggerEvent& triggerEvent) noexcept;

    /**
     * @brief Make voices started by a note-off - release samples - yield at the
     * engine's polyphony limit. See sfz::Sfizz::setReleaseVoicesYield.
     */
    void setReleaseVoicesYield(bool yield) noexcept { releaseVoicesYield_ = yield; }
    bool getReleaseVoicesYield() const noexcept { return releaseVoicesYield_; }

    /**
     * @brief Budget the release-triggered voices (note-off, `trigger=release`)
     * that start in one block. See sfz::Sfizz::setReleaseStartBudget.
     */
    void setReleaseStartBudget(unsigned perBlock, unsigned maxBlocksLate) noexcept;
    bool releaseStartBudgeted() const noexcept { return releaseStartsPerBlock_ != 0; }
    /**
     * @brief The earliest block, counted from the one being dispatched, that
     * still has a release start free, or -1 when every block within the limit
     * is full (the release voice is skipped). Claims nothing.
     */
    int findReleaseSlot() const noexcept;
    /** @brief Claim a start in that block, once a voice has taken it. */
    void claimReleaseSlot(int block) noexcept;
    void skipReleaseVoice() noexcept { skippedReleases_.fetch_add(1, std::memory_order_relaxed); }
    /**
     * @brief The block being dispatched has been rendered, `frames` long: the
     * schedule moves on, and the next blocks are taken to be as long. The step
     * is the callback's, not the buffer capacity the host prepared with.
     */
    void advanceReleaseSchedule(int frames) noexcept;
    /** @brief Frames between one schedule slot and the next; 0 until a block has rendered. */
    int releaseStepFrames() const noexcept { return releaseStepFrames_; }
    /** @brief Forget the schedule: nothing is waiting to start. */
    void clearReleaseSchedule() noexcept;
    uint64_t getNumSkippedReleaseVoices() const noexcept { return skippedReleases_.load(std::memory_order_relaxed); }

    /**
     * @brief Voices cut to make room at the engine's polyphony limit since this
     * manager was built. See sfz::Sfizz::getNumStolenVoices. Any thread.
     */
    uint64_t getNumStolenVoices() const noexcept { return stolenVoices_.load(std::memory_order_relaxed); }

    /**
     * @brief Get the number of active voices
     *
     * @return size_t
     */
    size_t getNumActiveVoices() const { return activeVoices_.size(); }

    /**
     * @brief Get the number of polyphony groups
     *
     * @return size_t
     */
    size_t getNumPolyphonyGroups() const noexcept { return polyphonyGroups_.size(); }

    /**
     * @brief Find a voice that is not currently playing
     *
     * @return Voice*
     */
    Voice* findFreeVoice() noexcept;
    /**
     * @brief Require a number of voices from this manager.
     * In practice, the manager will handle slightly more, in order to
     * allow voices to die off upon reaching higher polyphony count.
     *
     * @param numVoices
     * @param resources
     */
    void requireNumVoices(int numVoices, Resources& resources);

    /**
     * @brief Is this timestamp within the lo/hitimer range for this region based on current group activity
     *
     * @param region
     * @param timestampSamples in samples
     * @param sampleRate
     * @return bool
     */
    bool withinValidTimerRange(const Region* region, unsigned timestampSamples, float sampleRate) const noexcept;

private:
    int numRequiredVoices_ { config::numVoices };
    bool releaseVoicesYield_ { false };
    // Written by the thread that renders, read by any: relaxed, a count.
    std::atomic<uint64_t> stolenVoices_ { 0 };
    std::atomic<uint64_t> skippedReleases_ { 0 };
    // Release starts per block, 0 = no budget. releaseSlots_[b] counts the starts
    // already placed b blocks after the one being dispatched.
    static constexpr unsigned maxReleaseBlocksLate = 15;
    unsigned releaseStartsPerBlock_ { 0 };
    unsigned releaseBlocksLate_ { 0 };
    int releaseStepFrames_ { 0 };
    uint16_t releaseSlots_[maxReleaseBlocksLate + 1] {};
    std::vector<Voice> list_;
    std::vector<Voice*> activeVoices_;
    std::vector<Voice*> temp_;
    // These are the `group=` groups where you can off voices
    absl::flat_hash_map<int, PolyphonyGroup> polyphonyGroups_;
    std::unique_ptr<VoiceStealer> stealer_ { absl::make_unique<OldestStealer>() };

    /**
     * @brief Check the region polyphony, releasing voices if necessary
     *
     * @param region
     * @param delay
     */
    void checkRegionPolyphony(const Region* region, int delay) noexcept;

    /**
     * @brief Check the note polyphony, releasing voices if necessary
     *
     * @param region
     * @param delay
     * @param triggerEvent
     */
    void checkNotePolyphony(const Region* region, int delay, const TriggerEvent& triggerEvent) noexcept;

    /**
     * @brief Check the group polyphony, releasing voices if necessary
     *
     * @param region
     * @param delay
     */
    void checkGroupPolyphony(const Region* region, int delay) noexcept;

    /**
     * @brief Check the region set polyphony at all levels, releasing voices if necessary
     *
     * @param region
     * @param delay
     */
    void checkSetPolyphony(const Region* region, int delay) noexcept;

    /**
     * @brief Check the engine polyphony, fast releasing voices if necessary
     *
     * @param delay
     */
    bool checkEnginePolyphony(int delay, bool releaseVoice) noexcept;
    /**
     * @brief Off a voice and its sisters to make room at the engine limit,
     * counting the ones still sounding.
     */
    void stealForEngine(Voice* voice, int delay) noexcept;

public:
    // Vector shortcuts
    typename decltype(list_)::iterator begin() { return list_.begin(); }
    typename decltype(list_)::const_iterator cbegin() const { return list_.cbegin(); }
    typename decltype(list_)::iterator end() { return list_.end(); }
    typename decltype(list_)::const_iterator cend() const { return list_.cend(); }
    typename decltype(list_)::reference operator[] (size_t n) { return list_[n]; }
    typename decltype(list_)::const_reference operator[] (size_t n) const { return list_[n]; }
};

} // namespace sfz
