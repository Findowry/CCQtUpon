#include "api/CoolerControlClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>

namespace ccqt {
namespace {

constexpr int kRequestTimeoutMs = 4000;
constexpr int kStatusPollIntervalMs = 1000;
constexpr int kReconnectIntervalMs = 2000;
constexpr int kModesSyncEveryTicks = 5;

} // namespace

CoolerControlClient::CoolerControlClient(QObject* parent)
    : QObject(parent)
{
    connect(&m_nam, &QNetworkAccessManager::sslErrors, this, &CoolerControlClient::onSslErrors);

    m_pollTimer.setInterval(kStatusPollIntervalMs);
    m_pollTimer.setSingleShot(false);
    connect(&m_pollTimer, &QTimer::timeout, this, &CoolerControlClient::onPollTimeout);

    m_reconnectTimer.setInterval(kReconnectIntervalMs);
    m_reconnectTimer.setSingleShot(false);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &CoolerControlClient::onReconnectTimeout);

    m_rateLimitTimer.setSingleShot(true);
    connect(&m_rateLimitTimer, &QTimer::timeout, this, &CoolerControlClient::onRateLimitTimeout);
}

CoolerControlClient::~CoolerControlClient()
{
    disconnectFromDaemon();
}

bool CoolerControlClient::isValidBaseUrl(const QString& rawUrl, QString* normalizedOut)
{
    QString trimmed = rawUrl.trimmed();
    while (trimmed.endsWith(QLatin1Char('/'))) {
        trimmed.chop(1);
    }

    const QUrl url(trimmed, QUrl::StrictMode);
    if (!url.isValid()) {
        return false;
    }

    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        return false;
    }

    if (url.host().isEmpty()) {
        return false;
    }

    if (!url.query().isEmpty() || !url.fragment().isEmpty()) {
        return false;
    }

    if (normalizedOut != nullptr) {
        QString cleaned = url.toString(QUrl::StripTrailingSlash);
        while (cleaned.endsWith(QLatin1Char('/'))) {
            cleaned.chop(1);
        }
        *normalizedOut = cleaned;
    }
    return true;
}

bool CoolerControlClient::isLocalHost(const QString& host)
{
    QString h = host.trimmed().toLower();
    if (h.startsWith(QLatin1Char('[')) && h.endsWith(QLatin1Char(']')) && h.size() >= 2) {
        h = h.mid(1, h.size() - 2);
    }
    return h == QLatin1String("localhost")
        || h == QLatin1String("127.0.0.1")
        || h == QLatin1String("::1");
}

ConnectionConfig CoolerControlClient::config() const
{
    return m_config;
}

void CoolerControlClient::setConfig(const ConnectionConfig& config, bool reconnectNow)
{
    QString normalized;
    if (isValidBaseUrl(config.baseUrl, &normalized)) {
        m_config = config;
        m_config.baseUrl = normalized;
    } else {
        m_config = config;
    }
    m_config.accessToken = m_config.accessToken.trimmed();
    m_authFailedForCurrentToken = false;

    if (reconnectNow) {
        connectToDaemon();
    }
}

ConnectionState CoolerControlClient::state() const
{
    return m_state;
}

const HealthInfo& CoolerControlClient::lastHealth() const
{
    return m_lastHealth;
}

void CoolerControlClient::connectToDaemon()
{
    stopTimers();
    abortActiveReplies();
    ++m_sessionGen;
    m_statusInFlight = false;
    m_handshakeInFlight = false;
    m_pollTickCount = 0;
    m_authFailedForCurrentToken = false;

    QString normalized;
    if (!isValidBaseUrl(m_config.baseUrl, &normalized)) {
        setState(ConnectionState::Disconnected);
        emit connectionError(tr("Invalid daemon URL. Use http:// or https://."));
        return;
    }
    m_config.baseUrl = normalized;

    if (m_config.authMode == AuthMode::AccessToken && m_config.accessToken.isEmpty()) {
        setState(ConnectionState::AuthError);
        emit authenticationRequired(
            tr("No Access Token configured. Open Connection Settings to connect to coolercontrold."));
        return;
    }

    setState(ConnectionState::Connecting);
    fetchHealth();
}

void CoolerControlClient::retryNow()
{
    m_authFailedForCurrentToken = false;
    connectToDaemon();
}

