#include "uisetup.h"

#include "clipboardwriter.h"
#include "documentcontroller.h"
#include "themewatcher.h"

#include <QCoreApplication>
#include <QFile>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickTextDocument>
#include <QTextDocument>

namespace rasklad::app {

bool EditorGuard::configure(QObject *wrapper)
{
    auto *document = qobject_cast<QQuickTextDocument *>(wrapper);
    if (!document)
        return false;
    QTextDocument *text = document->textDocument();
    if (!text)
        return false;
    text->setUndoRedoEnabled(false);
    // Report success only if the history is really off.
    return !text->isUndoRedoEnabled();
}

LanguageSwitcher::LanguageSwitcher(QQmlEngine *engine, ThemeWatcher *theme, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_theme(theme)
{
    if (m_theme)
        connect(m_theme, &ThemeWatcher::languageChanged, this, &LanguageSwitcher::apply);
    apply();
}

LanguageSwitcher::~LanguageSwitcher()
{
    if (m_installed)
        QCoreApplication::removeTranslator(&m_ru);
}

void LanguageSwitcher::apply()
{
    const bool wantRussian = m_theme && m_theme->language() == QLatin1String("ru");
    if (wantRussian && !m_loaded) {
        QFile file(QStringLiteral(":/i18n/ru.json"));
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = file.read(kMaxTranslationBytes + 1);
            m_loaded = m_ru.loadJson(bytes, QStringLiteral("ru"));
        }
    }
    bool changed = false;
    if (wantRussian && m_loaded && !m_installed) {
        m_installed = QCoreApplication::installTranslator(&m_ru);
        changed = m_installed;
    } else if (!wantRussian && m_installed) {
        QCoreApplication::removeTranslator(&m_ru);
        m_installed = false;
        changed = true;
    }
    if (changed && m_engine)
        m_engine->retranslate();
}

void prepareQuickEnvironment()
{
    // No compiled-QML cache files under the user's cache directory.
    qputenv("QML_DISABLE_DISK_CACHE", "1");
    // Qt TextEdit automatically exports selections on platforms that support
    // PRIMARY. Disable its Wayland transports before QGuiApplication connects.
    qputenv("QT_WAYLAND_DISABLED_INTERFACES",
            clipboardInterfaceBlacklist(qgetenv("QT_WAYLAND_DISABLED_INTERFACES")));
    QQuickStyle::setStyle(QStringLiteral("Material"));
}

QByteArray clipboardInterfaceBlacklist(const QByteArray &previous)
{
    QList<QByteArray> names;
    for (const auto &name : previous.split(',')) {
        if (!name.trimmed().isEmpty())
            names.append(name.trimmed());
    }
    names.append("zwp_primary_selection_device_manager_v1");
    names.append("zwlr_data_control_manager_v1");
    names.append("ext_data_control_manager_v1");
    QByteArray result;
    for (const auto &name : std::as_const(names)) {
        if (!result.isEmpty())
            result.append(',');
        result.append(name);
    }
    return result;
}

QVariantMap mainWindowProperties(DocumentController *controller, ThemeWatcher *theme, ClipboardWriter *clipboard,
                                 bool profileRequested)
{
    QVariantMap properties;
    properties.insert(QStringLiteral("controller"), QVariant::fromValue(static_cast<QObject *>(controller)));
    properties.insert(QStringLiteral("theme"), QVariant::fromValue(static_cast<QObject *>(theme)));
    properties.insert(QStringLiteral("clipboard"), QVariant::fromValue(static_cast<QObject *>(clipboard)));
    properties.insert(QStringLiteral("profileRequested"), profileRequested);
    properties.insert(QStringLiteral("editorGuard"),
                      QVariant::fromValue(static_cast<QObject *>(new EditorGuard(controller))));
    return properties;
}

QString mainQmlUrl()
{
    return QStringLiteral("qrc:/Main.qml");
}

} // namespace rasklad::app
