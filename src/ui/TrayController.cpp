#include "ui/TrayController.h"

#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <cmath>

namespace ccqt {

TrayController::TrayController(bool enabled, QObject* parent)
    : QObject(parent)
    , m_enabled(enabled)
{
    if (!m_enabled || !QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }

    m_menu = std::make_unique<QMenu>();
    m_modeGroup = new QActionGroup(this);
    m_modeGroup->setExclusive(true);

    m_trayIcon = new QSystemTrayIcon(createTrayIcon(), this);
    m_trayIcon->setContextMenu(m_menu.get());
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &TrayController::onTrayActivated);

    rebuildMenu();
    updateTooltip();
    m_trayIcon->show();
}

TrayController::~TrayController() = default;

bool TrayController::isAvailable() const
{
    return m_trayIcon != nullptr && QSystemTrayIcon::isSystemTrayAvailable();
}

void TrayController::setMainWindowVisible(bool visible)
{
    m_windowVisible = visible;
    if (m_toggleAction != nullptr) {
        m_toggleAction->setText(m_windowVisible ? tr("Hide Window") : tr("Show Window"));
    }
}

void TrayController::setMinimizeToTrayChecked(bool checked)
{
    m_minimizeToTray = checked;
    if (m_minimizeAction != nullptr && m_minimizeAction->isChecked() != checked) {
        m_minimizeAction->setChecked(checked);
    }
}

void TrayController::updateConnectionState(ConnectionState state, const HealthInfo& health)
{
    m_state = state;
    m_health = health;
    updateTooltip();
}

void TrayController::updateModes(const QVector<ModeInfo>& modes, const QString& activeModeUid)
{
    m_modes = modes;
    m_activeModeUid = activeModeUid;
    rebuildMenu();
    updateTooltip();
}

void TrayController::retranslateUi()
{
    rebuildMenu();
    updateTooltip();
}

void TrayController::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger) {
        emit toggleMainWindowRequested();
    }
}

QIcon TrayController::createTrayIcon()
{
    QIcon icon;
    const int sizes[] = {22, 32, 48, 64};
    for (const int sz : sizes) {
        QPixmap pm(sz, sz);
        pm.fill(Qt::transparent);

        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);

        const qreal c = sz * 0.5;
        const qreal r = sz * 0.42;

        // Outer badge ring
        p.setPen(QPen(QColor(46, 158, 214), std::max(1.5, sz * 0.08)));
        p.setBrush(QColor(22, 34, 46, 220));
        p.drawEllipse(QPointF(c, c), r, r);

        // Three curved fan blades
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(115, 208, 248));
        for (int i = 0; i < 3; ++i) {
            const qreal baseAngle = i * (2.0 * M_PI / 3.0) - M_PI / 2.0;
            const qreal tipAngle = baseAngle + 0.55;
            const qreal midAngle = baseAngle + 0.95;

            QPainterPath blade;
            blade.moveTo(c, c);
            blade.quadTo(c + std::cos(tipAngle) * r * 0.85,
                         c + std::sin(tipAngle) * r * 0.85,
                         c + std::cos(midAngle) * r * 0.68,
                         c + std::sin(midAngle) * r * 0.68);
            blade.quadTo(c + std::cos(baseAngle + 0.22) * r * 0.35,
                         c + std::sin(baseAngle + 0.22) * r * 0.35,
                         c, c);
            p.drawPath(blade);
        }

        // Center hub
        p.setBrush(QColor(240, 248, 255));
        p.drawEllipse(QPointF(c, c), sz * 0.10, sz * 0.10);

        icon.addPixmap(pm);
    }
    return icon;
}

void TrayController::rebuildMenu()
{
    if (!m_menu) {
        return;
    }

    m_menu->clear();
    const auto oldActions = m_modeGroup->actions();
    for (QAction* act : oldActions) {
        m_modeGroup->removeAction(act);
    }

    m_toggleAction = m_menu->addAction(m_windowVisible ? tr("Hide Window") : tr("Show Window"));
    connect(m_toggleAction, &QAction::triggered, this, &TrayController::toggleMainWindowRequested);

    m_menu->addSeparator();

    if (m_modes.isEmpty()) {
        QAction* emptyAct = m_menu->addAction(tr("No modes available"));
        emptyAct->setEnabled(false);
    } else {
        QMenu* targetMenu = m_menu.get();
        if (m_modes.size() > 10) {
            targetMenu = m_menu->addMenu(tr("Modes"));
        }
        for (const ModeInfo& mode : m_modes) {
            QAction* act = targetMenu->addAction(mode.name);
            act->setCheckable(true);
            act->setChecked(mode.uid == m_activeModeUid);
            act->setData(mode.uid);
            m_modeGroup->addAction(act);
            const QString uid = mode.uid;
            connect(act, &QAction::triggered, this, [this, uid]() {
                emit modeActivationRequested(uid);
            });
        }
    }

    m_menu->addSeparator();

    m_minimizeAction = m_menu->addAction(tr("Minimize to Tray on Close"));
    m_minimizeAction->setCheckable(true);
    m_minimizeAction->setChecked(m_minimizeToTray);
    connect(m_minimizeAction, &QAction::toggled, this, [this](bool checked) {
        m_minimizeToTray = checked;
        emit minimizeToTrayToggled(checked);
    });

    m_settingsAction = m_menu->addAction(tr("Connection Settings..."));
    connect(m_settingsAction, &QAction::triggered, this, &TrayController::settingsRequested);

    m_quitAction = m_menu->addAction(tr("Quit"));
    connect(m_quitAction, &QAction::triggered, this, &TrayController::quitRequested);
}

void TrayController::updateTooltip()
{
    if (m_trayIcon == nullptr) {
        return;
    }

    QString statusLine;
    switch (m_state) {
    case ConnectionState::Connected: {
        const QString ver = m_health.version.isEmpty() ? QStringLiteral("?") : m_health.version;
        statusLine = tr("CCQt Upon — Connected (v%1)").arg(ver);
        break;
    }
    case ConnectionState::Offline:
        statusLine = tr("CCQt Upon — Offline");
        break;
    case ConnectionState::AuthError:
        statusLine = tr("CCQt Upon — Authentication Required");
        break;
    case ConnectionState::Connecting:
        statusLine = tr("Connecting...");
        break;
    case ConnectionState::RateLimited:
        statusLine = tr("Rate Limited (429)");
        break;
    case ConnectionState::Disconnected:
        statusLine = tr("CCQt Upon");
        break;
    }

    QString activeModeName;
    for (const ModeInfo& m : m_modes) {
        if (m.uid == m_activeModeUid) {
            activeModeName = m.name;
            break;
        }
    }

    if (!activeModeName.isEmpty()) {
        statusLine += QLatin1Char('\n') + tr("Active Mode: %1").arg(activeModeName);
    }

    m_trayIcon->setToolTip(statusLine);
}

} // namespace ccqt
