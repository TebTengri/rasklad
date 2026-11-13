#include "clientadapter.h"
#include <QTest>
#include <QQmlComponent>
#include <QQmlEngine>
#include <memory>
#include <functional>

class Evidence final : public rasklad::routing::EvidenceSource {
public:
    mutable int calls = 0;
    bool available = true;
    std::function<void()> during;
    std::optional<rasklad::routing::TokenEvidence> evidence(const rasklad::routing::EvidenceQuery &) const override {
        ++calls;
        if (during) during();
        if (!available) return std::nullopt;
        rasklad::routing::TokenEvidence e;
        e.full.alternative = true; e.model = rasklad::DecisionAction::Correct; return e;
    }
};

class ClientAdapterTests : public QObject {
    Q_OBJECT
    QQmlEngine *engine_ = nullptr;
    std::unique_ptr<QObject> root_;
    QQuickWindow *window_ = nullptr;
    QQuickItem *first_ = nullptr, *second_ = nullptr, *secret_ = nullptr, *unknown_ = nullptr;
    std::unique_ptr<ClientAdapter> adapter_;
    Evidence evidence_;
    void focus(QQuickItem *item) {
        window_->requestActivate();
        QMetaObject::invokeMethod(item, "forceActiveFocus");
        QTRY_COMPARE(QGuiApplication::focusObject(), item);
    }
    void original(const QString &text) {
        first_->setProperty("text", text);
        first_->setProperty("cursorPosition", text.size());
        focus(first_);
    }
private Q_SLOTS:
    void init() {
        engine_ = new QQmlEngine;
        QQmlComponent component(engine_);
        component.setData(R"(import QtQuick
Window { width: 400; height: 250; visible: true
Column { anchors.fill: parent
TextEdit { objectName: "first"; width: 400; height: 50; textFormat: TextEdit.PlainText }
TextEdit { objectName: "second"; width: 400; height: 50; textFormat: TextEdit.PlainText }
TextInput { objectName: "secret"; width: 400; height: 40; echoMode: TextInput.Password; inputMethodHints: Qt.ImhSensitiveData }
TextEdit { objectName: "unknown"; width: 400; height: 50; textFormat: TextEdit.PlainText }
}})", QUrl());
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        root_.reset(component.create());
        QVERIFY(root_);
        window_ = qobject_cast<QQuickWindow *>(root_.get()); QVERIFY(window_);
        first_ = window_->findChild<QQuickItem *>("first"); second_ = window_->findChild<QQuickItem *>("second");
        secret_ = window_->findChild<QQuickItem *>("secret"); unknown_ = window_->findChild<QQuickItem *>("unknown");
        QVERIFY(first_ && second_ && secret_ && unknown_);
        evidence_.calls = 0; evidence_.available = true; evidence_.during = {};
        adapter_ = std::make_unique<ClientAdapter>(window_, first_, second_, &evidence_, nullptr);
        qGuiApp->installEventFilter(adapter_.get());
        focus(first_);
    }
    void cleanup() {
        adapter_.reset(); root_.reset(); delete engine_; engine_ = nullptr;
    }
    void ordinaryKeysAreCommittedBeforeBoundary() {
        for (QChar c : QStringLiteral("ghbdtn")) QTest::keyClick(window_, c.toLatin1());
        QCOMPARE(first_->property("text").toString(), QStringLiteral("ghbdtn"));
        QVERIFY(first_->property("preeditText").toString().isEmpty());
        QCOMPARE(evidence_.calls, 0);
        QTest::keyClick(window_, Qt::Key_Space);
        QCOMPARE(first_->property("text").toString(), QString::fromUtf8("привет "));
        QCOMPARE(adapter_->corrections, 1);
    }
    void protectedAndUnapprovedDoNotQueryTextOrEvidence() {
        for (auto *item : {secret_, unknown_}) {
            focus(item); item->setProperty("text", QStringLiteral("ghbdtn")); item->setProperty("cursorPosition", 6);
            QTest::keyClick(window_, Qt::Key_Space);
            QCOMPARE(item->property("text").toString(), QStringLiteral("ghbdtn "));
        }
        QCOMPARE(evidence_.calls, 0); QCOMPARE(adapter_->textQueries, 0);
    }
    void cursorFragmentsAndSelectionPassThrough() {
        original(QStringLiteral("ghbdtn")); first_->setProperty("cursorPosition", 3);
        QTest::keyClick(window_, Qt::Key_Space);
        QCOMPARE(first_->property("text").toString(), QStringLiteral("ghb dtn"));
        original(QStringLiteral("ghbdtn"));
        QVERIFY(QMetaObject::invokeMethod(first_, "select", Q_ARG(int,0), Q_ARG(int,6)));
        QTest::keyClick(window_, Qt::Key_Space);
        QCOMPARE(first_->property("text").toString(), QStringLiteral(" "));
        QCOMPARE(evidence_.calls, 0);
    }
    void controlShortcutIsNotConsumed() {
        original(QStringLiteral("ghbdtn"));
        QTest::keyClick(window_, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(first_->property("selectedText").toString(), QStringLiteral("ghbdtn"));
        QTest::keyClick(window_, Qt::Key_Space);
        QCOMPARE(first_->property("text").toString(), QStringLiteral(" "));
        QCOMPARE(evidence_.calls, 0);
    }
    void missingEvidencePreservesNativeInput() {
        evidence_.available = false; original(QStringLiteral("ghbdtn"));
        QTest::keyClick(window_, Qt::Key_Space);
        QCOMPARE(first_->property("text").toString(), QStringLiteral("ghbdtn "));
        QCOMPARE(evidence_.calls, 1); QCOMPARE(adapter_->corrections, 0);
    }
    void focusChangeDuringEvidenceCancelsEdit() {
        original(QStringLiteral("ghbdtn"));
        evidence_.during = [this] { QMetaObject::invokeMethod(second_, "forceActiveFocus"); };
        QTest::keyClick(window_, Qt::Key_Space);
        QCOMPARE(first_->property("text").toString(), QStringLiteral("ghbdtn"));
        QCOMPARE(adapter_->corrections, 0); QCOMPARE(adapter_->refusals, 1);
    }
    void sensitivityChangeDuringEvidenceStopsBeforeTextRequery() {
        original(QStringLiteral("ghbdtn"));
        evidence_.during = [this] { first_->setProperty("inputMethodHints", int(Qt::ImhSensitiveData)); };
        QTest::keyClick(window_, Qt::Key_Space);
        QCOMPARE(adapter_->corrections, 0); QCOMPARE(adapter_->refusals, 1);
        QCOMPARE(adapter_->textQueries, 1); // initial approved snapshot only
        QCOMPARE(first_->property("text").toString(), QStringLiteral("ghbdtn "));
    }
    void tooLongTokenDoesNotReachEvidence() {
        const QString text(33, QLatin1Char('a')); original(text);
        QTest::keyClick(window_, Qt::Key_Space);
        QCOMPARE(first_->property("text").toString(), text + QLatin1Char(' '));
        QCOMPARE(evidence_.calls, 0);
    }
};
QTEST_MAIN(ClientAdapterTests)
#include "client_adapter_tests.moc"
