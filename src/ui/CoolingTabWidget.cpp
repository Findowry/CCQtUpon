#include "ui/CoolingTabWidget.h"

#include "api/CoolerControlClient.h"
#include "widgets/FanCurveEditorWidget.h"
#include "widgets/FanGaugeWidget.h"
#include "widgets/FanHistoryChartWidget.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace ccqt {
namespace {

constexpr std::size_t kMaxFanHistorySamples = 120;

void applyMuted(QLabel* label, const QPalette& pal)
{
    if (label == nullptr) {
        return;
    }
    QPalette p = pal;
    const QColor muted = pal.color(QPalette::PlaceholderText);
    p.setColor(QPalette::WindowText, muted);
    p.setColor(QPalette::Text, muted);
    label->setPalette(p);
}

} // namespace

CoolingTabWidget::CoolingTabWidget(CoolerControlClient* client, QWidget* parent)
    : QWidget(parent)
    , m_client(client)
{
    buildUi();
    retranslateUi();
    refreshMutedPalettes();
}

void CoolingTabWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);

    // Left sidebar
    auto* sidebar = new QWidget(m_splitter);
    sidebar->setMinimumWidth(250);
    auto* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(10, 10, 6, 10);
    sideLayout->setSpacing(10);

    m_fansGroup = new QGroupBox(sidebar);
    auto* fansLayout = new QVBoxLayout(m_fansGroup);
    fansLayout->setContentsMargins(8, 8, 8, 8);
    fansLayout->setSpacing(4);

    m_fansTree = new QTreeWidget(m_fansGroup);
    m_fansTree->setColumnCount(2);
    m_fansTree->setHeaderHidden(true);
    m_fansTree->setRootIsDecorated(true);
    m_fansTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fansTree->header()->setStretchLastSection(false);
    m_fansTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fansTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(m_fansTree,
            &QTreeWidget::itemSelectionChanged,
            this,
            &CoolingTabWidget::onFanTreeSelectionChanged);
    fansLayout->addWidget(m_fansTree);

    m_fansEmptyLabel = new QLabel(m_fansGroup);
    m_fansEmptyLabel->setWordWrap(true);
    fansLayout->addWidget(m_fansEmptyLabel);
    sideLayout->addWidget(m_fansGroup, 4);

    m_profilesGroup = new QGroupBox(sidebar);
    auto* profLayout = new QVBoxLayout(m_profilesGroup);
    profLayout->setContentsMargins(8, 8, 8, 8);
    m_profilesList = new QListWidget(m_profilesGroup);
    m_profilesList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_profilesList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item == nullptr || m_selectedDeviceUid.isEmpty() || m_selectedChannelName.isEmpty()) {
            return;
        }
        const QString pUid = item->data(Qt::UserRole).toString();
        if (pUid.isEmpty()) {
            return;
        }
        syncModeButtonsAndPage(ModeProfile);
        loadProfileIntoEditor(pUid);
        refreshProfileCombo();
    });
    profLayout->addWidget(m_profilesList);
    sideLayout->addWidget(m_profilesGroup, 3);

    m_functionsGroup = new QGroupBox(sidebar);
    auto* fnLayout = new QVBoxLayout(m_functionsGroup);
    fnLayout->setContentsMargins(8, 8, 8, 8);
    fnLayout->setSpacing(4);
    m_functionsList = new QListWidget(m_functionsGroup);
    m_functionsList->setSelectionMode(QAbstractItemView::NoSelection);
    fnLayout->addWidget(m_functionsList);
    m_functionsEmptyLabel = new QLabel(m_functionsGroup);
    m_functionsEmptyLabel->setWordWrap(true);
    fnLayout->addWidget(m_functionsEmptyLabel);
    sideLayout->addWidget(m_functionsGroup, 2);

    m_splitter->addWidget(sidebar);

    // Right workspace
    m_rightStack = new QStackedWidget(m_splitter);

    auto* emptyPage = new QWidget(m_rightStack);
    auto* emptyLayout = new QVBoxLayout(emptyPage);
    emptyLayout->addStretch(1);
    m_emptyWorkspaceLabel = new QLabel(emptyPage);
    m_emptyWorkspaceLabel->setAlignment(Qt::AlignCenter);
    m_emptyWorkspaceLabel->setWordWrap(true);
    emptyLayout->addWidget(m_emptyWorkspaceLabel, 0, Qt::AlignCenter);
    emptyLayout->addStretch(1);
    m_rightStack->addWidget(emptyPage);

    auto* workScroll = new QScrollArea(m_rightStack);
    workScroll->setWidgetResizable(true);
    workScroll->setFrameShape(QFrame::NoFrame);

    auto* workPage = new QWidget(workScroll);
    auto* workLayout = new QVBoxLayout(workPage);
    workLayout->setContentsMargins(8, 10, 12, 12);
    workLayout->setSpacing(10);

    // Top control strip
    auto* headerFrame = new QFrame(workPage);
    headerFrame->setFrameShape(QFrame::StyledPanel);
    headerFrame->setFrameShadow(QFrame::Plain);
    auto* headerLayout = new QHBoxLayout(headerFrame);
    headerLayout->setContentsMargins(12, 10, 12, 10);
    headerLayout->setSpacing(12);

    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(2);
    m_fanTitleLabel = new QLabel(headerFrame);
    QFont titleFont = m_fanTitleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 1.5);
    m_fanTitleLabel->setFont(titleFont);
    titleCol->addWidget(m_fanTitleLabel);

    m_fanTelemetryLabel = new QLabel(QStringLiteral("—"), headerFrame);
    m_fanTelemetryLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    titleCol->addWidget(m_fanTelemetryLabel);
    headerLayout->addLayout(titleCol, 1);

    auto* modeRow = new QHBoxLayout();
    modeRow->setSpacing(4);
    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->setExclusive(true);

    m_modeProfileBtn = new QPushButton(headerFrame);
    m_modeProfileBtn->setCheckable(true);
    m_modeGroup->addButton(m_modeProfileBtn, ModeProfile);
    modeRow->addWidget(m_modeProfileBtn);

    m_modeManualBtn = new QPushButton(headerFrame);
    m_modeManualBtn->setCheckable(true);
    m_modeGroup->addButton(m_modeManualBtn, ModeManual);
    modeRow->addWidget(m_modeManualBtn);

    m_modeUnmanagedBtn = new QPushButton(headerFrame);
    m_modeUnmanagedBtn->setCheckable(true);
    m_modeGroup->addButton(m_modeUnmanagedBtn, ModeUnmanaged);
    modeRow->addWidget(m_modeUnmanagedBtn);

    connect(m_modeGroup, &QButtonGroup::idClicked, this, &CoolingTabWidget::onModeButtonClicked);
    headerLayout->addLayout(modeRow);

    m_applyBtn = new QPushButton(headerFrame);
    m_applyBtn->setDefault(true);
    connect(m_applyBtn, &QPushButton::clicked, this, &CoolingTabWidget::onApplyClicked);
    headerLayout->addWidget(m_applyBtn);

    workLayout->addWidget(headerFrame);

    // Mode Stack
    m_modeStack = new QStackedWidget(workPage);

    // --- Page 0: Profile Mode ---
    auto* profilePage = new QWidget(m_modeStack);
    auto* profileLayout = new QVBoxLayout(profilePage);
    profileLayout->setContentsMargins(0, 0, 0, 0);
    profileLayout->setSpacing(10);

    auto* profileInfoFrame = new QFrame(profilePage);
    profileInfoFrame->setFrameShape(QFrame::StyledPanel);
    profileInfoFrame->setFrameShadow(QFrame::Plain);
    auto* pInfoLayout = new QVBoxLayout(profileInfoFrame);
    pInfoLayout->setContentsMargins(12, 10, 12, 10);
    pInfoLayout->setSpacing(8);

    auto* pSelRow = new QHBoxLayout();
    pSelRow->setSpacing(10);
    m_profileSelectLabel = new QLabel(profileInfoFrame);
    pSelRow->addWidget(m_profileSelectLabel);

    m_profileCombo = new QComboBox(profileInfoFrame);
    m_profileCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_profileCombo->setMinimumContentsLength(28);
    m_profileCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_profileCombo,
            &QComboBox::currentTextChanged,
            m_profileCombo,
            &QComboBox::setToolTip);
    connect(m_profileCombo, &QComboBox::activated, this, &CoolingTabWidget::onProfileComboActivated);
    pSelRow->addWidget(m_profileCombo, 1);
    pInfoLayout->addLayout(pSelRow);

    auto* metaRow = new QHBoxLayout();
    metaRow->setSpacing(14);
    m_profileTypeCaption = new QLabel(profileInfoFrame);
    m_profileTypeValue = new QLabel(QStringLiteral("—"), profileInfoFrame);
    m_tempSourceCaption = new QLabel(profileInfoFrame);
    m_tempSourceValue = new QLabel(QStringLiteral("—"), profileInfoFrame);
    m_functionCaption = new QLabel(profileInfoFrame);
    m_functionValue = new QLabel(QStringLiteral("—"), profileInfoFrame);

    metaRow->addWidget(m_profileTypeCaption);
    metaRow->addWidget(m_profileTypeValue);
    metaRow->addSpacing(8);
    metaRow->addWidget(m_tempSourceCaption);
    metaRow->addWidget(m_tempSourceValue);
    metaRow->addSpacing(8);
    metaRow->addWidget(m_functionCaption);
    metaRow->addWidget(m_functionValue);
    metaRow->addStretch(1);
    pInfoLayout->addLayout(metaRow);

    profileLayout->addWidget(profileInfoFrame);

    m_profileInnerStack = new QStackedWidget(profilePage);

    // Graph subpage
    auto* graphPage = new QWidget(m_profileInnerStack);
    auto* graphLayout = new QHBoxLayout(graphPage);
    graphLayout->setContentsMargins(0, 0, 0, 0);
    graphLayout->setSpacing(10);

    auto* curveFrame = new QFrame(graphPage);
    curveFrame->setFrameShape(QFrame::StyledPanel);
    curveFrame->setFrameShadow(QFrame::Plain);
    auto* curveFrameLayout = new QVBoxLayout(curveFrame);
    curveFrameLayout->setContentsMargins(10, 10, 10, 8);
    m_curveEditor = new FanCurveEditorWidget(curveFrame);
    connect(m_curveEditor,
            &FanCurveEditorWidget::pointsChanged,
            this,
            [this](const QVector<QPair<double, quint8>>& pts) {
                if (m_syncingPoints) {
                    return;
                }
                m_editingProfile.speedProfile = pts;
                m_profileDirty = true;
                populatePointsTable();
            });
    connect(m_curveEditor, &FanCurveEditorWidget::selectedPointChanged, this, [this](int idx) {
        if (m_syncingPoints || m_pointsTable == nullptr) {
            return;
        }
        const bool wasBlocked = m_pointsTable->blockSignals(true);
        if (idx >= 0 && idx < m_pointsTable->rowCount()) {
            m_pointsTable->selectRow(idx);
        } else {
            m_pointsTable->clearSelection();
        }
        m_pointsTable->blockSignals(wasBlocked);
    });
    curveFrameLayout->addWidget(m_curveEditor, 1);
    graphLayout->addWidget(curveFrame, 3);

    m_pointsGroup = new QGroupBox(graphPage);
    m_pointsGroup->setMinimumWidth(240);
    auto* ptsLayout = new QVBoxLayout(m_pointsGroup);
    ptsLayout->setContentsMargins(8, 8, 8, 8);
    ptsLayout->setSpacing(6);

    m_pointsTable = new QTableWidget(0, 2, m_pointsGroup);
    m_pointsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pointsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pointsTable->verticalHeader()->setVisible(false);
    m_pointsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    connect(m_pointsTable, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (!m_syncingPoints && m_curveEditor != nullptr) {
            m_curveEditor->setSelectedIndex(row);
        }
    });
    ptsLayout->addWidget(m_pointsTable, 1);

    auto* ptsBtnRow = new QHBoxLayout();
    ptsBtnRow->setSpacing(6);
    m_addPointBtn = new QPushButton(m_pointsGroup);
    connect(m_addPointBtn, &QPushButton::clicked, this, &CoolingTabWidget::onAddPointClicked);
    ptsBtnRow->addWidget(m_addPointBtn);

    m_removePointBtn = new QPushButton(m_pointsGroup);
    connect(m_removePointBtn, &QPushButton::clicked, this, &CoolingTabWidget::onRemovePointClicked);
    ptsBtnRow->addWidget(m_removePointBtn);
    ptsLayout->addLayout(ptsBtnRow);

    graphLayout->addWidget(m_pointsGroup, 2);
    m_profileInnerStack->addWidget(graphPage);

    // Fixed subpage
    auto* fixedPage = new QFrame(m_profileInnerStack);
    fixedPage->setFrameShape(QFrame::StyledPanel);
    fixedPage->setFrameShadow(QFrame::Plain);
    auto* fixedLayout = new QVBoxLayout(fixedPage);
    fixedLayout->setContentsMargins(16, 16, 16, 16);
    fixedLayout->setSpacing(10);

    auto* fixedRow = new QHBoxLayout();
    fixedRow->setSpacing(10);
    m_fixedSpeedLabel = new QLabel(fixedPage);
    fixedRow->addWidget(m_fixedSpeedLabel);

    m_fixedSlider = new QSlider(Qt::Horizontal, fixedPage);
    m_fixedSlider->setRange(0, 100);
    fixedRow->addWidget(m_fixedSlider, 1);

    m_fixedSpin = new QSpinBox(fixedPage);
    m_fixedSpin->setRange(0, 100);
    m_fixedSpin->setSuffix(QStringLiteral("%"));
    fixedRow->addWidget(m_fixedSpin);

    connect(m_fixedSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_fixedSpin->value() != v) {
            m_fixedSpin->setValue(v);
        }
        m_editingProfile.speedFixed = static_cast<quint8>(std::clamp(v, 0, 100));
        m_profileDirty = true;
    });
    connect(m_fixedSpin, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_fixedSlider->value() != v) {
            m_fixedSlider->setValue(v);
        }
        m_editingProfile.speedFixed = static_cast<quint8>(std::clamp(v, 0, 100));
        m_profileDirty = true;
    });

    fixedLayout->addLayout(fixedRow);
    fixedLayout->addStretch(1);
    m_profileInnerStack->addWidget(fixedPage);

    // Other profile subpage (Default / Mix / Overlay)
    auto* otherPage = new QFrame(m_profileInnerStack);
    otherPage->setFrameShape(QFrame::StyledPanel);
    otherPage->setFrameShadow(QFrame::Plain);
    auto* otherLayout = new QVBoxLayout(otherPage);
    otherLayout->addStretch(1);
    m_otherProfileInfoLabel = new QLabel(otherPage);
    m_otherProfileInfoLabel->setWordWrap(true);
    m_otherProfileInfoLabel->setAlignment(Qt::AlignCenter);
    otherLayout->addWidget(m_otherProfileInfoLabel, 0, Qt::AlignCenter);
    otherLayout->addStretch(1);
    m_profileInnerStack->addWidget(otherPage);

    profileLayout->addWidget(m_profileInnerStack, 1);
    m_modeStack->addWidget(profilePage);

    // --- Page 1: Manual Mode ---
    auto* manualPage = new QWidget(m_modeStack);
    auto* manualLayout = new QVBoxLayout(manualPage);
    manualLayout->setContentsMargins(0, 0, 0, 0);
    manualLayout->setSpacing(10);

    auto* gaugeFrame = new QFrame(manualPage);
    gaugeFrame->setFrameShape(QFrame::StyledPanel);
    gaugeFrame->setFrameShadow(QFrame::Plain);
    auto* gaugeLayout = new QVBoxLayout(gaugeFrame);
    gaugeLayout->setContentsMargins(14, 12, 14, 12);
    gaugeLayout->setSpacing(10);

    m_fanGauge = new FanGaugeWidget(gaugeFrame);
    gaugeLayout->addWidget(m_fanGauge, 1);

    auto* sliderRow = new QHBoxLayout();
    sliderRow->setSpacing(8);

    m_manualMinusBtn = new QToolButton(gaugeFrame);
    m_manualMinusBtn->setText(QStringLiteral("-"));
    m_manualMinusBtn->setAutoRepeat(true);
    sliderRow->addWidget(m_manualMinusBtn);

    m_manualSlider = new QSlider(Qt::Horizontal, gaugeFrame);
    m_manualSlider->setRange(0, 100);
    sliderRow->addWidget(m_manualSlider, 1);

    m_manualPlusBtn = new QToolButton(gaugeFrame);
    m_manualPlusBtn->setText(QStringLiteral("+"));
    m_manualPlusBtn->setAutoRepeat(true);
    sliderRow->addWidget(m_manualPlusBtn);

    m_manualSpin = new QSpinBox(gaugeFrame);
    m_manualSpin->setRange(0, 100);
    m_manualSpin->setSuffix(QStringLiteral("%"));
    sliderRow->addWidget(m_manualSpin);

    connect(m_manualMinusBtn, &QToolButton::clicked, this, [this]() {
        m_manualSpin->setValue(std::max(m_manualSpin->minimum(), m_manualSpin->value() - 1));
    });
    connect(m_manualPlusBtn, &QToolButton::clicked, this, [this]() {
        m_manualSpin->setValue(std::min(m_manualSpin->maximum(), m_manualSpin->value() + 1));
    });
    connect(m_manualSlider, &QSlider::valueChanged, this, [this](int val) {
        if (m_syncingManual) {
            return;
        }
        m_syncingManual = true;
        m_manualSpin->setValue(val);
        m_fanGauge->setTargetDuty(val, true);
        m_syncingManual = false;
    });
    connect(m_manualSpin, &QSpinBox::valueChanged, this, [this](int val) {
        if (m_syncingManual) {
            return;
        }
        m_syncingManual = true;
        m_manualSlider->setValue(val);
        m_fanGauge->setTargetDuty(val, true);
        m_syncingManual = false;
    });

    gaugeLayout->addLayout(sliderRow);
    manualLayout->addWidget(gaugeFrame, 3);

    auto* historyFrame = new QFrame(manualPage);
    historyFrame->setFrameShape(QFrame::StyledPanel);
    historyFrame->setFrameShadow(QFrame::Plain);
    auto* historyLayout = new QVBoxLayout(historyFrame);
    historyLayout->setContentsMargins(10, 8, 10, 8);
    m_historyChart = new FanHistoryChartWidget(historyFrame);
    historyLayout->addWidget(m_historyChart);
    manualLayout->addWidget(historyFrame, 2);

    m_modeStack->addWidget(manualPage);

    // --- Page 2: Unmanaged Mode ---
    auto* unmanagedPage = new QFrame(m_modeStack);
    unmanagedPage->setFrameShape(QFrame::StyledPanel);
    unmanagedPage->setFrameShadow(QFrame::Plain);
    auto* unmLayout = new QVBoxLayout(unmanagedPage);
    unmLayout->setContentsMargins(24, 24, 24, 24);
    unmLayout->setSpacing(10);
    unmLayout->addStretch(1);

    m_unmanagedTitleLabel = new QLabel(unmanagedPage);
    QFont unmFont = m_unmanagedTitleLabel->font();
    unmFont.setBold(true);
    unmFont.setPointSizeF(unmFont.pointSizeF() + 2.0);
    m_unmanagedTitleLabel->setFont(unmFont);
    m_unmanagedTitleLabel->setAlignment(Qt::AlignCenter);
    unmLayout->addWidget(m_unmanagedTitleLabel, 0, Qt::AlignCenter);

    m_unmanagedDescLabel = new QLabel(unmanagedPage);
    m_unmanagedDescLabel->setWordWrap(true);
    m_unmanagedDescLabel->setAlignment(Qt::AlignCenter);
    unmLayout->addWidget(m_unmanagedDescLabel, 0, Qt::AlignCenter);
    unmLayout->addStretch(1);

    m_modeStack->addWidget(unmanagedPage);

    workLayout->addWidget(m_modeStack, 1);

    workScroll->setWidget(workPage);
    m_rightStack->addWidget(workScroll);

    m_splitter->addWidget(m_rightStack);
    m_splitter->setSizes({270, 770});

    root->addWidget(m_splitter, 1);
}

