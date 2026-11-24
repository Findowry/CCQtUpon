#include "widgets/DeviceCardWidget.h"

#include "widgets/MetricBarWidget.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>

namespace ccqt {
namespace {

void applyMutedPalette(QLabel* label, const QPalette& sourcePalette)
{
    if (label == nullptr) {
        return;
    }
    QPalette pal = sourcePalette;
    const QColor muted = sourcePalette.color(QPalette::PlaceholderText);
    pal.setColor(QPalette::WindowText, muted);
    pal.setColor(QPalette::Text, muted);
    label->setPalette(pal);
}

} // namespace

DeviceCardWidget::DeviceCardWidget(const DeviceInfo& device,
                                   const NameOverrides& overrides,
                                   bool disambiguateName,
                                   QWidget* parent)
    : QFrame(parent)
    , m_device(device)
    , m_overrides(overrides)
    , m_disambiguateName(disambiguateName)
{
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Plain);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    buildUi();
    retranslateUi();
}

QString DeviceCardWidget::deviceUid() const
{
    return m_device.uid;
}

void DeviceCardWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(10);

    // Header row
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(8);

    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(2);

    m_titleLabel = new QLabel(this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 1.5);
    m_titleLabel->setFont(titleFont);
    titleCol->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel(this);
    QFont subFont = m_subtitleLabel->font();
    subFont.setPointSizeF(std::max(8.0, subFont.pointSizeF() - 1.0));
    m_subtitleLabel->setFont(subFont);
    titleCol->addWidget(m_subtitleLabel);

    headerLayout->addLayout(titleCol, 1);

    m_typeLabel = new QLabel(this);
    m_typeLabel->setFrameShape(QFrame::StyledPanel);
    m_typeLabel->setFrameShadow(QFrame::Plain);
    m_typeLabel->setMargin(4);
    m_typeLabel->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(m_typeLabel, 0, Qt::AlignTop);

    root->addLayout(headerLayout);

    const QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QFontMetrics valFm(monoFont);
    const int valueMinWidth = valFm.horizontalAdvance(QStringLiteral("10000 RPM  100%"));
    const int tempMinWidth = valFm.horizontalAdvance(QStringLiteral("100.0 °C"));

    // Channels & Fans section
    if (!m_device.channels.isEmpty()) {
        m_channelsSectionLabel = new QLabel(this);
        QFont secFont = m_channelsSectionLabel->font();
        secFont.setBold(true);
        m_channelsSectionLabel->setFont(secFont);
        root->addWidget(m_channelsSectionLabel);

        auto* grid = new QGridLayout();
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(6);
        grid->setColumnStretch(2, 1);

        int rowIdx = 0;
        for (const ChannelInfo& ch : m_device.channels) {
            ChannelRow row;
            row.info = ch;

            row.nameLabel = new QLabel(this);
            row.nameLabel->setMinimumWidth(120);
            grid->addWidget(row.nameLabel, rowIdx, 0, Qt::AlignVCenter);

            row.valueLabel = new QLabel(QStringLiteral("—"), this);
            row.valueLabel->setFont(monoFont);
            row.valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            row.valueLabel->setMinimumWidth(valueMinWidth);
            grid->addWidget(row.valueLabel, rowIdx, 1, Qt::AlignVCenter);

            row.bar = new MetricBarWidget(MetricBarWidget::Kind::Duty, this);
            row.bar->setRange(0.0, 100.0);
            const bool showBar = ch.isControllable()
                || ch.name.contains(QLatin1String("Load"), Qt::CaseInsensitive)
                || ch.name.startsWith(QLatin1String("fan"), Qt::CaseInsensitive)
                || ch.name.startsWith(QLatin1String("pwm"), Qt::CaseInsensitive);
            row.bar->setVisible(showBar);
            grid->addWidget(row.bar, rowIdx, 2, Qt::AlignVCenter);

            if (ch.isControllable()) {
                row.profileCombo = new QComboBox(this);
                row.profileCombo->setSizeAdjustPolicy(
                    QComboBox::AdjustToMinimumContentsLengthWithIcon);
                row.profileCombo->setMinimumContentsLength(16);
                row.profileCombo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
                grid->addWidget(row.profileCombo, rowIdx, 3, Qt::AlignVCenter);

                row.resetButton = new QPushButton(this);
                row.resetButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                grid->addWidget(row.resetButton, rowIdx, 4, Qt::AlignVCenter);

                const QString chName = ch.name;
                connect(row.profileCombo, &QComboBox::activated, this, [this, chName](int index) {
                    auto it = m_channelRows.find(chName);
                    if (it == m_channelRows.end() || it->profileCombo == nullptr) {
                        return;
                    }
                    const QString data = it->profileCombo->itemData(index).toString();
                    if (data.isEmpty() || data == QLatin1String("__manual__")) {
                        return;
                    }
                    if (data == it->activeProfileUid && !it->activeManualDuty.has_value()) {
                        return;
                    }
                    it->prevProfileUid = it->activeProfileUid;
                    it->prevManualDuty = it->activeManualDuty;
                    it->activeProfileUid = data;
                    it->activeManualDuty.reset();
                    setChannelPending(chName, true);
                    emit profileChangeRequested(m_device.uid, chName, data);
                });

                connect(row.resetButton, &QPushButton::clicked, this, [this, chName]() {
                    auto it = m_channelRows.find(chName);
                    if (it == m_channelRows.end()) {
                        return;
                    }
                    it->prevProfileUid = it->activeProfileUid;
                    it->prevManualDuty = it->activeManualDuty;
                    setChannelPending(chName, true);
                    emit resetChannelRequested(m_device.uid, chName);
                });
            }

            m_channelRows.insert(ch.name, row);
            ++rowIdx;
        }

        root->addLayout(grid);
    }

    // Temperature Sensors section
    if (!m_device.temps.isEmpty()) {
        m_tempsSectionLabel = new QLabel(this);
        QFont secFont = m_tempsSectionLabel->font();
        secFont.setBold(true);
        m_tempsSectionLabel->setFont(secFont);
        root->addWidget(m_tempsSectionLabel);

        auto* grid = new QGridLayout();
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(5);
        grid->setColumnStretch(2, 1);

        const double maxRange = std::clamp(static_cast<double>(m_device.tempMax), 90.0, 115.0);

        int rowIdx = 0;
        for (const TempInfo& tempInfo : m_device.temps) {
            TempRow row;
            row.info = tempInfo;

            row.nameLabel = new QLabel(this);
            row.nameLabel->setMinimumWidth(140);
            grid->addWidget(row.nameLabel, rowIdx, 0, Qt::AlignVCenter);

            row.valueLabel = new QLabel(QStringLiteral("—"), this);
            row.valueLabel->setFont(monoFont);
            row.valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            row.valueLabel->setMinimumWidth(tempMinWidth);
            grid->addWidget(row.valueLabel, rowIdx, 1, Qt::AlignVCenter);

            row.bar = new MetricBarWidget(MetricBarWidget::Kind::Temperature, this);
            row.bar->setRange(0.0, maxRange);
            row.bar->setSparklineEnabled(true);
            grid->addWidget(row.bar, rowIdx, 2, Qt::AlignVCenter);

            m_tempRows.insert(tempInfo.name, row);
            ++rowIdx;
        }

        root->addLayout(grid);
    }

    refreshHeaderText();
    refreshSecondaryPalette();
}

