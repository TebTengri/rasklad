#pragma once

// Write-only clipboard access for the explicit Copy action (SYS-239).
// The clipboard is never read: not at startup, not in the background.

#include <QObject>
#include <QString>

namespace rasklad::app {

class ClipboardWriter final : public QObject
{
    Q_OBJECT

public:
    explicit ClipboardWriter(QObject *parent = nullptr);

    // Puts plain text on the system clipboard; false if unavailable.
    Q_INVOKABLE bool setText(const QString &text);
};

} // namespace rasklad::app
