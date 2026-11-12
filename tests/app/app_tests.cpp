// SYS-239: tests of the manual-preview logic (no window). Synthetic text and
// synthetic profile fixtures only.

#include "analysisservice.h"
#include "appoptions.h"
#include "documentcontroller.h"
#include "jsontranslator.h"
#include "localprofile.h"
#include "profilefixture.h"
#include "profileverify.h"
#include "themewatcher.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QSemaphore>
#include <QSaveFile>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <QDir>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

using namespace rasklad;
using namespace rasklad::app;
using rasklad::profile::ProfileError;

namespace {

struct FakeState {
    std::optional<routing::TokenEvidence> evidence;
    QSemaphore gate{0};
    std::atomic<bool> blocking{false};
    std::atomic<int> calls{0};
    std::atomic<QThread *> createThread{nullptr};
    std::atomic<QThread *> queryThread{nullptr};
    std::atomic<QThread *> destroyThread{nullptr};
};

class FakeProvider final : public routing::EvidenceSource
{
public:
    explicit FakeProvider(std::shared_ptr<FakeState> state)
        : m_state(std::move(state))
    {
        m_state->createThread = QThread::currentThread();
    }
    ~FakeProvider() override { m_state->destroyThread = QThread::currentThread(); }

    [[nodiscard]] std::optional<routing::TokenEvidence> evidence(const routing::EvidenceQuery &) const override
    {
        ++m_state->calls;
        m_state->queryThread = QThread::currentThread();
        if (m_state->blocking)
            m_state->gate.acquire();
        return m_state->evidence;
    }

private:
    std::shared_ptr<FakeState> m_state;
};

ProviderFactory fakeFactory(const std::shared_ptr<FakeState> &state)
{
    return [state](ProfileError &) -> std::unique_ptr<routing::EvidenceSource> {
        return std::make_unique<FakeProvider>(state);
    };
}

routing::TokenEvidence correctEvidence()
{
    routing::TokenEvidence e;
    e.full.alternative = true;
    e.model = DecisionAction::Correct;
    return e;
}

class CountingSource final : public routing::EvidenceSource
{
public:
    [[nodiscard]] std::optional<routing::TokenEvidence> evidence(const routing::EvidenceQuery &) const override
    {
        ++calls;
        return correctEvidence();
    }
    mutable int calls = 0;
};

CheckRequest makeRequest(quint64 id, const QString &token)
{
    CheckRequest r;
    r.id = id;
    r.token = token;
    r.layout = routing::InputLayout::KnownUs;
    r.field = routing::FieldContext::Prose;
    return r;
}

QString u8(const char *text)
{
    return QString::fromUtf8(text);
}

QColor col(const char *name)
{
    return QColor::fromString(QString::fromLatin1(name));
}

QByteArray readSource(const char *relative)
{
    QFile file(QStringLiteral(RASKLAD_SOURCE_ROOT) + QLatin1Char('/') + QString::fromLatin1(relative));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

class AppTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ---- Document controller ----

    void manualPreviewApplyUndo()
    {
        DocumentController c;
        QVERIFY(c.setSourceText(u8("Ghbdtn 😀 vbh")));
        QCOMPARE(c.previewKind(), QStringLiteral("none"));
        QVERIFY(c.preview());
        QCOMPARE(c.previewKind(), QStringLiteral("manual"));
        QCOMPARE(c.previewText(), u8("Привет 😀 мир"));
        // A preview never changes the text by itself.
        QCOMPARE(c.sourceText(), u8("Ghbdtn 😀 vbh"));
        QVERIFY(c.canApply());
        QCOMPARE(c.copyKind(), QStringLiteral("preview"));
        QCOMPARE(c.copyPayload(), u8("Привет 😀 мир"));
        QSignalSpy sourceSpy(&c, &DocumentController::sourceTextChanged);
        QVERIFY(c.apply());
        QCOMPARE(sourceSpy.count(), 1);
        QCOMPARE(c.sourceText(), u8("Привет 😀 мир"));
        QCOMPARE(c.previewKind(), QStringLiteral("none"));
        QVERIFY(!c.canApply());
        QVERIFY(c.canUndo());
        QCOMPARE(c.notice(), QStringLiteral("applied"));
        QVERIFY(c.undo());
        QCOMPARE(c.sourceText(), u8("Ghbdtn 😀 vbh"));
        QVERIFY(!c.canUndo());
        QVERIFY(!c.undo());
        QCOMPARE(c.notice(), QStringLiteral("undoUnavailable"));
    }

    void ruToUsAndUnicodePassThrough()
    {
        DocumentController c;
        c.setDirection(DocumentController::RuToUs);
        QVERIFY(c.setSourceText(u8("Руддщ, é #️⃣")));
        QVERIFY(c.preview());
        // The pure converter is the reference; combining clusters and
        // characters outside the layout stay unchanged.
        QCOMPARE(c.previewText(), convertLayout(c.sourceText(), LayoutDirection::RuToUs));
        QVERIFY(c.previewText().startsWith(QStringLiteral("Hello")));
        QVERIFY(c.previewText().contains(u8("é")));
        QCOMPARE(c.previewText().size(), c.sourceText().size());
    }

    void previewInvalidatedByEditAndDirection()
    {
        DocumentController c;
        QVERIFY(c.setSourceText(QStringLiteral("ghbdtn")));
        QVERIFY(c.preview());
        QVERIFY(c.setSourceText(QStringLiteral("ghbdtnb")));
        QCOMPARE(c.previewKind(), QStringLiteral("none"));
        QCOMPARE(c.notice(), QStringLiteral("previewStale"));
        QVERIFY(!c.canApply());
        QVERIFY(!c.apply());
        QCOMPARE(c.sourceText(), QStringLiteral("ghbdtnb"));

        QVERIFY(c.preview());
        c.setDirection(DocumentController::RuToUs);
        QCOMPARE(c.previewKind(), QStringLiteral("none"));
        QVERIFY(!c.apply());
        c.setDirection(17);
        QCOMPARE(c.direction(), int(DocumentController::RuToUs));
    }

    void newEditInvalidatesUndo()
    {
        DocumentController c;
        QVERIFY(c.setSourceText(QStringLiteral("ghbdtn")));
        QVERIFY(c.preview());
        QVERIFY(c.apply());
        QVERIFY(c.canUndo());
        QVERIFY(c.setSourceText(u8("привет!")));
        QVERIFY(!c.canUndo());
        QVERIFY(!c.undo());
        QCOMPARE(c.sourceText(), u8("привет!"));
    }

    void editExtentCountsChangedRegion()
    {
        auto extent = editExtent(QStringLiteral("abc"), QStringLiteral("abcd"));
        QCOMPARE(extent.removed, qsizetype(0));
        QCOMPARE(extent.inserted, qsizetype(1));
        QVERIFY(!isBulkEdit(extent));
        extent = editExtent(QStringLiteral("abcd"), QStringLiteral("abd"));
        QCOMPARE(extent.removed, qsizetype(1));
        QCOMPARE(extent.inserted, qsizetype(0));
        QVERIFY(!isBulkEdit(extent));
        // Repeated letters: prefix and suffix must not overlap.
        extent = editExtent(QStringLiteral("aaaa"), QStringLiteral("aa"));
        QCOMPARE(extent.removed, qsizetype(2));
        QCOMPARE(extent.inserted, qsizetype(0));
        QVERIFY(isBulkEdit(extent));
        extent = editExtent(QStringLiteral("aa"), QStringLiteral("aaaa"));
        QCOMPARE(extent.inserted, qsizetype(2));
        extent = editExtent(QStringLiteral("hello world"), QStringLiteral("x"));
        QCOMPARE(extent.removed, qsizetype(11));
        QCOMPARE(extent.inserted, qsizetype(1));
        QVERIFY(isBulkEdit(extent));
        // A replaced single character is not bulk.
        extent = editExtent(QStringLiteral("abc"), QStringLiteral("aXc"));
        QCOMPARE(extent.removed, qsizetype(1));
        QCOMPARE(extent.inserted, qsizetype(1));
        QVERIFY(!isBulkEdit(extent));
        extent = editExtent(QString(), QStringLiteral("ab"));
        QVERIFY(isBulkEdit(extent));
    }

    void singleUnitEditDropsApplyUndo()
    {
        DocumentController c;
        QVERIFY(c.setSourceText(QStringLiteral("ghbdtn")));
        QVERIFY(c.preview());
        QVERIFY(c.apply());
        QVERIFY(c.canUndo());
        QVERIFY(c.setSourceText(u8("привет!")));
        QVERIFY(!c.canUndo());
        QVERIFY(!c.undo());
        QCOMPARE(c.notice(), QStringLiteral("undoUnavailable"));
        QCOMPARE(c.sourceText(), u8("привет!"));
        // Single-unit deletion also gets no history.
        QVERIFY(c.setSourceText(u8("привет")));
        QVERIFY(!c.canUndo());
    }

    void bulkEditAfterApplyUndoesOnlyTheBulkEdit()
    {
        DocumentController c;
        QVERIFY(c.setSourceText(QStringLiteral("ghbdtn")));
        QVERIFY(c.preview());
        QVERIFY(c.apply());
        QCOMPARE(c.sourceText(), u8("привет"));
        // Select-all overwrite after Apply replaces the Apply record.
        QVERIFY(c.setSourceText(QStringLiteral("x")));
        QVERIFY(c.canUndo());
        QVERIFY(c.undo());
        QCOMPARE(c.notice(), QStringLiteral("undoneEdit"));
        // The applied document comes back, not the pre-Apply one.
        QCOMPARE(c.sourceText(), u8("привет"));
        QVERIFY(!c.canUndo());
        QVERIFY(!c.undo());
        QCOMPARE(c.sourceText(), u8("привет"));
    }

    void bulkEditsHaveOneLevel_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::newRow("select-all-overwrite") << QStringLiteral("long fictional text") << QStringLiteral("q");
        QTest::newRow("bulk-delete") << QStringLiteral("keep this part and remove the rest")
                                     << QStringLiteral("keep this part");
        QTest::newRow("multi-char-paste") << QStringLiteral("ab") << QStringLiteral("a pasted words b");
        QTest::newRow("delete-all") << QStringLiteral("some text") << QString();
        QTest::newRow("emoji-surrogate-pair") << QStringLiteral("ab") << u8("a😀b");
    }