void DeviceCardWidget::refreshHeaderText()
{
    m_titleLabel->setText(resolveDeviceDisplayName(m_device, m_overrides, m_disambiguateName));
    m_typeLabel->setText(m_device.type);

    QStringList meta;
    if (!m_device.driverInfo.name.isEmpty()) {
        meta.append(m_device.driverInfo.name);
    }
    if (!m_device.driverInfo.drvType.isEmpty()) {
        meta.append(m_device.driverInfo.drvType);
    }
    if (!m_device.driverInfo.version.isEmpty()) {
        meta.append(m_device.driverInfo.version);
    }
    m_subtitleLabel->setText(meta.join(QStringLiteral(" · ")));
    m_subtitleLabel->setVisible(!meta.isEmpty());

    for (auto it = m_channelRows.begin(); it != m_channelRows.end(); ++it) {
        it->nameLabel->setText(
            resolveChannelDisplayName(m_device.uid, it->info.name, it->info.label, m_overrides));
    }
    for (auto it = m_tempRows.begin(); it != m_tempRows.end(); ++it) {
        it->nameLabel->setText(
            resolveChannelDisplayName(m_device.uid, it->info.name, it->info.label, m_overrides));
    }
}

void DeviceCardWidget::refreshSecondaryPalette()
{
    applyMutedPalette(m_subtitleLabel, palette());
    applyMutedPalette(m_typeLabel, palette());
    applyMutedPalette(m_channelsSectionLabel, palette());
    applyMutedPalette(m_tempsSectionLabel, palette());
}

void DeviceCardWidget::setOverrides(const NameOverrides& overrides, bool disambiguateName)
{
    m_overrides = overrides;
    m_disambiguateName = disambiguateName;
    refreshHeaderText();
}

void DeviceCardWidget::updateProfiles(const QVector<ProfileInfo>& profiles)
{
    m_profiles = profiles;
    for (auto it = m_channelRows.begin(); it != m_channelRows.end(); ++it) {
        if (it->profileCombo != nullptr) {
            populateChannelCombo(*it);
        }
    }
}

void DeviceCardWidget::updateSettings(const QVector<ChannelSetting>& settings)
{
    m_settingsByChannel.clear();
    for (const ChannelSetting& cs : settings) {
        m_settingsByChannel.insert(cs.channelName, cs);
    }

    for (auto it = m_channelRows.begin(); it != m_channelRows.end(); ++it) {
        if (it->profileCombo == nullptr) {
            continue;
        }
        const auto sIt = m_settingsByChannel.constFind(it->info.name);
        if (sIt != m_settingsByChannel.constEnd()) {
            if (sIt->profileUid.has_value()) {
                it->activeProfileUid = *sIt->profileUid;
                it->activeManualDuty.reset();
            } else if (sIt->speedFixed.has_value()) {
                it->activeProfileUid.clear();
                it->activeManualDuty = sIt->speedFixed;
            }
        } else {
            it->activeProfileUid = QStringLiteral("0");
            it->activeManualDuty.reset();
        }
        populateChannelCombo(*it);
    }
}

