#include "ScanManager.h"

#include "ClamdClient.h"
#include "ClamdConfig.h"

ScanManager::ScanManager(ClamdClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
}

void ScanManager::setOptions(const ScanOptions &options)
{
    m_options = options;
}

ScanOptions ScanManager::options() const
{
    return m_options;
}

void ScanManager::setQuickScan(const QStringList &paths, bool systemAreas)
{
    m_quickPaths = paths;
    m_quickSystemAreas = systemAreas;
}

QStringList ScanManager::quickScanPaths() const
{
    return m_quickPaths;
}

bool ScanManager::quickScanSystemAreas() const
{
    return m_quickSystemAreas;
}

void ScanManager::quickScan(Origin origin)
{
    scan(m_quickPaths, origin, m_quickSystemAreas);
}

void ScanManager::scan(const QStringList &paths, Origin origin, bool systemAreas)
{
    if (paths.isEmpty())
        return;
    // Même demande déjà en cours ou en attente (clé montée deux fois...) :
    // ignorée. Une analyse des seuls dossiers ne couvre pas une analyse
    // rapide, qui y ajoute les emplacements sensibles et les programmes en
    // cours ; une analyse en train de s'arrêter ne couvre plus rien.
    if (m_job && !m_job->isCancelled() && m_current.paths == paths && (m_current.systemAreas || !systemAreas))
        return;
    for (Request &request : m_queue) {
        if (request.paths != paths)
            continue;
        // En attente, sans les emplacements sensibles : elle les analysera aussi.
        if (systemAreas && !request.systemAreas) {
            request.systemAreas = true;
            request.origin = origin;
        }
        return;
    }

    m_queue.append({paths, origin, systemAreas});
    startNext();
}

void ScanManager::cancelAll()
{
    m_queue.clear();
    if (m_job)
        m_job->cancel(); // finished() arrivera avec `cancelled` à true
}

bool ScanManager::isScanning() const
{
    return m_job != nullptr;
}

QStringList ScanManager::currentPaths() const
{
    return m_job ? m_job->paths() : QStringList();
}

ScanManager::Origin ScanManager::currentOrigin() const
{
    return m_origin;
}

void ScanManager::startNext()
{
    if (m_job || m_queue.isEmpty())
        return;

    const Request request = m_queue.takeFirst();
    m_current = request;
    m_origin = request.origin;
    // Limite de taille de clamd relue à chaque analyse : sa configuration a pu changer.
    ScanOptions options = m_options;
    options.systemAreas = request.systemAreas;
    if (request.systemAreas)
        options.systemAreaPaths = ScanJob::systemAreaPaths();
    options.clamdUnscannedAbove = ClamdConfig::forSocket(m_client->socketPath()).unscannedAbove();
    m_job = new ScanJob(m_client->socketPath(), request.paths, options, this);

    // Le ScanJob émet depuis son thread : ces connexions passent par la file
    // d'événements et les signaux arrivent dans le thread de l'interface.
    connect(m_job, &ScanJob::counting, this, &ScanManager::counting);
    connect(m_job, &ScanJob::progressChanged, this, &ScanManager::progressChanged);
    connect(m_job, &ScanJob::resultsReady, this, &ScanManager::resultsReady);
    connect(m_job, &ScanJob::finished, this, [this](const ScanSummary &summary) {
        m_job->deleteLater();
        m_job = nullptr;
        emit scanFinished(summary, m_origin);
        startNext();
    });

    emit scanStarted(request.paths, request.origin);
    m_job->start();
}