    void bulkEditsHaveOneLevel()
    {
        QFETCH(QString, before);
        QFETCH(QString, after);
        DocumentController c;
        QVERIFY(c.setSourceText(before));
        QVERIFY(c.setSourceText(after));
        QVERIFY(c.canUndo());
        QVERIFY(c.undo());
        QCOMPARE(c.sourceText(), before);
        QCOMPARE(c.notice(), QStringLiteral("undoneEdit"));
        // One level only: the earlier initial paste is not reachable.
        QVERIFY(!c.canUndo());
        QVERIFY(!c.undo());
        QCOMPARE(c.sourceText(), before);
    }

    void initialPasteUndoesToEmptyAndClearDropsHistory()
    {
        DocumentController c;
        QVERIFY(c.setSourceText(QStringLiteral("pasted text")));
        QVERIFY(c.canUndo());
        QVERIFY(c.undo());
        QVERIFY(c.sourceText().isEmpty());
        QVERIFY(c.setSourceText(QStringLiteral("pasted text")));
        QVERIFY(c.canUndo());
        c.clear();
        QVERIFY(!c.canUndo());
        QVERIFY(!c.undo());
        QVERIFY(c.sourceText().isEmpty());
    }

    void bulkEditInvalidatesPreviewAndRevision()
    {
        DocumentController c;
        QVERIFY(c.setSourceText(QStringLiteral("ghbdtn")));
        QVERIFY(c.preview());
        const quint64 revision = c.currentSnapshot().revision;
        QVERIFY(c.setSourceText(QStringLiteral("vbh vbh")));
        QVERIFY(c.currentSnapshot().revision > revision);
        QCOMPARE(c.previewKind(), QStringLiteral("none"));
        QVERIFY(!c.apply());
        // Undoing the bulk edit is itself a new revision; no stale preview returns.
        QVERIFY(c.undo());
        QCOMPARE(c.sourceText(), QStringLiteral("ghbdtn"));
        QCOMPARE(c.previewKind(), QStringLiteral("none"));
        QVERIFY(!c.canApply());
    }

    void oversizeRejectedWithoutTruncation()
    {
        DocumentController c;
        QVERIFY(c.setSourceText(QStringLiteral("keep me")));
        QVERIFY(c.preview());
        QSignalSpy rejected(&c, &DocumentController::sourceRejected);
        const QString huge(kMaxInputUtf16 + 1, QLatin1Char('x'));
        QVERIFY(!c.setSourceText(huge));
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(c.sourceText(), QStringLiteral("keep me"));
        QCOMPARE(c.notice(), QStringLiteral("inputTooLong"));
        // The rejected edit did not touch the existing preview.
        QVERIFY(c.canApply());
        const QString exact(kMaxInputUtf16, QLatin1Char('x'));
        QVERIFY(c.setSourceText(exact));
        QCOMPARE(c.sourceLength(), kMaxInputUtf16);
    }

