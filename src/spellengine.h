#pragma once

// Minimal wrapper over the installed system Hunspell 1.7.5 C API for the
// optional local profile (SYS-239). Compiled only with RASKLAD_WITH_HUNSPELL.
//
// Same API surface and semantics as tools/hunspellengine (experiment 003):
// Hunspell_create with explicit, pre-verified .aff/.dic paths, required
// UTF-8 dictionary encoding, Hunspell_spell on the exact UTF-8 token.
// Suggestions, analysis, personal dictionaries and default dictionary search
// are not used. Not thread-safe: an engine is created, queried and destroyed
// on one thread (the analysis worker).

#include <QString>
#include <QStringView>

#include <memory>

struct Hunhandle;

namespace rasklad::profile {

[[nodiscard]] QString compiledHunspellVersion();
[[nodiscard]] QString runtimeHunspellVersion();
// Canonical path of the object that defines Hunspell_create (dladdr), or empty.
[[nodiscard]] QString loadedHunspellLibraryPath();

// Loaded library: canonical path under /usr/lib/ equal to expectedPath, size
// and SHA-256 equal to the pins, header and runtime versions equal
// expectedVersion. A reproducibility check, not a sandbox: the dynamic loader
// has already run.
[[nodiscard]] bool verifyLoadedHunspell(const QString &expectedPath, const QString &expectedSha256,
                                        qint64 expectedBytes, const QString &expectedVersion);

class SpellEngine final
{
public:
    [[nodiscard]] static std::unique_ptr<SpellEngine> create(const QString &affPath, const QString &dicPath);
    ~SpellEngine();
    SpellEngine(const SpellEngine &) = delete;
    SpellEngine &operator=(const SpellEngine &) = delete;

    // Tokens failing isQueryableToken are not passed to the library: false.
    [[nodiscard]] bool accepts(QStringView token) const;

private:
    explicit SpellEngine(Hunhandle *handle);
    Hunhandle *m_handle = nullptr;
};

} // namespace rasklad::profile
