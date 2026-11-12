#include "analysisservice.h"

#include <QMetaObject>
#include <QThread>

#include <utility>

namespace rasklad::app {

namespace {

// Calls the inner source at most once (both profiles share one lookup).
class MemoSource final : public routing::EvidenceSource
{
public:
    explicit MemoSource(const routing::EvidenceSource *inner)
        : m_inner(inner)
    {
    }

    [[nodiscard]] std::optional<routing::TokenEvidence> evidence(const routing::EvidenceQuery &query) const override
    {
        if (!m_done) {
            m_value = m_inner ? m_inner->evidence(query) : std::nullopt;
            m_done = true;
        }
        return m_value;
    }

private:
    const routing::EvidenceSource *m_inner = nullptr;
    mutable bool m_done = false;
    mutable std::optional<routing::TokenEvidence> m_value;
};

} // namespace

CheckResult evaluateRequest(const CheckRequest &request, const routing::EvidenceSource *source)
{
    CheckResult result;
    result.id = request.id;
    result.snapshot = request.snapshot;
    routing::RouteRequest route;
    route.field = request.field;
    route.layout = request.layout;
    route.token = request.token;
    MemoSource memo(source);
    const routing::EvidenceSource *effective = source ? &memo : nullptr;
    result.requested = routing::route(route, routing::RouteProfile::RequestedHybrid, effective);
    result.safe = routing::route(route, routing::RouteProfile::SafeConfirmed, effective);
    return result;
}

// Lives on the worker thread; owns the provider there.
class AnalysisWorker final : public QObject
{
public:
    explicit AnalysisWorker(AnalysisService *service)
        : m_service(service)
    {
    }

    void load(const ProviderFactory &factory)
    {
        profile::ProfileError error = profile::ProfileError::None;
        std::unique_ptr<routing::EvidenceSource> provider;
        if (factory)
            provider = factory(error);
        if (!provider && error == profile::ProfileError::None)
            error = profile::ProfileError::EngineFailed;
        m_provider = std::move(provider);
        const bool ok = m_provider != nullptr;
        AnalysisService *service = m_service;
        QMetaObject::invokeMethod(
            service, [service, ok, error]() { service->handleLoaded(ok, error); }, Qt::QueuedConnection);
    }

    void run(const CheckRequest &request)
    {
        const CheckResult result = evaluateRequest(request, m_provider.get());
        AnalysisService *service = m_service;
        QMetaObject::invokeMethod(
            service, [service, result]() { service->handleResult(result); }, Qt::QueuedConnection);
    }

    void release() { m_provider.reset(); }

private:
    AnalysisService *m_service = nullptr;
    std::unique_ptr<routing::EvidenceSource> m_provider;
};

AnalysisService::AnalysisService(QObject *parent)
    : QObject(parent)
{
}

AnalysisService::~AnalysisService()
{
    shutdown();
}

void AnalysisService::start(ProviderFactory factory)
{
    if (m_started)
        return;
    m_started = true;
    m_thread = new QThread();
    m_thread->setObjectName(QStringLiteral("rasklad-analysis"));
    AnalysisWorker *worker = new AnalysisWorker(this);
    m_worker = worker;
    worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);
    m_thread->start();
    setState(State::Loading);
    QMetaObject::invokeMethod(
        worker, [worker, f = std::move(factory)]() { worker->load(f); }, Qt::QueuedConnection);
}

void AnalysisService::shutdown()
{
    if (!m_thread)
        return;
    m_stopping = true;
    m_queued.reset();
    if (m_thread->isRunning()) {
        if (AnalysisWorker *worker = m_worker.data()) {
            // Waits for an in-flight load or request, then destroys the
            // provider on its owner thread.
            QMetaObject::invokeMethod(worker, [worker]() { worker->release(); }, Qt::BlockingQueuedConnection);
        }
        m_thread->quit();
        m_thread->wait();
    }
    // Normally finished->deleteLater has already destroyed the worker on its
    // own thread and cleared the guard. If the thread never ran (or deferred
    // deletion did not happen), the worker is still alive, holds no provider
    // created by a running thread, and no thread can touch it any more.
    if (AnalysisWorker *leftover = m_worker.data())
        delete leftover;
    m_worker = nullptr;
    delete m_thread;
    m_thread = nullptr;
    m_inflight = false;
    setState(State::Disabled);
}

AnalysisService::Submit AnalysisService::submit(CheckRequest request)
{
    const auto layout = routing::sourceLayoutOf(request.layout);
    if (request.field != routing::FieldContext::Prose || !layout) {
        cancelPending();
        return Submit::Rejected;
    }
    const auto canonical = canonicalizeToken(QStringView(request.token), *layout);
    if (canonical.status != CanonicalStatus::Ok || canonical.keys.size() < routing::kMinRouteKeys) {
        cancelPending();
        return Submit::Rejected;
    }
    if (m_state != State::Ready || !m_worker)
        return Submit::NotReady;
    if (!m_inflight) {
        dispatch(std::move(request));
        return Submit::Started;
    }
    const bool replaced = m_queued.has_value();
    m_queued = std::move(request);
    return replaced ? Submit::ReplacedQueued : Submit::Queued;
}

void AnalysisService::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    ++m_generation;
    Q_EMIT stateChanged();
}

void AnalysisService::cancelPending()
{
    m_queued.reset();
}

void AnalysisService::dispatch(CheckRequest request)
{
    AnalysisWorker *worker = m_worker.data();
    if (!worker)
        return;
    m_inflight = true;
    QMetaObject::invokeMethod(
        worker, [worker, r = std::move(request)]() { worker->run(r); }, Qt::QueuedConnection);
}

void AnalysisService::handleLoaded(bool ok, profile::ProfileError error)
{
    if (m_stopping || m_state != State::Loading)
        return;
    m_loadError = ok ? profile::ProfileError::None : error;
    setState(ok ? State::Ready : State::Unavailable);
}

void AnalysisService::handleResult(const CheckResult &result)
{
    if (m_stopping)
        return;
    m_inflight = false;
    if (m_queued && m_state == State::Ready && m_worker) {
        CheckRequest next = std::move(*m_queued);
        m_queued.reset();
        dispatch(std::move(next));
    }
    Q_EMIT resultReady(result);
}

} // namespace rasklad::app