    void noChangeAndEmpty()
    {
        DocumentController c;
        QVERIFY(!c.preview());
        QCOMPARE(c.notice(), QStringLiteral("nothingToConvert"));
        QVERIFY(c.setSourceText(QStringLiteral("123 456")));
        QVERIFY(!c.preview());
        QCOMPARE(c.notice(), QStringLiteral("conversionNoChange"));
        QCOMPARE(c.copyKind(), QStringLiteral("text"));
        QCOMPARE(c.copyPayload(), QStringLiteral("123 456"));
        c.noteCopied();
        QCOMPARE(c.notice(), QStringLiteral("copiedText"));
    }

    void clearReleasesEverything()
    {
        DocumentController c;
        QVERIFY(c.setSourceText(QStringLiteral("ghbdtn")));
        QVERIFY(c.preview());
        QVERIFY(c.apply());
        c.setDirection(DocumentController::RuToUs);
        QVERIFY(c.preview());
        QVERIFY(c.canUndo());
        c.clear();
        QVERIFY(c.sourceText().isEmpty());
        QCOMPARE(c.previewKind(), QStringLiteral("none"));
        QVERIFY(!c.canUndo());
        QCOMPARE(c.copyKind(), QStringLiteral("none"));
        QCOMPARE(c.notice(), QStringLiteral("cleared"));
        c.noteCopied();
        QCOMPARE(c.notice(), QStringLiteral("nothingToCopy"));
    }

    void checkWithoutProviderIsUnavailable()
    {
        DocumentController c;
        QVERIFY(c.setSourceText(QStringLiteral("ghbdtn")));
        QCOMPARE(c.analysisState(), QStringLiteral("disabled"));
        QVERIFY(!c.checkWord(0, 0));
        QCOMPARE(c.notice(), QStringLiteral("analysisUnavailable"));
        // Manual conversion does not need dictionaries.
        QVERIFY(c.preview());
    }

    void findCheckWordSpans()
    {
        const QString text = QStringLiteral("  ghbdtn   vbh ");
        auto span = findCheckWord(text, 4, 4);
        QVERIFY(span);
        QCOMPARE(text.mid(span->start, span->length), QStringLiteral("ghbdtn"));
        span = findCheckWord(text, 8, 8); // right after the word
        QVERIFY(span);
        QCOMPARE(text.mid(span->start, span->length), QStringLiteral("ghbdtn"));
        QVERIFY(!findCheckWord(text, 9, 9)); // between spaces
        span = findCheckWord(text, 10, 14); // selection with outer spaces
        QVERIFY(span);
        QCOMPARE(text.mid(span->start, span->length), QStringLiteral("vbh"));
        QVERIFY(!findCheckWord(text, 2, 14)); // two words
        QVERIFY(!findCheckWord(QString(), 0, 0));
        span = findCheckWord(text, 500, -3); // clamped and ordered
        QVERIFY(!span);
    }

    // ---- Analysis worker ----

    void evaluateRequestGatesAndSingleLookup()
    {
        CountingSource source;
        CheckRequest r = makeRequest(1, QStringLiteral("ghbdtn"));
        r.field = routing::FieldContext::Protected;
        CheckResult result = evaluateRequest(r, &source);
        QCOMPARE(result.safe.action, routing::RouteAction::Keep);
        QCOMPARE(source.calls, 0);
        r.field = routing::FieldContext::Unknown;
        result = evaluateRequest(r, &source);
        QCOMPARE(result.requested.action, routing::RouteAction::Abstain);
        QCOMPARE(source.calls, 0);
        r.field = routing::FieldContext::Prose;
        r.layout = routing::InputLayout::Unknown;
        result = evaluateRequest(r, &source);
        QCOMPARE(source.calls, 0);
        r.layout = routing::InputLayout::KnownUs;
        result = evaluateRequest(r, &source);
        // Both profiles from one lookup.
        QCOMPARE(source.calls, 1);
        QCOMPARE(result.safe.action, routing::RouteAction::Correct);
        QCOMPARE(result.requested.candidate, u8("привет"));
        result = evaluateRequest(r, nullptr);
        QCOMPARE(result.requested.reason, routing::RouteReason::EvidenceUnavailable);
    }

    void asyncLoadCheckAndExplicitApply()
    {
        auto state = std::make_shared<FakeState>();
        state->evidence = correctEvidence();
        AnalysisService service;
        DocumentController c(&service);
        QVERIFY(c.setSourceText(QStringLiteral("ok ghbdtn")));
        QVERIFY(c.preview());
        service.start(fakeFactory(state));
        QCOMPARE(service.state(), AnalysisService::State::Loading);
        // Provider generation changed: the manual preview is stale.
        QCOMPARE(c.previewKind(), QStringLiteral("none"));
        QTRY_COMPARE(service.state(), AnalysisService::State::Ready);
        QVERIFY(state->createThread.load() != QThread::currentThread());

        QVERIFY(c.checkWord(5, 5));
        QVERIFY(c.checking());
        QTRY_COMPARE(c.checkAction(), QStringLiteral("correct"));
        QVERIFY(!c.checking());
        QCOMPARE(c.checkReason(), QStringLiteral("dictionaryAndModelAgree"));
        QCOMPARE(c.checkSafeAction(), QStringLiteral("correct"));
        QCOMPARE(state->queryThread.load(), state->createThread.load());
        QCOMPARE(c.previewKind(), QStringLiteral("check"));
        QCOMPARE(c.previewText(), u8("ok привет"));
        // Even Correct waits for Apply.
        QCOMPARE(c.sourceText(), QStringLiteral("ok ghbdtn"));
        QVERIFY(c.apply());
        QCOMPARE(c.sourceText(), u8("ok привет"));
        QVERIFY(c.undo());
        QCOMPARE(c.sourceText(), QStringLiteral("ok ghbdtn"));
        service.shutdown();
        QCOMPARE(state->destroyThread.load(), state->createThread.load());
    }

