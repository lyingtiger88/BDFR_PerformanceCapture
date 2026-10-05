#include "PerformanceMainWindow.h"

#include <QComboBox>
#include <QFrame>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <utility>

namespace bdfrpc::studio {

namespace {

TimestampNs now_ns() {
    return static_cast<TimestampNs>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

QString health_text(SourceHealthState state) {
    return QString::fromLatin1(to_string(state));
}

} // namespace

PerformanceMainWindow::PerformanceMainWindow() {
    build_ui();
    rescan_devices();
    timer_->start(16);
}

PerformanceMainWindow::~PerformanceMainWindow() {
    timer_->stop();
    take_recorder_.stop();
    stop_solver_bridges();
    stop_cameras();
}

void PerformanceMainWindow::build_ui() {
    setWindowTitle("BDFR Performance Studio");
    resize(1280, 820);

    central_ = new QWidget(this);
    auto* root = new QVBoxLayout(central_);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto* top = new QHBoxLayout();

    mode_combo_ = new QComboBox(central_);
    mode_combo_->addItem("Face Only");
    mode_combo_->addItem("Body Only");
    mode_combo_->addItem("Hybrid");
    mode_combo_->setCurrentIndex(2);

    scan_button_ = new QPushButton("Scan Cameras", central_);
    start_button_ = new QPushButton("Start Cameras", central_);
    stop_button_ = new QPushButton("Stop Cameras", central_);
    stop_button_->setEnabled(false);

    top->addWidget(new QLabel("Capture Mode:", central_));
    top->addWidget(mode_combo_);
    top->addSpacing(16);
    top->addWidget(scan_button_);
    top->addWidget(start_button_);
    top->addWidget(stop_button_);
    top->addSpacing(16);
    record_button_ = new QPushButton("Record Take", central_);
    stop_record_button_ = new QPushButton("Stop Recording", central_);
    stop_record_button_->setEnabled(false);
    top->addWidget(record_button_);
    top->addWidget(stop_record_button_);
    top->addStretch(1);
    root->addLayout(top);

    auto* bridges = new QGroupBox("Solver Bridges", central_);
    auto* bridge_row = new QHBoxLayout(bridges);

    facial_port_ = new QSpinBox(bridges);
    facial_port_->setRange(0, 65535);
    facial_port_->setValue(7001);
    easymocap_port_ = new QSpinBox(bridges);
    easymocap_port_->setRange(0, 65535);
    easymocap_port_->setValue(9999);

    solver_start_button_ = new QPushButton("Start Bridges", bridges);
    solver_stop_button_ = new QPushButton("Stop Bridges", bridges);
    solver_stop_button_->setEnabled(false);

    bridge_row->addWidget(new QLabel("Facial UDP:", bridges));
    bridge_row->addWidget(facial_port_);
    bridge_row->addSpacing(10);
    bridge_row->addWidget(new QLabel("EasyMocap TCP:", bridges));
    bridge_row->addWidget(easymocap_port_);
    bridge_row->addWidget(solver_start_button_);
    bridge_row->addWidget(solver_stop_button_);
    bridge_row->addStretch(1);
    root->addWidget(bridges);

    capture_status_ = new QLabel("Cameras: stopped", central_);
    sync_status_ = new QLabel("Sync: no frame sets yet", central_);
    solver_status_ = new QLabel("Solvers: stopped", central_);
    fusion_status_ = new QLabel("Fusion: no solver data", central_);

    auto* telemetry = new QHBoxLayout();
    telemetry->addWidget(capture_status_, 1);
    telemetry->addWidget(sync_status_, 1);
    telemetry->addWidget(solver_status_, 1);
    telemetry->addWidget(fusion_status_, 1);
    root->addLayout(telemetry);

    auto* scroll = new QScrollArea(central_);
    scroll->setWidgetResizable(true);
    auto* grid_host = new QWidget(scroll);
    camera_grid_ = new QGridLayout(grid_host);
    camera_grid_->setContentsMargins(6, 6, 6, 6);
    camera_grid_->setSpacing(10);
    scroll->setWidget(grid_host);
    root->addWidget(scroll, 1);

    setCentralWidget(central_);
    statusBar()->showMessage("Ready");

    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);

