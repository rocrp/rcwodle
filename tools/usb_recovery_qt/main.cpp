#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QDataStream>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMap>
#include <QtCore/QPoint>
#include <QtCore/QTimer>
#include <QtCore/QVector>
#include <QtGui/QAction>
#include <QtGui/QGuiApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QPixmap>
#include <QtGui/QScreen>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QStyle>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>
#include <stdexcept>

namespace {

constexpr quint32 kMagic = 0x31525648U;
constexpr quint8 kVersion = 1U;
constexpr int kMaxPayload = 2048;
constexpr quint16 kHivetonVid = 0x38f4;
constexpr quint16 kRecoveryPid = 0x1001;
constexpr const char *kExpectedBoard = "sf32lb52-lcd_n16r8";

enum Command : quint8 {
    UsbRecoveryCmdHello = 1,
    UsbRecoveryCmdBegin = 2,
    UsbRecoveryCmdErase = 3,
    UsbRecoveryCmdWrite = 4,
    UsbRecoveryCmdVerify = 5,
    UsbRecoveryCmdCommit = 6,
    UsbRecoveryCmdReboot = 7,
};

enum RecoveryStatus : quint32 {
    RecoveryStatusOk = 0,
    RecoveryStatusBadFrame = 1,
    RecoveryStatusDenied = 2,
    RecoveryStatusFlashError = 3,
    RecoveryStatusCrcError = 4,
    RecoveryStatusBadPartition = 5,
};

enum Columns {
    ColEnable = 0,
    ColPartition,
    ColFile,
    ColSize,
    ColCrc,
    ColAddress,
    ColRegion,
    ColStatus,
    ColCount
};

struct FirmwareFile {
    QString name;
    QString path;
    QString partition;
    quint32 addr = 0;
    quint32 regionSize = 0;
    quint32 size = 0;
    quint32 crc32 = 0;
    bool selected = true;
    bool localValid = false;
};

struct Response {
    quint8 cmd = 0;
    quint16 seq = 0;
    quint32 size = 0;
    quint32 crc = 0;
    quint32 status = 0;
    QByteArray payload;
};

quint32 parseNumber(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString().toUInt(nullptr, 0);
    }
    return static_cast<quint32>(value.toInteger());
}

quint32 crc32Update(const QByteArray &data, quint32 seed = 0xFFFFFFFFU)
{
    quint32 crc = seed;
    for (unsigned char byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320U) : (crc >> 1);
        }
    }
    return crc;
}

quint32 crc32Final(const QByteArray &data)
{
    return crc32Update(data) ^ 0xFFFFFFFFU;
}

QString formatBytes(quint64 bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    return QString("%1 %2").arg(value, unit == 0 ? 0 : 2, 'f', unit == 0 ? 0 : 2).arg(units[unit]);
}

QString hex32(quint32 value)
{
    return QString("0x%1").arg(value, 8, 16, QLatin1Char('0')).toUpper();
}

QString partitionName(quint32 addr, const QString &name)
{
    if (name.contains("app", Qt::CaseInsensitive) || name.contains("hcpu", Qt::CaseInsensitive) ||
        addr == 0x12218000U) {
        return "app";
    }
    if (name.contains("font", Qt::CaseInsensitive) || addr == 0x12AE0000U) {
        return "font";
    }
    if (name.contains("ezip", Qt::CaseInsensitive) || name.contains("image", Qt::CaseInsensitive) ||
        addr == 0x12460000U) {
        return "ezip";
    }
    return "unknown";
}

QByteArray makeFrame(Command cmd, quint16 seq, quint32 addr = 0, quint32 size = 0,
                     quint32 payloadCrc = 0, quint32 value = 0,
                     const QByteArray &payload = {})
{
    QByteArray frame;
    frame.reserve(24 + payload.size());
    QDataStream out(&frame, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out << kMagic;
    out << kVersion;
    out << static_cast<quint8>(cmd);
    out << seq;
    out << addr;
    out << size;
    out << payloadCrc;
    out << value;
    frame.append(payload);
    return frame;
}

QLabel *makeLabel(const QString &text, const QString &objectName = {})
{
    auto *label = new QLabel(text);
    if (!objectName.isEmpty()) {
        label->setObjectName(objectName);
    }
    label->setMinimumHeight(20);
    return label;
}

QFrame *makeCard(const QString &title)
{
    auto *card = new QFrame;
    card->setObjectName("card");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(12);

    auto *titleRow = new QHBoxLayout;
    auto *titleLabel = makeLabel(title, "cardTitle");
    titleRow->addWidget(titleLabel);
    titleRow->addStretch(1);
    layout->addLayout(titleRow);
    return card;
}

QLabel *makeChip(const QString &text, const QString &tone)
{
    auto *chip = makeLabel(text, "chip");
    chip->setProperty("tone", tone);
    chip->setAlignment(Qt::AlignCenter);
    chip->setMinimumHeight(24);
    chip->setContentsMargins(8, 2, 8, 2);
    return chip;
}

} // namespace

class MainWindow final : public QMainWindow {
public:
    MainWindow()
    {
        buildUi();
        applyStyle();
        bindSignals();
        refreshPorts();
        loadDefaultPackageIfPresent();
    }

    void fitToAvailableScreen()
    {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen) {
            return;
        }

        const QRect available = screen->availableGeometry();
        const QSize preferred(1280, 760);
        QSize target(qMin(preferred.width(), available.width() - 32),
                     qMin(preferred.height(), available.height() - 48));
        target = target.expandedTo(minimumSize());
        resize(target);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && event->position().y() <= 46.0) {
            draggingWindow_ = true;
            dragOffset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
            return;
        }
        QMainWindow::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (draggingWindow_ && !isMaximized()) {
            move(event->globalPosition().toPoint() - dragOffset_);
            event->accept();
            return;
        }
        QMainWindow::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        draggingWindow_ = false;
        QMainWindow::mouseReleaseEvent(event);
    }