    void suggestionIsPreviewOnly()
    {
        auto state = std::make_shared<FakeState>();
        routing::TokenEvidence e;
        e.model = DecisionAction::Correct; // model only
        state->evidence = e;
        AnalysisService service;
        DocumentController c(&service);
        service.start(fakeFactory(state));
        QTRY_COMPARE(service.state(), AnalysisService::State::Ready);
        QVERIFY(c.setSourceText(QStringLiteral("ghbdtn")));
        QVERIFY(c.checkWord(0, 6));
        QTRY_COMPARE(c.checkAction(), QStringLiteral("suggest"));
        QCOMPARE(c.checkReason(), QStringLiteral("modelOnly"));
        QCOMPARE(c.checkSafeAction(), QStringLiteral("abstain"));
        QCOMPARE(c.previewKind(), QStringLiteral("check"));
        QCOMPARE(c.sourceText(), QStringLiteral("ghbdtn"));
    }

    void staleResultsDiscarded()
    {
        auto state = std::make_shared<FakeState>();
        state->evidence = correctEvidence();
        state->blocking = true;
        AnalysisService service;
        DocumentController c(&service);
        service.start(fakeFactory(state));
        QTRY_COMPARE(service.state(), AnalysisService::State::Ready);

        // Source edit while the worker is busy.
        QVERIFY(c.setSourceText(QStringLiteral("ghbdtn")));
        QVERIFY(c.checkWord(0, 0));
        QTRY_COMPARE(state->calls.load(), 1);
        QVERIFY(c.setSourceText(QStringLiteral("ghbdtn ")));
        QVERIFY(!c.checking());
        state->gate.release();
        QTRY_VERIFY(!service.busy());
        QCoreApplication::processEvents();
        QVERIFY(c.checkAction().isEmpty());
        QCOMPARE(c.previewKind(), QStringLiteral("none"));
        QCOMPARE(c.sourceText(), QStringLiteral("ghbdtn "));

        // Direction change while the worker is busy.
        QVERIFY(c.checkWord(0, 0));
        QTRY_COMPARE(state->calls.load(), 2);
        c.setDirection(DocumentController::RuToUs);
        state->gate.release();
        QTRY_VERIFY(!service.busy());
        QCoreApplication::processEvents();
        QVERIFY(c.checkAction().isEmpty());
        QCOMPARE(c.previewKind(), QStringLiteral("none"));

        // Manual preview replaces a pending check.
        c.setDirection(DocumentController::UsToRu);
        QVERIFY(c.checkWord(0, 0));
        QTRY_COMPARE(state->calls.load(), 3);
        QVERIFY(c.preview());
        state->gate.release();
        QTRY_VERIFY(!service.busy());
        QCoreApplication::processEvents();
        QCOMPARE(c.previewKind(), QStringLiteral("manual"));
        QVERIFY(c.checkAction().isEmpty());
        service.shutdown();
    }

    void latestRequestReplacesQueued()
    {
        auto state = std::make_shared<FakeState>();
        state->evidence = correctEvidence();
        state->blocking = true;
        AnalysisService service;
        QList<quint64> ids;
        connect(&service, &AnalysisService::resultReady, this,
                [&ids](const CheckResult &result) { ids.append(result.id); });
        QCOMPARE(service.submit(makeRequest(1, QStringLiteral("ghbdtn"))), AnalysisService::Submit::NotReady);
        service.start(fakeFactory(state));
        QTRY_COMPARE(service.state(), AnalysisService::State::Ready);
        QCOMPARE(service.submit(makeRequest(1, QStringLiteral("ghbdtn"))), AnalysisService::Submit::Started);
        QCOMPARE(service.submit(makeRequest(2, QStringLiteral("vbhjdjq"))), AnalysisService::Submit::Queued);
        QCOMPARE(service.submit(makeRequest(3, QStringLiteral("ghbdtnb"))), AnalysisService::Submit::ReplacedQueued);
        QVERIFY(service.busy());
        QVERIFY(service.hasQueued());
        state->gate.release(2);
        QTRY_COMPARE(ids.size(), 2);
        QCOMPARE(ids.at(0), quint64(1));
        QCOMPARE(ids.at(1), quint64(3));
        QCOMPARE(state->calls.load(), 2);
        QVERIFY(!service.busy());
        QVERIFY(!service.hasQueued());
        service.shutdown();
    }

    void shutdownJoinsInflightWorker()
    {
        auto state = std::make_shared<FakeState>();
        state->evidence = correctEvidence();
        state->blocking = true;
        auto service = std::make_unique<AnalysisService>();
        DocumentController c(service.get());
        service->start(fakeFactory(state));
        QTRY_COMPARE(service->state(), AnalysisService::State::Ready);
        QVERIFY(c.setSourceText(QStringLiteral("ghbdtn")));
        QVERIFY(c.checkWord(0, 0));
        QTRY_COMPARE(state->calls.load(), 1);
        std::thread releaser([state]() {
            QThread::msleep(50);
            state->gate.release();
        });
        service->shutdown();
        releaser.join();
        QCOMPARE(service->state(), AnalysisService::State::Disabled);
        QVERIFY(state->destroyThread.load() != nullptr);
        QCOMPARE(state->destroyThread.load(), state->createThread.load());
        QVERIFY(state->destroyThread.load() != QThread::currentThread());
        QVERIFY(!c.checking());
        // A result posted before the join is delivered to a live service and ignored.
        QCoreApplication::processEvents();
        QVERIFY(c.checkAction().isEmpty());
        QCOMPARE(c.analysisState(), QStringLiteral("disabled"));
        service->shutdown(); // idempotent
        service.reset();
    }

    void blockedRequestsNeverEnterQueue()
    {
        auto state = std::make_shared<FakeState>();
        state->evidence = correctEvidence();
        AnalysisService service;
        service.start(fakeFactory(state));
        QTRY_COMPARE(service.state(), AnalysisService::State::Ready);
        for (int kind = 0; kind < 5; ++kind) {
            auto request = makeRequest(1, QStringLiteral("ghbdtn"));
            if (kind == 0) request.field = routing::FieldContext::Protected;
            if (kind == 1) request.field = routing::FieldContext::Unknown;
            if (kind == 2) request.layout = routing::InputLayout::Unknown;
            if (kind == 3) request.token = QString(65537, QLatin1Char('a'));
            if (kind == 4) request.token = QStringLiteral("ab");
            QCOMPARE(service.submit(std::move(request)), AnalysisService::Submit::Rejected);
            QVERIFY(!service.busy());
            QVERIFY(!service.hasQueued());
        }
        QCoreApplication::processEvents();
        QCOMPARE(state->calls.load(), 0);
    }

