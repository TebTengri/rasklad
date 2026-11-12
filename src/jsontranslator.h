#pragma once

// QTranslator backed by a small JSON message table (SYS-239).
//
// Format: {"schema": 1, "language": "ru", "messages": {"English source": "Перевод"}}.
// The source text is the qsTr() literal; the QML context is ignored, so one
// English string has one translation. Bounded (kMaxTranslationBytes), strict:
// any malformed entry rejects the whole table. Only built-in resources are
// loaded; nothing is written.

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QTranslator>

namespace rasklad::app {

inline constexpr qint64 kMaxTranslationBytes = 256LL * 1024;

class JsonTranslator final : public QTranslator
{
public:
    explicit JsonTranslator(QObject *parent = nullptr);

    // Replaces the table; false (and an empty table) on malformed input.
    bool loadJson(const QByteArray &bytes, const QString &expectedLanguage);

    [[nodiscard]] QString translate(const char *context, const char *sourceText, const char *disambiguation,
                                    int n) const override;
    [[nodiscard]] bool isEmpty() const override;
    [[nodiscard]] QString language() const;

    [[nodiscard]] qsizetype size() const { return m_messages.size(); }
    [[nodiscard]] bool contains(const QString &source) const { return m_messages.contains(source); }

private:
    QHash<QString, QString> m_messages;
    QString m_language;
};

} // namespace rasklad::app