    connect(scan_button_, &QPushButton::clicked, this, [this] {
        rescan_devices();
    });
    connect(start_button_, &QPushButton::clicked, this, [this] {
        start_cameras();
    });
    connect(stop_button_, &QPushButton::clicked, this, [this] {
        stop_cameras();
    });
    connect(record_button_, &QPushButton::clicked, this, [this] {
        start_recording();
    });
    connect(stop_record_button_, &QPushButton::clicked, this, [this] {
        stop_recording();
    });
    connect(solver_start_button_, &QPushButton::clicked, this, [this] {
        start_solver_bridges();
    });
    connect(solver_stop_button_, &QPushButton::clicked, this, [this] {
        stop_solver_bridges();
    });
    connect(mode_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        apply_mode(index);
    });
    connect(timer_, &QTimer::timeout, this, [this] {
        tick();
    });
}

void PerformanceMainWindow::rescan_devices() {
    stop_cameras();

    const auto devices = OpenCVDeviceEnumerator::probe(0, 15);
    cameras_.clear();
    cameras_.reserve(devices.size());

    for (const auto& device : devices) {
        CameraUi entry;
        entry.descriptor = device;
        cameras_.push_back(std::move(entry));
    }

    rebuild_camera_grid();
    capture_status_->setText(
        QString("Cameras: %1 detected").arg(cameras_.size()));
    statusBar()->showMessage(
        QString("Detected %1 camera(s)").arg(cameras_.size()), 3000);
}