void CoolerControlClient::disconnectFromDaemon()
{
    stopTimers();
    abortActiveReplies();
    ++m_sessionGen;
    ++m_testGen;
    m_statusInFlight = false;
    m_handshakeInFlight = false;
    setState(ConnectionState::Disconnected);
}

void CoolerControlClient::refreshAllMetadata()
{
    if (m_state != ConnectionState::Connected) {
        return;
    }
    fetchOverrides();
    fetchDevices();
    fetchModes();
    fetchActiveModes();
    fetchProfiles();
    fetchFunctions();
    fetchStatus();
}

void CoolerControlClient::testConnection(const QString& rawUrl, const QString& accessToken)
{
    QString normalized;
    if (!isValidBaseUrl(rawUrl, &normalized)) {
        emit connectionTestFinished(false, tr("Please enter a valid URL."), HealthInfo{});
        return;
    }

    const QString trimmedToken = accessToken.trimmed();
    if (trimmedToken.isEmpty()) {
        emit connectionTestFinished(false, tr("Please enter an Access Token."), HealthInfo{});
        return;
    }

    const quint64 testGen = ++m_testGen;
    ConnectionConfig testCfg;
    testCfg.baseUrl = normalized;
    testCfg.accessToken = trimmedToken;
    testCfg.authMode = AuthMode::AccessToken;

    QNetworkReply* handshakeReply = m_nam.get(buildRequest(QStringLiteral("/handshake"), testCfg, false));
    connect(handshakeReply, &QNetworkReply::finished, this, [this, handshakeReply, testGen, testCfg]() {
        handshakeReply->deleteLater();
        const bool sslRejected = m_rejectedSslReplies.remove(handshakeReply);
        if (testGen != m_testGen) {
            return;
        }

        if (sslRejected) {
            const QString host = QUrl(testCfg.baseUrl).host();
            emit connectionTestFinished(
                false,
                tr("TLS certificate rejected for non-local host: %1").arg(host),
                HealthInfo{});
            return;
        }

        if (handshakeReply->error() != QNetworkReply::NoError
            || !parseHandshake(handshakeReply->readAll())) {
            emit connectionTestFinished(
                false,
                tr("✗ Cannot reach coolercontrold: %1").arg(handshakeReply->errorString()),
                HealthInfo{});
            return;
        }

        QNetworkReply* healthReply = m_nam.get(buildRequest(QStringLiteral("/health"), testCfg, true));
        connect(healthReply, &QNetworkReply::finished, this, [this, healthReply, testGen]() {
            healthReply->deleteLater();
            m_rejectedSslReplies.remove(healthReply);
            if (testGen != m_testGen) {
                return;
            }

            const int status = healthReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = healthReply->readAll();

            if (status == 401) {
                emit connectionTestFinished(
                    false,
                    tr("✗ Authentication failed (HTTP 401): Invalid or expired Access Token."),
                    HealthInfo{});
                return;
            }

            if (status == 429) {
                const int retrySec = parseRetryAfterSeconds(healthReply);
                emit connectionTestFinished(
                    false,
                    tr("Rate limited by coolercontrold (HTTP 429). Pausing requests for %1 seconds.")
                        .arg(retrySec),
                    HealthInfo{});
                return;
            }

            if (healthReply->error() != QNetworkReply::NoError || status >= 400) {
                emit connectionTestFinished(
                    false,
                    tr("✗ Cannot reach coolercontrold: %1")
                        .arg(extractApiError(body, healthReply->errorString())),
                    HealthInfo{});
                return;
            }

            const auto health = parseHealth(body);
            if (!health.has_value()) {
                emit connectionTestFinished(
                    false,
                    tr("✗ Cannot reach coolercontrold: %1").arg(QStringLiteral("Invalid /health JSON")),
                    HealthInfo{});
                return;
            }

            const QString version = health->version.isEmpty() ? QStringLiteral("?") : health->version;
            const QString sys = health->systemName.isEmpty() ? QStringLiteral("localhost") : health->systemName;
            emit connectionTestFinished(
                true,
                tr("✓ Connected to coolercontrold v%1 (%2)").arg(version, sys),
                *health);
        });
    });
}