    void editReleasesWaitingRequest()
    {
        auto state = std::make_shared<FakeState>();
        state->evidence = correctEvidence();
        state->blocking = true;
        AnalysisService service;
        DocumentController controller(&service);
        service.start(fakeFactory(state));
        QTRY_COMPARE(service.state(), AnalysisService::State::Ready);
        QVERIFY(controller.setSourceText(QStringLiteral("ghbdtn")));
        QVERIFY(controller.checkWord(0, 0));
        QTRY_COMPARE(state->calls.load(), 1);
        QVERIFY(controller.checkWord(0, 0));
        QVERIFY(service.hasQueued());
        QVERIFY(controller.setSourceText(QStringLiteral("hello")));
        QVERIFY(!service.hasQueued());
        state->gate.release();
        QTRY_VERIFY(!service.busy());
        QCOMPARE(state->calls.load(), 1);
        QVERIFY(controller.checkAction().isEmpty());
        QVERIFY(!controller.canApply());
    }

    void shutdownDuringLoadIgnoresQueuedCallbacks()
    {
        auto state = std::make_shared<FakeState>();
        QSemaphore loaded;
        AnalysisService service;
        QSignalSpy results(&service, &AnalysisService::resultReady);
        service.start([state, &loaded](ProfileError &) -> std::unique_ptr<routing::EvidenceSource> {
            auto provider = std::make_unique<FakeProvider>(state);
            loaded.release();
            return provider;
        });
        QVERIFY(loaded.tryAcquire(1, 5000));
        service.shutdown();
        bool restarted = false;
        service.start([&restarted](ProfileError &) -> std::unique_ptr<routing::EvidenceSource> {
            restarted = true;
            return nullptr;
        });
        QCoreApplication::processEvents();
        QCOMPARE(service.state(), AnalysisService::State::Disabled);
        QVERIFY(!restarted);
        QVERIFY(!service.busy());
        QCOMPARE(results.count(), 0);
        QCOMPARE(state->createThread.load(), state->destroyThread.load());
    }

    void unavailableProviderKeepsManualConversion()
    {
        AnalysisService service;
        DocumentController c(&service);
        service.start([](ProfileError &error) -> std::unique_ptr<routing::EvidenceSource> {
            error = ProfileError::MissingFile;
            return nullptr;
        });
        QTRY_COMPARE(service.state(), AnalysisService::State::Unavailable);
        QCOMPARE(service.loadError(), ProfileError::MissingFile);
        QCOMPARE(c.analysisState(), QStringLiteral("unavailable"));
        QVERIFY(c.setSourceText(QStringLiteral("ghbdtn")));
        QVERIFY(!c.checkWord(0, 0));
        QCOMPARE(c.notice(), QStringLiteral("analysisUnavailable"));
        QVERIFY(c.preview());
        QCOMPARE(c.previewText(), u8("привет"));
    }

    void checkWordBounds()
    {
        auto state = std::make_shared<FakeState>();
        state->evidence = correctEvidence();
        AnalysisService service;
        DocumentController c(&service);
        service.start(fakeFactory(state));
        QTRY_COMPARE(service.state(), AnalysisService::State::Ready);
        QVERIFY(c.setSourceText(QString(kMaxCheckUtf16 + 1, QLatin1Char('a'))));
        QVERIFY(!c.checkWord(0, 0));
        QCOMPARE(c.notice(), QStringLiteral("checkWordTooLong"));
        QVERIFY(c.setSourceText(QStringLiteral("   ")));
        QVERIFY(!c.checkWord(1, 1));
        QCOMPARE(c.notice(), QStringLiteral("checkNeedsWord"));
        QCOMPARE(state->calls.load(), 0);
        // Too short for the router: answered without evidence.
        QVERIFY(c.setSourceText(QStringLiteral("ab")));
        QVERIFY(c.checkWord(0, 0));
        QTRY_COMPARE(c.checkReason(), QStringLiteral("tooShort"));
        QCOMPARE(state->calls.load(), 0);
        QCOMPARE(c.previewKind(), QStringLiteral("none"));
    }

    // ---- Verified profile gates (synthetic fixtures) ----

    void productionPinsArePinned()
    {
        const profile::ProfilePins &p = profile::productionPins();
        QCOMPARE(p.model.sha256, QStringLiteral("e92e549e5f0853e4cd78d6d997653098b78550eb9911de11421434e26e3985d8"));
        QCOMPARE(p.model.bytes, qint64(88290));
        QCOMPARE(p.stem.sha256, QStringLiteral("3bdd740d4b39bf2b5fe361e0d3c670187563ec59f41913db3525340a30887871"));
        QCOMPARE(p.stem.bytes, qint64(1200379));
        QVERIFY(profile::isSafeRelativePath(p.morphManifest.path, QStringLiteral("data/")));
        QVERIFY(profile::isSafeRelativePath(p.model.path, QStringLiteral("build/")));
        for (const profile::LanguagePins &l : p.languages) {
            for (const profile::FilePin *pin : {&l.dic, &l.aff, &l.controlAff}) {
                QVERIFY(profile::isLowerHex64(pin->sha256));
                QVERIFY(pin->bytes > 0);
                QVERIFY(profile::isSafeRelativePath(pin->path, QStringLiteral("build/")));
            }
            QVERIFY(profile::isSafeRelativePath(l.notice.path, QStringLiteral("data/notices/")));
        }
    }

    void publicManifestsMatchProductionPins()
    {
        // Only public, committed manifests are read here.
        const profile::ProfilePins &p = profile::productionPins();
        QVERIFY(profile::verifyMorphManifest(readSource("data/morphology-003-sources.json"), p));
        QVERIFY(profile::verifyTrainingManifest(readSource("data/sources.json"), p));
        QVERIFY(profile::verifyProtocol002(readSource("data/experiment-002-protocol.json"), p));
    }

    void safePaths()
    {
        const QString data = QStringLiteral("data/");
        QVERIFY(profile::isSafeRelativePath(QStringLiteral("data/notices/a.txt"), data));
        QVERIFY(!profile::isSafeRelativePath(QStringLiteral("data/../x"), data));
        QVERIFY(!profile::isSafeRelativePath(QStringLiteral("data//x"), data));
        QVERIFY(!profile::isSafeRelativePath(QStringLiteral("data/./x"), data));
        QVERIFY(!profile::isSafeRelativePath(QStringLiteral("/data/x"), data));
        QVERIFY(!profile::isSafeRelativePath(QStringLiteral("data/x y"), data));
        QVERIFY(!profile::isSafeRelativePath(QStringLiteral("build/x"), data));
        QVERIFY(!profile::isSafeRelativePath(QStringLiteral("data/"), data));
        QVERIFY(!profile::isSafeRelativePath(QStringLiteral("data/") + QString(300, QLatin1Char('a')), data));
    }