void CoolingTabWidget::setDevices(const QVector<DeviceInfo>& devices)
{
    m_devices = devices;
    rebuildFanTree();
    refreshProfileMetaAndLiveTemp();
}

void CoolingTabWidget::setOverrides(const NameOverrides& overrides)
{
    m_overrides = overrides;
    rebuildFanTree();
    refreshProfileMetaAndLiveTemp();
    updateSelectedFanTelemetryHeader();
}

void CoolingTabWidget::setProfiles(const QVector<ProfileInfo>& profiles)
{
    m_profiles = profiles;
    refreshSidebarProfilesAndFunctions();
    refreshProfileCombo();

    if (!m_selectedDeviceUid.isEmpty() && !m_selectedChannelName.isEmpty() && !m_profileDirty) {
        if (!m_editingProfile.uid.isEmpty()) {
            loadProfileIntoEditor(m_editingProfile.uid);
        }
    }
}

void CoolingTabWidget::setFunctions(const QVector<FunctionInfo>& functions)
{
    m_functions = functions;
    refreshSidebarProfilesAndFunctions();
    refreshProfileMetaAndLiveTemp();
}

void CoolingTabWidget::setDeviceSettings(const QString& deviceUid,
                                         const QVector<ChannelSetting>& settings)
{
    m_settingsByDevice.insert(deviceUid, settings);
    if (deviceUid == m_selectedDeviceUid && !m_selectedChannelName.isEmpty() && !m_pendingApply) {
        loadSelectedFanWorkspace();
    }
}