void CoolerControlClient::activateMode(const QString& modeUid)
{
    if (modeUid.isEmpty()) {
        return;
    }

    const quint64 gen = m_sessionGen;
    const QString path = QStringLiteral("/modes-active/%1").arg(encodeSegment(modeUid));
    QNetworkRequest req = buildRequest(path, m_config, true);
    QNetworkReply* reply = m_nam.post(req, QByteArray());
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, modeUid]() {
        reply->deleteLater();
        m_activeReplies.removeAll(reply);
        const bool sslRejected = m_rejectedSslReplies.remove(reply);
        if (gen != m_sessionGen) {
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        if (sslRejected || reply->error() != QNetworkReply::NoError || status >= 400) {
            if (status == 403) {
                emit writeOperationFailed(
                    {},
                    {},
                    tr("Write access denied (HTTP 403). Your Access Token is read-only; enable 'Write Access' to change modes or fan profiles."),
                    true);
                return;
            }
            if (status == 409) {
                emit writeOperationFailed(
                    {},
                    {},
                    tr("Channel is busy or calibrating (HTTP 409): %1")
                        .arg(extractApiError(body, reply->errorString())),
                    false);
                return;
            }
            if (status == 401 || status == 429 || status == 0) {
                handleTransportOrAuthFailure(reply, status, true);
                return;
            }
            emit writeOperationFailed(
                {},
                {},
                tr("Failed to apply change: %1").arg(extractApiError(body, reply->errorString())),
                false);
            return;
        }

        emit modeActivated(modeUid);
        fetchActiveModes();
        fetchAllDeviceSettings();
    });
}

void CoolerControlClient::setChannelProfile(const QString& deviceUid,
                                            const QString& channelName,
                                            const QString& profileUid)
{
    if (deviceUid.isEmpty() || channelName.isEmpty() || profileUid.isEmpty()) {
        return;
    }

    const quint64 gen = m_sessionGen;
    const QString path = QStringLiteral("/devices/%1/settings/%2/profile")
                             .arg(encodeSegment(deviceUid), encodeSegment(channelName));

    QNetworkRequest req = buildRequest(path, m_config, true);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject payload;
    payload.insert(QStringLiteral("profile_uid"), profileUid);
    const QByteArray jsonBytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = m_nam.put(req, jsonBytes);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, deviceUid, channelName]() {
        handleChannelWriteFinished(reply, gen, deviceUid, channelName);
    });
}

void CoolerControlClient::setChannelManual(const QString& deviceUid,
                                           const QString& channelName,
                                           quint8 speedFixed)
{
    if (deviceUid.isEmpty() || channelName.isEmpty()) {
        return;
    }

    const quint64 gen = m_sessionGen;
    const QString path = QStringLiteral("/devices/%1/settings/%2/manual")
                             .arg(encodeSegment(deviceUid), encodeSegment(channelName));

    QNetworkRequest req = buildRequest(path, m_config, true);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject payload;
    payload.insert(QStringLiteral("speed_fixed"),
                   static_cast<int>(std::clamp(static_cast<int>(speedFixed), 0, 100)));
    const QByteArray jsonBytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = m_nam.put(req, jsonBytes);
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, deviceUid, channelName]() {
        handleChannelWriteFinished(reply, gen, deviceUid, channelName);
    });
}

void CoolerControlClient::resetChannelSetting(const QString& deviceUid, const QString& channelName)
{
    if (deviceUid.isEmpty() || channelName.isEmpty()) {
        return;
    }

    const quint64 gen = m_sessionGen;
    const QString path = QStringLiteral("/devices/%1/settings/%2/reset")
                             .arg(encodeSegment(deviceUid), encodeSegment(channelName));

    QNetworkRequest req = buildRequest(path, m_config, true);
    QNetworkReply* reply = m_nam.put(req, QByteArray());
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, deviceUid, channelName]() {
        handleChannelWriteFinished(reply, gen, deviceUid, channelName);
    });
}

