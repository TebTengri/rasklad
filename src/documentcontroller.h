#pragma once

// Owned plain-text document of the manual-preview window (SYS-239).
//
// The user types or pastes into the app's own editor. Nothing is read from
// other windows, the clipboard or input devices. State changes only through
// explicit calls from the UI:
//   setSourceText — accepted edit (rejected, without truncation, above
//                   kMaxInputUtf16);
//   preview       — forced conversion of the whole text in the explicit
//                   direction (no dictionaries needed);
//   checkWord     — optional bounded single-word analysis through
//                   AnalysisService; Correct and Suggest become a preview;
//   apply / undo  — one atomic visible replacement and one transient undo
//                   level. A bulk edit (more than one UTF-16 unit removed or
//                   inserted: select-all overwrite, cut, paste, deleting a
//                   selection) replaces that level with its own record;
//                   ordinary single-unit typing or deleting drops it. The
//                   editor's native document history stays disabled, so the
//                   controller holds at most one record of two documents;
//   clear         — drops text, preview, undo and analysis result.
// Every preview — including a router Correct — is applied only by apply().
// A preview is valid only for the snapshot (revision, field epoch, direction
// generation, provider generation) it was computed for.
//
// Nothing here is logged, persisted or sent anywhere. Notices are fixed
// codes; the UI maps them to translated text.

#include "analysisservice.h"
#include "layoutconverter.h"

#include <QObject>
#include <QString>

#include <optional>

namespace rasklad::app {

inline constexpr int kMaxInputUtf16 = 65536;
inline constexpr int kMaxCheckUtf16 = 64;

class DocumentController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString sourceText READ sourceText NOTIFY sourceTextChanged)
    Q_PROPERTY(int sourceLength READ sourceLength NOTIFY sourceTextChanged)
    Q_PROPERTY(int maxInputLength READ maxInputLength CONSTANT)
    Q_PROPERTY(int direction READ direction WRITE setDirection NOTIFY directionChanged)
    Q_PROPERTY(QString previewText READ previewText NOTIFY previewChanged)
    Q_PROPERTY(QString previewKind READ previewKind NOTIFY previewChanged)
    Q_PROPERTY(bool canApply READ canApply NOTIFY previewChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(QString copyKind READ copyKind NOTIFY copyKindChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QString analysisState READ analysisState NOTIFY analysisChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY analysisChanged)
    Q_PROPERTY(QString checkAction READ checkAction NOTIFY checkChanged)
    Q_PROPERTY(QString checkReason READ checkReason NOTIFY checkChanged)
    Q_PROPERTY(QString checkSafeAction READ checkSafeAction NOTIFY checkChanged)

public:
    enum Direction { UsToRu = 0, RuToUs = 1 };
    Q_ENUM(Direction)

    // analysis may be null (manual-only).
    explicit DocumentController(AnalysisService *analysis = nullptr, QObject *parent = nullptr);
    ~DocumentController() override;

    [[nodiscard]] QString sourceText() const { return m_source; }
    [[nodiscard]] int sourceLength() const { return int(m_source.size()); }
    [[nodiscard]] int maxInputLength() const { return kMaxInputUtf16; }
    [[nodiscard]] int direction() const { return int(m_direction); }
    void setDirection(int direction);
    [[nodiscard]] QString previewText() const;
    [[nodiscard]] QString previewKind() const;
    [[nodiscard]] bool canApply() const;
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] QString copyKind() const;
    [[nodiscard]] QString notice() const { return m_notice; }
    [[nodiscard]] QString analysisState() const;
    [[nodiscard]] bool checking() const { return m_awaitingId != 0; }
    [[nodiscard]] QString checkAction() const { return m_checkAction; }
    [[nodiscard]] QString checkReason() const { return m_checkReason; }
    [[nodiscard]] QString checkSafeAction() const { return m_checkSafeAction; }

    [[nodiscard]] AnalysisSnapshot currentSnapshot() const;

    // Accepts the editor text; false (nothing changes) above the limit.
    Q_INVOKABLE bool setSourceText(const QString &text);
    Q_INVOKABLE bool preview();
    // Selection [start, end) in UTF-16 units; empty selection means the word
    // at the cursor (start).
    Q_INVOKABLE bool checkWord(int selectionStart, int selectionEnd);
    Q_INVOKABLE bool apply();
    Q_INVOKABLE bool undo();
    // Preview if a current preview exists, otherwise the accepted text.
    Q_INVOKABLE QString copyPayload() const;
    Q_INVOKABLE void noteCopied();
    Q_INVOKABLE void noteCopyFailed();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void dismissNotice();

Q_SIGNALS:
    void sourceTextChanged();
    // An edit was rejected; the editor must show sourceText again.
    void sourceRejected();
    void directionChanged();
    void previewChanged();
    void undoChanged();
    void copyKindChanged();
    void noticeChanged();
    void analysisChanged();
    void checkChanged();

private:
    enum class PreviewKind { None, Manual, Check };

    struct Preview {
        PreviewKind kind = PreviewKind::None;
        QString text;
        AnalysisSnapshot snapshot;
    };

    enum class UndoKind { Apply, BulkEdit };

    struct UndoRecord {
        UndoKind kind = UndoKind::Apply;
        QString before;
        QString after;
        quint64 revisionAfter = 0;
        quint64 fieldEpoch = 0;
    };

    [[nodiscard]] bool previewIsCurrent() const;
    [[nodiscard]] LayoutDirection layoutDirection() const;
    void setNotice(const QString &code);
    // Drops preview/check state; stale notice only if something was visible.
    void invalidateDerived(bool announce);
    void clearPreview();
    void clearCheck();
    void clearUndo();
    void onAnalysisStateChanged();
    void onCheckResult(const CheckResult &result);

    AnalysisService *m_analysis = nullptr;
    QString m_source;
    Direction m_direction = UsToRu;
    quint64 m_revision = 0;
    quint64 m_fieldEpoch = 0;
    quint64 m_layoutGeneration = 0;
    std::optional<Preview> m_preview;
    std::optional<UndoRecord> m_undo;
    QString m_notice;
    quint64 m_nextRequestId = 0;
    quint64 m_awaitingId = 0;
    // Span [start, start + length) of the word being checked, in m_source.
    qsizetype m_checkStart = 0;
    qsizetype m_checkLength = 0;
    QString m_checkAction;
    QString m_checkReason;
    QString m_checkSafeAction;
};

// The word for checkWord, or nullopt: the trimmed selection (no whitespace
// inside), or the whitespace-delimited word around the cursor. At most
// kMaxCheckUtf16 units. Exposed for tests.
struct WordSpan {
    qsizetype start = 0;
    qsizetype length = 0;
};
[[nodiscard]] std::optional<WordSpan> findCheckWord(const QString &text, int selectionStart, int selectionEnd);

// Size of the changed region between two texts: the common prefix and the
// non-overlapping common suffix are excluded. Exposed for tests.
struct EditExtent {
    qsizetype removed = 0;
    qsizetype inserted = 0;
};
[[nodiscard]] EditExtent editExtent(const QString &before, const QString &after);
// More than one UTF-16 unit removed or inserted.
[[nodiscard]] bool isBulkEdit(const EditExtent &extent);

} // namespace rasklad::app