void CoolingTabWidget::updateStatus(const QVector<DeviceStatus>& statuses)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (const DeviceStatus& st : statuses) {
        m_lastStatusByDevice.insert(st.uid, st);
        for (const ChannelReading& ch : st.channels) {
            if (!ch.duty.has_value() && !ch.rpm.has_value()) {
                continue;
            }
            const QString key = historyKey(st.uid, ch.name);
            auto& queue = m_historyByChannel[key];
            queue.push_back(FanHistoryPoint{nowMs, ch.duty, ch.rpm});
            while (queue.size() > kMaxFanHistorySamples) {
                queue.pop_front();
            }
        }
    }

    updateFanTreeTelemetry();
    updateSelectedFanTelemetryHeader();
    updateCurveLiveTelemetry();

    if (!m_selectedDeviceUid.isEmpty() && !m_selectedChannelName.isEmpty()) {
        const QString key = historyKey(m_selectedDeviceUid, m_selectedChannelName);
        m_historyChart->setHistory(m_historyByChannel.value(key));
    }
}

void CoolingTabWidget::handleChannelWriteResult(const QString& deviceUid,
                                                const QString& channelName,
                                                bool success)
{
    Q_UNUSED(deviceUid)
    Q_UNUSED(channelName)
    if (m_pendingProfileChain) {
        m_pendingProfileChain = false;
    }
    m_pendingApply = false;
    updateApplyEnabled();
    if (success) {
        m_profileDirty = false;
    }
}