void CoolerControlClient::updateProfile(const ProfileInfo& profile)
{
    if (profile.uid.isEmpty()) {
        return;
    }

    const quint64 gen = m_sessionGen;
    QNetworkRequest req = buildRequest(QStringLiteral("/profiles"), m_config, true);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    const QByteArray jsonBytes = serializeProfile(profile);
    QNetworkReply* reply = m_nam.put(req, jsonBytes);
    m_activeReplies.append(reply);

    const QString uid = profile.uid;
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, uid]() {
        reply->deleteLater();
        m_activeReplies.removeAll(reply);
        const bool sslRejected = m_rejectedSslReplies.remove(reply);
        if (gen != m_sessionGen) {
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        if (sslRejected || reply->error() != QNetworkReply::NoError || status >= 400) {
            if (status == 403) {
                emit profileUpdateFailed(
                    uid,
                    tr("Write access denied (HTTP 403). Your Access Token is read-only; enable 'Write Access' to modify profiles or fan settings."),
                    true);
                return;
            }
            if (status == 409) {
                emit profileUpdateFailed(
                    uid,
                    tr("Channel is busy or calibrating (HTTP 409): %1")
                        .arg(extractApiError(body, reply->errorString())),
                    false);
                return;
            }
            if (status == 401 || status == 429 || status == 0) {
                handleTransportOrAuthFailure(reply, status, true);
            }
            emit profileUpdateFailed(
                uid,
                tr("Failed to apply change: %1").arg(extractApiError(body, reply->errorString())),
                false);
            return;
        }

        emit profileUpdated(uid);
        fetchProfiles();
    });
}

void CoolerControlClient::handleChannelWriteFinished(QNetworkReply* reply,
                                                     quint64 gen,
                                                     const QString& deviceUid,
                                                     const QString& channelName)
{
    reply->deleteLater();
    m_activeReplies.removeAll(reply);
    const bool sslRejected = m_rejectedSslReplies.remove(reply);
    if (gen != m_sessionGen) {
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();

    if (sslRejected || reply->error() != QNetworkReply::NoError || status >= 400) {
        if (status == 403) {
            emit writeOperationFailed(
                deviceUid,
                channelName,
                tr("Write access denied (HTTP 403). Your Access Token is read-only; enable 'Write Access' to change modes or fan profiles."),
                true);
            return;
        }
        if (status == 409) {
            emit writeOperationFailed(
                deviceUid,
                channelName,
                tr("Channel is busy or calibrating (HTTP 409): %1")
                    .arg(extractApiError(body, reply->errorString())),
                false);
            return;
        }
        if (status == 401 || status == 429 || status == 0) {
            handleTransportOrAuthFailure(reply, status, true);
        }
        emit writeOperationFailed(
            deviceUid,
            channelName,
            tr("Failed to apply change: %1").arg(extractApiError(body, reply->errorString())),
            false);
        return;
    }

    emit channelSettingUpdated(deviceUid, channelName);
    fetchDeviceSettings(deviceUid);
    fetchActiveModes();
}

void CoolerControlClient::onSslErrors(QNetworkReply* reply, const QList<QSslError>& errors)
{
    Q_UNUSED(errors)
    if (reply == nullptr) {
        return;
    }

    const QString host = reply->url().host();
    if (isLocalHost(host)) {
        reply->ignoreSslErrors();
        return;
    }

    m_rejectedSslReplies.insert(reply);
    reply->abort();
}

void CoolerControlClient::onPollTimeout()
{
    if (m_state != ConnectionState::Connected) {
        return;
    }

    fetchStatus();
    ++m_pollTickCount;
    if ((m_pollTickCount % kModesSyncEveryTicks) == 0) {
        fetchActiveModes();
    }
}

void CoolerControlClient::onReconnectTimeout()
{
    if (m_handshakeInFlight) {
        return;
    }
    if (m_state != ConnectionState::Offline && m_state != ConnectionState::AuthError) {
        m_reconnectTimer.stop();
        return;
    }

    m_handshakeInFlight = true;
    const quint64 gen = m_sessionGen;
    QNetworkReply* reply = m_nam.get(buildRequest(QStringLiteral("/handshake"), m_config, false));
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        reply->deleteLater();
        m_activeReplies.removeAll(reply);
        m_handshakeInFlight = false;
        const bool sslRejected = m_rejectedSslReplies.remove(reply);
        if (gen != m_sessionGen) {
            return;
        }

        if (sslRejected) {
            const QString host = QUrl(m_config.baseUrl).host();
            setState(ConnectionState::Disconnected);
            m_reconnectTimer.stop();
            emit connectionError(tr("TLS certificate rejected for non-local host: %1").arg(host));
            return;
        }

        if (reply->error() == QNetworkReply::NoError && parseHandshake(reply->readAll())) {
            if (m_authFailedForCurrentToken) {
                // Daemon is reachable, but current token already got 401; do not spam auth attempts
                // and trigger coolercontrold auth_throttle. Keep AuthError until user updates/retries.
                if (m_state != ConnectionState::AuthError) {
                    setState(ConnectionState::AuthError);
                    emit authenticationRequired(
                        tr("Authentication failed (HTTP 401). Please verify your Access Token in Connection Settings."));
                }
                return;
            }
            m_reconnectTimer.stop();
            connectToDaemon();
        } else {
            // Daemon unreachable; ensure banner stays Offline even if it dropped while in AuthError.
            if (m_state != ConnectionState::Offline) {
                setState(ConnectionState::Offline);
                emit connectionError(
                    tr("coolercontrold is unreachable at %1. Retrying automatically every 2 seconds...")
                        .arg(m_config.baseUrl));
            }
        }
    });
}

