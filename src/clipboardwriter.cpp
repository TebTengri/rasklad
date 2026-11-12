#include "clipboardwriter.h"

#include <QClipboard>
#include <QGuiApplication>

namespace rasklad::app {

ClipboardWriter::ClipboardWriter(QObject *parent)
    : QObject(parent)
{
}

bool ClipboardWriter::setText(const QString &text)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard || text.isEmpty())
        return false;
    clipboard->setText(text, QClipboard::Clipboard);
    return true;
}

} // namespace rasklad::app
