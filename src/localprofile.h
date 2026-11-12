#pragma once

// Exact verified local evidence provider (SYS-239, ADR-001 profile).
//
// Holds full and control Hunspell pairs, the legacy stem-index pair and the
// pinned model. All of them are immutable after load. Hunspell is not
// thread-safe, so the provider has one processing owner: it must be created,
// queried and destroyed on the same thread (the analysis worker). A query
// from any other thread fails closed (nullopt).
//
// Without Hunspell (no RASKLAD_WITH_HUNSPELL) load() always fails with
// ProfileError::NotBuilt and the app stays manual-only.

#include "decisionrouter.h"
#include "profileverify.h"

#include <QString>

#include <memory>

class QThread;

namespace rasklad::profile {

[[nodiscard]] bool builtWithHunspell();

class SpellEngine;

class LocalEvidenceProvider final : public routing::EvidenceSource
{
public:
    // Verifies everything (profileverify gates, loaded library, engines,
    // recheck after Hunspell read the files). On failure returns nullptr and
    // a generic error category.
    [[nodiscard]] static std::unique_ptr<LocalEvidenceProvider> load(const QString &projectRoot,
                                                                     const ProfilePins &pins, ProfileError &error);

    ~LocalEvidenceProvider() override;
    LocalEvidenceProvider(const LocalEvidenceProvider &) = delete;
    LocalEvidenceProvider &operator=(const LocalEvidenceProvider &) = delete;

    // Exactly the experiment-003/006 features for an eligible token: stem pair
    // on canonical keys, full/control Hunspell on the exact typed text and its
    // conversion, frozen twoScore on the model score. Not stored, not logged.
    [[nodiscard]] std::optional<routing::TokenEvidence> evidence(const routing::EvidenceQuery &query) const override;

private:
    LocalEvidenceProvider();

    QThread *m_owner = nullptr;
    NgramModel m_model;
    std::array<benchmark::FrontCodedLexicon, 2> m_stem;
    std::array<std::unique_ptr<SpellEngine>, 2> m_full;
    std::array<std::unique_ptr<SpellEngine>, 2> m_control;
};

} // namespace rasklad::profile