    void removePfxSfxRowsRules()
    {
        QByteArray out;
        QVERIFY(profile::removePfxSfxRows(QByteArray("SET UTF-8\n  PFX A Y 1\r\nKEEP\nSFX\tB 0 s .\nlast"), out));
        QCOMPARE(out, QByteArray("SET UTF-8\nKEEP\nlast"));
        QVERIFY(!profile::removePfxSfxRows(QByteArray("SET UTF-8\nPFXA Y 1\n"), out));
        QVERIFY(!profile::removePfxSfxRows(QByteArray("\xEF\xBB\xBFPFX A Y 1\n"), out));
        QVERIFY(!profile::removePfxSfxRows(QByteArray("SET UTF-8\n"), out));
        QVERIFY(!profile::removePfxSfxRows(QByteArray(), out));
        QVERIFY(!profile::removePfxSfxRows(QByteArray("PFX\0A", 5), out));
        QVERIFY(profile::removePfxSfxRows(QByteArray("\xEF\xBB\xBFSET UTF-8\nSFX A Y 1\n"), out));
        QCOMPARE(out, QByteArray("\xEF\xBB\xBFSET UTF-8\n"));
    }

    void fixtureVerifies()
    {
        testfixture::ProfileFixture f;
        QVERIFY(f.build());
        profile::VerifiedProfile verified;
        QCOMPARE(profile::verifyProfileFiles(f.root, f.pins, verified), ProfileError::None);
        QVERIFY(verified.model.isValid());
        QVERIFY(verified.stem[0].contains(testfixture::keysOf(QStringLiteral("hello"))));
        QVERIFY(verified.stem[1].contains(testfixture::keysOf(QStringLiteral("ghbdtn"))));
        QVERIFY(!verified.files[0].dic.isEmpty());
        QCOMPARE(profile::recheckMorphologyFiles(f.root, f.pins), ProfileError::None);
        // A pinned non-root path is not a project root.
        QCOMPARE(profile::verifyProfileFiles(f.root + QStringLiteral("/missing"), f.pins, verified),
                 ProfileError::BadRoot);
        QCOMPARE(profile::verifyProfileFiles(QString(), f.pins, verified), ProfileError::BadRoot);
    }

    void fixtureRejections()
    {
        using Mutate = std::function<bool(testfixture::ProfileFixture &)>;
        const std::vector<std::pair<Mutate, ProfileError>> cases{
            {[](testfixture::ProfileFixture &f) { return QFile::remove(f.root + QLatin1Char('/') + f.pins.model.path); },
             ProfileError::MissingFile},
            {[](testfixture::ProfileFixture &f) {
                 // Same pins and metadata, different model bytes on disk.
                 return testfixture::writeBytes(f.root, f.pins.model.path, QByteArray(int(f.pins.model.bytes), '\x01'));
             },
             ProfileError::SizeOrHash},
            {[](testfixture::ProfileFixture &f) {
                 // Pins that disagree with the artifact metadata.
                 f.pins.model.sha256 = QString(64, QLatin1Char('a'));
                 return true;
             },
             ProfileError::MetadataMismatch},
            {[](testfixture::ProfileFixture &f) {
                 const QString path = f.root + QLatin1Char('/') + f.pins.stem.path;
                 return QFile::rename(path, path + QStringLiteral(".real"))
                     && QFile::link(path + QStringLiteral(".real"), path);
             },
             ProfileError::MissingFile},
            {[](testfixture::ProfileFixture &f) {
                 const QString build = f.root + QStringLiteral("/build");
                 return QDir().rename(build, f.root + QStringLiteral("/build-real"))
                     && QFile::link(f.root + QStringLiteral("/build-real"), build);
             },
             ProfileError::MissingFile},
            {[](testfixture::ProfileFixture &f) {
                 return f.replacePinned(f.pins.languages[0].controlAff, QByteArray("SET UTF-8\nTRY x\n"))
                     && f.writeMorph();
             },
             ProfileError::ControlMismatch},
            {[](testfixture::ProfileFixture &f) {
                 return f.replacePinned(f.pins.languages[1].dic, QByteArray("1\n\xff\xfe\n")) && f.writeMorph()
                     && f.writeTraining() && f.writeMetadata();
             },
             ProfileError::NotUtf8},
            {[](testfixture::ProfileFixture &f) {
                 QJsonObject m = f.metadataObject();
                 m.insert(QStringLiteral("scope"), QStringLiteral("release_approved=false"));
                 return testfixture::writeBytes(f.root, f.pins.artifactMetadataPath, testfixture::json(m));
             },
             ProfileError::MetadataMismatch},
            {[](testfixture::ProfileFixture &f) {
                 return testfixture::writeBytes(f.root, f.pins.modificationsPath, QByteArray("modified\n"));
             },
             ProfileError::ModificationsMismatch},
            {[](testfixture::ProfileFixture &f) {
                 QJsonObject m = f.morphObject();
                 m.insert(QStringLiteral("release_approved"), true);
                 return f.writeManifest(f.pins.morphManifest.path, testfixture::json(m), f.pins.morphManifest);
             },
             ProfileError::ManifestMismatch},
            {[](testfixture::ProfileFixture &f) {
                 // The manifest bytes differ from the pinned SHA-256.
                 return testfixture::writeBytes(f.root, f.pins.trainingManifest.path, QByteArray("{}"));
             },
             ProfileError::ManifestMismatch},
            {[](testfixture::ProfileFixture &f) {
                 return testfixture::writeBytes(f.root, f.pins.libraryNotice.path, QByteArray("changed\n"));
             },
             ProfileError::NoticeMismatch},
            {[](testfixture::ProfileFixture &f) {
                 return testfixture::writeBytes(f.root, f.pins.languages[0].artifactNoticePath, QByteArray("x\n"));
             },
             ProfileError::NoticeMismatch},
            {[](testfixture::ProfileFixture &f) {
                 QByteArray stem = QByteArray("RSKP") + QByteArray(40, '\0');
                 return f.replacePinned(f.pins.stem, stem) && f.writeMetadata();
             },
             ProfileError::FormatMismatch},
        };
        for (std::size_t i = 0; i < cases.size(); ++i) {
            testfixture::ProfileFixture f;
            QVERIFY(f.build());
            QVERIFY2(cases[i].first(f), QByteArray::number(qulonglong(i)).constData());
            profile::VerifiedProfile verified;
            const ProfileError error = profile::verifyProfileFiles(f.root, f.pins, verified);
            QVERIFY2(error == cases[i].second,
                     (QByteArray("case ") + QByteArray::number(qulonglong(i)) + " got "
                      + QByteArray(profile::profileErrorName(error)))
                         .constData());
        }
    }