void PerformanceMainWindow::rebuild_camera_grid() {
    while (auto* item = camera_grid_->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    const int columns = 3;
    for (std::size_t i = 0; i < cameras_.size(); ++i) {
        auto& camera = cameras_[i];

        auto* card = new QFrame(central_);
        card->setFrameShape(QFrame::StyledPanel);
        auto* layout = new QVBoxLayout(card);

        auto* title = new QLabel(
            QString::fromStdString(camera.descriptor.id) + "  ·  " +
            QString::fromStdString(camera.descriptor.backend),
            card);
        title->setStyleSheet("font-weight: 600;");

        auto* preview = new QLabel("Preview unavailable", card);
        preview->setAlignment(Qt::AlignCenter);
        preview->setMinimumSize(320, 180);
        preview->setStyleSheet(
            "background:#15171a; color:#8d949e; border:1px solid #30343a;");

        auto* stats = new QLabel(card);
        stats->setText(
            QString("%1×%2  |  requested/detected %3 fps")
                .arg(camera.descriptor.width)
                .arg(camera.descriptor.height)
                .arg(camera.descriptor.fps, 0, 'f', 1));

        layout->addWidget(title);
        layout->addWidget(preview, 1);
        layout->addWidget(stats);

        camera.card = card;
        camera.preview = preview;
        camera.stats = stats;

        camera_grid_->addWidget(
            card,
            static_cast<int>(i) / columns,
            static_cast<int>(i) % columns);
    }

    if (cameras_.empty()) {
        auto* empty = new QLabel(
            "No local OpenCV cameras detected.\n"
            "Connect a UVC/capture device and press Scan Cameras.",
            central_);
        empty->setAlignment(Qt::AlignCenter);
        camera_grid_->addWidget(empty, 0, 0);
    }
}

void PerformanceMainWindow::start_cameras() {
    if (cameras_.empty()) {
        rescan_devices();
        if (cameras_.empty()) return;
    }

    stop_cameras();

    std::vector<std::string> streams;
    std::size_t started = 0;

    for (auto& camera : cameras_) {
        CameraConfig config;
        config.device_index = camera.descriptor.device_index;
        config.width = camera.descriptor.width > 0
            ? camera.descriptor.width : 1280;
        config.height = camera.descriptor.height > 0
            ? camera.descriptor.height : 720;
        config.fps = camera.descriptor.fps > 1.0
            ? camera.descriptor.fps : 30.0;
        config.buffer_frames = 4;
        config.source_id = camera.descriptor.id;
        config.stream_id = camera.descriptor.id;

        camera.source = std::make_unique<OpenCVCameraSource>(config);
        if (camera.source->start()) {
            streams.push_back(config.stream_id);
            ++started;
        } else {
            camera.source.reset();
            if (camera.stats) camera.stats->setText("Failed to open camera");
        }
    }

    if (!streams.empty()) {
        synchronizer_ = std::make_unique<FrameSynchronizer>(
            20'000'000,
            8);
        synchronizer_->set_required_streams(std::move(streams));
    }

    synchronized_sets_ = 0;
    start_button_->setEnabled(false);
    stop_button_->setEnabled(true);
    scan_button_->setEnabled(false);
    capture_status_->setText(
        QString("Cameras: %1/%2 live")
            .arg(started)
            .arg(cameras_.size()));
}

void PerformanceMainWindow::stop_cameras() {
    for (auto& camera : cameras_) {
        if (camera.source) {
            camera.source->stop();
            camera.source.reset();
        }
        if (camera.preview) camera.preview->setText("Camera stopped");
    }

    synchronizer_.reset();
    synchronized_sets_ = 0;

    if (start_button_) start_button_->setEnabled(true);
    if (stop_button_) stop_button_->setEnabled(false);
    if (scan_button_) scan_button_->setEnabled(true);
    if (capture_status_) capture_status_->setText("Cameras: stopped");
    if (sync_status_) sync_status_->setText("Sync: stopped");
}

void PerformanceMainWindow::start_solver_bridges() {
    stop_solver_bridges();

    BDFRFacialUdpConfig facial_config;
    facial_config.port =
        static_cast<std::uint16_t>(facial_port_->value());
    facial_config.source_id = "bdfr_facial";
    facial_source_ =
        std::make_unique<BDFRFacialUdpSource>(facial_config);

    EasyMocapTcpConfig easy_config;
    easy_config.port =
        static_cast<std::uint16_t>(easymocap_port_->value());
    easy_config.source_id = "easymocap";
    easymocap_source_ =
        std::make_unique<EasyMocapTcpSource>(easy_config);

    const bool facial_ok = facial_source_->start();
    const bool easy_ok = easymocap_source_->start();

    if (!facial_ok) facial_source_.reset();
    if (!easy_ok) easymocap_source_.reset();

    solver_start_button_->setEnabled(false);
    solver_stop_button_->setEnabled(true);
    update_solver_status();
}

void PerformanceMainWindow::stop_solver_bridges() {
    if (facial_source_) {
        facial_source_->stop();
        facial_source_.reset();
    }
    if (easymocap_source_) {
        easymocap_source_->stop();
        easymocap_source_.reset();
    }

    if (solver_start_button_) solver_start_button_->setEnabled(true);
    if (solver_stop_button_) solver_stop_button_->setEnabled(false);
    if (solver_status_) solver_status_->setText("Solvers: stopped");
}

void PerformanceMainWindow::apply_mode(int index) {
    OperatingMode mode = OperatingMode::Hybrid;
    if (index == 0) mode = OperatingMode::FaceOnly;
    else if (index == 1) mode = OperatingMode::BodyOnly;
    fusion_runtime_.set_mode(mode);
}

void PerformanceMainWindow::start_recording() {
    const auto path = QFileDialog::getSaveFileName(
        this,
        "Record Performance Take",
        "capture.bdfrtake.csv",
        "BDFR Performance Take (*.bdfrtake.csv);;CSV (*.csv)");
    if (path.isEmpty()) return;

    if (!take_recorder_.start(path.toStdString())) {
        statusBar()->showMessage("Failed to open take file", 4000);
        return;
    }

    record_button_->setEnabled(false);
    stop_record_button_->setEnabled(true);
    statusBar()->showMessage("Recording fused performance take");
}

void PerformanceMainWindow::stop_recording() {
    if (!take_recorder_.recording()) return;
    const auto frames = take_recorder_.frames_written();
    take_recorder_.stop();
    record_button_->setEnabled(true);
    stop_record_button_->setEnabled(false);
    statusBar()->showMessage(
        QString("Take saved · %1 fused frames").arg(frames),
        5000);
}

void PerformanceMainWindow::show_frame(
    CameraUi& camera,
    const CaptureFrame& frame) {

    if (!camera.preview || !frame.image.valid()) return;
    if (camera.preview_sequence == frame.sequence) return;
    camera.preview_sequence = frame.sequence;

    const auto& image = frame.image;
    QImage qimage(
        image.bytes->data(),
        image.width,
        image.height,
        image.stride_bytes,
        QImage::Format_BGR888);

    camera.preview->setPixmap(
        QPixmap::fromImage(qimage.copy()).scaled(
            camera.preview->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
}

void PerformanceMainWindow::tick() {
    for (auto& camera : cameras_) {
        if (!camera.source || !camera.source->running()) continue;

        while (auto frame = camera.source->poll()) {
            show_frame(camera, *frame);
            if (synchronizer_) synchronizer_->push(std::move(*frame));
        }

        if (camera.stats) {
            camera.stats->setText(
                QString("FPS %1  |  capture drop %2  |  queue %3")
                    .arg(camera.source->measured_fps(), 0, 'f', 1)
                    .arg(camera.source->dropped_frames())
                    .arg(camera.source->buffered_frames()));
        }
    }

    if (synchronizer_) {
        while (auto set = synchronizer_->try_pop()) {
            (void)set;
            ++synchronized_sets_;
        }
    }

    if (facial_source_) {
        while (auto frame = facial_source_->poll()) {
            fusion_runtime_.submit(std::move(*frame));
        }
    }

    if (easymocap_source_) {
        while (auto frame = easymocap_source_->poll()) {
            fusion_runtime_.submit(std::move(*frame));
        }
    }

    update_sync_status();
    update_solver_status();

    const auto now = now_ns();
    const auto fused = fusion_runtime_.evaluate(now);
    if (fused.samples.empty()) {
        fusion_status_->setText("Fusion: no fresh solver data");
    } else {
        if (take_recorder_.recording()) {
            if (!take_recorder_.append(fused)) {
                statusBar()->showMessage("Take recording write failed", 4000);
                stop_recording();
            }
        }
        std::string sources;
        for (std::size_t i = 0; i < fused.selected_sources.size(); ++i) {
            if (i) sources += ", ";
            sources += fused.selected_sources[i];
        }
        fusion_status_->setText(
            QString("Fusion: %1 domains · %2")
                .arg(fused.samples.size())
                .arg(QString::fromStdString(sources)));
    }
}

void PerformanceMainWindow::update_sync_status() {
    if (!synchronizer_) return;
    const auto& s = synchronizer_->stats();

    sync_status_->setText(
        QString("Sync: %1 sets · last %2 ms · max %3 ms")
            .arg(synchronized_sets_)
            .arg(static_cast<double>(s.last_set_skew_ns) / 1.0e6, 0, 'f', 2)
            .arg(static_cast<double>(s.max_set_skew_ns) / 1.0e6, 0, 'f', 2));
}

void PerformanceMainWindow::update_solver_status() {
    QString face = "Face OFF";
    QString body = "Body OFF";

    if (facial_source_) {
        face = QString("Face UDP %1 · %2 packets")
            .arg(facial_source_->local_port())
            .arg(facial_source_->stats().packets_received);
    }

    if (easymocap_source_) {
        body = QString("EasyMocap TCP %1 · %2 frames")
            .arg(easymocap_source_->local_port())
            .arg(easymocap_source_->stats().frames_received);
    }

    const auto health = fusion_runtime_.health().snapshot(now_ns());
    QString health_summary;
    for (const auto& source : health) {
        if (!health_summary.isEmpty()) health_summary += " | ";
        health_summary +=
            QString::fromStdString(source.source_id) + ":" +
            health_text(source.state);
    }

    solver_status_->setText(
        QString("Solvers: %1 | %2%3")
            .arg(face)
            .arg(body)
            .arg(health_summary.isEmpty()
                ? QString()
                : QString(" | %1").arg(health_summary)));
}

} // namespace bdfrpc::studio
