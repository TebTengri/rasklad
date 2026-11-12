#pragma once

// Owner of the optional word-analysis provider (SYS-239).
//
// One worker thread is the single processing owner of the provider: the
// provider is created (loaded and verified), queried and destroyed there, so
// Hunspell is never called concurrently. The GUI thread only submits
// requests and receives results; it never blocks on analysis except during
// shutdown, which joins the worker.
//
// Work is request-by-request (no polling, no timers): at most one request is
// in flight and at most one newer request waits; a newer request replaces the
// waiting one. Every result carries the snapshot of its request; the caller
// discards results whose snapshot is no longer current. Tokens live only in
// memory for the duration of a request; nothing is logged or written.

#include "decisionrouter.h"
#include "profileverify.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

class QThread;

namespace rasklad::app {

class AnalysisWorker;

// Document state a request or preview was computed for.
struct AnalysisSnapshot {
    quint64 revision = 0;
    quint64 fieldEpoch = 0;
    quint64 layoutGeneration = 0;
    quint64 profileGeneration = 0;

    friend bool operator==(const AnalysisSnapshot &a, const AnalysisSnapshot &b)
    {
        return a.revision == b.revision && a.fieldEpoch == b.fieldEpoch && a.layoutGeneration == b.layoutGeneration
            && a.profileGeneration == b.profileGeneration;
    }
    friend bool operator!=(const AnalysisSnapshot &a, const AnalysisSnapshot &b) { return !(a == b); }
};

struct CheckRequest {
    quint64 id = 0;
    QString token;
    routing::InputLayout layout = routing::InputLayout::Unknown;
    routing::FieldContext field = routing::FieldContext::Unknown;
    AnalysisSnapshot snapshot;
};

struct CheckResult {
    quint64 id = 0;
    AnalysisSnapshot snapshot;
    // Conservative automatic profile and the explicit-request profile,
    // computed from one evidence lookup.
    routing::RouteDecision safe;
    routing::RouteDecision requested;
};

// Runs on the worker thread. Returns nullptr and an error category on failure.
using ProviderFactory = std::function<std::unique_ptr<routing::EvidenceSource>(profile::ProfileError &)>;

class AnalysisService final : public QObject
{
    Q_OBJECT

public:
    enum class State { Disabled, Loading, Ready, Unavailable };
    Q_ENUM(State)

    enum class Submit { Started, Queued, ReplacedQueued, NotReady, Rejected };
    Q_ENUM(Submit)

    explicit AnalysisService(QObject *parent = nullptr);
    ~AnalysisService() override;

    AnalysisService(const AnalysisService &) = delete;
    AnalysisService &operator=(const AnalysisService &) = delete;

    // Starts the worker and loads asynchronously. Only once per service.
    void start(ProviderFactory factory);
    // Releases the provider on the worker thread, then joins it. Idempotent.
    void shutdown();

    [[nodiscard]] State state() const { return m_state; }
    // Incremented on every state change (provider generation).
    [[nodiscard]] quint64 generation() const { return m_generation; }
    [[nodiscard]] profile::ProfileError loadError() const { return m_loadError; }
    [[nodiscard]] bool busy() const { return m_inflight; }
    [[nodiscard]] bool hasQueued() const { return m_queued.has_value(); }

    Submit submit(CheckRequest request);
    // Release waiting text when the document changes. A running bounded
    // query may finish, but its snapshot can no longer be applied.
    void cancelPending();

Q_SIGNALS:
    void stateChanged();
    // GUI thread. Only for requests that ran; replaced requests never answer.
    void resultReady(const rasklad::app::CheckResult &result);

private:
    friend class AnalysisWorker;
    void setState(State state);
    void dispatch(CheckRequest request);
    void handleLoaded(bool ok, profile::ProfileError error);
    void handleResult(const CheckResult &result);

    State m_state = State::Disabled;
    quint64 m_generation = 0;
    profile::ProfileError m_loadError = profile::ProfileError::None;
    bool m_inflight = false;
    bool m_started = false;
    bool m_stopping = false;
    std::optional<CheckRequest> m_queued;
    QThread *m_thread = nullptr;
    // Deleted on its own thread by finished->deleteLater; the guard is read
    // only while that thread cannot delete it (running worker or after join).
    QPointer<AnalysisWorker> m_worker;
};

// Pure evaluation used by the worker (exposed for tests): one evidence lookup,
// both profiles. A null source gives EvidenceUnavailable/unqueried decisions.
[[nodiscard]] CheckResult evaluateRequest(const CheckRequest &request, const routing::EvidenceSource *source);

} // namespace rasklad::app

Q_DECLARE_METATYPE(rasklad::app::CheckResult)