void CoolerControlClient::onRateLimitTimeout()
{
    if (m_state == ConnectionState::RateLimited && !m_authFailedForCurrentToken) {
        connectToDaemon();
    }
}

QNetworkRequest CoolerControlClient::buildRequest(const QString& path,
                                                  const ConnectionConfig& cfg,
                                                  bool withAuth) const
{
    const QUrl url(cfg.baseUrl + path);
    QNetworkRequest req(url);
    req.setTransferTimeout(kRequestTimeoutMs);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CCQtUpon/0.1.0"));
    req.setRawHeader("Accept", "application/json");
    if (withAuth) {
        applyAuth(req, cfg);
    }
    return req;
}

void CoolerControlClient::applyAuth(QNetworkRequest& request, const ConnectionConfig& cfg) const
{
    if (cfg.authMode == AuthMode::AccessToken) {
        if (!cfg.accessToken.isEmpty()) {
            const QByteArray bearer = QByteArrayLiteral("Bearer ") + cfg.accessToken.toUtf8();
            request.setRawHeader("Authorization", bearer);
        }
    } else if (cfg.authMode == AuthMode::SessionCookie) {
        // Phase 2 placeholder: QNetworkAccessManager manages session cookies via its cookie jar.
    }
}

void CoolerControlClient::setState(ConnectionState newState)
{
    if (m_state == newState) {
        return;
    }
    m_state = newState;
    emit connectionStateChanged(m_state);
}

void CoolerControlClient::startLiveUpdates()
{
    if (m_config.authMode == AuthMode::AccessToken) {
        // CoolerControl v5.1.1 does not allow SSE streams with Bearer Access Tokens.
        // Strictly use lightweight 1s GET /status polling in AccessToken mode.
        m_pollTimer.start();
    } else {
        // Fallback polling for SessionCookie until SSE stream reader is implemented in Phase 2.
        m_pollTimer.start();
    }
}

void CoolerControlClient::stopLiveUpdates()
{
    m_pollTimer.stop();
}

void CoolerControlClient::startOfflineWatch()
{
    stopLiveUpdates();
    if (!m_reconnectTimer.isActive()) {
        m_reconnectTimer.start();
    }
}

void CoolerControlClient::stopTimers()
{
    m_pollTimer.stop();
    m_reconnectTimer.stop();
    m_rateLimitTimer.stop();
}

void CoolerControlClient::abortActiveReplies()
{
    const auto copy = m_activeReplies;
    m_activeReplies.clear();
    for (const QPointer<QNetworkReply>& rep : copy) {
        if (rep) {
            rep->abort();
        }
    }
}

void CoolerControlClient::fetchHealth()
{
    const quint64 gen = m_sessionGen;
    QNetworkReply* reply = m_nam.get(buildRequest(QStringLiteral("/health"), m_config, true));
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        reply->deleteLater();
        m_activeReplies.removeAll(reply);
        const bool sslRejected = m_rejectedSslReplies.remove(reply);
        if (gen != m_sessionGen) {
            return;
        }

        if (sslRejected) {
            const QString host = QUrl(m_config.baseUrl).host();
            setState(ConnectionState::Disconnected);
            emit connectionError(tr("TLS certificate rejected for non-local host: %1").arg(host));
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            handleTransportOrAuthFailure(reply, status, false);
            return;
        }

        const auto health = parseHealth(body);
        if (!health.has_value()) {
            handleTransportOrAuthFailure(reply, status, false);
            return;
        }

        m_lastHealth = *health;
        m_authFailedForCurrentToken = false;
        emit healthUpdated(m_lastHealth);
        setState(ConnectionState::Connected);
        m_reconnectTimer.stop();
        refreshAllMetadata();
        startLiveUpdates();
    });
}