void CoolingTabWidget::handleProfileWriteResult(const QString& profileUid, bool success)
{
    if (!m_pendingProfileChain) {
        return;
    }

    if (!success) {
        m_pendingProfileChain = false;
        m_pendingApply = false;
        updateApplyEnabled();
        return;
    }

    m_profileDirty = false;
    const QString devUid = m_chainDeviceUid;
    const QString chName = m_chainChannelName;
    const QString pUid = m_chainProfileUid.isEmpty() ? profileUid : m_chainProfileUid;
    m_pendingProfileChain = false;

    if (m_client != nullptr && !devUid.isEmpty() && !chName.isEmpty() && !pUid.isEmpty()) {
        m_client->setChannelProfile(devUid, chName, pUid);
    } else {
        m_pendingApply = false;
        updateApplyEnabled();
    }
}

QByteArray CoolingTabWidget::saveSplitterState() const
{
    return m_splitter != nullptr ? m_splitter->saveState() : QByteArray();
}

bool CoolingTabWidget::restoreSplitterState(const QByteArray& state)
{
    if (m_splitter == nullptr || state.isEmpty()) {
        return false;
    }
    return m_splitter->restoreState(state);
}

void CoolingTabWidget::rebuildFanTree()
{
    const bool wasBlocked = m_fansTree->blockSignals(true);
    m_fansTree->clear();
    m_fanItemsByKey.clear();

    const QSet<QString> dups = findDuplicateDeviceNames(m_devices, m_overrides);
    QTreeWidgetItem* itemToSelect = nullptr;
    QTreeWidgetItem* firstLeaf = nullptr;

    for (const DeviceInfo& dev : std::as_const(m_devices)) {
        if (!dev.hasControllableChannels()) {
            continue;
        }
        const bool disambiguate = dups.contains(resolveDeviceDisplayName(dev, m_overrides, false));
        const QString devName = resolveDeviceDisplayName(dev, m_overrides, disambiguate);

        auto* devItem = new QTreeWidgetItem(m_fansTree);
        devItem->setText(0, devName);
        devItem->setFlags(Qt::ItemIsEnabled);
        QFont devFont = devItem->font(0);
        devFont.setBold(true);
        devItem->setFont(0, devFont);

        for (const ChannelInfo& ch : dev.channels) {
            if (!ch.isControllable()) {
                continue;
            }
            const QString chLabel =
                resolveChannelDisplayName(dev.uid, ch.name, ch.label, m_overrides);
            auto* chItem = new QTreeWidgetItem(devItem);
            chItem->setText(0, chLabel);
            chItem->setText(1, QStringLiteral("—"));
            chItem->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            chItem->setData(0, Qt::UserRole, dev.uid);
            chItem->setData(0, Qt::UserRole + 1, ch.name);

            const QString key = historyKey(dev.uid, ch.name);
            m_fanItemsByKey.insert(key, chItem);

            if (firstLeaf == nullptr) {
                firstLeaf = chItem;
            }
            if (dev.uid == m_selectedDeviceUid && ch.name == m_selectedChannelName) {
                itemToSelect = chItem;
            }
        }
        devItem->setExpanded(true);
    }

    const bool hasFans = !m_fanItemsByKey.isEmpty();
    m_fansTree->setVisible(hasFans);
    m_fansEmptyLabel->setVisible(!hasFans);

    if (itemToSelect == nullptr) {
        itemToSelect = firstLeaf;
    }

    if (itemToSelect != nullptr) {
        m_fansTree->setCurrentItem(itemToSelect);
        m_selectedDeviceUid = itemToSelect->data(0, Qt::UserRole).toString();
        m_selectedChannelName = itemToSelect->data(0, Qt::UserRole + 1).toString();
        m_rightStack->setCurrentIndex(1);
    } else {
        m_selectedDeviceUid.clear();
        m_selectedChannelName.clear();
        m_rightStack->setCurrentIndex(0);
    }

    m_fansTree->blockSignals(wasBlocked);

    updateFanTreeTelemetry();
    if (!m_selectedDeviceUid.isEmpty() && !m_selectedChannelName.isEmpty()) {
        loadSelectedFanWorkspace();
    }
}

void CoolingTabWidget::updateFanTreeTelemetry()
{
    for (auto it = m_fanItemsByKey.cbegin(); it != m_fanItemsByKey.cend(); ++it) {
        QTreeWidgetItem* item = it.value();
        const QString devUid = item->data(0, Qt::UserRole).toString();
        const QString chName = item->data(0, Qt::UserRole + 1).toString();

        const auto stIt = m_lastStatusByDevice.constFind(devUid);
        if (stIt == m_lastStatusByDevice.constEnd()) {
            continue;
        }
        for (const ChannelReading& cr : stIt->channels) {
            if (cr.name != chName) {
                continue;
            }
            QStringList parts;
            if (cr.duty.has_value()) {
                parts.append(QStringLiteral("%1%").arg(QString::number(*cr.duty, 'f', 0)));
            }
            if (cr.rpm.has_value()) {
                parts.append(QStringLiteral("%1 RPM").arg(*cr.rpm));
            }
            item->setText(1, parts.isEmpty() ? QStringLiteral("—") : parts.join(QStringLiteral(" · ")));
            break;
        }
    }
}