    void providerWithoutHunspellOrBadRootFailsClosed()
    {
        ProfileError error = ProfileError::None;
        const QTemporaryDir empty;
        auto provider = profile::LocalEvidenceProvider::load(empty.path(), profile::productionPins(), error);
        QVERIFY(!provider);
        QVERIFY(error != ProfileError::None);
        if (!profile::builtWithHunspell())
            QCOMPARE(error, ProfileError::NotBuilt);
    }

    // Opt-in check of the real local profile (never needed for a clean clone):
    // RASKLAD_TEST_PROFILE_ROOT=<project root>.
    void optionalRealProfile()
    {
        const QString root = qEnvironmentVariable("RASKLAD_TEST_PROFILE_ROOT");
        if (root.isEmpty())
            QSKIP("RASKLAD_TEST_PROFILE_ROOT not set; real local profile not checked");
        profile::VerifiedProfile verified;
        QCOMPARE(profile::verifyProfileFiles(root, profile::productionPins(), verified), ProfileError::None);
        if (!profile::builtWithHunspell())
            QSKIP("built without Hunspell");
        ProfileError error = ProfileError::None;
        auto provider = profile::LocalEvidenceProvider::load(root, profile::productionPins(), error);
        QCOMPARE(error, ProfileError::None);
        QVERIFY(provider);
    }

    // ---- Theme and language ----

    void themeParsing()
    {
        const QByteArray alacritty("# comment\n[colors.primary]\nbackground = \"#1a1b26\"\nforeground = '#a9b1d6'\n"
                                   "[colors.normal]\nred = \"0xf7768e\"\nblue = \"#7aa2f7\" # trailing\n"
                                   "black = \"not a color\"\n[colors.bright]\nblack = \"#414868\"\n");
        const std::optional<ThemePalette> a = parseThemeColors(alacritty);
        QVERIFY(a);
        QCOMPARE(a->background, col("#1a1b26"));
        QCOMPARE(a->foreground, col("#a9b1d6"));
        QCOMPARE(a->accent, col("#7aa2f7"));
        QCOMPARE(a->error, col("#f7768e"));
        QCOMPARE(a->muted, col("#414868"));
        QVERIFY(a->dark);

        const std::optional<ThemePalette> light =
            parseThemeColors(QByteArray("accent = \"#ff8800\"\nbackground = \"#ffffff\"\nforeground = \"#111111\"\n"));
        QVERIFY(light);
        QVERIFY(!light->dark);
        QCOMPARE(light->accent, col("#ff8800"));

        QVERIFY(!parseThemeColors(QByteArray("background = \"#000000\"\n")));
        QVERIFY(!parseThemeColors(QByteArray()));
        QVERIFY(!parseThemeColors(QByteArray(kMaxThemeFileBytes + 1, 'a')));
        QVERIFY(!parseThemeColors(QByteArray("background = \"#00000\"\nforeground = \"#fff\"\n")));
    }

    void localeParsing()
    {
        QCOMPARE(languageFromLocaleConf(QByteArray("LANG=ru_RU.UTF-8\n")), QStringLiteral("ru"));
        QCOMPARE(languageFromLocaleConf(QByteArray("LANG=ru_RU.UTF-8\nLC_MESSAGES=\"en_US.UTF-8\"\n")),
                 QStringLiteral("en"));
        QCOMPARE(languageFromLocaleConf(QByteArray("LANG=en_US.UTF-8\nLC_ALL=ru_RU.UTF-8\n")), QStringLiteral("ru"));
        QCOMPARE(languageFromLocaleConf(QByteArray("# nothing\n")), QString());
        QCOMPARE(languageFromLocaleName(QStringLiteral("de_DE")), QStringLiteral("en"));
        QCOMPARE(languageFromLocaleName(QString()), QString());
    }

    void themeWatcherReloads()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ThemeSources sources;
        sources.themeParent = dir.path();
        sources.themeDirectory = dir.path() + QStringLiteral("/theme");
        sources.localeFiles = {dir.path() + QStringLiteral("/locale.conf")};
        QVERIFY(testfixture::writeBytes(dir.path(), QStringLiteral("theme/colors.toml"),
                                        QByteArray("background = \"#101010\"\nforeground = \"#eeeeee\"\n")));
        QVERIFY(testfixture::writeBytes(dir.path(), QStringLiteral("locale.conf"), QByteArray("LANG=ru_RU.UTF-8\n")));
        ThemeWatcher watcher(sources, QString(), false);
        QVERIFY(watcher.fromDesktopTheme());
        QCOMPARE(watcher.background(), col("#101010"));
        QCOMPARE(watcher.language(), QStringLiteral("ru"));
        QSignalSpy palette(&watcher, &ThemeWatcher::paletteChanged);
        QSignalSpy language(&watcher, &ThemeWatcher::languageChanged);
        QVERIFY(testfixture::writeBytes(dir.path(), QStringLiteral("theme/colors.toml"),
                                        QByteArray("background = \"#fafafa\"\nforeground = \"#202020\"\n")));
        QVERIFY(testfixture::writeBytes(dir.path(), QStringLiteral("locale.conf"), QByteArray("LANG=en_GB.UTF-8\n")));
        watcher.reload();
        QCOMPARE(palette.count(), 1);
        QCOMPARE(language.count(), 1);
        QVERIFY(!watcher.dark());
        QCOMPARE(watcher.language(), QStringLiteral("en"));
        watcher.reload();
        QCOMPARE(palette.count(), 1);

