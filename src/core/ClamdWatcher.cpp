#include "ClamdWatcher.h"

ClamdWatcher::ClamdWatcher(ClamdClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
    m_timer.setInterval(30 * 1000);
    connect(&m_timer, &QTimer::timeout, this, &ClamdWatcher::checkNow);

    connect(m_client, &ClamdClient::versionReceived, this, [this](const ClamdVersion &version) {
        finishCheck(State::Connected, version, {});
    });
    connect(m_client, &ClamdClient::errorOccurred, this, [this](ClamdClient::Error, const QString &message) {
        finishCheck(State::Error, {}, message);
    });
}

ClamdWatcher::State ClamdWatcher::state() const
{
    return m_state;
}

ClamdVersion ClamdWatcher::version() const
{
    return m_version;
}

QString ClamdWatcher::errorMessage() const
{
    return m_errorMessage;
}

QDateTime ClamdWatcher::lastCheck() const
{
    return m_lastCheck;
}

QString ClamdWatcher::socketPath() const
{
    return m_client->socketPath();
}

void ClamdWatcher::setInterval(int msecs)
{
    m_timer.setInterval(msecs);
}

void ClamdWatcher::start()
{
    checkNow();
    m_timer.start();
}

void ClamdWatcher::checkNow()
{
    if (m_checking)
        return;
    m_checking = true;
    m_client->version();
}

void ClamdWatcher::finishCheck(State state, const ClamdVersion &version, const QString &errorMessage)
{
    m_checking = false;
    m_lastCheck = QDateTime::currentDateTime();

    const bool changed = state != m_state || version != m_version || errorMessage != m_errorMessage;
    m_state = state;
    m_version = version;
    m_errorMessage = errorMessage;

    if (changed)
        emit statusChanged();
    emit checkFinished();
}
