// SPDX-License-Identifier: MIT
// Developer-only client for the installed Qt Fcitx input context, not an editor backend.
#include <QApplication>
#include <QLineEdit>
#include <QInputMethodEvent>
#include <QInputMethod>
#include <QKeyEvent>
#include <QJsonObject>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QThread>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <private/qguiapplication_p.h>
#include <qpa/qplatformintegration.h>
#include <qpa/qplatforminputcontext.h>
#include <cstdio>
#include <algorithm>
#include <vector>

class ObservedLine final : public QLineEdit {
public:
    QString preedit;
    int preeditEvents = 0;
    int commitEvents = 0;
    void inputMethodEvent(QInputMethodEvent *event) override {
        preedit = event->preeditString();
        if (!preedit.isEmpty()) ++preeditEvents;
        if (!event->commitString().isEmpty()) ++commitEvents;
        QLineEdit::inputMethodEvent(event);
    }
};

static void settle(int ms = 100) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
}

int main(int argc, char **argv) {
    // Only a private session bus and the offscreen platform are admitted.
    if (qgetenv("RASKLAD_PRIVATE_LAB") != "1" ||
        !qgetenv("DISPLAY").isEmpty() || !qgetenv("WAYLAND_DISPLAY").isEmpty() ||
        qgetenv("QT_QPA_PLATFORM") != "offscreen" || qgetenv("QT_IM_MODULE") != "fcitx") return 2;
    QApplication::setApplicationName("rasklad-ime-lab");
    QApplication app(argc, argv);
    auto *context = QGuiApplicationPrivate::platformIntegration()->inputContext();
    QJsonObject report{{"schema",1},{"client","Qt Fcitx offscreen transport fixture"},
        {"realApplicationSupport",false},{"syntheticInputOnly",true},
        {"inputContextLoaded",context != nullptr},
        {"inputContextIsFcitx",context && QByteArray(context->metaObject()->className()).toLower().contains("fcitx")}};
    if (!context || !report["inputContextIsFcitx"].toBool()) {
        report["passed"]=false;
        puts(QJsonDocument(report).toJson(QJsonDocument::Compact).constData()); return 3;
    }
    ObservedLine line;
    line.setAttribute(Qt::WA_InputMethodEnabled);
    line.setInputMethodHints(Qt::ImhNone);
    line.resize(400,50); line.show(); line.activateWindow(); line.setFocus();
    settle(900);
    report["focusAccepted"] = line.hasFocus() && context->inputMethodAccepted();
    // This invokes the real installed Qt platform input context. Offscreen lacks
    // a native keyboard backend, so the fixture performs that backend's filter
    // call explicitly and delivers only unfiltered events to the focused widget.
    // No QInputMethodEvent is constructed by this fixture.
    int filtered = 0;
    std::vector<qint64> filterNanos; filterNanos.reserve(512);
    ObservedLine *active = &line;
    auto key = [&](int qtKey, quint32 sym, const QString &text, Qt::KeyboardModifiers mods=Qt::NoModifier) {
        for (QEvent::Type type : {QEvent::KeyPress,QEvent::KeyRelease}) {
            // Fcitx's real Qt plugin reads native XKB-style state, not only
            // Qt modifiers. Keep both representations consistent.
            quint32 nativeMods=0;
            if (mods & Qt::ShiftModifier) nativeMods |= 1;
            if (mods & Qt::ControlModifier) nativeMods |= 4;
            if (mods & Qt::AltModifier) nativeMods |= 8;
            if (mods & Qt::MetaModifier) nativeMods |= 64;
            QKeyEvent event(type,qtKey,mods,0,sym,nativeMods,text,false,1);
            QElapsedTimer rpcTimer; rpcTimer.start();
            const bool consumed=context->filterEvent(&event);
            const qint64 duration=rpcTimer.nsecsElapsed();
            if (filterNanos.size()<512) filterNanos.push_back(duration);
            if (consumed) ++filtered;
            else QApplication::sendEvent(active,&event);
            settle(30);
        }
    };
    auto word = [&](const QString &text) {
        for (QChar c : text) key(c.toUpper().unicode(),c.unicode(),QString(c));
    };
    word(QStringLiteral("ghbdtn"));
    report["wordHeldBeforeSpace"] = line.text().isEmpty() && line.preedit == QStringLiteral("ghbdtn");
    report["preeditObserved"] = line.preeditEvents > 0;
    key(Qt::Key_Space,0x20,QStringLiteral(" "));
    report["correctedOnce"] = line.text() == QString::fromUtf8("привет ");
    report["oneCommit"] = line.commitEvents == 1;
    word(QStringLiteral("x"));
    report["nextCharacterInPreedit"] = line.text() == QString::fromUtf8("привет ") && line.preedit == QStringLiteral("x");
    key(Qt::Key_Space,0x20,QStringLiteral(" "));
    report["nextWordPreserved"] = line.text() == QString::fromUtf8("привет x ");
    key(Qt::Key_Z,'z',QStringLiteral("z"),Qt::ControlModifier);
    report["nativeUndoChangesDocument"] = line.text() != QString::fromUtf8("привет x ");
    // Backspace is processed by the real IME before any document commit.
    context->reset(); line.clear(); line.preedit.clear(); settle(80);
    word(QStringLiteral("ghbdt"));
    key(Qt::Key_Backspace,0xff08,QString());
    word(QStringLiteral("tn"));
    key(Qt::Key_Space,0x20,QStringLiteral(" "));
    report["backspaceInPreedit"] = line.text() == QString::fromUtf8("привет ");

    // A distinct window creates a distinct actual toolkit input context.
    context->reset(); line.clear(); line.preedit.clear();
    word(QStringLiteral("ghb"));
    ObservedLine other;
    other.resize(400,50); other.show(); other.activateWindow(); other.setFocus();
    active=&other; settle(160);
    key(Qt::Key_Space,0x20,QStringLiteral(" "));
    report["focusSwitchDoesNotLeakWord"] = other.text() == QStringLiteral(" ") && other.preedit.isEmpty();
    line.activateWindow(); line.setFocus(); active=&line; settle(160);

    bool preservedMixed=true;
    const QStringList mixed{QStringLiteral("Aghbdtn"),QStringLiteral("1ghbdtn"),QStringLiteral("@ghbdtn"),QString(33,QChar('a'))+QStringLiteral("ghbdtn")};
    for (const auto &token:mixed) {
        context->reset(); line.clear(); line.preedit.clear(); settle(60);
        word(token); key(Qt::Key_Space,0x20,QStringLiteral(" "));
        preservedMixed &= line.text() == token+QStringLiteral(" ");
    }
    report["mixedAndLongTokensPreserved"] = preservedMixed;

    context->reset(); line.clear(); line.preedit.clear();
    word(QStringLiteral("ghb"));
    line.setInputMethodHints(Qt::ImhSensitiveData);
    app.inputMethod()->update(Qt::ImQueryAll); settle(100);
    report["capabilityLossClearsPending"] = line.preedit.isEmpty() && line.text().isEmpty();

    // Capability changes must disable the fixture addon; toolkit/daemon events
    // are not a promise that no IME component ever observes password events.
    context->reset(); line.clear(); line.preedit.clear();
    line.setEchoMode(QLineEdit::Password);
    line.setInputMethodHints(Qt::ImhHiddenText | Qt::ImhSensitiveData);
    app.inputMethod()->update(Qt::ImQueryAll); settle(100);
    const int beforePasswordPreedit = line.preeditEvents;
    word(QStringLiteral("ghbdtn")); key(Qt::Key_Space,0x20,QStringLiteral(" "));
    report["passwordPassthrough"] = line.text() == QStringLiteral("ghbdtn ") && line.preeditEvents == beforePasswordPreedit;

    context->reset(); line.clear(); line.preedit.clear();
    line.setEchoMode(QLineEdit::Normal); line.setInputMethodHints(Qt::ImhNone);
    app.inputMethod()->update(Qt::ImQueryAll); settle(100);
    word(QStringLiteral("ghb"));
    report["preeditHeldAtCrash"] = line.text().isEmpty() && line.preedit == QStringLiteral("ghb");
    // Parent controls only its own isolated daemon. This handshake ensures a
    // crash with live preedit, rather than an unrelated fresh client test.
    puts("{\"phase\":\"ready_for_daemon_crash\"}"); fflush(stdout);
    const int ack = getchar();
    if (ack != '1') return 4;
    settle(450);
    word(QStringLiteral("abc")); key(Qt::Key_Space,0x20,QStringLiteral(" "));
    report["postCrashInputContinues"] = line.text().endsWith(QStringLiteral("abc "));
    report["postCrashDocumentLength"] = line.text().size();
    report["pendingWordSurvivedCrash"] = line.text().contains(QStringLiteral("ghb"));
    std::sort(filterNanos.begin(),filterNanos.end());
    qint64 totalNanos=0;for(auto n:filterNanos)totalNanos+=n;
    report["filterCallsMeasured"] = int(filterNanos.size());
    report["filterMeanUs"] = filterNanos.empty()?0.0:double(totalNanos)/double(filterNanos.size())/1000.0;
    report["filterP95Us"] = filterNanos.empty()?0.0:double(filterNanos[(filterNanos.size()-1)*95/100])/1000.0;
    report["timingScope"] = "synthetic synchronous Qt filterEvent RPC; excludes fixture settle waits; not physical/native-app latency";
    report["filteredEvents"] = filtered;
    report["preeditEventCount"] = line.preeditEvents;
    report["commitEventCount"] = line.commitEvents;
    const QStringList required{"inputContextIsFcitx","focusAccepted","wordHeldBeforeSpace","preeditObserved","correctedOnce","oneCommit","nextCharacterInPreedit","nextWordPreserved","backspaceInPreedit","mixedAndLongTokensPreserved","capabilityLossClearsPending","focusSwitchDoesNotLeakWord","passwordPassthrough","preeditHeldAtCrash","postCrashInputContinues"};
    bool passed=true; for(const auto &name:required) passed &= report[name].toBool();
    report["passed"]=passed;
    puts(QJsonDocument(report).toJson(QJsonDocument::Compact).constData());
    return passed?0:1;
}
