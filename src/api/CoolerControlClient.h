#pragma once

#include "models/CoolerControlModels.h"

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSslError>
#include <QString>
#include <QTimer>
#include <QVector>

class QNetworkReply;
class QNetworkRequest;

namespace ccqt {

enum class AuthMode {
    AccessToken,
    SessionCookie
};

struct ConnectionConfig {
    QString baseUrl = QStringLiteral("http://localhost:11987");
    QString accessToken;
    AuthMode authMode = AuthMode::AccessToken;
};

enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    AuthError,
    Offline,
    RateLimited
};

class CoolerControlClient : public QObject {
    Q_OBJECT

public:
    explicit CoolerControlClient(QObject* parent = nullptr);
    ~CoolerControlClient() override;

    static bool isValidBaseUrl(const QString& rawUrl, QString* normalizedOut = nullptr);
    static bool isLocalHost(const QString& host);

    ConnectionConfig config() const;
    void setConfig(const ConnectionConfig& config, bool reconnectNow = true);

    ConnectionState state() const;
    const HealthInfo& lastHealth() const;

    void connectToDaemon();
    void retryNow();
    void disconnectFromDaemon();
    void refreshAllMetadata();

    void testConnection(const QString& rawUrl, const QString& accessToken);

    void activateMode(const QString& modeUid);
    void setChannelProfile(const QString& deviceUid,
                           const QString& channelName,
                           const QString& profileUid);
    void resetChannelSetting(const QString& deviceUid, const QString& channelName);

signals:
    void connectionStateChanged(ccqt::ConnectionState state);
    void connectionError(const QString& message);
    void authenticationRequired(const QString& reason);
    void rateLimited(int retryAfterSeconds);

    void healthUpdated(const ccqt::HealthInfo& health);
    void devicesUpdated(const QVector<ccqt::DeviceInfo>& devices);
    void statusUpdated(const QVector<ccqt::DeviceStatus>& statuses);
    void modesUpdated(const QVector<ccqt::ModeInfo>& modes);
    void activeModesUpdated(const ccqt::ActiveModes& activeModes);
    void profilesUpdated(const QVector<ccqt::ProfileInfo>& profiles);
    void overridesUpdated(const ccqt::NameOverrides& overrides);
    void deviceSettingsUpdated(const QString& deviceUid,
                               const QVector<ccqt::ChannelSetting>& settings);

    void modeActivated(const QString& modeUid);
    void channelSettingUpdated(const QString& deviceUid, const QString& channelName);
    void writeOperationFailed(const QString& deviceUid,
                              const QString& channelName,
                              const QString& message,
                              bool isForbidden);

    void connectionTestFinished(bool ok,
                                const QString& message,
                                const ccqt::HealthInfo& health);

private slots:
    void onSslErrors(QNetworkReply* reply, const QList<QSslError>& errors);
    void onPollTimeout();
    void onReconnectTimeout();
    void onRateLimitTimeout();

private:
    QNetworkRequest buildRequest(const QString& path,
                                 const ConnectionConfig& cfg,
                                 bool withAuth = true) const;
    void applyAuth(QNetworkRequest& request, const ConnectionConfig& cfg) const;

    void setState(ConnectionState newState);
    void startLiveUpdates();
    void stopLiveUpdates();
    void startOfflineWatch();
    void stopTimers();
    void abortActiveReplies();

    void fetchHealth();
    void fetchDevices();
    void fetchStatus();
    void fetchModes();
    void fetchActiveModes();
    void fetchProfiles();
    void fetchOverrides();
    void fetchAllDeviceSettings();
    void fetchDeviceSettings(const QString& deviceUid);

    void handleTransportOrAuthFailure(QNetworkReply* reply, int httpStatus, bool isWriteOp);
    static QString extractApiError(const QByteArray& body, const QString& fallback);
    static int parseRetryAfterSeconds(QNetworkReply* reply);
    static QString encodeSegment(const QString& segment);

    QNetworkAccessManager m_nam;
    ConnectionConfig m_config;
    ConnectionState m_state = ConnectionState::Disconnected;
    HealthInfo m_lastHealth;
    QVector<DeviceInfo> m_devices;

    QTimer m_pollTimer;
    QTimer m_reconnectTimer;
    QTimer m_rateLimitTimer;

    quint64 m_sessionGen = 0;
    quint64 m_testGen = 0;
    quint64 m_pollTickCount = 0;
    bool m_statusInFlight = false;
    bool m_handshakeInFlight = false;
    bool m_authFailedForCurrentToken = false;

    QList<QPointer<QNetworkReply>> m_activeReplies;
    QSet<QNetworkReply*> m_rejectedSslReplies;
};

} // namespace ccqt
