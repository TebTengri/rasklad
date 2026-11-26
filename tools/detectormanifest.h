#pragma once

// Проверка манифеста источников (data/sources.json) до обучения в
// offline-эксперименте SYS-232.
//
// Функции чистые: разбирают переданные байты и строки путей, не открывают
// файлы, не используют сеть и не логируют. Чтение файлов и проверка
// canonical-путей (symlink) выполняются в CLI. Сообщения об ошибках не
// содержат данных файлов.

#include "ngramdetector.h"

#include <QByteArray>
#include <QString>

#include <array>

namespace rasklad::experiment {

inline constexpr qint64 kMaxManifestBytes = 64LL * 1024;
inline constexpr qint64 kMaxNoticeBytes = 1024LL * 1024;
inline constexpr int kMaxNoticePathChars = 200;

// Обязательные значения: эксперимент разрешён только локально и без выпуска.
inline constexpr char kRequiredStatus[] = "approved-local-experiment";
inline constexpr char kRequiredScope[] = "local-experiment-only";
// Уведомления лежат только здесь (относительно корня репозитория).
inline constexpr char kNoticePrefix[] = "data/notices/";

enum class ManifestError {
    None,
    TooLarge,
    InvalidJson,
    SchemaVersion,
    StatusNotApproved,
    ScopeMismatch,
    ReleaseApproved,
    BadCommit,
    // Нужны ровно два источника: en и ru, без повторов.
    BadSources,
    BadSourceField,
    BadNoticePath,
};

[[nodiscard]] QString manifestErrorText(ManifestError error);

struct ManifestSource {
    QString sha256;
    qint64 bytes = 0;
    QString notice;
    QString noticeSha256;
};

struct ExperimentManifest {
    QString manifestSha256;
    QString status;
    QString scope;
    QString commit;
    // Индекс — DetectorLanguage.
    std::array<ManifestSource, 2> sources;
};

struct ManifestParseResult {
    ManifestError error = ManifestError::None;
    ExperimentManifest manifest;
};

// Проверки: размер <= kMaxManifestBytes, корень — JSON-объект,
// schema_version == 1, status и scope равны обязательным значениям,
// release_approved — логическое false, commit — 40 строчных hex,
// training_sources — ровно en и ru; у каждого sha256 и notice_sha256 —
// 64 строчных hex, bytes — целое в [1, kMaxDicBytes], notice — безопасный
// относительный путь (isSafeNoticePath).
[[nodiscard]] ManifestParseResult parseExperimentManifest(const QByteArray &bytes);

// Относительный путь под kNoticePrefix: только [A-Za-z0-9._-] и '/', без
// пустых сегментов, '.', '..', ведущего '/', '\\' и не длиннее
// kMaxNoticePathChars.
[[nodiscard]] bool isSafeNoticePath(const QString &relative);

// Корень репозитория — родитель каталога манифеста. Возвращает абсолютный
// очищенный путь внутри корня или пустую строку при недопустимом пути.
// Symlink здесь не разрешаются: CLI дополнительно сверяет canonical-путь.
[[nodiscard]] QString manifestRoot(const QString &manifestFilePath);
[[nodiscard]] QString resolveNoticePath(const QString &manifestFilePath, const QString &relative);

// Точный размер и SHA-256 (строчный hex).
[[nodiscard]] bool bytesMatch(const QByteArray &data, qint64 expectedBytes,
                              const QString &expectedSha256);

[[nodiscard]] QString sha256Hex(const QByteArray &data);

} // namespace rasklad::experiment