void DeviceCardWidget::updateStatus(const DeviceStatus& status)
{
    for (const TempReading& t : status.temps) {
        const auto it = m_tempRows.find(t.name);
        if (it == m_tempRows.end()) {
            continue;
        }
        it->valueLabel->setText(tr("%1 °C").arg(QString::number(t.temp, 'f', 1)));
        it->bar->setValue(t.temp);
        it->bar->pushSample(t.temp);
    }

    for (const ChannelReading& ch : status.channels) {
        const auto it = m_channelRows.find(ch.name);
        if (it == m_channelRows.end()) {
            continue;
        }

        QStringList parts;
        if (ch.rpm.has_value()) {
            parts.append(tr("%1 RPM").arg(*ch.rpm));
        }
        if (ch.duty.has_value()) {
            const int precision = ch.rpm.has_value() ? 0 : 1;
            parts.append(tr("%1%").arg(QString::number(*ch.duty, 'f', precision)));
            if (it->bar != nullptr) {
                it->bar->setVisible(true);
                it->bar->setValue(*ch.duty);
            }
        }
        if (ch.watts.has_value()) {
            parts.append(tr("%1 W").arg(QString::number(*ch.watts, 'f', 1)));
        }
        if (ch.freq.has_value()) {
            parts.append(tr("%1 MHz").arg(*ch.freq));
        }

        it->valueLabel->setText(parts.isEmpty() ? QStringLiteral("—")
                                                : parts.join(QStringLiteral("  ")));
    }
}

void DeviceCardWidget::setChannelPending(const QString& channelName, bool pending)
{
    const auto it = m_channelRows.find(channelName);
    if (it == m_channelRows.end()) {
        return;
    }
    it->pending = pending;
    if (it->profileCombo != nullptr) {
        it->profileCombo->setEnabled(!pending);
    }
    if (it->resetButton != nullptr) {
        it->resetButton->setEnabled(!pending);
    }
}

void DeviceCardWidget::revertChannelSelection(const QString& channelName)
{
    const auto it = m_channelRows.find(channelName);
    if (it == m_channelRows.end()) {
        return;
    }
    it->activeProfileUid = it->prevProfileUid;
    it->activeManualDuty = it->prevManualDuty;
    setChannelPending(channelName, false);
    populateChannelCombo(*it);
}

void DeviceCardWidget::populateChannelCombo(ChannelRow& row)
{
    if (row.profileCombo == nullptr) {
        return;
    }
    if (row.profileCombo->view() && row.profileCombo->view()->isVisible()) {
        return;
    }

    row.profileCombo->clear();

    int targetIdx = -1;
    for (const ProfileInfo& p : m_profiles) {
        row.profileCombo->addItem(formatProfileLabel(p), p.uid);
        if (!row.activeManualDuty.has_value() && p.uid == row.activeProfileUid) {
            targetIdx = row.profileCombo->count() - 1;
        }
    }

    if (row.activeManualDuty.has_value()) {
        row.profileCombo->addItem(tr("Manual (%1%)").arg(*row.activeManualDuty),
                                  QStringLiteral("__manual__"));
        targetIdx = row.profileCombo->count() - 1;
    }

    if (targetIdx >= 0) {
        row.profileCombo->setCurrentIndex(targetIdx);
    } else if (row.profileCombo->count() > 0) {
        row.profileCombo->setCurrentIndex(0);
    }
}

QString DeviceCardWidget::formatProfileLabel(const ProfileInfo& profile) const
{
    if (profile.type == QLatin1String("Default")) {
        return profile.name;
    }
    if (profile.type == QLatin1String("Fixed") && profile.speedFixed.has_value()) {
        return QStringLiteral("%1 (%2%)").arg(profile.name).arg(*profile.speedFixed);
    }
    return QStringLiteral("%1 (%2)").arg(profile.name, tr(profile.type.toUtf8().constData()));
}

void DeviceCardWidget::retranslateUi()
{
    if (m_channelsSectionLabel != nullptr) {
        m_channelsSectionLabel->setText(tr("Channels & Fans"));
    }
    if (m_tempsSectionLabel != nullptr) {
        m_tempsSectionLabel->setText(tr("Temperature Sensors"));
    }
    for (auto it = m_channelRows.begin(); it != m_channelRows.end(); ++it) {
        if (it->resetButton != nullptr) {
            it->resetButton->setText(tr("Reset"));
            it->resetButton->setToolTip(tr("Reset channel to hardware default"));
        }
        if (it->profileCombo != nullptr) {
            populateChannelCombo(*it);
        }
    }
}

void DeviceCardWidget::changeEvent(QEvent* event)
{
    QFrame::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    } else if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        refreshSecondaryPalette();
        update();
    }
}

} // namespace ccqt
