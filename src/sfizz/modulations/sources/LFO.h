// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#pragma once
#include "../ModGenerator.h"
#include "../../VoiceManager.h"
namespace sfz {
class Synth;

class LFOSource : public ModGenerator {
public:
    explicit LFOSource(VoiceManager &manager);
    void init(const ModKey& sourceKey, NumericId<Voice> voiceId, unsigned delay) override;
    void generate(const ModKey& sourceKey, NumericId<Voice> voiceId, absl::Span<float> buffer) override;
    // Moves the LFO on without computing its wave where it can (LFO::advance),
    // so one nobody reads this cycle - every depth it has at zero - costs
    // nearly nothing and still keeps its delay and phase (StageKeys #163).
    void generateDiscarded(const ModKey& sourceKey, NumericId<Voice> voiceId, absl::Span<float> buffer) override;

private:
    LFO* find(const ModKey& sourceKey, NumericId<Voice> voiceId) const;

    VoiceManager& voiceManager_;
};

} // namespace sfz