        ThemeWatcher forced(sources, QStringLiteral("ru"), false);
        QCOMPARE(forced.language(), QStringLiteral("ru"));
        ThemeSources missing;
        missing.themeDirectory = dir.path() + QStringLiteral("/absent");
        ThemeWatcher fallback(missing, QStringLiteral("en"), false);
        QVERIFY(!fallback.fromDesktopTheme());
        QCOMPARE(fallback.background(), defaultPalette().background);
    }

    // ---- Options ----

    void omarchyFilesFollowAtomicReplacementAndCreation()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ThemeSources sources;
        sources.themeParent = dir.path() + QStringLiteral("/state/current");
        sources.themeDirectory = sources.themeParent + QStringLiteral("/theme");
        sources.languageFile = dir.path() + QStringLiteral("/settings/omarchy/language");
        sources.localeFiles = {dir.path() + QStringLiteral("/locale.conf")};
        QVERIFY(testfixture::writeBytes(dir.path(), QStringLiteral("locale.conf"), QByteArray("LANG=en_US.UTF-8\n")));
        ThemeWatcher watcher(sources, QString(), true);
        QCOMPARE(watcher.language(), QStringLiteral("en"));
        QCOMPARE(watcher.radius(), 0);
        QVERIFY(testfixture::writeBytes(dir.path(), QStringLiteral("state/current/theme/colors.toml"),
                                       QByteArray("accent = '#FFD60A'\n")));
        QVERIFY(testfixture::writeBytes(dir.path(), QStringLiteral("settings/omarchy/language"), QByteArray("ru\n")));
        QTRY_COMPARE(watcher.language(), QStringLiteral("ru"));
        QTRY_VERIFY(watcher.fromDesktopTheme());
        QCOMPARE(watcher.accent(), col("#FFD60A"));
        auto replace = [](const QString &path, const QByteArray &bytes) {
            QSaveFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
        };
        QVERIFY(replace(sources.languageFile, QByteArray("en\n")));
        QVERIFY(replace(sources.themeDirectory + QStringLiteral("/colors.toml"), QByteArray("accent = '#aaccff'\n")));
        QTRY_COMPARE(watcher.language(), QStringLiteral("en"));
        QTRY_COMPARE(watcher.accent(), col("#aaccff"));
        QVERIFY(replace(sources.languageFile, QByteArray("ru\n")));
        QTRY_COMPARE(watcher.language(), QStringLiteral("ru"));
    }

    void options()
    {
        const QString app = QStringLiteral("rasklad");
        AppOptionsResult r = parseAppOptions({app});
        QVERIFY(r.ok);
        QVERIFY(r.options.localProfileRoot.isEmpty());
        r = parseAppOptions({app, QStringLiteral("--language"), QStringLiteral("ru"), QStringLiteral("--local-profile"),
                             QStringLiteral("/tmp/root")});
        QVERIFY(r.ok);
        QCOMPARE(r.options.language, QStringLiteral("ru"));
        QCOMPARE(r.options.localProfileRoot, QStringLiteral("/tmp/root"));
        QVERIFY(parseAppOptions({app, QStringLiteral("--help")}).options.help);
        QVERIFY(parseAppOptions({app, QStringLiteral("--version")}).options.version);
        QVERIFY(parseAppOptions({app, QStringLiteral("--self-check")}).options.selfCheck);
        QVERIFY(!parseAppOptions({app, QStringLiteral("--language"), QStringLiteral("de")}).ok);
        QVERIFY(!parseAppOptions({app, QStringLiteral("--language")}).ok);
        QVERIFY(!parseAppOptions({app, QStringLiteral("--language"), QStringLiteral("en"), QStringLiteral("--language"),
                                  QStringLiteral("en")})
                     .ok);
        QVERIFY(!parseAppOptions({app, QStringLiteral("--local-profile"), QStringLiteral("--language")}).ok);
        QVERIFY(!parseAppOptions({app, QStringLiteral("some text")}).ok);
        QVERIFY(!parseAppOptions({app, QStringLiteral("--text"), QStringLiteral("x")}).ok);
        const AppOptionsResult bad = parseAppOptions({app, QStringLiteral("secret-token")});
        QVERIFY(!bad.error.contains(QStringLiteral("secret")));
        QVERIFY(usageText().contains(QStringLiteral("--local-profile")));
        QVERIFY(versionText().startsWith(QStringLiteral("rasklad ")));
    }

    // ---- Translations ----

    void jsonTranslatorStrict()
    {
        JsonTranslator t;
        QVERIFY(t.loadJson(QByteArray("{\"schema\":1,\"language\":\"ru\",\"messages\":{\"Apply\":\"Применить\"}}"),
                           QStringLiteral("ru")));
        QCOMPARE(t.translate("Main", "Apply", nullptr, -1), u8("Применить"));
        QVERIFY(t.translate("Main", "Unknown", nullptr, -1).isNull());
        QVERIFY(!t.loadJson(QByteArray("{\"schema\":1,\"language\":\"en\",\"messages\":{}}"), QStringLiteral("ru")));
        QVERIFY(t.isEmpty());
        QVERIFY(!t.loadJson(QByteArray("{\"schema\":1,\"language\":\"ru\",\"messages\":{\"A\":1}}"),
                            QStringLiteral("ru")));
        QVERIFY(!t.loadJson(QByteArray("{\"schema\":2,\"language\":\"ru\",\"messages\":{}}"), QStringLiteral("ru")));
        QVERIFY(!t.loadJson(QByteArray(kMaxTranslationBytes + 1, ' '), QStringLiteral("ru")));
    }

    void russianTableCoversEveryQmlString()
    {
        const QByteArray qml = readSource("src/Main.qml");
        const QByteArray table = readSource("src/i18n/ru.json");
        QVERIFY(!qml.isEmpty());
        JsonTranslator t;
        QVERIFY(t.loadJson(table, QStringLiteral("ru")));
        const QRegularExpression pattern(QStringLiteral("qsTr\\(\"((?:[^\"\\\\]|\\\\.)*)\"\\)"));
        QSet<QString> used;
        auto it = pattern.globalMatch(QString::fromUtf8(qml));
        while (it.hasNext()) {
            const QByteArray literal = it.next().captured(1).toUtf8();
            const auto decoded = QJsonDocument::fromJson(QByteArray("[\"") + literal + QByteArray("\"]"));
            QVERIFY(decoded.isArray());
            used.insert(decoded.array().at(0).toString());
        }
        QVERIFY(used.size() > 50);
        for (const QString &source : used) {
            QVERIFY2(!source.contains(QLatin1Char('\\')), "qsTr literals must not use escapes");
            const QByteArray utf8 = source.toUtf8();
            QVERIFY2(t.contains(source), utf8.constData());
            const QString translated = t.translate("Main", utf8.constData(), nullptr, -1);
            for (const char *placeholder : {"%1", "%2"}) {
                const QString p = QString::fromLatin1(placeholder);
                QVERIFY2(source.count(p) == translated.count(p), utf8.constData());
            }
        }
        QCOMPARE(t.size(), qsizetype(used.size()));
    }
};

QTEST_GUILESS_MAIN(AppTests)
#include "app_tests.moc"
