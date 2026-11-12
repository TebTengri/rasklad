#pragma once

// Verification gates of the optional local experiment profile (SYS-239).
//
// The profile is the exact research provider of ADR-001: full and control
// (no PFX/SFX) Hunspell pairs, the legacy stem-index pair and the pinned
// n-gram model. It is enabled only by the developer flag
// --local-profile <project-root>; it is never downloaded, generated or
// enabled automatically.
//
// Gates mirror the research loader (tools/domain_decisions.cpp), stricter
// where noted:
//   * every file is <root>/<relative> with a safe relative path under data/
//     or build/; no path component is a symbolic link (stricter: research
//     allowed symlinked notices inside the repository); regular file;
//   * public manifests (morphology 003, training 001, protocol 002) have the
//     pinned SHA-256 and their fields equal the compiled pins;
//   * all notices (library, additional, per-language) have pinned SHA-256;
//   * .dic/.aff/control .aff: exact size, SHA-256 and strict UTF-8; the
//     control .aff equals the deterministic PFX/SFX-row removal of the .aff;
//   * artifact metadata lists the pinned model/stem files, protocol and
//     training sources and forbids distribution; notice copies match;
//     MODIFICATIONS.txt is strict UTF-8 and states release_approved=false;
//   * model and stem pair: exact size, SHA-256 and serialization round trip.
// Missing or mismatched data makes the whole profile unavailable; there is no
// weaker fallback profile.
//
// Error values carry no paths, words or file contents. Only QFileInfo/QFile
// reads of the listed files are performed; nothing is written.

#include "frontcodedlexicon.h"
#include "ngramdetector.h"

#include <QByteArray>
#include <QString>
#include <QStringView>

#include <array>
#include <vector>

namespace rasklad::profile {

enum class ProfileError {
    None,
    // Built without Hunspell: only manual conversion is available.
    NotBuilt,
    BadRoot,
    // Missing, not a regular file, unsafe path or reached through a symlink.
    MissingFile,
    SizeOrHash,
    ManifestMismatch,
    NoticeMismatch,
    NotUtf8,
    ControlMismatch,
    MetadataMismatch,
    ModificationsMismatch,
    FormatMismatch,
    LibraryMismatch,
    EngineFailed,
    ChangedDuringLoad,
    WrongThread,
};

// Generic category text without paths or payload (for tests and stderr-free diagnostics).
[[nodiscard]] const char *profileErrorName(ProfileError error);

inline constexpr qint64 kMaxManifestBytes = 64LL * 1024;
inline constexpr qint64 kMaxNoticeBytes = 1024LL * 1024;
inline constexpr qint64 kMaxAffBytes = 1024LL * 1024;
inline constexpr qint64 kMaxDicBytes = 64LL * 1024 * 1024;
inline constexpr qint64 kMaxModelBytes = 1024LL * 1024;
inline constexpr qint64 kMaxStemBytes = 16LL * 1024 * 1024;
inline constexpr qint64 kMaxMetadataBytes = 64LL * 1024;
inline constexpr qint64 kMaxModificationsBytes = 64LL * 1024;
inline constexpr qint64 kMaxLibraryBytes = 4LL * 1024 * 1024;
inline constexpr qint64 kMaxPinnedArtifactBytes = 64LL * 1024 * 1024;
inline constexpr int kMaxPathChars = 200;

struct FilePin {
    QString path;
    QString sha256;
    qint64 bytes = 0;
};

struct NoticePin {
    QString path;
    QString sha256;
};

struct LanguagePins {
    FilePin dic;
    FilePin aff;
    FilePin controlAff;
    NoticePin notice;
    // Copy of the notice next to the derived artifacts (same SHA-256).
    QString artifactNoticePath;
};

struct ProfilePins {
    FilePin morphManifest;
    FilePin trainingManifest;
    FilePin protocol002;
    QString hunspellVersion;
    FilePin library;
    NoticePin libraryNotice;
    std::vector<NoticePin> additionalLibraryNotices;
    QString dictionaryCommit;
    // Index — DetectorLanguage (0 = en, 1 = ru).
    std::array<LanguagePins, 2> languages;
    QString artifactDir;
    FilePin model;
    FilePin stem;
    QString artifactMetadataPath;
    QString modificationsPath;
};

// The pins of data/morphology-003-sources.json, data/sources.json,
// data/experiment-002-protocol.json and the experiment-002 artifacts.
[[nodiscard]] const ProfilePins &productionPins();

// ---- Pure helpers (exposed for tests) ----

[[nodiscard]] QString sha256Hex(const QByteArray &data);
[[nodiscard]] bool isLowerHex64(const QString &value);
[[nodiscard]] bool isStrictUtf8(const QByteArray &bytes);

// [A-Za-z0-9._-] segments separated by '/', starting with prefix ("data/" or
// "build/"), no empty, "." or ".." segments, not longer than kMaxPathChars.
[[nodiscard]] bool isSafeRelativePath(const QString &relative, const QString &prefix);

// <canonical root>/<relative>: the top directory (data or build) is a real
// directory, the file exists, is regular, and its canonical path equals the
// joined path (no component is a symlink) strictly inside the top directory.
// Returns the path or an empty string.
[[nodiscard]] QString resolveContainedFile(const QString &canonicalRoot, const QString &relative);

// Reads at most limit bytes; size must not change while reading.
[[nodiscard]] bool readBounded(const QString &path, qint64 limit, QByteArray &out);

// Port of tools/morphologydata removePfxSfxRows (parity-tested): drops rows
// whose first field is exactly PFX or SFX; rejects PFX*/SFX* look-alikes,
// a BOM before an affix row, NUL bytes, empty input and inputs without affix
// rows. Returns false on any rejection.
[[nodiscard]] bool removePfxSfxRows(const QByteArray &source, QByteArray &out);

// Port of tools/hunspellengine isQueryableToken: 1..32 UTF-16, no Cc, no lone surrogates.
[[nodiscard]] bool isQueryableToken(QStringView token);

// Port of domain::verifyArtifactMetadata with the given pins.
[[nodiscard]] bool verifyArtifactMetadata(const QByteArray &bytes, const ProfilePins &pins);

// Manifests: pinned SHA-256 first, then fields compared with the pins.
[[nodiscard]] bool verifyMorphManifest(const QByteArray &bytes, const ProfilePins &pins);
[[nodiscard]] bool verifyTrainingManifest(const QByteArray &bytes, const ProfilePins &pins);
[[nodiscard]] bool verifyProtocol002(const QByteArray &bytes, const ProfilePins &pins);

// ---- Whole-profile file verification (no Hunspell) ----

struct LanguageFiles {
    QString dic;
    QString aff;
    QString controlAff;
};

struct VerifiedProfile {
    std::array<LanguageFiles, 2> files;
    NgramModel model;
    std::array<benchmark::FrontCodedLexicon, 2> stem;
};

[[nodiscard]] ProfileError verifyProfileFiles(const QString &projectRoot, const ProfilePins &pins,
                                              VerifiedProfile &out);

// After Hunspell read the files by path: same size, SHA-256 and canonical path.
[[nodiscard]] ProfileError recheckMorphologyFiles(const QString &projectRoot, const ProfilePins &pins);

} // namespace rasklad::profile