void CoolingTabWidget::refreshSidebarProfilesAndFunctions()
{
    m_profilesList->clear();
    for (const ProfileInfo& p : std::as_const(m_profiles)) {
        const QString displayName = (p.uid == QLatin1String("0") && p.name == QLatin1String("Unmanaged"))
            ? tr("Unmanaged")
            : p.name;
        const QString typeText = tr(p.type.toUtf8().constData());
        auto* item = new QListWidgetItem(QStringLiteral("%1 · %2").arg(displayName, typeText),
                                         m_profilesList);
        item->setData(Qt::UserRole, p.uid);
        item->setToolTip(displayName);
    }

    m_functionsList->clear();
    const bool hasFns = !m_functions.isEmpty();
    m_functionsList->setVisible(hasFns);
    m_functionsEmptyLabel->setVisible(!hasFns);
    for (const FunctionInfo& fn : std::as_const(m_functions)) {
        const QString displayName =
            (fn.uid == QLatin1String("0") && fn.name == QLatin1String("Default Function"))
            ? tr("Default Function")
            : fn.name;
        const QString typeText = tr(fn.type.toUtf8().constData());
        auto* item = new QListWidgetItem(QStringLiteral("%1 · %2").arg(displayName, typeText),
                                         m_functionsList);
        item->setData(Qt::UserRole, fn.uid);
        item->setToolTip(displayName);
    }
}

void CoolingTabWidget::onFanTreeSelectionChanged()
{
    QTreeWidgetItem* item = m_fansTree->currentItem();
    if (item == nullptr) {
        return;
    }
    const QString devUid = item->data(0, Qt::UserRole).toString();
    const QString chName = item->data(0, Qt::UserRole + 1).toString();
    if (devUid.isEmpty() || chName.isEmpty()) {
        return;
    }
    if (devUid == m_selectedDeviceUid && chName == m_selectedChannelName) {
        return;
    }
    m_selectedDeviceUid = devUid;
    m_selectedChannelName = chName;
    loadSelectedFanWorkspace();
}

void CoolingTabWidget::loadSelectedFanWorkspace()
{
    const DeviceInfo* dev = findDevice(m_selectedDeviceUid);
    if (dev == nullptr) {
        m_rightStack->setCurrentIndex(0);
        return;
    }
    const ChannelInfo* ch = findChannel(*dev, m_selectedChannelName);
    if (ch == nullptr) {
        m_rightStack->setCurrentIndex(0);
        return;
    }

    m_rightStack->setCurrentIndex(1);

    const int minDuty = ch->speedOptions.has_value() ? ch->speedOptions->minDuty : 0;
    const int maxDuty = ch->speedOptions.has_value() ? ch->speedOptions->maxDuty : 100;
    updateManualControlsBounds(minDuty, maxDuty);

    const auto settingOpt = findChannelSetting(m_selectedDeviceUid, m_selectedChannelName);
    if (settingOpt.has_value() && settingOpt->speedFixed.has_value()) {
        const int fixedVal = std::clamp<int>(*settingOpt->speedFixed, minDuty, maxDuty);
        m_syncingManual = true;
        m_manualSlider->setValue(fixedVal);
        m_manualSpin->setValue(fixedVal);
        m_fanGauge->setTargetDuty(fixedVal, true);
        m_syncingManual = false;
        syncModeButtonsAndPage(ModeManual);
    } else if (settingOpt.has_value() && settingOpt->profileUid.has_value()
               && *settingOpt->profileUid != QLatin1String("0")) {
        syncModeButtonsAndPage(ModeProfile);
        loadProfileIntoEditor(*settingOpt->profileUid);
    } else {
        // Default ("0") or unset -> start on Profile with Default selected (or Unmanaged if user prefers)
        syncModeButtonsAndPage(ModeProfile);
        loadProfileIntoEditor(QStringLiteral("0"));
    }

    refreshProfileCombo();
    updateSelectedFanTelemetryHeader();
    m_historyChart->setHistory(
        m_historyByChannel.value(historyKey(m_selectedDeviceUid, m_selectedChannelName)));
}

void CoolingTabWidget::syncModeButtonsAndPage(ControlMode mode)
{
    m_currentMode = mode;
    if (QAbstractButton* btn = m_modeGroup->button(mode)) {
        btn->setChecked(true);
    }
    m_modeStack->setCurrentIndex(static_cast<int>(mode));
    if (m_fanGauge != nullptr) {
        m_fanGauge->setTargetDuty(m_manualSpin->value(), mode == ModeManual);
    }
    updateApplyEnabled();
}

void CoolingTabWidget::onModeButtonClicked(int id)
{
    if (id < 0 || id > 2) {
        return;
    }
    const ControlMode newMode = static_cast<ControlMode>(id);
    if (newMode == ModeManual) {
        // Seed manual slider with live duty if switching from non-manual and slider is still 0
        if (m_currentMode != ModeManual) {
            const auto stIt = m_lastStatusByDevice.constFind(m_selectedDeviceUid);
            if (stIt != m_lastStatusByDevice.constEnd()) {
                for (const ChannelReading& cr : stIt->channels) {
                    if (cr.name == m_selectedChannelName && cr.duty.has_value()) {
                        const int live = std::clamp(static_cast<int>(std::round(*cr.duty)),
                                                    m_manualSpin->minimum(),
                                                    m_manualSpin->maximum());
                        m_syncingManual = true;
                        m_manualSlider->setValue(live);
                        m_manualSpin->setValue(live);
                        m_syncingManual = false;
                        break;
                    }
                }
            }
        }
    }
    syncModeButtonsAndPage(newMode);
}

void CoolingTabWidget::refreshProfileCombo()
{
    if (m_profileCombo == nullptr) {
        return;
    }
    if (m_profileCombo->view() && m_profileCombo->view()->isVisible()) {
        return;
    }

    m_profileCombo->clear();
    const QFontMetrics fm = m_profileCombo->fontMetrics();
    int maxW = 0;
    int targetIdx = -1;

    for (const ProfileInfo& p : std::as_const(m_profiles)) {
        const QString displayName = (p.uid == QLatin1String("0") && p.name == QLatin1String("Unmanaged"))
            ? tr("Unmanaged")
            : p.name;
        const QString typeStr = tr(p.type.toUtf8().constData());
        const QString label = (p.type == QLatin1String("Default"))
            ? displayName
            : QStringLiteral("%1 (%2)").arg(displayName, typeStr);
        m_profileCombo->addItem(label, p.uid);
        m_profileCombo->setItemData(m_profileCombo->count() - 1, label, Qt::ToolTipRole);
        maxW = std::max(maxW, fm.horizontalAdvance(label));
        if (p.uid == m_editingProfile.uid) {
            targetIdx = m_profileCombo->count() - 1;
        }
    }

    if (m_profileCombo->view() != nullptr && maxW > 0) {
        m_profileCombo->view()->setMinimumWidth(maxW + 36);
    }

    if (targetIdx >= 0) {
        m_profileCombo->setCurrentIndex(targetIdx);
    } else if (m_profileCombo->count() > 0) {
        m_profileCombo->setCurrentIndex(0);
    }
    m_profileCombo->setToolTip(m_profileCombo->currentText());
}

void CoolingTabWidget::onProfileComboActivated(int index)
{
    const QString uid = m_profileCombo->itemData(index).toString();
    if (!uid.isEmpty()) {
        loadProfileIntoEditor(uid);
    }
}