void CoolerControlClient::fetchDevices()
{
    const quint64 gen = m_sessionGen;
    QNetworkReply* reply = m_nam.get(buildRequest(QStringLiteral("/devices"), m_config, true));
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        reply->deleteLater();
        m_activeReplies.removeAll(reply);
        const bool sslRejected = m_rejectedSslReplies.remove(reply);
        if (gen != m_sessionGen) {
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (sslRejected || reply->error() != QNetworkReply::NoError || status >= 400) {
            handleTransportOrAuthFailure(reply, status, false);
            return;
        }

        if (const auto parsed = parseDevices(body)) {
            m_devices = *parsed;
            emit devicesUpdated(m_devices);
            fetchAllDeviceSettings();
        }
    });
}

void CoolerControlClient::fetchStatus()
{
    if (m_statusInFlight) {
        return;
    }

    m_statusInFlight = true;
    const quint64 gen = m_sessionGen;
    QNetworkReply* reply = m_nam.get(buildRequest(QStringLiteral("/status"), m_config, true));
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        reply->deleteLater();
        m_activeReplies.removeAll(reply);
        m_statusInFlight = false;
        const bool sslRejected = m_rejectedSslReplies.remove(reply);
        if (gen != m_sessionGen) {
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (sslRejected || reply->error() != QNetworkReply::NoError || status >= 400) {
            handleTransportOrAuthFailure(reply, status, false);
            return;
        }

        if (const auto parsed = parseStatus(body)) {
            emit statusUpdated(*parsed);
        }
    });
}

void CoolerControlClient::fetchModes()
{
    const quint64 gen = m_sessionGen;
    QNetworkReply* reply = m_nam.get(buildRequest(QStringLiteral("/modes"), m_config, true));
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        reply->deleteLater();
        m_activeReplies.removeAll(reply);
        const bool sslRejected = m_rejectedSslReplies.remove(reply);
        if (gen != m_sessionGen) {
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (sslRejected || reply->error() != QNetworkReply::NoError || status >= 400) {
            handleTransportOrAuthFailure(reply, status, false);
            return;
        }

        if (const auto parsed = parseModes(body)) {
            emit modesUpdated(*parsed);
        }
    });
}

void CoolerControlClient::fetchActiveModes()
{
    const quint64 gen = m_sessionGen;
    QNetworkReply* reply = m_nam.get(buildRequest(QStringLiteral("/modes-active"), m_config, true));
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        reply->deleteLater();
        m_activeReplies.removeAll(reply);
        const bool sslRejected = m_rejectedSslReplies.remove(reply);
        if (gen != m_sessionGen) {
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (sslRejected || reply->error() != QNetworkReply::NoError || status >= 400) {
            handleTransportOrAuthFailure(reply, status, false);
            return;
        }

        if (const auto parsed = parseActiveModes(body)) {
            emit activeModesUpdated(*parsed);
        }
    });
}

void CoolerControlClient::fetchProfiles()
{
    const quint64 gen = m_sessionGen;
    QNetworkReply* reply = m_nam.get(buildRequest(QStringLiteral("/profiles"), m_config, true));
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        reply->deleteLater();
        m_activeReplies.removeAll(reply);
        const bool sslRejected = m_rejectedSslReplies.remove(reply);
        if (gen != m_sessionGen) {
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (sslRejected || reply->error() != QNetworkReply::NoError || status >= 400) {
            handleTransportOrAuthFailure(reply, status, false);
            return;
        }

        if (const auto parsed = parseProfiles(body)) {
            emit profilesUpdated(*parsed);
        }
    });
}

void CoolerControlClient::fetchFunctions()
{
    const quint64 gen = m_sessionGen;
    QNetworkReply* reply = m_nam.get(buildRequest(QStringLiteral("/functions"), m_config, true));
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        reply->deleteLater();
        m_activeReplies.removeAll(reply);
        const bool sslRejected = m_rejectedSslReplies.remove(reply);
        if (gen != m_sessionGen) {
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (sslRejected || reply->error() != QNetworkReply::NoError || status >= 400) {
            handleTransportOrAuthFailure(reply, status, false);
            return;
        }

        if (const auto parsed = parseFunctions(body)) {
            emit functionsUpdated(*parsed);
        }
    });
}