private:
    void buildUi()
    {
        setWindowFlag(Qt::FramelessWindowHint, true);

        auto *root = new QWidget(this);
        auto *rootLayout = new QVBoxLayout(root);
        rootLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->setSpacing(0);

        rootLayout->addWidget(buildTopBar());
        auto *scrollArea = new QScrollArea;
        scrollArea->setObjectName("contentScroll");
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollArea->setWidget(buildContent());
        rootLayout->addWidget(scrollArea, 1);
        createHiddenRuntimeLabels(root);

        setCentralWidget(root);
        setWindowTitle("Hiveton USB Recovery");
        resize(1280, 760);
        setMinimumSize(1080, 680);
    }

    QWidget *buildTopBar()
    {
        auto *bar = new QFrame;
        bar->setObjectName("topBar");
        bar->setFixedHeight(46);
        auto *layout = new QHBoxLayout(bar);
        layout->setContentsMargins(18, 0, 18, 0);
        layout->setSpacing(14);
        auto *dots = buildWindowControls();
        auto *brand = new QWidget;
        brand->setObjectName("windowBrand");
        auto *brandLayout = new QHBoxLayout(brand);
        brandLayout->setContentsMargins(0, 0, 0, 0);
        brandLayout->setSpacing(5);
        auto *mark = makeLabel("◆", "brandMark");
        auto *title = makeLabel("hiveton USB Recovery", "windowTitle");
        brandLayout->addWidget(mark);
        brandLayout->addWidget(title);
        footerStatus_ = makeChip("● 未连接", "warn");
        footerStatus_->setMinimumWidth(92);
        reconnectButton_ = new QPushButton("↻  刷新");
        reconnectButton_->setObjectName("toolbarButton");
        reconnectButton_->setMinimumWidth(86);

        layout->addWidget(dots);
        layout->addWidget(brand);
        layout->addStretch(1);
        layout->addWidget(footerStatus_);
        layout->addWidget(reconnectButton_);
        return bar;
    }

    QWidget *buildWindowControls()
    {
        auto *wrap = new QWidget;
        wrap->setObjectName("windowDots");
        auto *layout = new QHBoxLayout(wrap);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        auto makeDot = [](const char *name) {
            auto *button = new QPushButton;
            button->setObjectName(name);
            button->setFixedSize(13, 13);
            button->setCursor(Qt::PointingHandCursor);
            return button;
        };
        auto *closeButton = makeDot("dotClose");
        auto *minButton = makeDot("dotMin");
        auto *maxButton = makeDot("dotMax");
        connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
        connect(minButton, &QPushButton::clicked, this, &QWidget::showMinimized);
        connect(maxButton, &QPushButton::clicked, this, [this]() {
            isMaximized() ? showNormal() : showMaximized();
        });
        layout->addWidget(closeButton);
        layout->addWidget(minButton);
        layout->addWidget(maxButton);
        return wrap;
    }

    QWidget *buildSidebar()
    {
        auto *side = new QFrame;
        side->setObjectName("sidebar");
        side->setFixedWidth(150);
        auto *layout = new QVBoxLayout(side);
        layout->setContentsMargins(10, 32, 10, 14);
        layout->setSpacing(12);

        auto *logo = makeLabel("USB", "logo");
        logo->setAlignment(Qt::AlignCenter);
        auto *brand = makeLabel("Hiveton", "brand");
        brand->setAlignment(Qt::AlignCenter);
        auto *subtitle = makeLabel("USB Recovery", "brandSub");
        subtitle->setAlignment(Qt::AlignCenter);
        auto *version = makeLabel("v1.2.0", "brandVersion");
        version->setAlignment(Qt::AlignCenter);
        layout->addWidget(logo, 0, Qt::AlignHCenter);
        layout->addWidget(brand);
        layout->addWidget(subtitle);
        layout->addWidget(version);
        layout->addSpacing(12);

        layout->addWidget(navButton("USB刷机", true));
        auto *flowHint = makeLabel("连接设备\n选择升级包\n开始刷写", "sideHint");
        flowHint->setAlignment(Qt::AlignLeft);
        layout->addWidget(flowHint);
        layout->addStretch(1);

        auto *statusCard = new QFrame;
        statusCard->setObjectName("sideStatus");
        auto *statusLayout = new QVBoxLayout(statusCard);
        statusLayout->setContentsMargins(12, 12, 12, 12);
        statusLayout->setSpacing(10);
        sideStatus_ = makeLabel("● 设备状态\n未连接", "sideStatusText");
        auto *autoRow = new QHBoxLayout;
        autoRow->addWidget(makeLabel("自动检测", "sideMuted"));
        autoDetect_ = new QCheckBox;
        autoDetect_->setChecked(true);
        autoRow->addStretch(1);
        autoRow->addWidget(autoDetect_);
        reconnectButton_ = new QPushButton("重新连接");
        reconnectButton_->setObjectName("sideButton");
        statusLayout->addWidget(sideStatus_);
        statusLayout->addLayout(autoRow);
        statusLayout->addWidget(reconnectButton_);
        layout->addWidget(statusCard);

        return side;
    }

    QPushButton *navButton(const QString &text, bool active)
    {
        auto *button = new QPushButton(text);
        button->setObjectName(active ? "navActive" : "navButton");
        button->setMinimumHeight(40);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    }

    QWidget *buildContent()
    {
        auto *container = new QWidget;
        container->setObjectName("content");
        container->setMinimumWidth(0);
        container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto *layout = new QVBoxLayout(container);
        layout->setContentsMargins(20, 16, 20, 18);
        layout->setSpacing(14);

        layout->addWidget(buildWorkflowBar());
        auto *summary = new QHBoxLayout;
        summary->setSpacing(16);
        summary->addWidget(buildRecoveryHint(), 5);
        summary->addWidget(buildManifestSummaryCard(), 7);
        layout->addLayout(summary);
        layout->addWidget(buildFirmwareCard());
        auto *bottom = new QHBoxLayout;
        bottom->setSpacing(16);
        bottom->addWidget(buildProgressCard(), 5);
        bottom->addWidget(buildLogCard(), 7);
        layout->addLayout(bottom);

        return container;
    }

    QWidget *buildWorkflowBar()
    {
        auto *bar = new QFrame;
        bar->setObjectName("workflowBar");
        bar->setFixedHeight(64);
        auto *layout = new QHBoxLayout(bar);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(18);

        auto *mode = makeLabel("USB刷机", "modeTitle");
        layout->addWidget(mode);
        auto *divider = new QFrame;
        divider->setObjectName("verticalDivider");
        divider->setFixedSize(1, 26);
        layout->addWidget(divider);

        layout->addWidget(makeLabel("USB CDC 端口", "toolbarLabel"));
        deviceSelectControl_ = new QFrame;
        deviceSelectControl_->setObjectName("deviceSelectControl");
        deviceSelectControl_->setFixedHeight(42);
        deviceSelectControl_->setMinimumWidth(340);
        auto *selectLayout = new QHBoxLayout(deviceSelectControl_);
        selectLayout->setContentsMargins(0, 0, 0, 0);
        selectLayout->setSpacing(0);
        deviceSelectButton_ = new QPushButton("请选择 HVR Recovery 设备");
        deviceSelectButton_->setObjectName("deviceSelectText");
        deviceSelectButton_->setFixedHeight(42);
        deviceSelectButton_->setCursor(Qt::PointingHandCursor);
        deviceSelectButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        deviceSelectArrowButton_ = new QPushButton("▾");
        deviceSelectArrowButton_->setObjectName("deviceSelectArrow");
        deviceSelectArrowButton_->setFixedSize(42, 42);
        deviceSelectArrowButton_->setCursor(Qt::PointingHandCursor);
        selectLayout->addWidget(deviceSelectButton_, 1);
        selectLayout->addWidget(deviceSelectArrowButton_);
        layout->addWidget(deviceSelectControl_, 1);

        layout->addSpacing(22);
        layout->addWidget(makeLabel("升级包", "toolbarLabel"));
        packagePath_ = new QLabel("update.json");
        packagePath_->setObjectName("pathBox");
        packagePath_->setMinimumWidth(340);
        packagePath_->setFixedHeight(42);
        packagePath_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        packagePath_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        chooseButton_ = new QPushButton("浏览...");
        chooseButton_->setObjectName("outlineButton");
        chooseButton_->setMinimumWidth(86);
        chooseButton_->setFixedHeight(42);
        layout->addWidget(packagePath_, 1);
        layout->addWidget(chooseButton_);
        return bar;
    }

    QWidget *buildRecoveryHint()
    {
        auto *hint = new QFrame;
        hint->setObjectName("recoveryHint");
        hint->setFixedHeight(94);
        auto *layout = new QHBoxLayout(hint);
        layout->setContentsMargins(22, 0, 22, 0);
        layout->setSpacing(14);
        auto *icon = makeLabel("⚠", "warningIcon");
        deviceHint_ = makeLabel("未发现 HVR Recovery，请按住 B 键上电进入 USB 刷机模式。", "warningText");
        deviceHint_->setWordWrap(true);
        layout->addWidget(icon);
        layout->addWidget(deviceHint_, 1);
        return hint;
    }

    QWidget *buildManifestSummaryCard()
    {
        auto *card = new QFrame;
        card->setObjectName("card");
        card->setFixedHeight(94);
        auto *grid = new QGridLayout(card);
        grid->setContentsMargins(20, 10, 20, 10);
        grid->setHorizontalSpacing(26);
        grid->setVerticalSpacing(8);

        manifestStatusChip_ = makeChip("未校验", "neutral");
        grid->addWidget(manifestStatusChip_, 0, 0, Qt::AlignLeft);
        manifestVersion_ = addCheckLine(grid, 0, 1, "固件版本", "--");
        manifestFiles_ = addCheckLine(grid, 0, 3, "文件数量", "--");
        manifestSize_ = addCheckLine(grid, 0, 5, "总大小", "--");
        manifestTime_ = addCheckLine(grid, 1, 1, "生成时间", "--");
        manifestBoard_ = addCheckLine(grid, 1, 3, "目标板卡", kExpectedBoard);
        manifestResult_ = addCheckLine(grid, 1, 5, "校验结果", "--");
        grid->setColumnStretch(2, 1);
        grid->setColumnStretch(4, 1);
        return card;
    }

    QWidget *buildDeviceCard()
    {
        auto *card = new QFrame;
        card->setObjectName("card");
        card->setMinimumHeight(218);
        card->setMaximumHeight(240);
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(16, 14, 16, 14);
        layout->setSpacing(10);

        auto *titleRow = new QHBoxLayout;
        titleRow->setSpacing(10);
        titleRow->addWidget(makeLabel("设备信息", "cardTitle"));
        titleRow->addStretch(1);
        deviceStatusChip_ = makeChip("未连接", "neutral");
        deviceStatusChip_->setMinimumWidth(86);
        readDeviceButton_ = new QPushButton("读取设备");
        readDeviceButton_->setObjectName("primaryButton");
        readDeviceButton_->setFixedHeight(32);
        readDeviceButton_->setMinimumWidth(96);
        titleRow->addWidget(deviceStatusChip_);
        titleRow->addWidget(readDeviceButton_);
        layout->addLayout(titleRow);

        auto *portPanel = new QFrame;
        portPanel->setObjectName("devicePortPanel");
        auto *portLayout = new QHBoxLayout(portPanel);
        portLayout->setContentsMargins(12, 8, 12, 8);
        portLayout->setSpacing(12);
        portLayout->addWidget(makeLabel("USB CDC 端口", "muted"));
        deviceSelectControl_ = new QFrame;
        deviceSelectControl_->setObjectName("deviceSelectControl");
        deviceSelectControl_->setFixedHeight(38);
        deviceSelectControl_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto *selectLayout = new QHBoxLayout(deviceSelectControl_);
        selectLayout->setContentsMargins(0, 0, 0, 0);
        selectLayout->setSpacing(0);

        deviceSelectButton_ = new QPushButton("未发现设备");
        deviceSelectButton_->setObjectName("deviceSelectText");
        deviceSelectButton_->setFixedHeight(38);
        deviceSelectButton_->setCursor(Qt::PointingHandCursor);
        deviceSelectButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        deviceSelectArrowButton_ = new QPushButton("▾");
        deviceSelectArrowButton_->setObjectName("deviceSelectArrow");
        deviceSelectArrowButton_->setFixedSize(38, 38);
        deviceSelectArrowButton_->setCursor(Qt::PointingHandCursor);
        selectLayout->addWidget(deviceSelectButton_, 1);
        selectLayout->addWidget(deviceSelectArrowButton_);
        portLayout->addWidget(deviceSelectControl_, 1);
        layout->addWidget(portPanel);

        deviceHint_ = makeLabel("等待 HVR Recovery 设备。若只看到普通串口，请按住 B 键上电进入 USB 刷机模式。", "hintText");
        deviceHint_->setWordWrap(true);
        layout->addWidget(deviceHint_);

        devicePath_ = makeLabel("--", "value");
        devicePath_->hide();

        auto *metrics = new QGridLayout;
        metrics->setContentsMargins(0, 2, 0, 0);
        metrics->setHorizontalSpacing(22);
        metrics->setVerticalSpacing(9);
        vidPidValue_ = addDeviceMetric(metrics, 0, 0, "VID / PID", "--");
        protoValue_ = addDeviceMetric(metrics, 0, 2, "协议", "--");
        boardValue_ = addDeviceMetric(metrics, 1, 0, "板卡", "--");
        maxPayloadValue_ = addDeviceMetric(metrics, 1, 2, "最大负载", "--");
        metrics->addWidget(makeLabel("允许分区", "muted"), 2, 0);
        allowedPartitions_ = makeLabel("--", "chipsLine");
        metrics->addWidget(allowedPartitions_, 2, 1, 1, 3, Qt::AlignLeft);
        metrics->setColumnStretch(1, 1);
        metrics->setColumnStretch(3, 1);
        layout->addLayout(metrics);
        return card;
    }

    QLabel *addDeviceMetric(QGridLayout *grid, int row, int col, const QString &label, const QString &value)
    {
        grid->addWidget(makeLabel(label, "muted"), row, col);
        auto *valueLabel = makeLabel(value, "value");
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(valueLabel, row, col + 1);
        return valueLabel;
    }

    QWidget *buildPackageCard()
    {
        auto *card = makeCard("升级包");
        card->setMinimumHeight(228);
        card->setMaximumHeight(240);
        auto *layout = qobject_cast<QVBoxLayout *>(card->layout());

        auto *chooseRow = new QHBoxLayout;
        chooseRow->addWidget(makeLabel("选择升级包", "muted"));
        packagePath_ = new QLabel("未选择 update.json");
        packagePath_->setObjectName("pathBox");
        packagePath_->setMinimumWidth(0);
        packagePath_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        chooseButton_ = new QPushButton("浏览...");
        chooseButton_->setObjectName("outlineButton");
        chooseButton_->setMinimumWidth(76);
        chooseRow->addWidget(packagePath_, 1);
        chooseRow->addWidget(chooseButton_);
        layout->addLayout(chooseRow);

        auto *summary = new QFrame;
        summary->setObjectName("innerPanel");
        auto *grid = new QGridLayout(summary);
        grid->setContentsMargins(14, 12, 14, 12);
        grid->setHorizontalSpacing(28);
        grid->setVerticalSpacing(10);
        manifestStatusChip_ = makeChip("未校验", "neutral");
        grid->addWidget(makeLabel("清单校验（update.json）", "sectionTitle"), 0, 0);
        grid->addWidget(manifestStatusChip_, 0, 1, Qt::AlignLeft);
        manifestVersion_ = addCheckLine(grid, 1, 0, "固件版本", "--");
        manifestTime_ = addCheckLine(grid, 2, 0, "生成时间", "--");
        manifestBoard_ = addCheckLine(grid, 3, 0, "目标板卡", kExpectedBoard);
        manifestFiles_ = addCheckLine(grid, 1, 2, "文件数量", "--");
        manifestSize_ = addCheckLine(grid, 2, 2, "总大小", "--");
        manifestResult_ = addCheckLine(grid, 3, 2, "校验结果", "--");
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(3, 1);
        layout->addWidget(summary, 1);
        return card;
    }

    QLabel *addCheckLine(QGridLayout *grid, int row, int col, const QString &label, const QString &value)
    {
        auto *key = makeLabel(label, "muted");
        auto *val = makeLabel(value, "value");
        grid->addWidget(key, row, col);
        grid->addWidget(val, row, col + 1);
        return val;
    }

    QWidget *buildFirmwareCard()
    {
        auto *card = new QFrame;
        card->setObjectName("card");
        card->setMinimumHeight(214);
        card->setMaximumHeight(230);
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(16, 12, 16, 14);
        layout->setSpacing(10);

        auto *actions = new QHBoxLayout;
        firmwareTitle_ = makeLabel("固件文件（0 个）", "cardTitle");
        actions->addWidget(firmwareTitle_);
        actions->addStretch(1);
        auto *importButton = new QPushButton("导入固件");
        importButton->setObjectName("outlineButton");
        auto *exportButton = new QPushButton("导出清单");
        exportButton->setObjectName("outlineButton");
        auto *clearButton = new QPushButton("清空");
        clearButton->setObjectName("outlineButton");
        actions->addWidget(importButton);
        actions->addWidget(exportButton);
        actions->addWidget(clearButton);
        layout->addLayout(actions);
        connect(importButton, &QPushButton::clicked, this, &MainWindow::choosePackage);
        connect(clearButton, &QPushButton::clicked, this, &MainWindow::clearPackage);
        connect(exportButton, &QPushButton::clicked, this, &MainWindow::saveLog);

        firmwareTable_ = new QTableWidget(0, ColCount);
        firmwareTable_->setObjectName("firmwareTable");
        firmwareTable_->setHorizontalHeaderLabels({"", "分区", "文件名", "大小", "CRC32", "地址", "区域", "状态"});
        firmwareTable_->verticalHeader()->setVisible(false);
        firmwareTable_->horizontalHeader()->setStretchLastSection(false);
        applyTableColumnSizing();
        firmwareTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
        firmwareTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        firmwareTable_->setAlternatingRowColors(true);
        firmwareTable_->setShowGrid(true);
        firmwareTable_->setFocusPolicy(Qt::NoFocus);
        firmwareTable_->setSelectionMode(QAbstractItemView::NoSelection);
        firmwareTable_->verticalHeader()->setDefaultSectionSize(36);
        firmwareTable_->setMinimumHeight(148);
        firmwareTable_->setMaximumHeight(166);
        layout->addWidget(firmwareTable_);
        return card;
    }

    QWidget *buildProgressCard()
    {
        auto *card = new QFrame;
        card->setObjectName("card");
        card->setMinimumHeight(196);
        card->setMaximumHeight(210);
        auto *layout = new QHBoxLayout(card);
        layout->setContentsMargins(16, 10, 16, 10);
        layout->setSpacing(14);

        auto *left = new QVBoxLayout;
        left->setSpacing(8);
        auto *progressTitle = makeLabel("刷写进度", "cardTitle");
        left->addWidget(progressTitle);
        currentOperation_ = makeLabel("当前操作：等待", "value");
        left->addWidget(currentOperation_);
        fileProgress_ = new QProgressBar;
        fileProgress_->setRange(0, 100);
        fileProgress_->setValue(0);
        fileProgress_->setFormat("%p%");
        totalProgress_ = new QProgressBar;
        totalProgress_->setRange(0, 100);
        totalProgress_->setValue(0);
        totalProgress_->setFormat("%p%");
        left->addWidget(fileProgress_);
        auto *meta = new QHBoxLayout;
        transferInfo_ = makeLabel("进度：--", "muted");
        timeInfo_ = makeLabel("已用时间：--    预计剩余：--", "muted");
        meta->addWidget(transferInfo_);
        meta->addStretch(1);
        meta->addWidget(timeInfo_);
        left->addLayout(meta);
        auto *overall = new QHBoxLayout;
        overall->addWidget(makeLabel("整体进度", "muted"));
        overall->addWidget(totalProgress_, 1);
        left->addLayout(overall);
        layout->addLayout(left, 1);

        auto *actions = new QVBoxLayout;
        actions->setSpacing(8);
        flashButton_ = new QPushButton("开始刷写");
        flashButton_->setObjectName("primaryButton");
        flashButton_->setMinimumWidth(190);
        flashButton_->setFixedHeight(48);
        readDeviceButton_ = new QPushButton("读取设备");
        readDeviceButton_->setObjectName("outlineButton");
        readDeviceButton_->setMinimumWidth(92);
        verifyButton_ = new QPushButton("校验");
        verifyButton_->setObjectName("outlineButton");
        verifyButton_->setMinimumWidth(92);
        rebootButton_ = new QPushButton("重启设备");
        rebootButton_->setObjectName("outlineButton");
        rebootButton_->setMinimumWidth(92);
        stopButton_ = new QPushButton("停止");
        stopButton_->setObjectName("dangerButton");
        stopButton_->setMinimumWidth(92);
        stopButton_->setEnabled(false);
        actions->addWidget(flashButton_);
        auto *row1 = new QHBoxLayout;
        row1->setSpacing(8);
        row1->addWidget(readDeviceButton_);
        row1->addWidget(verifyButton_);
        actions->addLayout(row1);
        auto *row2 = new QHBoxLayout;
        row2->setSpacing(8);
        row2->addWidget(rebootButton_);
        row2->addWidget(stopButton_);
        actions->addLayout(row2);
        actions->addStretch(1);
        layout->addLayout(actions);
        return card;
    }

    QWidget *buildLogCard()
    {
        auto *card = new QFrame;
        card->setObjectName("card");
        card->setMinimumHeight(196);
        card->setMaximumHeight(210);
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(16, 10, 16, 14);
        layout->setSpacing(8);
        auto *top = new QHBoxLayout;
        top->addWidget(makeLabel("日志", "cardTitle"));
        top->addStretch(1);
        clearLogButton_ = new QPushButton("清空日志");
        clearLogButton_->setObjectName("outlineButton");
        saveLogButton_ = new QPushButton("保存日志");
        saveLogButton_->setObjectName("outlineButton");
        top->addWidget(clearLogButton_);
        top->addWidget(saveLogButton_);
        layout->addLayout(top);
        log_ = new QPlainTextEdit;
        log_->setReadOnly(true);
        log_->setObjectName("logView");
        log_->setMinimumHeight(132);
        layout->addWidget(log_, 1);
        return card;
    }

    QWidget *buildFooter()
    {
        auto *footer = new QFrame;
        footer->setObjectName("footer");
        footer->setFixedHeight(36);
        auto *layout = new QHBoxLayout(footer);
        layout->setContentsMargins(16, 0, 16, 0);
        footerStatus_ = makeLabel("● 就绪", "footerStatus");
        footerTransport_ = makeLabel("USB CDC", "footerItem");
        footerVersion_ = makeLabel("固件版本：--", "footerItem");
        footerSn_ = makeLabel("SN：--", "footerItem");
        layout->addWidget(footerStatus_);
        layout->addStretch(1);
        layout->addWidget(footerTransport_);
        layout->addStretch(1);
        layout->addWidget(footerVersion_);
        layout->addStretch(1);
        layout->addWidget(footerSn_);
        return footer;
    }

    void createHiddenRuntimeLabels(QWidget *parent)
    {
        auto makeHidden = [parent](const QString &text = QString()) {
            auto *label = new QLabel(text, parent);
            label->hide();
            return label;
        };

        deviceStatusChip_ = footerStatus_;
        sideStatus_ = nullptr;
        autoDetect_ = new QCheckBox(parent);
        autoDetect_->setChecked(true);
        autoDetect_->hide();
        devicePath_ = makeHidden("--");
        vidPidValue_ = makeHidden("--");
        boardValue_ = makeHidden("--");
        protoValue_ = makeHidden("--");
        maxPayloadValue_ = makeHidden("--");
        allowedPartitions_ = makeHidden("--");
        footerTransport_ = makeHidden("USB CDC");
        footerVersion_ = makeHidden("固件版本：--");
        footerSn_ = makeHidden("SN：--");
    }

    void applyStyle()
    {
        qApp->setStyleSheet(R"(
            QWidget {
                font-family: "PingFang SC", "Helvetica Neue", Arial, sans-serif;
                font-size: 13px;
                color: #172033;
            }
            QMainWindow, #content { background: #f7f9fc; }
            #topBar {
                background: #fbfcfe;
                border-bottom: 1px solid #d8e0ec;
            }
            #windowDots { background: transparent; }
            #dotClose, #dotMin, #dotMax {
                border: 0;
                border-radius: 6px;
                min-width: 13px;
                max-width: 13px;
                min-height: 13px;
                max-height: 13px;
                padding: 0;
            }
            #dotClose { background: #ff5f57; }
            #dotMin { background: #ffbd2e; }
            #dotMax { background: #28c840; }
            #dotClose:hover, #dotMin:hover, #dotMax:hover { border: 1px solid rgba(0,0,0,0.14); }
            #windowBrand { background: transparent; }
            #brandMark {
                color: #1f78ff;
                font-size: 12px;
                font-weight: 900;
                min-width: 12px;
                max-width: 12px;
            }
            #windowTitle { font-size: 16px; font-weight: 700; color: #263247; }
            #workflowBar {
                background: transparent;
                border: 0;
            }
            #modeTitle {
                color: #172033;
                font-size: 17px;
                font-weight: 700;
            }
            #toolbarLabel {
                color: #48566d;
                font-size: 15px;
            }
            #toolbarButton {
                color: #1d56a6;
                background: #ffffff;
                border: 1px solid #cbd6e6;
                border-radius: 8px;
                padding: 8px 14px;
                font-weight: 700;
            }
            #toolbarButton:hover { background: #f1f7ff; }
            #verticalDivider {
                background: #d9e1ec;
                border: 0;
            }
            #recoveryHint {
                background: #fffaf0;
                border: 1px solid #f4d9a6;
                border-radius: 8px;
            }
            #warningIcon {
                color: #f59e0b;
                font-size: 24px;
                font-weight: 700;
            }
            #warningText {
                color: #8a5200;
                font-size: 16px;
                font-weight: 600;
            }
            #sidebar { background: #132230; }
            #logo {
                background: #2563eb;
                color: white;
                font-weight: 700;
                border-radius: 12px;
                min-width: 48px;
                min-height: 48px;
            }
            #brand { color: white; font-size: 17px; font-weight: 700; }
            #brandSub { color: #e5edf7; font-size: 13px; }
            #brandVersion, #sideMuted { color: #8fa3b8; font-size: 12px; }
            #sideHint {
                color: #b8c7d8;
                background: rgba(255,255,255,0.05);
                border: 1px solid rgba(255,255,255,0.10);
                border-radius: 8px;
                padding: 10px 12px;
                line-height: 1.45;
            }
            #navActive {
                background: #2f75f4;
                color: white;
                border: 0;
                border-radius: 6px;
                text-align: left;
                padding-left: 18px;
                font-weight: 600;
            }
            #navButton {
                background: transparent;
                color: #d7e2ee;
                border: 0;
                border-radius: 6px;
                text-align: left;
                padding-left: 18px;
            }
            #navButton:hover { background: rgba(255,255,255,0.08); }
            #sideStatus {
                background: rgba(255,255,255,0.06);
                border: 1px solid rgba(255,255,255,0.12);
                border-radius: 8px;
            }
            #sideStatusText { color: #72e48b; line-height: 1.4; font-weight: 600; }
            #sideButton {
                color: #d7e2ee;
                background: transparent;
                border: 1px solid rgba(255,255,255,0.16);
                border-radius: 6px;
                min-height: 30px;
            }
            #card {
                background: #ffffff;
                border: 1px solid #d7e0ea;
                border-radius: 8px;
            }
            #innerPanel {
                background: #fbfdff;
                border: 1px solid #d7e0ea;
                border-radius: 6px;
            }
            #cardTitle { font-size: 15px; font-weight: 700; color: #111827; }
            #sectionTitle { font-weight: 700; color: #111827; }
            #muted, .QLabel#muted { color: #5c6875; }
            #value { color: #1f2933; font-weight: 500; }
            #hintText {
                color: #496275;
                background: #eef6ff;
                border: 1px solid #cfe3fb;
                border-radius: 6px;
                padding: 7px 10px;
            }
            #pathBox {
                background: #ffffff;
                border: 1px solid #c8d3e0;
                border-radius: 7px;
                padding: 0 16px;
                color: #334155;
                min-height: 42px;
                max-height: 42px;
            }
            #devicePortPanel {
                background: #f8fbff;
                border: 1px solid #d7e0ea;
                border-radius: 6px;
            }
            #chip {
                border-radius: 10px;
                padding: 2px 8px;
                font-weight: 600;
            }
            #chip[tone="ok"] { color: #14712e; background: #e7f7ea; border: 1px solid #92d7a2; }
            #chip[tone="warn"] { color: #b45309; background: #fff7e6; border: 1px solid #edcf8a; }
            #chip[tone="error"] { color: #9b1c1c; background: #fde8e8; border: 1px solid #f3a6a6; }
            #chip[tone="neutral"] { color: #475569; background: #eef2f7; border: 1px solid #d4dde8; }
            #chipsLine { color: #14712e; font-weight: 700; }
            QPushButton {
                border-radius: 6px;
                padding: 8px 10px;
                min-height: 20px;
                font-weight: 600;
            }
            QPushButton:disabled {
                color: #94a3b8;
                background: #f1f5f9;
                border: 1px solid #d8e0ea;
            }
            #primaryButton {
                color: white;
                background: #0f6bf2;
                border: 1px solid #0b5fdc;
            }
            #primaryButton:hover { background: #0b5fdc; }
            #outlineButton, #ghostButton {
                color: #1f4f93;
                background: #ffffff;
                border: 1px solid #c8d3e0;
            }
            #outlineButton:hover, #ghostButton:hover { background: #f1f6fd; }
            #dangerButton {
                color: #b42318;
                background: #fff7f7;
                border: 1px solid #f0b4b4;
            }
            #deviceSelectControl {
                background: #ffffff;
                border: 1px solid #c8d3e0;
                border-radius: 7px;
                min-height: 42px;
                max-height: 42px;
            }
            #deviceSelectControl:hover {
                border: 1px solid #9fb3cf;
            }
            #deviceSelectText {
                background: transparent;
                border: 0;
                border-radius: 7px;
                padding: 0 16px;
                color: #1f2933;
                font-size: 15px;
                font-weight: 600;
                text-align: left;
            }
            #deviceSelectText:hover { background: transparent; }
            #deviceSelectArrow {
                background: #f5f8fc;
                border: 0;
                border-left: 1px solid #e0e7f0;
                border-top-right-radius: 7px;
                border-bottom-right-radius: 7px;
                color: #475569;
                font-size: 14px;
                font-weight: 700;
                padding: 0;
            }
            #deviceSelectArrow:hover { background: #edf4ff; }
            QMenu {
                background: #ffffff;
                border: 1px solid #c8d3e0;
                border-radius: 6px;
                padding: 6px;
            }
            QMenu::item {
                padding: 8px 28px 8px 12px;
                border-radius: 5px;
                color: #1f2933;
            }
            QMenu::item:selected {
                background: #edf4ff;
                color: #0f172a;
            }
            QTableWidget {
                background: #ffffff;
                alternate-background-color: #f8fbff;
                border: 1px solid #d7e0ea;
                border-radius: 6px;
                gridline-color: #e6edf5;
            }
            QHeaderView::section {
                background: #f2f6fb;
                border: 0;
                border-right: 1px solid #dfe7f0;
                border-bottom: 1px solid #dfe7f0;
                padding: 8px;
                font-weight: 600;
                color: #475569;
            }
            QProgressBar {
                background: #e7edf5;
                border: 0;
                border-radius: 6px;
                height: 13px;
                text-align: center;
                color: #ffffff;
                font-weight: 700;
                font-size: 11px;
            }
            QProgressBar::chunk {
                background: #0f6bf2;
                border-radius: 6px;
            }
            #logView {
                background: #0f1d29;
                color: #c7d7e6;
                border: 0;
                border-radius: 6px;
                font-family: "SF Mono", Menlo, Consolas, monospace;
                font-size: 12px;
                padding: 10px;
            }
            #footer {
                background: #fbfdff;
                border-top: 1px solid #d9e1ec;
            }
            #footerStatus { color: #16a34a; font-weight: 700; }
            #footerItem { color: #64748b; }
        )");
    }

    void bindSignals()
    {
        refreshTimer_ = new QTimer(this);
        refreshTimer_->setInterval(2500);
        connect(refreshTimer_, &QTimer::timeout, this, [this]() {
            if (autoDetect_->isChecked() && !isBusy_) {
                refreshPorts(false);
            }
        });
        refreshTimer_->start();
        QTimer::singleShot(350, this, [this]() {
            if (autoDetect_->isChecked() && !serial_.isOpen() && !currentDevicePath_.isEmpty()) {
                connectAndHello();
            }
        });

        connect(readDeviceButton_, &QPushButton::clicked, this, &MainWindow::connectAndHello);
        connect(reconnectButton_, &QPushButton::clicked, this, &MainWindow::reconnect);
        connect(chooseButton_, &QPushButton::clicked, this, &MainWindow::choosePackage);
        connect(flashButton_, &QPushButton::clicked, this, &MainWindow::flashPackage);
        connect(verifyButton_, &QPushButton::clicked, this, &MainWindow::verifyPackage);
        connect(rebootButton_, &QPushButton::clicked, this, &MainWindow::rebootDevice);
        connect(deviceSelectButton_, &QPushButton::clicked, this, &MainWindow::showDeviceMenu);
        connect(deviceSelectArrowButton_, &QPushButton::clicked, this, &MainWindow::showDeviceMenu);
        connect(stopButton_, &QPushButton::clicked, this, [this]() {
            stopRequested_ = true;
            appendLog("WARN", "已请求停止，当前分块结束后生效");
        });
        connect(clearLogButton_, &QPushButton::clicked, log_, &QPlainTextEdit::clear);
        connect(saveLogButton_, &QPushButton::clicked, this, &MainWindow::saveLog);
    }

    void appendLog(const QString &level, const QString &line)
    {
        const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
        log_->appendPlainText(QString("[%1]  %2  %3").arg(ts, level.leftJustified(5), line));
    }

    void setBusy(bool busy)
    {
        isBusy_ = busy;
        flashButton_->setEnabled(!busy);
        verifyButton_->setEnabled(!busy);
        chooseButton_->setEnabled(!busy);
        readDeviceButton_->setEnabled(!busy);
        rebootButton_->setEnabled(!busy);
        reconnectButton_->setEnabled(!busy);
        deviceSelectButton_->setEnabled(!busy);
        deviceSelectArrowButton_->setEnabled(!busy);
        stopButton_->setEnabled(busy);
    }

    void refreshPorts(bool logResult = true)
    {
        const QString current = currentDevicePath_;
        devicePortNames_.clear();
        devicePortPaths_.clear();
        int preferredIndex = -1;
        int restoreIndex = -1;
        const auto infos = QSerialPortInfo::availablePorts();
        for (const auto &info : infos) {
            const bool recovery = isRecoveryPort(info);
            const bool physicalUart = info.portName().contains("usbserial", Qt::CaseInsensitive);
            const QString label = recovery ? QString("%1  ·  HVR Recovery").arg(info.portName()) :
                (physicalUart ? QString("%1  ·  物理串口").arg(info.portName()) :
                                QString("%1  ·  普通串口").arg(info.portName()));
            const QString tooltip = QString("%1\n%2")
                .arg(info.systemLocation(),
                     info.description().isEmpty() ? info.portName() : info.description());
            devicePortNames_.push_back(label);
            devicePortPaths_.push_back(info.systemLocation());
            devicePortTooltips_.push_back(tooltip);
            if (info.systemLocation() == current) {
                restoreIndex = devicePortPaths_.size() - 1;
            }
            if (recovery && preferredIndex < 0) {
                preferredIndex = devicePortPaths_.size() - 1;
            }
        }
        if (restoreIndex >= 0) {
            currentDevicePath_ = devicePortPaths_[restoreIndex];
        } else if (preferredIndex >= 0) {
            currentDevicePath_ = devicePortPaths_[preferredIndex];
        } else {
            currentDevicePath_.clear();
        }
        updateDeviceSelectionLabels();
        if (logResult) {
            appendLog("INFO", QString("发现 %1 个串口/USB CDC 设备").arg(devicePortPaths_.size()));
            if (preferredIndex < 0) {
                appendLog("WARN", "未发现 HVR Recovery 设备，已避免自动打开普通串口");
            }
        }
    }

    void updateDeviceSelectionLabels()
    {
        const QString path = currentDevicePath_;
        const int index = devicePortPaths_.indexOf(path);
        const QString name = index >= 0 ? devicePortNames_[index] : QString();
        deviceSelectButton_->setText(name.isEmpty() ? "请选择 HVR Recovery 设备" : name);
        deviceSelectButton_->setToolTip(index >= 0 ? devicePortTooltips_[index] : QString());
        deviceSelectArrowButton_->setToolTip(index >= 0 ? devicePortTooltips_[index] : QString());
        devicePath_->setText(path.isEmpty() ? "--" : path);
        if (path.isEmpty()) {
            setDeviceStatus(false, "未连接");
            deviceHint_->setText(devicePortPaths_.isEmpty()
                                     ? "未发现 HVR Recovery，请连接设备并按住 B 键上电进入 USB 刷机模式。"
                                     : "未发现 HVR Recovery，请按住 B 键上电进入 USB 刷机模式。");
        } else {
            deviceHint_->setText("已选择 Recovery 设备，点击“读取设备”确认板卡和协议。");
        }
    }

    void showDeviceMenu()
    {
        QMenu menu(this);
        for (int i = 0; i < devicePortPaths_.size(); ++i) {
            QAction *action = menu.addAction(devicePortNames_[i]);
            action->setToolTip(devicePortTooltips_[i]);
            action->setData(devicePortPaths_[i]);
            action->setCheckable(true);
            action->setChecked(devicePortPaths_[i] == currentDevicePath_);
        }
        if (devicePortPaths_.isEmpty()) {
            QAction *empty = menu.addAction("未发现设备");
            empty->setEnabled(false);
        }
        QAction *chosen = menu.exec(deviceSelectControl_->mapToGlobal(QPoint(0, deviceSelectControl_->height() + 4)));
        if (!chosen || !chosen->isEnabled()) {
            return;
        }
        const QString nextPath = chosen->data().toString();
        if (nextPath != currentDevicePath_) {
            closeSerial();
            currentDevicePath_ = nextPath;
            updateDeviceSelectionLabels();
        }
    }

    void setDeviceStatus(bool connected, const QString &text)
    {
        deviceStatusChip_->setText(text);
        deviceStatusChip_->setProperty("tone", connected ? "ok" : "warn");
        deviceStatusChip_->style()->unpolish(deviceStatusChip_);
        deviceStatusChip_->style()->polish(deviceStatusChip_);
        if (sideStatus_ != nullptr) {
            sideStatus_->setText(connected ? "● 设备状态\n已连接" : "● 设备状态\n未连接");
        }
        footerStatus_->setText(connected ? "● 已连接" : "● 未连接");
    }

    bool ensureOpen()
    {
        if (serial_.isOpen()) {
            return true;
        }
        if (currentDevicePath_.isEmpty()) {
            QMessageBox::warning(this, "未连接 Recovery", "未发现 HVR Recovery 设备。\n\n请按住 B 键上电进入 USB 刷机模式，看到 Recovery 端口后再读取设备。");
            return false;
        }
        serial_.setPortName(currentDevicePath_);
        serial_.setBaudRate(1000000);
        serial_.setDataBits(QSerialPort::Data8);
        serial_.setParity(QSerialPort::NoParity);
        serial_.setStopBits(QSerialPort::OneStop);
        serial_.setFlowControl(QSerialPort::NoFlowControl);
        if (!serial_.open(QIODevice::ReadWrite)) {
            QMessageBox::warning(this, "连接失败", serial_.errorString());
            setDeviceStatus(false, "连接失败");
            return false;
        }
        appendLog("INFO", "已打开设备：" + serial_.portName());
        return true;
    }

    bool isRecoveryPort(const QSerialPortInfo &info) const
    {
        const QString text = QString("%1 %2 %3")
            .arg(info.portName(), info.description(), info.manufacturer());
        if (text.contains("App Serial", Qt::CaseInsensitive) ||
            text.contains("debug", Qt::CaseInsensitive)) {
            return false;
        }
        if (text.contains("HVR", Qt::CaseInsensitive) ||
            text.contains("Recovery", Qt::CaseInsensitive)) {
            return true;
        }
        return info.hasVendorIdentifier() &&
            info.vendorIdentifier() == kHivetonVid &&
            (!info.hasProductIdentifier() || info.productIdentifier() == kRecoveryPid);
    }

    void closeSerial()
    {
        if (serial_.isOpen()) {
            serial_.close();
        }
        setDeviceStatus(false, "未连接");
    }

    Response transact(Command cmd, quint32 addr = 0, quint32 size = 0, quint32 crc = 0,
                      const QByteArray &payload = {}, int timeoutMs = 5000)
    {
        const quint16 seq = ++seq_;
        const QByteArray frame = makeFrame(cmd, seq, addr, size, crc, 0, payload);
        serial_.readAll();
        if (serial_.write(frame) != frame.size() || !serial_.waitForBytesWritten(timeoutMs)) {
            throw std::runtime_error("写入设备失败");
        }

        QByteArray data;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            if (!serial_.waitForReadyRead(50)) {
                qApp->processEvents();
                continue;
            }
            data += serial_.readAll();

            while (true) {
                const int magicOffset = findFrameMagic(data);
                if (magicOffset < 0) {
                    if (data.size() > 3) {
                        data = data.right(3);
                    }
                    break;
                }
                if (magicOffset > 0) {
                    data.remove(0, magicOffset);
                }
                if (data.size() < 24) {
                    break;
                }

                Response rsp;
                quint32 magic = 0;
                quint8 version = 0;
                quint32 ignoredAddr = 0;
                QDataStream header(data.left(24));
                header.setByteOrder(QDataStream::LittleEndian);
                header >> magic >> version >> rsp.cmd >> rsp.seq >> ignoredAddr >> rsp.size >> rsp.crc >> rsp.status;
                const int expected = 24 + static_cast<int>(rsp.size);
                if (rsp.size > static_cast<quint32>(kMaxPayload)) {
                    data.remove(0, 4);
                    appendLog("WARN", "丢弃异常响应帧：payload 过大");
                    continue;
                }
                if (data.size() < expected) {
                    break;
                }

                rsp.payload = data.mid(24, rsp.size);
                data.remove(0, expected);
                if (version != kVersion) {
                    appendLog("WARN", QString("丢弃版本不匹配响应：version=%1").arg(version));
                    continue;
                }
                if (rsp.size > 0 && crc32Final(rsp.payload) != rsp.crc) {
                    throw std::runtime_error("设备响应 CRC 错误");
                }
                if (rsp.cmd == static_cast<quint8>(cmd) && rsp.seq == seq) {
                    return rsp;
                }
                appendLog("WARN", QString("丢弃旧响应：期望 cmd=%1 seq=%2，收到 cmd=%3 seq=%4 status=%5")
                                      .arg(static_cast<int>(cmd))
                                      .arg(seq)
                                      .arg(rsp.cmd)
                                      .arg(rsp.seq)
                                      .arg(rsp.status));
            }
            qApp->processEvents();
        }
        throw std::runtime_error(QString("等待设备响应超时：cmd=%1 seq=%2").arg(static_cast<int>(cmd)).arg(seq).toStdString());
    }

    int findFrameMagic(const QByteArray &data) const
    {
        const char magicBytes[] = {
            static_cast<char>(kMagic & 0xFFU),
            static_cast<char>((kMagic >> 8) & 0xFFU),
            static_cast<char>((kMagic >> 16) & 0xFFU),
            static_cast<char>((kMagic >> 24) & 0xFFU),
        };
        return data.indexOf(QByteArray(magicBytes, sizeof(magicBytes)));
    }

    void expectOk(const Response &rsp)
    {
        if (rsp.status != RecoveryStatusOk) {
            throw std::runtime_error(statusMessage(rsp).toStdString());
        }
    }

    QString statusMessage(const Response &rsp) const
    {
        switch (rsp.status) {
        case RecoveryStatusBadFrame:
            return "设备返回错误：数据帧格式错误";
        case RecoveryStatusDenied:
            return "设备返回错误：分区不允许写入";
        case RecoveryStatusFlashError:
            return "设备返回错误：Flash 读写失败";
        case RecoveryStatusCrcError:
            return "设备返回错误：CRC 校验不匹配";
        case RecoveryStatusBadPartition:
            return "设备返回错误：目标分区无效";
        default:
            return QString("设备返回未知错误状态 %1").arg(rsp.status);
        }
    }

    quint32 responseCrcValue(const Response &rsp) const
    {
        if (rsp.payload.size() < static_cast<int>(sizeof(quint32))) {
            return 0U;
        }
        QDataStream stream(rsp.payload.left(sizeof(quint32)));
        stream.setByteOrder(QDataStream::LittleEndian);
        quint32 value = 0;
        stream >> value;
        return value;
    }

    void connectAndHello()
    {
        try {
            if (!ensureOpen()) {
                return;
            }
            const Response rsp = transact(UsbRecoveryCmdHello, 0, 0, 0, {}, 5000);
            expectOk(rsp);
            parseHello(QString::fromUtf8(rsp.payload));
            setDeviceStatus(true, "设备已连接");
            appendLog("INFO", "读取设备信息成功，板卡：" + boardValue_->text());
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "握手失败", e.what());
            appendLog("ERROR", QString::fromUtf8(e.what()));
            closeSerial();
        }
    }

    void parseHello(const QString &payload)
    {
        QMap<QString, QString> fields;
        for (const QString &entry : payload.split(';', Qt::SkipEmptyParts)) {
            const int pos = entry.indexOf('=');
            if (pos > 0) {
                fields.insert(entry.left(pos), entry.mid(pos + 1));
            }
        }
        boardValue_->setText(fields.value("board", "--"));
        protoValue_->setText("v" + fields.value("proto", "--"));
        maxPayloadValue_->setText(fields.value("max_payload", "--"));
        allowedPartitions_->setText(QString("app    font    ezip"));
        vidPidValue_->setText("38F4:1001");
        footerSn_->setText("SN：HVR-RECOVERY");
    }

    void reconnect()
    {
        closeSerial();
        refreshPorts();
        connectAndHello();
    }

    void choosePackage()
    {
        const QString path = QFileDialog::getOpenFileName(this, "选择 update.json", QString(),
                                                          "update.json (update.json);;JSON (*.json)");
        if (!path.isEmpty()) {
            loadPackage(path);
        }
    }

    void loadDefaultPackageIfPresent()
    {
        for (const QString &path : defaultPackageCandidates()) {
            if (QFileInfo::exists(path)) {
                loadPackage(path, false);
                appendLog("INFO", "自动载入升级包：" + path);
                return;
            }
        }
    }

    QStringList defaultPackageCandidates() const
    {
        QStringList paths;
        paths << QDir::current().filePath("output/tfupdate/firmware/update.json");

        QDir sourceDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());
        if (sourceDir.cd("../..")) {
            paths << sourceDir.filePath("output/tfupdate/firmware/update.json");
        }

        QDir appDir(QCoreApplication::applicationDirPath());
        paths << appDir.filePath("output/tfupdate/firmware/update.json");
        if (appDir.cd("../..")) {
            paths << appDir.filePath("output/tfupdate/firmware/update.json");
        }
        paths.removeDuplicates();
        return paths;
    }

    void clearPackage()
    {
        firmware_.clear();
        packagePath_->setText("未选择 update.json");
        updateFirmwareTable();
        updateManifestSummary(false, "未校验");
    }

    void loadPackage(const QString &path, bool showLog = true)
    {
        QFile jsonFile(path);
        if (!jsonFile.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, "读取失败", jsonFile.errorString());
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            QMessageBox::warning(this, "升级包无效", parseError.errorString());
            return;
        }

        const QJsonObject root = doc.object();
        const QJsonArray files = root.value("files").toArray();
        QVector<FirmwareFile> parsed;
        quint64 totalSize = 0;
        bool allValid = !files.isEmpty();
        const QFileInfo manifestInfo(path);

        for (const QJsonValue &value : files) {
            const QJsonObject obj = value.toObject();
            FirmwareFile file;
            file.name = obj.value("name").toString();
            file.path = manifestInfo.dir().filePath(file.name);
            file.addr = parseNumber(obj.value("addr"));
            file.regionSize = parseNumber(obj.value("region_size"));
            file.size = parseNumber(obj.value("size"));
            file.crc32 = parseNumber(obj.value("crc32"));
            file.partition = partitionName(file.addr, file.name);
            totalSize += file.size;

            QFile bin(file.path);
            if (bin.open(QIODevice::ReadOnly)) {
                const QByteArray data = bin.readAll();
                file.localValid = static_cast<quint32>(data.size()) == file.size &&
                    crc32Final(data) == file.crc32;
            }
            allValid = allValid && !file.name.isEmpty() && QFileInfo::exists(file.path) &&
                file.localValid && file.partition != "unknown";
            parsed.push_back(file);
        }

        std::stable_sort(parsed.begin(), parsed.end(), [](const FirmwareFile &left, const FirmwareFile &right) {
            auto rank = [](const QString &partition) {
                if (partition == "app") {
                    return 0;
                }
                if (partition == "font") {
                    return 1;
                }
                if (partition == "ezip") {
                    return 2;
                }
                return 3;
            };
            return rank(left.partition) < rank(right.partition);
        });

        firmware_ = parsed;
        manifestPath_ = path;
        packagePath_->setText(QFileInfo(path).fileName());
        packagePath_->setToolTip(path);
        firmwareVersion_ = root.value("version").toString("--");
        footerVersion_->setText("固件版本：" + firmwareVersion_);
        manifestVersion_->setText(firmwareVersion_);
        manifestTime_->setText(root.value("generated_at").toString("--"));
        manifestBoard_->setText(kExpectedBoard);
        manifestFiles_->setText(QString::number(firmware_.size()));
        manifestSize_->setText(formatBytes(totalSize));
        manifestResult_->setText(allValid ? "全部通过" : "存在异常");
        updateManifestSummary(allValid, allValid ? "校验通过" : "校验失败");
        updateFirmwareTable();
        if (showLog) {
            appendLog(allValid ? "INFO" : "WARN",
                      QString("载入升级包：%1，文件数=%2，总大小=%3")
                          .arg(QFileInfo(path).fileName())
                          .arg(firmware_.size())
                          .arg(formatBytes(totalSize)));
        }
    }

    void updateManifestSummary(bool ok, const QString &text)
    {
        manifestStatusChip_->setText(text);
        manifestStatusChip_->setProperty("tone", ok ? "ok" : "neutral");
        manifestStatusChip_->style()->unpolish(manifestStatusChip_);
        manifestStatusChip_->style()->polish(manifestStatusChip_);
    }

    void updateFirmwareTable()
    {
        firmwareTitle_->setText(QString("固件文件（%1 个）").arg(firmware_.size()));
        firmwareTable_->setRowCount(firmware_.size());
        for (int row = 0; row < firmware_.size(); ++row) {
            const FirmwareFile &file = firmware_[row];
            auto *check = new QTableWidgetItem;
            check->setCheckState(file.selected ? Qt::Checked : Qt::Unchecked);
            check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            firmwareTable_->setItem(row, ColEnable, check);
            setCell(row, ColPartition, file.partition);
            setCell(row, ColFile, file.name);
            setCell(row, ColSize, formatBytes(file.size));
            setCell(row, ColCrc, hex32(file.crc32));
            setCell(row, ColAddress, hex32(file.addr));
            setCell(row, ColRegion, formatBytes(file.regionSize));
            setCell(row, ColStatus, file.localValid ? "待刷写" : "本地校验失败");
        }
        firmwareTable_->resizeColumnsToContents();
        applyTableColumnSizing();
    }

    void applyTableColumnSizing()
    {
        if (firmwareTable_ == nullptr) {
            return;
        }
        firmwareTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        firmwareTable_->setColumnWidth(ColEnable, 34);
        firmwareTable_->setColumnWidth(ColPartition, 72);
        firmwareTable_->horizontalHeader()->setSectionResizeMode(ColFile, QHeaderView::Stretch);
        firmwareTable_->setColumnWidth(ColSize, 90);
        firmwareTable_->setColumnWidth(ColCrc, 126);
        firmwareTable_->setColumnWidth(ColAddress, 126);
        firmwareTable_->setColumnWidth(ColRegion, 90);
        firmwareTable_->setColumnWidth(ColStatus, 116);
    }

    void setCell(int row, int col, const QString &text)
    {
        auto *item = new QTableWidgetItem(text);
        item->setTextAlignment(Qt::AlignCenter);
        firmwareTable_->setItem(row, col, item);
    }

    QVector<int> selectedRows() const
    {
        QVector<int> rows;
        for (int row = 0; row < firmware_.size(); ++row) {
            const auto *item = firmwareTable_->item(row, ColEnable);
            if (item == nullptr || item->checkState() == Qt::Checked) {
                rows.push_back(row);
            }
        }
        return rows;
    }

    void flashPackage()
    {
        runFirmwareOperation(true);
    }

    void verifyPackage()
    {
        runFirmwareOperation(false);
    }

    void runFirmwareOperation(bool write)
    {
        const QVector<int> rows = selectedRows();
        if (rows.isEmpty()) {
            QMessageBox::warning(this, "未选择固件", "请至少选择一个固件文件");
            return;
        }
        try {
            setBusy(true);
            stopRequested_ = false;
            if (!ensureOpen()) {
                setBusy(false);
                return;
            }
            expectOk(transact(UsbRecoveryCmdHello));
            if (write) {
                expectOk(transact(UsbRecoveryCmdBegin));
            }

            quint64 totalBytes = 0;
            quint64 doneBytes = 0;
            for (int row : rows) {
                totalBytes += firmware_[row].size;
            }
            QElapsedTimer timer;
            timer.start();
            bool verifyMismatch = false;

            for (int index = 0; index < rows.size(); ++index) {
                if (stopRequested_) {
                    throw std::runtime_error("操作已停止");
                }
                const int row = rows[index];
                const FirmwareFile &file = firmware_[row];
                QFile bin(file.path);
                if (!bin.open(QIODevice::ReadOnly)) {
                    throw std::runtime_error(QString("无法打开 %1").arg(file.path).toStdString());
                }
                const QByteArray all = bin.readAll();
                if (static_cast<quint32>(all.size()) != file.size || crc32Final(all) != file.crc32) {
                    throw std::runtime_error(QString("本地文件校验失败: %1").arg(file.name).toStdString());
                }

                if (write) {
                    updateRowStatus(row, "擦除中");
                    currentOperation_->setText(QString("当前操作：擦除 %1 (%2)").arg(file.name, file.partition));
                    appendLog("INFO", QString("擦除分区 %1 addr=%2 size=%3")
                                      .arg(file.partition, hex32(file.addr), formatBytes(file.size)));
                    expectOk(transact(UsbRecoveryCmdErase, file.addr, file.size, 0, {}, 35000));

                    updateRowStatus(row, "写入中");
                    for (int offset = 0; offset < all.size(); offset += kMaxPayload) {
                        if (stopRequested_) {
                            throw std::runtime_error("操作已停止");
                        }
                        const QByteArray chunk = all.mid(offset, kMaxPayload);
                        expectOk(transact(UsbRecoveryCmdWrite,
                                          file.addr + static_cast<quint32>(offset),
                                          static_cast<quint32>(chunk.size()),
                                          crc32Final(chunk),
                                          chunk,
                                          6000));
                        doneBytes += chunk.size();
                        updateProgress(doneBytes, totalBytes, offset + chunk.size(), all.size(),
                                       file.name, timer.elapsed(), index + 1, rows.size(), true);
                    }
                }

                updateRowStatus(row, "校验中");
                currentOperation_->setText(QString("当前操作：校验 %1 (%2)").arg(file.name, file.partition));
                const Response verify = transact(UsbRecoveryCmdVerify, file.addr, file.size, file.crc32, {}, 45000);
                if (verify.status == RecoveryStatusOk) {
                    updateRowStatus(row, "完成");
                    appendLog("INFO", QString("校验通过：%1 CRC=%2").arg(file.name, hex32(file.crc32)));
                } else if (!write && verify.status == RecoveryStatusCrcError) {
                    verifyMismatch = true;
                    const quint32 actualCrc = responseCrcValue(verify);
                    updateRowStatus(row, "CRC不匹配");
                    appendLog("WARN", QString("校验不匹配：%1 期望=%2 设备=%3")
                                          .arg(file.name, hex32(file.crc32), hex32(actualCrc)));
                } else {
                    expectOk(verify);
                }
                if (!write) {
                    doneBytes += file.size;
                    updateProgress(doneBytes, totalBytes, file.size, file.size,
                                   file.name, timer.elapsed(), index + 1, rows.size(), false);
                }
            }
            if (write) {
                expectOk(transact(UsbRecoveryCmdCommit));
                appendLog("INFO", "刷写完成，COMMIT 已确认");
            }
            if (write) {
                currentOperation_->setText("当前操作：刷写完成");
            } else {
                currentOperation_->setText(verifyMismatch ? "当前操作：校验完成（存在不匹配）" : "当前操作：校验完成");
            }
            fileProgress_->setValue(100);
            totalProgress_->setValue(100);
        } catch (const std::exception &e) {
            QMessageBox::warning(this, write ? "刷写失败" : "校验失败", e.what());
            appendLog("ERROR", QString::fromUtf8(e.what()));
            currentOperation_->setText("当前操作：失败");
        }
        setBusy(false);
    }

    void updateProgress(quint64 doneBytes, quint64 totalBytes, quint64 fileDone, quint64 fileTotal,
                        const QString &fileName, qint64 elapsedMs, int fileIndex, int fileCount, bool write)
    {
        const int filePct = fileTotal == 0 ? 0 : static_cast<int>((fileDone * 100) / fileTotal);
        const int totalPct = totalBytes == 0 ? 0 : static_cast<int>((doneBytes * 100) / totalBytes);
        fileProgress_->setValue(std::clamp(filePct, 0, 100));
        totalProgress_->setValue(std::clamp(totalPct, 0, 100));
        const double seconds = elapsedMs / 1000.0;
        const double speed = seconds > 0.0 ? static_cast<double>(doneBytes) / seconds : 0.0;
        const double remain = speed > 0.0 ? static_cast<double>(totalBytes - doneBytes) / speed : 0.0;
        currentOperation_->setText(QString("当前操作：正在%1 %2").arg(write ? "刷写" : "校验", fileName));
        transferInfo_->setText(QString("进度：%1 / %2 (%3/s)")
                                   .arg(formatBytes(doneBytes), formatBytes(totalBytes), formatBytes(speed)));
        timeInfo_->setText(QString("已用时间：%1    预计剩余：%2    文件：%3 / %4")
                               .arg(formatDuration(seconds), formatDuration(remain))
                               .arg(fileIndex)
                               .arg(fileCount));
        qApp->processEvents();
    }

    QString formatDuration(double seconds) const
    {
        const int total = static_cast<int>(seconds + 0.5);
        return QString("%1:%2").arg(total / 60, 2, 10, QLatin1Char('0'))
            .arg(total % 60, 2, 10, QLatin1Char('0'));
    }

    void updateRowStatus(int row, const QString &status)
    {
        if (auto *item = firmwareTable_->item(row, ColStatus)) {
            item->setText(status);
        }
        qApp->processEvents();
    }

    void rebootDevice()
    {
        try {
            if (!ensureOpen()) {
                return;
            }
            expectOk(transact(UsbRecoveryCmdReboot, 0, 0, 0, {}, 3000));
            appendLog("INFO", "设备已重启");
            closeSerial();
            QTimer::singleShot(1800, this, [this]() {
                refreshPorts();
                connectAndHello();
            });
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "重启失败", e.what());
            appendLog("ERROR", QString::fromUtf8(e.what()));
        }
    }

    void saveLog()
    {
        const QString path = QFileDialog::getSaveFileName(this, "保存日志", "hiveton-usb-recovery.log", "Log (*.log);;Text (*.txt)");
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "保存失败", file.errorString());
            return;
        }
        file.write(log_->toPlainText().toUtf8());
        appendLog("INFO", "日志已保存：" + path);
    }

    QPushButton *readDeviceButton_ = nullptr;
    QPushButton *reconnectButton_ = nullptr;
    QFrame *deviceSelectControl_ = nullptr;
    QPushButton *deviceSelectButton_ = nullptr;
    QPushButton *deviceSelectArrowButton_ = nullptr;
    QPushButton *chooseButton_ = nullptr;
    QPushButton *flashButton_ = nullptr;
    QPushButton *verifyButton_ = nullptr;
    QPushButton *rebootButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QPushButton *clearLogButton_ = nullptr;
    QPushButton *saveLogButton_ = nullptr;
    QCheckBox *autoDetect_ = nullptr;
    QLabel *sideStatus_ = nullptr;
    QLabel *deviceStatusChip_ = nullptr;
    QLabel *deviceHint_ = nullptr;
    QLabel *devicePath_ = nullptr;
    QLabel *vidPidValue_ = nullptr;
    QLabel *boardValue_ = nullptr;
    QLabel *protoValue_ = nullptr;
    QLabel *maxPayloadValue_ = nullptr;
    QLabel *allowedPartitions_ = nullptr;
    QLabel *packagePath_ = nullptr;
    QLabel *manifestStatusChip_ = nullptr;
    QLabel *manifestVersion_ = nullptr;
    QLabel *manifestTime_ = nullptr;
    QLabel *manifestBoard_ = nullptr;
    QLabel *manifestFiles_ = nullptr;
    QLabel *manifestSize_ = nullptr;
    QLabel *manifestResult_ = nullptr;
    QLabel *firmwareTitle_ = nullptr;
    QLabel *currentOperation_ = nullptr;
    QLabel *transferInfo_ = nullptr;
    QLabel *timeInfo_ = nullptr;
    QLabel *footerStatus_ = nullptr;
    QLabel *footerTransport_ = nullptr;
    QLabel *footerVersion_ = nullptr;
    QLabel *footerSn_ = nullptr;
    QTableWidget *firmwareTable_ = nullptr;
    QProgressBar *fileProgress_ = nullptr;
    QProgressBar *totalProgress_ = nullptr;
    QPlainTextEdit *log_ = nullptr;
    QTimer *refreshTimer_ = nullptr;
    QSerialPort serial_;
    quint16 seq_ = 0;
    QVector<FirmwareFile> firmware_;
    QVector<QString> devicePortNames_;
    QVector<QString> devicePortPaths_;
    QVector<QString> devicePortTooltips_;
    QString currentDevicePath_;
    QString manifestPath_;
    QString firmwareVersion_ = "--";
    bool isBusy_ = false;
    bool stopRequested_ = false;
    bool draggingWindow_ = false;
    QPoint dragOffset_;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    const int screenshotIndex = args.indexOf("--screenshot");
    MainWindow window;
    if (screenshotIndex < 0) {
        window.fitToAvailableScreen();
    }
    window.show();
    if (screenshotIndex >= 0 && screenshotIndex + 1 < args.size()) {
        QTimer::singleShot(1200, &app, [&window, path = args.at(screenshotIndex + 1)]() {
            window.grab().save(path);
            QCoreApplication::quit();
        });
    }
    return app.exec();
}
