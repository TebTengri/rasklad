#pragma once

// Ограниченный файловый ввод-вывод стенда SYS-233 (tools/detector_benchmark).
//
// Чтение: только обычные файлы, явный предел размера, отказ при ошибке
// чтения или изменении размера. Запись: только через QSaveFile в
// существующий каталог строго внутри <корень проекта>/build (canonical-пути,
// без выхода через symlink), без перезаписи symlink и не-обычных файлов.
// Сеть и процессы не используются. Сообщения об ошибках не содержат путей
// и содержимого файлов.

#include <QByteArray>
#include <QString>

namespace rasklad::benchmark {

[[nodiscard]] bool readBoundedFile(const QString &path, qint64 limit, QByteArray &out, QString &error);

// Уведомление ищется под data/notices/ корня репозитория (родителя
// каталога манифеста); canonical-путь должен остаться внутри корня; файл
// не больше 1 MiB, SHA-256 равен expectedSha256.
[[nodiscard]] bool verifyNoticeFile(const QString &manifestPath, const QString &relativeNotice,
                                    const QString &expectedSha256, QString &error);

// Canonical-путь child лежит строго внутри canonical-пути parent.
[[nodiscard]] bool isStrictlyInside(const QString &canonicalChild, const QString &canonicalParent);

// Проверяет, что <projectRoot>/build — настоящий каталог (не symlink), и
// requested — существующий каталог строго внутри него после разрешения
// symlink. Возвращает canonical-путь или пустую строку.
[[nodiscard]] QString resolveArtifactDir(const QString &projectRoot, const QString &requested,
                                         QString &error);

// Атомарная запись fileName (только [A-Za-z0-9._-]) в canonical-каталог
// artifactDir. Существующая цель должна быть обычным файлом, не symlink.
[[nodiscard]] bool writeArtifactFile(const QString &artifactDir, const QString &fileName,
                                     const QByteArray &bytes, QString &error);

} // namespace rasklad::benchmark