void CoolerControlClient::fetchOverrides()
{
    const quint64 gen = m_sessionGen;
    QNetworkReply* reply = m_nam.get(buildRequest(QStringLiteral("/settings/overrides"), m_config, true));
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        reply->deleteLater();
        m_activeReplies.removeAll(reply);
        m_rejectedSslReplies.remove(reply);
        if (gen != m_sessionGen) {
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            // Optional endpoint: silently ignore 404 or transient issues.
            return;
        }

        emit overridesUpdated(parseOverrides(reply->readAll()));
    });
}

void CoolerControlClient::fetchAllDeviceSettings()
{
    for (const DeviceInfo& dev : m_devices) {
        if (dev.hasControllableChannels()) {
            fetchDeviceSettings(dev.uid);
        }
    }
}

void CoolerControlClient::fetchDeviceSettings(const QString& deviceUid)
{
    if (deviceUid.isEmpty()) {
        return;
    }

    const quint64 gen = m_sessionGen;
    const QString path = QStringLiteral("/devices/%1/settings").arg(encodeSegment(deviceUid));
    QNetworkReply* reply = m_nam.get(buildRequest(path, m_config, true));
    m_activeReplies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, deviceUid]() {
        reply->deleteLater();
        m_activeReplies.removeAll(reply);
        const bool sslRejected = m_rejectedSslReplies.remove(reply);
        if (gen != m_sessionGen) {
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (sslRejected || reply->error() != QNetworkReply::NoError || status >= 400) {
            if (status == 401 || status == 429 || status == 0) {
                handleTransportOrAuthFailure(reply, status, false);
            }
            return;
        }

        if (const auto parsed = parseDeviceSettings(body)) {
            emit deviceSettingsUpdated(deviceUid, *parsed);
        }
    });
}

void CoolerControlClient::handleTransportOrAuthFailure(QNetworkReply* reply,
                                                       int httpStatus,
                                                       bool isWriteOp)
{
    if (reply != nullptr && reply->error() == QNetworkReply::OperationCanceledError
        && httpStatus == 0 && m_state == ConnectionState::Disconnected) {
        return;
    }

    if (httpStatus == 401) {
        m_authFailedForCurrentToken = true;
        setState(ConnectionState::AuthError);
        startOfflineWatch();
        emit authenticationRequired(
            tr("Authentication failed (HTTP 401). Please verify your Access Token in Connection Settings."));
        return;
    }

    if (httpStatus == 403 && !isWriteOp) {
        setState(ConnectionState::AuthError);
        startOfflineWatch();
        emit authenticationRequired(
            tr("Authentication failed (HTTP 401). Please verify your Access Token in Connection Settings."));
        return;
    }

    if (httpStatus == 429) {
        const int retrySec = parseRetryAfterSeconds(reply);
        stopTimers();
        setState(ConnectionState::RateLimited);
        emit rateLimited(retrySec);
        emit connectionError(
            tr("Rate limited by coolercontrold (HTTP 429). Pausing requests for %1 seconds.")
                .arg(retrySec));
        m_rateLimitTimer.start(retrySec * 1000);
        return;
    }

    setState(ConnectionState::Offline);
    startOfflineWatch();
    emit connectionError(
        tr("coolercontrold is unreachable at %1. Retrying automatically every 2 seconds...")
            .arg(m_config.baseUrl));
}

QString CoolerControlClient::extractApiError(const QByteArray& body, const QString& fallback)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QJsonValue errVal = doc.object().value(QStringLiteral("error"));
        if (errVal.isString() && !errVal.toString().isEmpty()) {
            return errVal.toString();
        }
        const QJsonValue msgVal = doc.object().value(QStringLiteral("message"));
        if (msgVal.isString() && !msgVal.toString().isEmpty()) {
            return msgVal.toString();
        }
    }
    return fallback;
}

int CoolerControlClient::parseRetryAfterSeconds(QNetworkReply* reply)
{
    if (reply == nullptr) {
        return 10;
    }
    const QByteArray raw = reply->rawHeader("Retry-After").trimmed();
    bool ok = false;
    const int parsed = raw.toInt(&ok);
    if (!ok || parsed <= 0) {
        return 10;
    }
    return std::clamp(parsed, 1, 300);
}

QString CoolerControlClient::encodeSegment(const QString& segment)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(segment));
}

} // namespace ccqt