void CoolingTabWidget::loadProfileIntoEditor(const QString& profileUid)
{
    const ProfileInfo* found = findProfile(profileUid);
    if (found != nullptr) {
        m_editingProfile = *found;
    } else if (!m_profiles.isEmpty()) {
        m_editingProfile = m_profiles.first();
    } else {
        m_editingProfile = ProfileInfo{};
    }
    m_profileDirty = false;

    const DeviceInfo* dev = findDevice(m_selectedDeviceUid);
    const ChannelInfo* ch = dev ? findChannel(*dev, m_selectedChannelName) : nullptr;
    const quint8 minDuty = (ch && ch->speedOptions) ? ch->speedOptions->minDuty : 0;
    const quint8 maxDuty = (ch && ch->speedOptions) ? ch->speedOptions->maxDuty : 100;
    const int minPts = dev ? dev->profileMinLength : 2;
    const int maxPts = dev ? dev->profileMaxLength : 21;

    if (m_editingProfile.type == QLatin1String("Graph")) {
        m_profileInnerStack->setCurrentIndex(0);
        double tMin = m_editingProfile.tempMin.value_or(dev ? static_cast<double>(dev->tempMin) : 0.0);
        double tMax = m_editingProfile.tempMax.value_or(dev ? static_cast<double>(dev->tempMax) : 100.0);
        for (const auto& pt : std::as_const(m_editingProfile.speedProfile)) {
            tMin = std::min(tMin, pt.first);
            tMax = std::max(tMax, pt.first);
        }
        m_curveEditor->setBounds(tMin, tMax, minDuty, maxDuty, minPts, maxPts);
        m_curveEditor->setReadOnly(false);
        m_curveEditor->setPoints(m_editingProfile.speedProfile);
        populatePointsTable();
        updateCurveLiveTelemetry();
    } else if (m_editingProfile.type == QLatin1String("Fixed")) {
        m_profileInnerStack->setCurrentIndex(1);
        m_fixedSlider->setRange(minDuty, maxDuty);
        m_fixedSpin->setRange(minDuty, maxDuty);
        const int fixedVal = std::clamp<int>(m_editingProfile.speedFixed.value_or(50),
                                             minDuty,
                                             maxDuty);
        const bool sb = m_fixedSlider->blockSignals(true);
        const bool spb = m_fixedSpin->blockSignals(true);
        m_fixedSlider->setValue(fixedVal);
        m_fixedSpin->setValue(fixedVal);
        m_fixedSlider->blockSignals(sb);
        m_fixedSpin->blockSignals(spb);
    } else {
        m_profileInnerStack->setCurrentIndex(2);
        const QString tName = m_editingProfile.type.isEmpty()
            ? tr("Default")
            : tr(m_editingProfile.type.toUtf8().constData());
        m_otherProfileInfoLabel->setText(
            tr("This profile type (%1) is evaluated by coolercontrold and can be assigned directly with Apply.")
                .arg(tName));
    }

    refreshProfileMetaAndLiveTemp();
    updateApplyEnabled();
}

void CoolingTabWidget::refreshProfileMetaAndLiveTemp()
{
    if (m_editingProfile.uid.isEmpty()) {
        m_profileTypeValue->setText(QStringLiteral("—"));
        m_tempSourceValue->setText(QStringLiteral("—"));
        m_functionValue->setText(QStringLiteral("—"));
        return;
    }

    const QString tName = m_editingProfile.type.isEmpty()
        ? tr("Default")
        : tr(m_editingProfile.type.toUtf8().constData());
    m_profileTypeValue->setText(tName);

    if (m_editingProfile.tempSource.has_value()) {
        const TempSource& ts = *m_editingProfile.tempSource;
        const DeviceInfo* srcDev = findDevice(ts.deviceUid);
        QString devLabel = ts.deviceUid;
        QString tempLabel = ts.tempName;
        if (srcDev != nullptr) {
            const QSet<QString> dups = findDuplicateDeviceNames(m_devices, m_overrides);
            const bool disambig =
                dups.contains(resolveDeviceDisplayName(*srcDev, m_overrides, false));
            devLabel = resolveDeviceDisplayName(*srcDev, m_overrides, disambig);
            QString infoLbl;
            for (const TempInfo& ti : srcDev->temps) {
                if (ti.name == ts.tempName) {
                    infoLbl = ti.label;
                    break;
                }
            }
            tempLabel = resolveChannelDisplayName(ts.deviceUid, ts.tempName, infoLbl, m_overrides);
        }

        QString text = QStringLiteral("%1 · %2").arg(devLabel, tempLabel);
        const auto stIt = m_lastStatusByDevice.constFind(ts.deviceUid);
        if (stIt != m_lastStatusByDevice.constEnd()) {
            for (const TempReading& trReading : stIt->temps) {
                if (trReading.name == ts.tempName) {
                    text += QStringLiteral(" (%1 °C)").arg(QString::number(trReading.temp, 'f', 1));
                    break;
                }
            }
        }
        m_tempSourceValue->setText(text);
    } else {
        m_tempSourceValue->setText(QStringLiteral("—"));
    }

    if (const FunctionInfo* fn = findFunction(m_editingProfile.functionUid)) {
        const QString fnName =
            (fn->uid == QLatin1String("0") && fn->name == QLatin1String("Default Function"))
            ? tr("Default Function")
            : fn->name;
        m_functionValue->setText(fnName);
    } else if (!m_editingProfile.functionUid.isEmpty()) {
        m_functionValue->setText(m_editingProfile.functionUid);
    } else {
        m_functionValue->setText(QStringLiteral("—"));
    }
}

