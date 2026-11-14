// SPDX-License-Identifier: MIT
#include "fcitxlabprofile.h"

#include "decisionrouter.h"
#include "localprofile.h"
#include "profileverify.h"

#include <QByteArray>
#include <QString>
#include <QStringView>

namespace rasklad::fcitxlab {

namespace {
constexpr std::size_t kMinWord = 4;
constexpr std::size_t kMaxWord = 32;
constexpr qsizetype kMaxCandidateBytes = 128;

bool isLowerAsciiWord(std::string_view word) noexcept
{
    if (word.size() < kMinWord || word.size() > kMaxWord)
        return false;
    for (const char c : word) {
        if (c < 'a' || c > 'z')
            return false;
    }
    return true;
}
} // namespace

struct ProfileDecision::Impl {
    std::unique_ptr<profile::LocalEvidenceProvider> provider;
};

ProfileDecision::ProfileDecision() noexcept
{
    try {
        auto impl = std::make_unique<Impl>();
        profile::ProfileError error = profile::ProfileError::None;
        impl->provider = profile::LocalEvidenceProvider::load(QStringLiteral("/lab/profile"),
                                                               profile::productionPins(), error);
        if (impl->provider && error == profile::ProfileError::None)
            m_impl = std::move(impl);
    } catch (...) {
        m_impl.reset();
    }
}

ProfileDecision::~ProfileDecision() = default;

bool ProfileDecision::ready() const noexcept
{
    return m_impl && m_impl->provider;
}

std::optional<std::string> ProfileDecision::correct(std::string_view word) noexcept
{
    if (!ready() || !isLowerAsciiWord(word))
        return std::nullopt;
    try {
        const QString token = QString::fromLatin1(word.data(), static_cast<qsizetype>(word.size()));
        // Prose is an explicit contract of the isolated fixture, never inferred
        // for arbitrary application fields from program() or a window title.
        const routing::RouteRequest request{routing::FieldContext::Prose, routing::InputLayout::KnownUs,
                                            QStringView(token)};
        const routing::RouteDecision decision =
            routing::route(request, routing::RouteProfile::SafeConfirmed, m_impl->provider.get());
        if (decision.action != routing::RouteAction::Correct || decision.candidate.isEmpty())
            return std::nullopt;
        const QByteArray utf8 = decision.candidate.toUtf8();
        if (utf8.isEmpty() || utf8.size() > kMaxCandidateBytes)
            return std::nullopt;
        return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
    } catch (...) {
        m_impl->provider.reset();
        return std::nullopt;
    }
}

} // namespace rasklad::fcitxlab
