#pragma once

// Shared QML bootstrap for the app and the offscreen UI smoke test (SYS-239).

#include "jsontranslator.h"

#include <QObject>
#include <QVariantMap>

class QQmlApplicationEngine;
class QQmlEngine;

namespace rasklad::app {

class AnalysisService;
class ClipboardWriter;
class DocumentController;
class ThemeWatcher;

// The controller owns the only undo record. Disable the editor's separate
// QTextDocument history so Clear and subsequent edits cannot retain it.
// configure() returns false for anything but a QQuickTextDocument with a
// document; Main.qml then keeps the source editor read-only (fail closed).
class EditorGuard final : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE bool configure(QObject *wrapper);
};

// Installs the built-in RU table when the theme language is "ru", removes it
// otherwise, and retranslates the engine on every language change.
class LanguageSwitcher final : public QObject
{
    Q_OBJECT

public:
    LanguageSwitcher(QQmlEngine *engine, ThemeWatcher *theme, QObject *parent = nullptr);
    ~LanguageSwitcher() override;

    [[nodiscard]] bool russianInstalled() const { return m_installed; }

private:
    void apply();

    QQmlEngine *m_engine = nullptr;
    ThemeWatcher *m_theme = nullptr;
    JsonTranslator m_ru;
    bool m_loaded = false;
    bool m_installed = false;
};

// Material style plus process-wide settings that avoid writes (QML disk
// cache off). Call before creating the QML engine.
void prepareQuickEnvironment();
// Preserve existing restrictions and disable all Qt Wayland primary-selection
// transports in this process. Standard explicit Clipboard copy/paste remains.
[[nodiscard]] QByteArray clipboardInterfaceBlacklist(const QByteArray &previous);

// Required properties of Main.qml.
[[nodiscard]] QVariantMap mainWindowProperties(DocumentController *controller, ThemeWatcher *theme,
                                               ClipboardWriter *clipboard, bool profileRequested);

// "qrc:/Main.qml"
[[nodiscard]] QString mainQmlUrl();

} // namespace rasklad::app
