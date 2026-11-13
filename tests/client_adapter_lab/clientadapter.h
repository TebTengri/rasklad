#pragma once
#include "decisionrouter.h"
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>

// Developer-only in-process adapter for two explicitly approved fixture fields.
// No global hook, preedit buffer, asynchronous result, clipboard or IPC. Ordinary
// keys remain native editor input. This is NOT an adapter for arbitrary programs.
class ClientAdapter final : public QObject {
public:
    ClientAdapter(QQuickWindow *window, QQuickItem *first, QQuickItem *second,
                  const rasklad::routing::EvidenceSource *source, QObject *parent)
        : QObject(parent), window_(window), first_(first), second_(second), source_(source) {}
    int corrections = 0, refusals = 0, textQueries = 0;
protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (disabled_ || watched != window_ || event->type() != QEvent::KeyPress) return false;
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() != Qt::Key_Space || key->modifiers() != Qt::NoModifier || key->isAutoRepeat()) return false;
        QPointer<QQuickItem> target = qobject_cast<QQuickItem *>(QGuiApplication::focusObject());
        if (!target || (target != first_ && target != second_)) { ++refusals; return false; }
        if (!eligible(target)) { ++refusals; return false; }
        const auto before = snapshot(target);
        if (!before.valid || before.cursor != before.anchor) return false;
        // Do not infer a token from an arbitrary cursor fragment or large field.
        if (before.cursor != before.text.size()) return false;
        int start = before.cursor;
        while (start > 0 && letter(before.text.at(start-1)) && before.cursor-start <= rasklad::routing::kMaxRouteKeys) --start;
        if (start == before.cursor || (start > 0 && !before.text.at(start-1).isSpace())) return false;
        const QString token = before.text.mid(start, before.cursor-start);
        const rasklad::routing::RouteRequest request{rasklad::routing::FieldContext::Prose,
            rasklad::routing::InputLayout::KnownUs, QStringView(token)};
        const auto decision = rasklad::routing::route(request, rasklad::routing::RouteProfile::SafeConfirmed, source_);
        if (decision.action != rasklad::routing::RouteAction::Correct || decision.candidate.isEmpty()) return false;
        if (!target || QGuiApplication::focusObject() != target || !eligible(target) || !same(before, snapshot(target))) { ++refusals; return false; }
        QInputMethodEvent commit;
        commit.setCommitString(decision.candidate + QLatin1Char(' '), -token.size(), token.size());
        commit.setAccepted(false);
        QCoreApplication::sendEvent(target, &commit); // synchronous, same GUI event dispatch
        if (!target || !eligible(target)) { disabled_ = true; return true; }
        const auto after = snapshot(target);
        const QString expected = before.text.left(start) + decision.candidate + QLatin1Char(' ');
        if (after.valid && after.text == expected && after.cursor == expected.size() && after.anchor == after.cursor) {
            ++corrections; return true; // original Space was included in the edit
        }
        disabled_ = true; // no retry or guessed rollback after an unexpected editor response
        return !after.valid || after.text != before.text;
    }
private:
    static bool eligible(QQuickItem *item) {
        QInputMethodQueryEvent hints(Qt::ImEnabled | Qt::ImHints);
        QCoreApplication::sendEvent(item, &hints);
        const int flags = hints.value(Qt::ImHints).toInt();
        if (!hints.value(Qt::ImEnabled).toBool() || (flags & (Qt::ImhHiddenText | Qt::ImhSensitiveData | Qt::ImhNoPredictiveText))) return false;
        return item->property("preeditText").toString().isEmpty();
    }
    struct Snapshot { QString text; int cursor = -1, anchor = -1; bool valid = false; };
    Snapshot snapshot(QQuickItem *item) {
        ++textQueries;
        QInputMethodQueryEvent query(Qt::ImSurroundingText | Qt::ImCursorPosition | Qt::ImAnchorPosition);
        QCoreApplication::sendEvent(item, &query);
        Snapshot s{query.value(Qt::ImSurroundingText).toString(), query.value(Qt::ImCursorPosition).toInt(), query.value(Qt::ImAnchorPosition).toInt(), false};
        s.valid = s.text.size() <= 4096 && s.cursor >= 0 && s.cursor <= s.text.size() && s.anchor >= 0 && s.anchor <= s.text.size();
        return s;
    }
    static bool same(const Snapshot &a, const Snapshot &b) { return a.valid && b.valid && a.text == b.text && a.cursor == b.cursor && a.anchor == b.anchor; }
    static bool letter(QChar c) {
        const auto n = c.unicode();
        return (n >= 'a' && n <= 'z') || (n >= 'A' && n <= 'Z') || (n >= 0x410 && n <= 0x44f) || n == 0x401 || n == 0x451;
    }
    QPointer<QQuickWindow> window_;
    QPointer<QQuickItem> first_, second_;
    const rasklad::routing::EvidenceSource *source_;
    bool disabled_ = false;
};