void CoolingTabWidget::populatePointsTable()
{
    if (m_pointsTable == nullptr) {
        return;
    }

    m_syncingPoints = true;
    const bool wasBlocked = m_pointsTable->blockSignals(true);

    const DeviceInfo* dev = findDevice(m_selectedDeviceUid);
    const ChannelInfo* ch = dev ? findChannel(*dev, m_selectedChannelName) : nullptr;
    const int minDuty = (ch && ch->speedOptions) ? ch->speedOptions->minDuty : 0;
    const int maxDuty = (ch && ch->speedOptions) ? ch->speedOptions->maxDuty : 100;
    const int minPts = dev ? dev->profileMinLength : 2;
    const int maxPts = dev ? dev->profileMaxLength : 21;

    double tMin = m_editingProfile.tempMin.value_or(dev ? static_cast<double>(dev->tempMin) : 0.0);
    double tMax = m_editingProfile.tempMax.value_or(dev ? static_cast<double>(dev->tempMax) : 100.0);
    for (const auto& pt : std::as_const(m_editingProfile.speedProfile)) {
        tMin = std::min(tMin, pt.first);
        tMax = std::max(tMax, pt.first);
    }

    const int n = m_editingProfile.speedProfile.size();
    m_pointsTable->setRowCount(n);

    for (int i = 0; i < n; ++i) {
        const auto& pt = m_editingProfile.speedProfile.at(i);

        auto* tempSpin = new QDoubleSpinBox(m_pointsTable);
        tempSpin->setDecimals(1);
        tempSpin->setSingleStep(0.5);
        tempSpin->setRange(tMin, tMax);
        tempSpin->setValue(pt.first);
        tempSpin->setSuffix(QStringLiteral(" °C"));

        auto* dutySpin = new QSpinBox(m_pointsTable);
        dutySpin->setRange(minDuty, maxDuty);
        dutySpin->setValue(pt.second);
        dutySpin->setSuffix(QStringLiteral("%"));

        connect(tempSpin, &QDoubleSpinBox::valueChanged, this, [this, i](double val) {
            if (m_syncingPoints || i < 0 || i >= m_editingProfile.speedProfile.size()) {
                return;
            }
            const double rounded = std::round(val * 10.0) / 10.0;
            m_editingProfile.speedProfile[i].first = rounded;
            m_profileDirty = true;

            const bool prevOrdered = (i == 0 || m_editingProfile.speedProfile.at(i - 1).first <= rounded);
            const bool nextOrdered =
                (i + 1 >= m_editingProfile.speedProfile.size()
                 || rounded <= m_editingProfile.speedProfile.at(i + 1).first);
            if (prevOrdered && nextOrdered) {
                m_syncingPoints = true;
                m_curveEditor->setPoints(m_editingProfile.speedProfile);
                m_curveEditor->setSelectedIndex(i);
                m_syncingPoints = false;
            }
        });

        connect(tempSpin, &QDoubleSpinBox::editingFinished, this, [this]() {
            if (m_syncingPoints) {
                return;
            }
            std::sort(m_editingProfile.speedProfile.begin(),
                      m_editingProfile.speedProfile.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            m_syncingPoints = true;
            m_curveEditor->setPoints(m_editingProfile.speedProfile);
            m_syncingPoints = false;
            populatePointsTable();
        });

        connect(dutySpin, &QSpinBox::valueChanged, this, [this, i](int val) {
            if (m_syncingPoints || i < 0 || i >= m_editingProfile.speedProfile.size()) {
                return;
            }
            m_editingProfile.speedProfile[i].second = static_cast<quint8>(std::clamp(val, 0, 100));
            m_profileDirty = true;
            m_syncingPoints = true;
            m_curveEditor->setPoints(m_editingProfile.speedProfile);
            m_curveEditor->setSelectedIndex(i);
            m_syncingPoints = false;
        });

        m_pointsTable->setCellWidget(i, 0, tempSpin);
        m_pointsTable->setCellWidget(i, 1, dutySpin);
    }

    m_addPointBtn->setEnabled(n < maxPts);
    m_removePointBtn->setEnabled(n > minPts);

    m_pointsTable->blockSignals(wasBlocked);
    m_syncingPoints = false;
}

void CoolingTabWidget::onAddPointClicked()
{
    const DeviceInfo* dev = findDevice(m_selectedDeviceUid);
    const int maxPts = dev ? dev->profileMaxLength : 21;
    if (m_editingProfile.speedProfile.size() >= maxPts) {
        return;
    }

    double newTemp = 50.0;
    quint8 newDuty = 50;
    const auto& pts = m_editingProfile.speedProfile;
    if (pts.size() >= 2) {
        // Insert midpoint in largest temperature gap
        int bestGapIdx = 0;
        double bestGap = -1.0;
        for (int i = 0; i + 1 < pts.size(); ++i) {
            const double gap = pts.at(i + 1).first - pts.at(i).first;
            if (gap > bestGap) {
                bestGap = gap;
                bestGapIdx = i;
            }
        }
        newTemp = std::round(((pts.at(bestGapIdx).first + pts.at(bestGapIdx + 1).first) * 0.5) * 10.0)
            / 10.0;
        newDuty = static_cast<quint8>(
            (static_cast<int>(pts.at(bestGapIdx).second) + pts.at(bestGapIdx + 1).second) / 2);
    } else if (pts.size() == 1) {
        newTemp = std::min(100.0, pts.at(0).first + 10.0);
        newDuty = std::min<quint8>(100, static_cast<quint8>(pts.at(0).second + 15));
    }

    m_editingProfile.speedProfile.append({newTemp, newDuty});
    std::sort(m_editingProfile.speedProfile.begin(),
              m_editingProfile.speedProfile.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    m_profileDirty = true;

    m_syncingPoints = true;
    m_curveEditor->setPoints(m_editingProfile.speedProfile);
    m_syncingPoints = false;
    populatePointsTable();
}

void CoolingTabWidget::onRemovePointClicked()
{
    const DeviceInfo* dev = findDevice(m_selectedDeviceUid);
    const int minPts = dev ? dev->profileMinLength : 2;
    if (m_editingProfile.speedProfile.size() <= minPts) {
        return;
    }

    int row = m_pointsTable->currentRow();
    if (row < 0 || row >= m_editingProfile.speedProfile.size()) {
        row = m_editingProfile.speedProfile.size() - 1;
    }
    m_editingProfile.speedProfile.removeAt(row);
    m_profileDirty = true;

    m_syncingPoints = true;
    m_curveEditor->setPoints(m_editingProfile.speedProfile);
    m_syncingPoints = false;
    populatePointsTable();
}

void CoolingTabWidget::updateCurveLiveTelemetry()
{
    if (m_curveEditor == nullptr || m_selectedDeviceUid.isEmpty()) {
        return;
    }

    std::optional<double> liveDuty;
    const auto fanStIt = m_lastStatusByDevice.constFind(m_selectedDeviceUid);
    if (fanStIt != m_lastStatusByDevice.constEnd()) {
        for (const ChannelReading& cr : fanStIt->channels) {
            if (cr.name == m_selectedChannelName) {
                liveDuty = cr.duty;
                break;
            }
        }
    }

    std::optional<double> liveTemp;
    if (m_editingProfile.tempSource.has_value()) {
        const TempSource& ts = *m_editingProfile.tempSource;
        const auto tempStIt = m_lastStatusByDevice.constFind(ts.deviceUid);
        if (tempStIt != m_lastStatusByDevice.constEnd()) {
            for (const TempReading& trReading : tempStIt->temps) {
                if (trReading.name == ts.tempName) {
                    liveTemp = trReading.temp;
                    break;
                }
            }
        }
    }

    m_curveEditor->setLiveTelemetry(liveTemp, liveDuty);
    refreshProfileMetaAndLiveTemp();
}

void CoolingTabWidget::updateManualControlsBounds(int minDuty, int maxDuty)
{
    m_syncingManual = true;
    m_manualSlider->setRange(minDuty, maxDuty);
    m_manualSpin->setRange(minDuty, maxDuty);
    m_fanGauge->setDutyRange(minDuty, maxDuty);
    m_syncingManual = false;
}

void CoolingTabWidget::updateSelectedFanTelemetryHeader()
{
    if (m_selectedDeviceUid.isEmpty() || m_selectedChannelName.isEmpty()) {
        return;
    }

    const DeviceInfo* dev = findDevice(m_selectedDeviceUid);
    const ChannelInfo* ch = dev ? findChannel(*dev, m_selectedChannelName) : nullptr;
    if (dev == nullptr || ch == nullptr) {
        return;
    }

    const QSet<QString> dups = findDuplicateDeviceNames(m_devices, m_overrides);
    const bool disambig = dups.contains(resolveDeviceDisplayName(*dev, m_overrides, false));
    const QString devName = resolveDeviceDisplayName(*dev, m_overrides, disambig);
    const QString chLabel =
        resolveChannelDisplayName(dev->uid, ch->name, ch->label, m_overrides);
    m_fanTitleLabel->setText(QStringLiteral("%1 — %2").arg(devName, chLabel));

    std::optional<double> liveDuty;
    std::optional<quint32> liveRpm;
    const auto stIt = m_lastStatusByDevice.constFind(m_selectedDeviceUid);
    if (stIt != m_lastStatusByDevice.constEnd()) {
        for (const ChannelReading& cr : stIt->channels) {
            if (cr.name == m_selectedChannelName) {
                liveDuty = cr.duty;
                liveRpm = cr.rpm;
                break;
            }
        }
    }

    QStringList parts;
    if (liveDuty.has_value()) {
        parts.append(QStringLiteral("%1%").arg(QString::number(*liveDuty, 'f', 0)));
    }
    if (liveRpm.has_value()) {
        parts.append(tr("%1 RPM").arg(*liveRpm));
    }
    m_fanTelemetryLabel->setText(parts.isEmpty() ? QStringLiteral("—")
                                                 : parts.join(QStringLiteral("  ·  ")));

    if (m_fanGauge != nullptr) {
        m_fanGauge->setLiveMetrics(liveDuty, liveRpm);
    }
}

void CoolingTabWidget::onApplyClicked()
{
    if (m_client == nullptr || m_pendingApply || m_selectedDeviceUid.isEmpty()
        || m_selectedChannelName.isEmpty()) {
        return;
    }

    m_pendingApply = true;
    updateApplyEnabled();

    if (m_currentMode == ModeManual) {
        const quint8 duty = static_cast<quint8>(std::clamp(m_manualSpin->value(), 0, 100));
        m_client->setChannelManual(m_selectedDeviceUid, m_selectedChannelName, duty);
        return;
    }

    if (m_currentMode == ModeUnmanaged) {
        m_client->resetChannelSetting(m_selectedDeviceUid, m_selectedChannelName);
        return;
    }

    // ModeProfile
    const QString profileUid = m_editingProfile.uid;
    if (profileUid.isEmpty()) {
        m_pendingApply = false;
        updateApplyEnabled();
        return;
    }

    const bool needsProfilePut = m_profileDirty && profileUid != QLatin1String("0")
        && (m_editingProfile.type == QLatin1String("Graph")
            || m_editingProfile.type == QLatin1String("Fixed"));

    if (needsProfilePut) {
        m_pendingProfileChain = true;
        m_chainDeviceUid = m_selectedDeviceUid;
        m_chainChannelName = m_selectedChannelName;
        m_chainProfileUid = profileUid;
        m_client->updateProfile(m_editingProfile);
    } else {
        m_client->setChannelProfile(m_selectedDeviceUid, m_selectedChannelName, profileUid);
    }
}

void CoolingTabWidget::updateApplyEnabled()
{
    const bool hasSelection = !m_selectedDeviceUid.isEmpty() && !m_selectedChannelName.isEmpty();
    m_applyBtn->setEnabled(hasSelection && !m_pendingApply);
}

void CoolingTabWidget::refreshMutedPalettes()
{
    applyMuted(m_fanTelemetryLabel, palette());
    applyMuted(m_profileTypeCaption, palette());
    applyMuted(m_tempSourceCaption, palette());
    applyMuted(m_functionCaption, palette());
    applyMuted(m_unmanagedDescLabel, palette());
}

void CoolingTabWidget::retranslateUi()
{
    m_fansGroup->setTitle(tr("Controllable Fans"));
    m_fansEmptyLabel->setText(tr("No controllable fan channels detected."));
    m_profilesGroup->setTitle(tr("Profiles"));
    m_functionsGroup->setTitle(tr("Functions"));
    m_functionsEmptyLabel->setText(tr("No functions found."));

    m_emptyWorkspaceLabel->setText(tr("Select a controllable fan channel from the left panel."));

    m_modeProfileBtn->setText(tr("Profile"));
    m_modeManualBtn->setText(tr("Manual"));
    m_modeUnmanagedBtn->setText(tr("Unmanaged"));
    m_applyBtn->setText(tr("Apply"));

    m_profileSelectLabel->setText(tr("Profile:"));
    m_profileTypeCaption->setText(tr("Type:"));
    m_tempSourceCaption->setText(tr("Temp Source:"));
    m_functionCaption->setText(tr("Function:"));

    m_pointsGroup->setTitle(tr("Points"));
    m_pointsTable->setHorizontalHeaderLabels({tr("Temp (°C)"), tr("Duty (%)")});
    m_addPointBtn->setText(tr("Add Point"));
    m_removePointBtn->setText(tr("Remove Point"));
    m_fixedSpeedLabel->setText(tr("Fixed Speed:"));

    m_unmanagedTitleLabel->setText(tr("Reset to Hardware Default"));
    m_unmanagedDescLabel->setText(
        tr("Unmanaged mode hands fan control back to hardware / BIOS defaults."));

    refreshSidebarProfilesAndFunctions();
    refreshProfileCombo();
    refreshProfileMetaAndLiveTemp();
    updateFanTreeTelemetry();
    updateSelectedFanTelemetryHeader();
    if (m_fanGauge != nullptr) {
        m_fanGauge->update();
    }
    if (m_historyChart != nullptr) {
        m_historyChart->update();
    }
    if (m_curveEditor != nullptr) {
        m_curveEditor->update();
    }
    if (!m_editingProfile.uid.isEmpty() && m_editingProfile.type != QLatin1String("Graph")
        && m_editingProfile.type != QLatin1String("Fixed")) {
        const QString tName = m_editingProfile.type.isEmpty()
            ? tr("Default")
            : tr(m_editingProfile.type.toUtf8().constData());
        m_otherProfileInfoLabel->setText(
            tr("This profile type (%1) is evaluated by coolercontrold and can be assigned directly with Apply.")
                .arg(tName));
    }
}

void CoolingTabWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    } else if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        refreshMutedPalettes();
    }
}

