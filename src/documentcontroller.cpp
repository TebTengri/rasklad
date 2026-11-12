#include "documentcontroller.h"

#include <utility>

namespace rasklad::app {

namespace {

QString latin(const char *text)
{
    return QString::fromLatin1(text);
}

} // namespace

std::optional<WordSpan> findCheckWord(const QString &text, int selectionStart, int selectionEnd)
{
    const qsizetype size = text.size();
    qsizetype a = qBound(qsizetype(0), qsizetype(selectionStart), size);
    qsizetype b = qBound(qsizetype(0), qsizetype(selectionEnd), size);
    if (a > b)
        std::swap(a, b);
    if (a != b) {
        while (a < b && text.at(a).isSpace())
            ++a;
        while (b > a && text.at(b - 1).isSpace())
            --b;
        if (a == b)
            return std::nullopt;
        for (qsizetype i = a; i < b; ++i) {
            if (text.at(i).isSpace())
                return std::nullopt;
        }
        return WordSpan{a, b - a};
    }
    qsizetype left = a;
    while (left > 0 && !text.at(left - 1).isSpace())
        --left;
    qsizetype right = a;
    while (right < size && !text.at(right).isSpace())
        ++right;
    if (left == right)
        return std::nullopt;
    return WordSpan{left, right - left};
}

EditExtent editExtent(const QString &before, const QString &after)
{
    const qsizetype oldSize = before.size();
    const qsizetype newSize = after.size();
    const qsizetype common = qMin(oldSize, newSize);
    qsizetype prefix = 0;
    while (prefix < common && before.at(prefix) == after.at(prefix))
        ++prefix;
    // The suffix may not overlap the prefix in either text.
    qsizetype suffix = 0;
    while (suffix < common - prefix && before.at(oldSize - 1 - suffix) == after.at(newSize - 1 - suffix))
        ++suffix;
    return EditExtent{oldSize - prefix - suffix, newSize - prefix - suffix};
}

bool isBulkEdit(const EditExtent &extent)
{
    return extent.removed > 1 || extent.inserted > 1;
}

DocumentController::DocumentController(AnalysisService *analysis, QObject *parent)
    : QObject(parent)
    , m_analysis(analysis)
{
    if (m_analysis) {
        connect(m_analysis, &AnalysisService::stateChanged, this, &DocumentController::onAnalysisStateChanged);
        connect(m_analysis, &AnalysisService::resultReady, this, &DocumentController::onCheckResult);
    }
}

DocumentController::~DocumentController() = default;

AnalysisSnapshot DocumentController::currentSnapshot() const
{
    AnalysisSnapshot s;
    s.revision = m_revision;
    s.fieldEpoch = m_fieldEpoch;
    s.layoutGeneration = m_layoutGeneration;
    s.profileGeneration = m_analysis ? m_analysis->generation() : 0;
    return s;
}

bool DocumentController::previewIsCurrent() const
{
    return m_preview.has_value() && m_preview->snapshot == currentSnapshot();
}

LayoutDirection DocumentController::layoutDirection() const
{
    return m_direction == RuToUs ? LayoutDirection::RuToUs : LayoutDirection::UsToRu;
}

QString DocumentController::previewText() const
{
    return m_preview ? m_preview->text : QString();
}

QString DocumentController::previewKind() const
{
    if (!m_preview)
        return latin("none");
    return m_preview->kind == PreviewKind::Check ? latin("check") : latin("manual");
}

bool DocumentController::canApply() const
{
    return previewIsCurrent();
}

bool DocumentController::canUndo() const
{
    return m_undo.has_value() && m_undo->revisionAfter == m_revision && m_undo->fieldEpoch == m_fieldEpoch;
}

QString DocumentController::copyKind() const
{
    if (previewIsCurrent())
        return latin("preview");
    return m_source.isEmpty() ? latin("none") : latin("text");
}

QString DocumentController::analysisState() const
{
    if (!m_analysis)
        return latin("disabled");
    switch (m_analysis->state()) {
    case AnalysisService::State::Disabled:
        return latin("disabled");
    case AnalysisService::State::Loading:
        return latin("loading");
    case AnalysisService::State::Ready:
        return latin("ready");
    case AnalysisService::State::Unavailable:
        return latin("unavailable");
    }
    return latin("unavailable");
}

void DocumentController::setNotice(const QString &code)
{
    if (m_notice == code)
        return;
    m_notice = code;
    Q_EMIT noticeChanged();
}

void DocumentController::dismissNotice()
{
    setNotice(QString());
}

void DocumentController::clearPreview()
{
    if (!m_preview)
        return;
    m_preview.reset();
    Q_EMIT previewChanged();
    Q_EMIT copyKindChanged();
}

void DocumentController::clearCheck()
{
    m_checkStart = 0;
    m_checkLength = 0;
    if (m_checkAction.isEmpty() && m_checkReason.isEmpty() && m_checkSafeAction.isEmpty())
        return;
    m_checkAction = QString();
    m_checkReason = QString();
    m_checkSafeAction = QString();
    Q_EMIT checkChanged();
}

void DocumentController::clearUndo()
{
    if (!m_undo)
        return;
    m_undo.reset();
    Q_EMIT undoChanged();
}

void DocumentController::invalidateDerived(bool announce)
{
    if (m_analysis)
        m_analysis->cancelPending();
    const bool visible = m_preview.has_value() || m_awaitingId != 0 || !m_checkAction.isEmpty();
    clearPreview();
    clearCheck();
    if (m_awaitingId != 0) {
        m_awaitingId = 0;
        Q_EMIT analysisChanged();
    }
    if (announce && visible)
        setNotice(latin("previewStale"));
}

bool DocumentController::setSourceText(const QString &text)
{
    if (text.size() > kMaxInputUtf16) {
        // No truncation: the accepted text stays unchanged and the editor resets.
        setNotice(latin("inputTooLong"));
        Q_EMIT sourceRejected();
        return false;
    }
    if (text == m_source)
        return true;
    // Both texts are at most kMaxInputUtf16 units: the comparison is bounded.
    const bool bulk = isBulkEdit(editExtent(m_source, text));
    QString before = m_source;
    m_source = text;
    ++m_revision;
    if (bulk) {
        // One level only: the bulk edit replaces any earlier record (including
        // an Apply), so Undo never reaches past the newest bulk revision.
        UndoRecord record;
        record.kind = UndoKind::BulkEdit;
        record.before = std::move(before);
        record.after = m_source;
        record.revisionAfter = m_revision;
        record.fieldEpoch = m_fieldEpoch;
        m_undo = std::move(record);
        Q_EMIT undoChanged();
    } else {
        before = QString();
        clearUndo();
    }
    setNotice(QString());
    invalidateDerived(true);
    Q_EMIT sourceTextChanged();
    Q_EMIT copyKindChanged();
    return true;
}

void DocumentController::setDirection(int direction)
{
    if (direction != UsToRu && direction != RuToUs)
        return;
    if (Direction(direction) == m_direction)
        return;
    m_direction = Direction(direction);
    ++m_layoutGeneration;
    invalidateDerived(true);
    Q_EMIT directionChanged();
}

bool DocumentController::preview()
{
    if (m_analysis)
        m_analysis->cancelPending();
    if (m_awaitingId != 0) {
        m_awaitingId = 0;
        Q_EMIT analysisChanged();
    }
    clearCheck();
    if (m_source.isEmpty()) {
        clearPreview();
        setNotice(latin("nothingToConvert"));
        return false;
    }
    QString converted = convertLayout(m_source, layoutDirection());
    if (converted == m_source) {
        clearPreview();
        setNotice(latin("conversionNoChange"));
        return false;
    }
    m_preview = Preview{PreviewKind::Manual, std::move(converted), currentSnapshot()};
    setNotice(QString());
    Q_EMIT previewChanged();
    Q_EMIT copyKindChanged();
    return true;
}

bool DocumentController::checkWord(int selectionStart, int selectionEnd)
{
    if (!m_analysis || m_analysis->state() != AnalysisService::State::Ready) {
        setNotice(latin("analysisUnavailable"));
        return false;
    }
    const std::optional<WordSpan> span = findCheckWord(m_source, selectionStart, selectionEnd);
    if (!span) {
        setNotice(latin("checkNeedsWord"));
        return false;
    }
    if (span->length > kMaxCheckUtf16) {
        setNotice(latin("checkWordTooLong"));
        return false;
    }
    invalidateDerived(false);
    CheckRequest request;
    request.id = ++m_nextRequestId;
    request.token = m_source.mid(span->start, span->length);
    request.layout = m_direction == RuToUs ? routing::InputLayout::KnownRu : routing::InputLayout::KnownUs;
    // The app's own plain-text editor: the user explicitly asked for this word.
    request.field = routing::FieldContext::Prose;
    request.snapshot = currentSnapshot();
    // Report unsupported/short words immediately without putting their text
    // in the worker queue. The same pure router defines these early gates.
    const routing::RouteRequest gateRequest{request.field, request.layout, QStringView(request.token)};
    const auto gated = routing::route(gateRequest, routing::RouteProfile::RequestedHybrid, nullptr);
    if (gated.reason != routing::RouteReason::EvidenceUnavailable) {
        m_checkAction = latin(routing::routeActionName(gated.action));
        m_checkSafeAction = m_checkAction;
        m_checkReason = latin(routing::routeReasonName(gated.reason));
        setNotice(QString());
        Q_EMIT checkChanged();
        return true;
    }
    const quint64 id = request.id;
    const auto submitted = m_analysis->submit(std::move(request));
    if (submitted == AnalysisService::Submit::NotReady || submitted == AnalysisService::Submit::Rejected) {
        setNotice(latin("analysisUnavailable"));
        return false;
    }
    m_checkStart = span->start;
    m_checkLength = span->length;
    m_awaitingId = id;
    setNotice(QString());
    Q_EMIT analysisChanged();
    return true;
}

void DocumentController::onCheckResult(const CheckResult &result)
{
    // Replaced or cancelled requests are ignored.
    if (result.id == 0 || result.id != m_awaitingId)
        return;
    m_awaitingId = 0;
    Q_EMIT analysisChanged();
    if (result.snapshot != currentSnapshot()) {
        setNotice(latin("previewStale"));
        return;
    }
    m_checkAction = latin(routing::routeActionName(result.requested.action));
    m_checkReason = latin(routing::routeReasonName(result.requested.reason));
    m_checkSafeAction = latin(routing::routeActionName(result.safe.action));
    Q_EMIT checkChanged();
    const bool candidate = result.requested.action == routing::RouteAction::Correct
        || result.requested.action == routing::RouteAction::Suggest;
    if (!candidate || result.requested.candidate.isEmpty() || m_checkLength <= 0
        || m_checkStart + m_checkLength > m_source.size())
        return;
    QString text = m_source;
    text.replace(m_checkStart, m_checkLength, result.requested.candidate);
    if (text == m_source || text.size() > kMaxInputUtf16)
        return;
    m_preview = Preview{PreviewKind::Check, std::move(text), currentSnapshot()};
    Q_EMIT previewChanged();
    Q_EMIT copyKindChanged();
}

bool DocumentController::apply()
{
    if (!m_preview) {
        setNotice(latin("nothingToApply"));
        return false;
    }
    if (!previewIsCurrent()) {
        invalidateDerived(true);
        return false;
    }
    UndoRecord record;
    record.kind = UndoKind::Apply;
    record.before = m_source;
    record.after = m_preview->text;
    record.fieldEpoch = m_fieldEpoch;
    m_source = m_preview->text;
    ++m_revision;
    record.revisionAfter = m_revision;
    m_undo = std::move(record);
    invalidateDerived(false);
    setNotice(latin("applied"));
    Q_EMIT undoChanged();
    Q_EMIT sourceTextChanged();
    Q_EMIT copyKindChanged();
    return true;
}

bool DocumentController::undo()
{
    if (!m_undo) {
        setNotice(latin("undoUnavailable"));
        return false;
    }
    if (m_undo->revisionAfter != m_revision || m_undo->fieldEpoch != m_fieldEpoch || m_source != m_undo->after) {
        clearUndo();
        setNotice(latin("undoStale"));
        return false;
    }
    const bool bulkEdit = m_undo->kind == UndoKind::BulkEdit;
    m_source = m_undo->before;
    ++m_revision;
    clearUndo();
    invalidateDerived(false);
    setNotice(bulkEdit ? latin("undoneEdit") : latin("undone"));
    Q_EMIT sourceTextChanged();
    Q_EMIT copyKindChanged();
    return true;
}

QString DocumentController::copyPayload() const
{
    return previewIsCurrent() ? m_preview->text : m_source;
}

void DocumentController::noteCopied()
{
    const QString kind = copyKind();
    if (kind == QLatin1String("preview"))
        setNotice(latin("copiedPreview"));
    else if (kind == QLatin1String("text"))
        setNotice(latin("copiedText"));
    else
        setNotice(latin("nothingToCopy"));
}

void DocumentController::noteCopyFailed()
{
    setNotice(latin("copyFailed"));
}

void DocumentController::clear()
{
    m_source = QString();
    ++m_revision;
    ++m_fieldEpoch;
    clearUndo();
    invalidateDerived(false);
    setNotice(latin("cleared"));
    Q_EMIT sourceTextChanged();
    Q_EMIT copyKindChanged();
}

void DocumentController::onAnalysisStateChanged()
{
    // Provider generation changed: every preview and pending check is stale.
    invalidateDerived(true);
    Q_EMIT analysisChanged();
}

} // namespace rasklad::app
