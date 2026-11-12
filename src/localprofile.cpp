#include "localprofile.h"

#include "layoutconverter.h"

#ifdef RASKLAD_WITH_HUNSPELL
#include "spellengine.h"
#endif

#include <QThread>

namespace rasklad::profile {

#ifdef RASKLAD_WITH_HUNSPELL

bool builtWithHunspell()
{
    return true;
}

LocalEvidenceProvider::LocalEvidenceProvider()
    : m_owner(QThread::currentThread())
{
}

LocalEvidenceProvider::~LocalEvidenceProvider() = default;

std::unique_ptr<LocalEvidenceProvider> LocalEvidenceProvider::load(const QString &projectRoot,
                                                                   const ProfilePins &pins, ProfileError &error)
{
    error = ProfileError::None;
    // Thresholds of the protocol file are compared with these exact values.
    const TwoScoreThresholds frozen = routing::frozenTwoScore();
    if (!frozen.enabled || frozen.minAlternative != -13.0 || frozen.maxObserved != -9.0 || frozen.gap != 2.5) {
        error = ProfileError::ManifestMismatch;
        return nullptr;
    }
    if (!verifyLoadedHunspell(pins.library.path, pins.library.sha256, pins.library.bytes, pins.hunspellVersion)) {
        error = ProfileError::LibraryMismatch;
        return nullptr;
    }
    VerifiedProfile verified;
    error = verifyProfileFiles(projectRoot, pins, verified);
    if (error != ProfileError::None)
        return nullptr;

    std::unique_ptr<LocalEvidenceProvider> provider(new LocalEvidenceProvider());
    provider->m_model = std::move(verified.model);
    provider->m_stem = std::move(verified.stem);
    for (std::size_t li = 0; li < 2; ++li) {
        const LanguageFiles &files = verified.files[li];
        provider->m_full[li] = SpellEngine::create(files.aff, files.dic);
        provider->m_control[li] = SpellEngine::create(files.controlAff, files.dic);
        if (!provider->m_full[li] || !provider->m_control[li]) {
            error = ProfileError::EngineFailed;
            return nullptr;
        }
    }
    // Hunspell read the files by path: they must still be the verified bytes.
    error = recheckMorphologyFiles(projectRoot, pins);
    if (error != ProfileError::None)
        return nullptr;
    if (!provider->m_model.isValid()) {
        error = ProfileError::FormatMismatch;
        return nullptr;
    }
    return provider;
}

std::optional<routing::TokenEvidence> LocalEvidenceProvider::evidence(const routing::EvidenceQuery &query) const
{
    if (QThread::currentThread() != m_owner)
        return std::nullopt;
    if (!isValidSourceLayout(query.layout) || !benchmark::isValidCanonicalKey(query.keys)
        || query.keys.size() < routing::kMinRouteKeys || query.keys.size() > routing::kMaxRouteKeys)
        return std::nullopt;
    // Bound even direct callers before allocating strings or querying engines.
    const auto canonical = canonicalizeToken(query.token, query.layout);
    if (canonical.status != CanonicalStatus::Ok || canonical.keys != query.keys)
        return std::nullopt;
    for (std::size_t li = 0; li < 2; ++li) {
        if (!m_full[li] || !m_control[li])
            return std::nullopt;
    }
    const std::size_t observed = std::size_t(observedLanguage(query.layout));
    const std::size_t alternative = std::size_t(alternativeLanguage(query.layout));
    const QString observedText = query.token.toString();
    const QString alternativeText = convertLayout(observedText, routing::correctionDirection(query.layout));

    routing::TokenEvidence e;
    e.stem.observed = m_stem[observed].contains(query.keys);
    e.stem.alternative = m_stem[alternative].contains(query.keys);
    e.full.observed = m_full[observed]->accepts(observedText);
    e.full.alternative = m_full[alternative]->accepts(alternativeText);
    e.control.observed = m_control[observed]->accepts(observedText);
    e.control.alternative = m_control[alternative]->accepts(alternativeText);
    e.model = decideTwoScore(m_model.score(query.keys), query.layout, routing::frozenTwoScore()).action;
    return e;
}

#else // !RASKLAD_WITH_HUNSPELL

class SpellEngine
{
};

bool builtWithHunspell()
{
    return false;
}

LocalEvidenceProvider::LocalEvidenceProvider()
    : m_owner(QThread::currentThread())
{
}

LocalEvidenceProvider::~LocalEvidenceProvider() = default;

std::unique_ptr<LocalEvidenceProvider> LocalEvidenceProvider::load(const QString &, const ProfilePins &,
                                                                   ProfileError &error)
{
    error = ProfileError::NotBuilt;
    return nullptr;
}

std::optional<routing::TokenEvidence> LocalEvidenceProvider::evidence(const routing::EvidenceQuery &) const
{
    return std::nullopt;
}

#endif

} // namespace rasklad::profile