const DeviceInfo* CoolingTabWidget::findDevice(const QString& uid) const
{
    for (const DeviceInfo& d : m_devices) {
        if (d.uid == uid) {
            return &d;
        }
    }
    return nullptr;
}

const ChannelInfo* CoolingTabWidget::findChannel(const DeviceInfo& dev,
                                                 const QString& channelName) const
{
    for (const ChannelInfo& ch : dev.channels) {
        if (ch.name == channelName) {
            return &ch;
        }
    }
    return nullptr;
}

const ProfileInfo* CoolingTabWidget::findProfile(const QString& uid) const
{
    for (const ProfileInfo& p : m_profiles) {
        if (p.uid == uid) {
            return &p;
        }
    }
    return nullptr;
}

const FunctionInfo* CoolingTabWidget::findFunction(const QString& uid) const
{
    for (const FunctionInfo& fn : m_functions) {
        if (fn.uid == uid) {
            return &fn;
        }
    }
    return nullptr;
}

std::optional<ChannelSetting> CoolingTabWidget::findChannelSetting(const QString& deviceUid,
                                                                   const QString& channelName) const
{
    const auto it = m_settingsByDevice.constFind(deviceUid);
    if (it == m_settingsByDevice.constEnd()) {
        return std::nullopt;
    }
    for (const ChannelSetting& cs : *it) {
        if (cs.channelName == channelName) {
            return cs;
        }
    }
    return std::nullopt;
}

QString CoolingTabWidget::historyKey(const QString& deviceUid, const QString& channelName)
{
    return deviceUid + QLatin1Char('\n') + channelName;
}

} // namespace ccqt
