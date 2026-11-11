#pragma once

// Данные эксперимента 003 (SYS-235, tools/morphology_benchmark и тесты):
// замороженный протокол data/experiment-003-protocol.json, паспорт
// морфологии data/morphology-003-sources.json, детерминированный контроль
// .aff без строк PFX/SFX и проверка путей внутри <корень>/build.
//
// Разбор протокола, паспорта и .aff — чистые функции над байтами. Только
// resolveProjectFile обращается к файловой системе (QFileInfo, без чтения
// содержимого). Сети, процессов и логирования нет. Сообщения об ошибках не
// содержат путей, слов и содержимого файлов.

#include "benchmarkprotocol.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <array>
#include <vector>

namespace rasklad::morphology {

// ---- Замороженный протокол 003 ----

// Выборка: те же seed/dedup/ранг, что в 002. Берутся первые 10000 по рангу;
// [0, 5000) — старый диагностический блок (равен выборке 002), [5000, 10000)
// — новый блок оценки.
inline constexpr int kHoldoutOffset = 5000;
inline constexpr int kHoldoutCount = 5000;
inline constexpr int kSelectionTake = kHoldoutOffset + kHoldoutCount;
inline constexpr char kModelSha256[] =
    "e92e549e5f0853e4cd78d6d997653098b78550eb9911de11421434e26e3985d8";
// selectedIdsSha256 опыта 002 (docs/results/experiment-002.json), индекс —
// DetectorLanguage. Старый блок обязан совпасть с ними.
inline constexpr std::array<const char *, 2> kOldDiagnosticIdsSha256{{
    "fad2594aa59ce795fde968758779b1e293c5b4a78d472b4231d870afc5ca8ab0",
    "9d99383b950669ae6b411a7bb26814b969b5ba2bcad11f15b505ccf7e841e7de",
}};
// Число случаев испытания опыта 002: 123 fixtures + 2*256 шума + 2*5 ключей.
inline constexpr int kExpectedChallengeCases = 645;

[[nodiscard]] QJsonObject expectedProtocol003();

// Глубокое сравнение всего файла с expectedProtocol003(): те же поля, типы,
// значения и порядок массивов; лишнее поле — отказ. Размер <= 64 KiB.
[[nodiscard]] benchmark::ProtocolResult validateProtocol003(const QByteArray &bytes);

// ---- Паспорт морфологии ----

inline constexpr qint64 kMaxMorphManifestBytes = 64LL * 1024;
inline constexpr qint64 kMaxAffBytes = 1024LL * 1024;
inline constexpr qint64 kMaxMorphDicBytes = 64LL * 1024 * 1024;
inline constexpr qint64 kMaxLibraryBytes = 4LL * 1024 * 1024;
inline constexpr int kMaxPathChars = 200;
inline constexpr int kMaxAdditionalNotices = 4;

inline constexpr char kMorphRequiredStatus[] = "approved-local-morphology";
inline constexpr char kMorphRequiredScope[] = "local-experiment-only";
inline constexpr char kRequiredHunspellVersion[] = "1.7.5";
inline constexpr char kLibraryPrefix[] = "/usr/lib/";
inline constexpr char kDicPrefix[] = "build/experiment-data/";
inline constexpr char kAffPrefix[] = "build/experiment-003-data/";
inline constexpr char kControlAffPrefix[] = "build/experiment-003-data/control/";

enum class MorphManifestError {
    None,
    TooLarge,
    InvalidJson,
    SchemaVersion,
    StatusNotApproved,
    ScopeMismatch,
    ReleaseApproved,
    BadHunspell,
    BadLibraryPath,
    // Нужны ровно en и ru, без повторов.
    BadLanguages,
    BadLanguageField,
    BadPath,
    BadNoticePath,
};

[[nodiscard]] QString morphManifestErrorText(MorphManifestError error);

struct FileRef {
    QString path;
    QString sha256;
    qint64 bytes = 0;
};

struct NoticeRef {
    QString notice;
    QString sha256;
};

struct MorphLanguage {
    FileRef dic;
    FileRef aff;
    FileRef controlAff;
    NoticeRef notice;
};

struct MorphManifest {
    QString manifestSha256;
    QString hunspellVersion;
    FileRef library;
    NoticeRef libraryNotice;
    // Необязательный массив hunspell.additional_notices: {notice, notice_sha256}.
    std::vector<NoticeRef> additionalLibraryNotices;
    // Индекс — DetectorLanguage.
    std::array<MorphLanguage, 2> languages;
};

struct MorphManifestResult {
    MorphManifestError error = MorphManifestError::None;
    MorphManifest manifest;
};

// Проверки: размер <= 64 KiB; schema_version == 1; status/scope равны
// обязательным; release_approved — логическое false. hunspell.version ==
// "1.7.5"; library_path — абсолютный путь под /usr/lib/ (isSafeLibraryPath);
// library_sha256/notice_sha256 — 64 строчных hex; library_bytes в
// [1, 4 MiB]; notice под data/notices/. languages — ровно en и ru: dic_path
// под build/experiment-data/, aff_path под build/experiment-003-data/ (не
// в control/), control_aff_path под build/experiment-003-data/control/;
// размеры .dic в [1, 64 MiB], .aff в [1, 1 MiB]; SHA-256 исходного и
// контрольного .aff различны. Описательные поля сверх обязательных
// допускаются и не используются.
[[nodiscard]] MorphManifestResult parseMorphManifest(const QByteArray &bytes);

// Относительный путь проекта под prefix: [A-Za-z0-9._-/], без пустых
// сегментов, '.', '..', ведущего '/', не длиннее kMaxPathChars.
[[nodiscard]] bool isSafeProjectPath(const QString &relative, const char *prefix);

// Абсолютный путь под /usr/lib/: [A-Za-z0-9._+-/], без пустых сегментов,
// '.', '..', не длиннее kMaxPathChars.
[[nodiscard]] bool isSafeLibraryPath(const QString &absolute);

// Файл <canonical root>/<relative>: <root>/build — настоящий каталог, путь
// после разрешения symlink точно равен склеенному (ни один компонент не
// symlink), лежит строго внутри build/ и является обычным файлом.
// Возвращает canonical-путь или пустую строку.
[[nodiscard]] QString resolveProjectFile(const QString &projectRoot, const QString &relative,
                                         QString &error);

// ---- Контрольный .aff (noPfxSfx) ----

enum class AffControlError {
    None,
    TooLarge,
    Empty,
    NulByte,
    // Первое поле строки начинается с PFX/SFX, но не равно им точно.
    NonExactAffixKeyword,
    // BOM в начале файла перед строкой PFX/SFX: удаление сменило бы BOM.
    BomBeforeAffixRow,
    // Нет ни одной строки PFX/SFX: контроль совпал бы с исходником.
    NoAffixRows,
};

[[nodiscard]] QString affControlErrorText(AffControlError error);

struct AffControlResult {
    AffControlError error = AffControlError::None;
    QByteArray bytes;
    qint64 rows = 0;
    qint64 removedPfxRows = 0;
    qint64 removedSfxRows = 0;
    qint64 keptRows = 0;
};

// Строки — байты до '\n' включительно (последняя может быть без '\n').
// Для анализа отбрасываются завершающий '\r' и ведущие пробелы/табуляции;
// первое поле — байты до следующего пробела/табуляции. Строки с первым
// полем, точно равным "PFX" или "SFX", удаляются целиком; все остальные
// копируются байт в байт (флаги, регистр, COMPOUND*, NEEDAFFIX, KEEPCASE,
// комментарии, пустые строки). Поле вида "PFXA"/"SFX1" — отказ, а не
// догадка: Hunspell сравнивает только первые три байта.
[[nodiscard]] AffControlResult removePfxSfxRows(const QByteArray &source);

// Весь буфер — корректный UTF-8 (BOM допускается).
[[nodiscard]] bool isStrictUtf8(const QByteArray &bytes);

} // namespace rasklad::morphology
