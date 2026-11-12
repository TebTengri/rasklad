#include "jsontranslator.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <utility>

namespace rasklad::app {

JsonTranslator::JsonTranslator(QObject *parent)
    : QTranslator(parent)
{
}

bool JsonTranslator::loadJson(const QByteArray &bytes, const QString &expectedLanguage)
{
    m_messages.clear();
    m_language.clear();
    if (bytes.isEmpty() || qint64(bytes.size()) > kMaxTranslationBytes)
        return false;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;
    const QJsonObject root = document.object();
    const QJsonValue schema = root.value(QStringLiteral("schema"));
    const QJsonValue language = root.value(QStringLiteral("language"));
    const QJsonValue messages = root.value(QStringLiteral("messages"));
    if (!schema.isDouble() || schema.toDouble() != 1.0 || !language.isString()
        || language.toString() != expectedLanguage || !messages.isObject())
        return false;
    QHash<QString, QString> table;
    const QJsonObject object = messages.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (it.key().isEmpty() || !it.value().isString() || it.value().toString().isEmpty())
            return false;
        table.insert(it.key(), it.value().toString());
    }
    m_messages = std::move(table);
    m_language = expectedLanguage;
    return true;
}

QString JsonTranslator::translate(const char *context, const char *sourceText, const char *disambiguation,
                                  int n) const
{
    Q_UNUSED(context);
    Q_UNUSED(disambiguation);
    Q_UNUSED(n);
    if (!sourceText)
        return {};
    // A null string tells Qt to fall back to the source text.
    return m_messages.value(QString::fromUtf8(sourceText));
}

bool JsonTranslator::isEmpty() const
{
    return m_messages.isEmpty();
}

QString JsonTranslator::language() const
{
    return m_language;
}

} // namespace rasklad::app
