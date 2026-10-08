#include "PerformanceMainWindow.h"
#include "SkeletonViewportWidget.h"
#include "CalibrationWizardDialog.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
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
    shutdown_easymocap_worker();
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
    calibration_button_ =
        new QPushButton("Calibration", central_);
    top->addWidget(calibration_button_);
    top->addSpacing(16);
    record_button_ = new QPushButton("Record Take", central_);
    stop_record_button_ = new QPushButton("Stop Recording", central_);
    stop_record_button_->setEnabled(false);
    top->addWidget(record_button_);
    top->addWidget(stop_record_button_);
    top->addSpacing(16);

    open_take_button_ = new QPushButton("Open Take", central_);
    session_browser_button_ = new QPushButton("Sessions", central_);
    play_take_button_ = new QPushButton("Play Take", central_);
    stop_take_button_ = new QPushButton("Stop Take", central_);
    export_bvh_button_ = new QPushButton("Export BVH", central_);
    play_take_button_->setEnabled(false);
    stop_take_button_->setEnabled(false);
    export_bvh_button_->setEnabled(false);

    top->addWidget(open_take_button_);
    top->addWidget(session_browser_button_);
    top->addWidget(play_take_button_);
    top->addWidget(stop_take_button_);
    top->addWidget(export_bvh_button_);
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

    auto* process_box = new QGroupBox("EasyMocap Process", central_);
    auto* process_row = new QHBoxLayout(process_box);

    easymocap_cwd_ = new QLineEdit(process_box);
    easymocap_cwd_->setPlaceholderText("EasyMocap repository / working directory");
    easymocap_command_ = new QLineEdit(process_box);
    easymocap_command_->setPlaceholderText(
        "Solver command, e.g. python apps/demo/... <args>");

    easymocap_browse_button_ =
        new QPushButton("Browse", process_box);
    easymocap_launch_button_ =
        new QPushButton("Launch Solver", process_box);
    easymocap_stop_button_ =
        new QPushButton("Stop Solver", process_box);
    easymocap_stop_button_->setEnabled(false);
    easymocap_process_status_ =
        new QLabel("Worker: stopped", process_box);

    process_row->addWidget(
        new QLabel("Working dir:", process_box));
    process_row->addWidget(easymocap_cwd_, 2);
    process_row->addWidget(easymocap_browse_button_);
    process_row->addWidget(
        new QLabel("Command:", process_box));
    process_row->addWidget(easymocap_command_, 3);
    process_row->addWidget(easymocap_launch_button_);
    process_row->addWidget(easymocap_stop_button_);
    process_row->addWidget(easymocap_process_status_);
    root->addWidget(process_box);

    easymocap_worker_process_ = new QProcess(this);
    easymocap_worker_process_->setProcessChannelMode(
        QProcess::SeparateChannels);

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

    auto* playback_row = new QHBoxLayout();
    playback_status_ = new QLabel("Take: none", central_);
    playback_slider_ = new QSlider(Qt::Horizontal, central_);
    playback_slider_->setRange(0, 0);
    playback_slider_->setEnabled(false);
    playback_row->addWidget(playback_status_);
    playback_row->addWidget(playback_slider_, 1);
    root->addLayout(playback_row);

    auto* scroll = new QScrollArea(central_);
    scroll->setWidgetResizable(true);
    auto* grid_host = new QWidget(scroll);
    camera_grid_ = new QGridLayout(grid_host);
    camera_grid_->setContentsMargins(6, 6, 6, 6);
    camera_grid_->setSpacing(10);
    scroll->setWidget(grid_host);

    auto* workspace = new QHBoxLayout();
    workspace->addWidget(scroll, 3);
    skeleton_view_ = new SkeletonViewportWidget(central_);
    workspace->addWidget(skeleton_view_, 2);
    root->addLayout(workspace, 1);

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
    connect(calibration_button_, &QPushButton::clicked, this, [this] {
        open_calibration_wizard();
    });
    connect(record_button_, &QPushButton::clicked, this, [this] {
        start_recording();
    });
    connect(stop_record_button_, &QPushButton::clicked, this, [this] {
        stop_recording();
    });
    connect(open_take_button_, &QPushButton::clicked, this, [this] {
        open_take();
    });
    connect(session_browser_button_, &QPushButton::clicked, this, [this] {
        browse_sessions();
    });
    connect(play_take_button_, &QPushButton::clicked, this, [this] {
        toggle_playback();
    });
    connect(stop_take_button_, &QPushButton::clicked, this, [this] {
        stop_playback();
    });
    connect(export_bvh_button_, &QPushButton::clicked, this, [this] {
        export_current_take_bvh();
    });
    connect(easymocap_browse_button_, &QPushButton::clicked, this, [this] {
        browse_easymocap_directory();
    });
    connect(easymocap_launch_button_, &QPushButton::clicked, this, [this] {
        launch_easymocap_solver();
    });
    connect(easymocap_stop_button_, &QPushButton::clicked, this, [this] {
        stop_easymocap_solver();
    });
    connect(
        easymocap_worker_process_,
        &QProcess::readyReadStandardOutput,
        this,
        [this] { handle_easymocap_worker_output(); });
    connect(
        easymocap_worker_process_,
        &QProcess::readyReadStandardError,
        this,
        [this] {
            const auto text =
                easymocap_worker_process_->readAllStandardError();
            if (!text.isEmpty()) {
                statusBar()->showMessage(
                    QString::fromUtf8(text).trimmed(),
                    2500);
            }
        });
    connect(
        easymocap_worker_process_,
        &QProcess::finished,
        this,
        [this](int, QProcess::ExitStatus) {
            easymocap_process_status_->setText("Worker: stopped");
            easymocap_stop_button_->setEnabled(false);
        });
    connect(playback_slider_, &QSlider::valueChanged, this, [this](int value) {
        seek_playback(value);
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
        "capture.bdfrtake",
        "BDFR Binary Take (*.bdfrtake);;Legacy CSV Take (*.bdfrtake.csv *.csv)");
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


void PerformanceMainWindow::open_take() {
    const auto path = QFileDialog::getOpenFileName(
        this,
        "Open Performance Take",
        QString(),
        "BDFR Performance Take (*.bdfrtake *.bdfrtake.csv *.csv)");
    if (path.isEmpty()) return;
    load_take_path(path);
}

bool PerformanceMainWindow::load_take_path(const QString& path) {
    TakeReader reader;
    if (!reader.load(path.toStdString())) {
        statusBar()->showMessage(
            QString("Failed to open take: %1")
                .arg(QString::fromStdString(reader.last_error())),
            5000);
        return false;
    }

    take_reader_ = std::move(reader);
    playback_mode_ = true;
    playback_running_ = false;
    playback_index_ = 0;

    {
        const QSignalBlocker blocker(playback_slider_);
        playback_slider_->setRange(
            0,
            static_cast<int>(take_reader_.frames().size() - 1));
        playback_slider_->setValue(0);
    }

    playback_slider_->setEnabled(true);
    play_take_button_->setEnabled(true);
    stop_take_button_->setEnabled(true);
    export_bvh_button_->setEnabled(true);
    play_take_button_->setText("Play Take");

    if (const auto* frame = take_reader_.frame(0)) {
        display_fused_skeleton(*frame);
    }
    update_playback();

    statusBar()->showMessage(
        QString("Take loaded · %1 fused frames")
            .arg(static_cast<qulonglong>(take_reader_.frames().size())),
        4000);
    return true;
}


void PerformanceMainWindow::open_calibration_wizard() {
    if (std::any_of(
            cameras_.begin(),
            cameras_.end(),
            [](const CameraUi& camera) {
                return camera.source && camera.source->running();
            })) {
        statusBar()->showMessage(
            "Stop live camera capture before calibration",
            4000);
        return;
    }

    CalibrationWizardDialog dialog(this);
    dialog.exec();
}

void PerformanceMainWindow::browse_sessions() {
    const auto directory = QFileDialog::getExistingDirectory(
        this,
        "Browse BDFR Take Sessions");
    if (directory.isEmpty()) return;

    const auto sessions =
        TakeSessionIndex::scan(directory.toStdString(), true);

    QDialog dialog(this);
    dialog.setWindowTitle("BDFR Session Browser");
    dialog.resize(760, 460);

    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);

    for (const auto& session : sessions) {
        const double seconds =
            static_cast<double>(session.duration_ns) / 1.0e9;

        const QString line = session.valid
            ? QString("%1   ·   %2   ·   %3 frames   ·   %4 s")
                  .arg(QString::fromStdString(session.filename))
                  .arg(TakeSessionIndex::format_name(session.format))
                  .arg(static_cast<qulonglong>(session.frame_count))
                  .arg(seconds, 0, 'f', 2)
            : QString("%1   ·   INVALID   ·   %2")
                  .arg(QString::fromStdString(session.filename))
                  .arg(QString::fromStdString(session.error));

        auto* item = new QListWidgetItem(line, list);
        item->setData(
            Qt::UserRole,
            QString::fromStdString(session.path));
        item->setData(
            Qt::UserRole + 1,
            session.valid);
        if (!session.valid) {
            item->setFlags(
                item->flags() & ~Qt::ItemIsEnabled);
        }
    }

    if (sessions.empty()) {
        auto* item = new QListWidgetItem(
            "No .bdfrtake or CSV sessions found in this folder.",
            list);
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }

    layout->addWidget(list, 1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Open | QDialogButtonBox::Cancel,
        &dialog);
    auto* open_button = buttons->button(QDialogButtonBox::Open);
    open_button->setEnabled(false);
    layout->addWidget(buttons);

    connect(
        list,
        &QListWidget::currentItemChanged,
        &dialog,
        [open_button](QListWidgetItem* current, QListWidgetItem*) {
            open_button->setEnabled(
                current &&
                current->data(Qt::UserRole + 1).toBool());
        });

    connect(
        list,
        &QListWidget::itemDoubleClicked,
        &dialog,
        [&dialog](QListWidgetItem* item) {
            if (item &&
                item->data(Qt::UserRole + 1).toBool()) {
                dialog.accept();
            }
        });

    connect(
        buttons,
        &QDialogButtonBox::accepted,
        &dialog,
        &QDialog::accept);
    connect(
        buttons,
        &QDialogButtonBox::rejected,
        &dialog,
        &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    const auto* item = list->currentItem();
    if (!item) return;

    const QString path =
        item->data(Qt::UserRole).toString();
    if (!path.isEmpty()) {
        load_take_path(path);
    }
}

void PerformanceMainWindow::toggle_playback() {
    if (!take_reader_.loaded()) return;

    if (playback_running_) {
        playback_running_ = false;
        play_take_button_->setText("Play Take");
        return;
    }

    if (playback_index_ >= take_reader_.frames().size() - 1) {
        playback_index_ = 0;
    }

    const auto* frame = take_reader_.frame(playback_index_);
    if (!frame) return;

    playback_mode_ = true;
    playback_running_ = true;
    playback_wall_anchor_ns_ = now_ns();
    playback_take_anchor_ns_ = frame->timestamp_ns;
    play_take_button_->setText("Pause");
}

void PerformanceMainWindow::stop_playback() {
    playback_running_ = false;
    playback_mode_ = false;
    playback_index_ = 0;
    play_take_button_->setText("Play Take");

    if (take_reader_.loaded()) {
        const QSignalBlocker blocker(playback_slider_);
        playback_slider_->setValue(0);
        playback_status_->setText(
            QString("Take loaded · %1 frames")
                .arg(take_reader_.frames().size()));
    }

    if (skeleton_view_) skeleton_view_->clear_skeleton();
}

void PerformanceMainWindow::seek_playback(int index) {
    if (!take_reader_.loaded() || index < 0) return;
    const auto count = take_reader_.frames().size();
    if (static_cast<std::size_t>(index) >= count) return;

    playback_mode_ = true;
    playback_index_ = static_cast<std::size_t>(index);

    if (const auto* frame = take_reader_.frame(playback_index_)) {
        display_fused_skeleton(*frame);
        if (playback_running_) {
            playback_wall_anchor_ns_ = now_ns();
            playback_take_anchor_ns_ = frame->timestamp_ns;
        }
    }
    update_playback();
}

void PerformanceMainWindow::display_fused_skeleton(
    const FusedPerformanceFrame& fused) {

    CaptureFrame frame;
    frame.source_id = "take";
    frame.stream_id = "take";
    frame.timestamp_ns = fused.timestamp_ns;

    for (const auto& sample : fused.samples) {
        if (sample.domain == Domain::Body ||
            sample.domain == Domain::Face) {
            frame.samples.push_back(sample);
        }
    }

    const auto ids = EasyMocapSkeletonMapper::subject_ids(frame);
    if (ids.empty()) return;

    const auto skeleton =
        EasyMocapSkeletonMapper::map_subject(frame, ids.front());
    if (skeleton && skeleton_view_) {
        skeleton_view_->set_skeleton(*skeleton);
    }
}

void PerformanceMainWindow::update_playback() {
    if (!take_reader_.loaded()) return;

    if (playback_running_) {
        const TimestampNs target =
            playback_take_anchor_ns_ +
            (now_ns() - playback_wall_anchor_ns_);

        if (target >= take_reader_.end_time_ns()) {
            playback_index_ = take_reader_.frames().size() - 1;
            playback_running_ = false;
            play_take_button_->setText("Play Take");
        } else {
            auto index = take_reader_.lower_bound_index(target);
            if (index >= take_reader_.frames().size()) {
                index = take_reader_.frames().size() - 1;
            } else if (
                index > 0 &&
                take_reader_.frames()[index].timestamp_ns > target) {
                --index;
            }
            playback_index_ = index;
        }

        if (const auto* frame = take_reader_.frame(playback_index_)) {
            display_fused_skeleton(*frame);
            fusion_status_->setText(
                QString("Playback: %1 domains")
                    .arg(frame->samples.size()));
        }
    }

    {
        const QSignalBlocker blocker(playback_slider_);
        playback_slider_->setValue(
            static_cast<int>(playback_index_));
    }

    const auto* frame = take_reader_.frame(playback_index_);
    const double seconds = frame
        ? static_cast<double>(
              frame->timestamp_ns - take_reader_.start_time_ns()) / 1.0e9
        : 0.0;

    playback_status_->setText(
        QString("Take %1/%2 · %3 s")
            .arg(playback_index_ + 1)
            .arg(take_reader_.frames().size())
            .arg(seconds, 0, 'f', 2));
}



void PerformanceMainWindow::browse_easymocap_directory() {
    const auto directory = QFileDialog::getExistingDirectory(
        this,
        "Select EasyMocap Working Directory",
        easymocap_cwd_->text());
    if (!directory.isEmpty()) {
        easymocap_cwd_->setText(directory);
    }
}

bool PerformanceMainWindow::ensure_easymocap_worker() {
    if (!easymocap_worker_process_) return false;

    if (easymocap_worker_process_->state() != QProcess::NotRunning) {
        return true;
    }

#ifdef _WIN32
    QString python = QStandardPaths::findExecutable("python");
    if (python.isEmpty()) {
        python = QStandardPaths::findExecutable("py");
    }
#else
    QString python = QStandardPaths::findExecutable("python3");
    if (python.isEmpty()) {
        python = QStandardPaths::findExecutable("python");
    }
#endif

    if (python.isEmpty()) {
        statusBar()->showMessage(
            "Python executable not found for EasyMocap worker",
            5000);
        return false;
    }

    const QString worker =
        QString::fromUtf8(BDFRPC_SOURCE_DIR) +
        "/adapters/easymocap_worker.py";

    easymocap_worker_process_->setProgram(python);
    easymocap_worker_process_->setArguments({worker});
    easymocap_worker_process_->start();

    if (!easymocap_worker_process_->waitForStarted(3000)) {
        statusBar()->showMessage(
            "Failed to start EasyMocap worker process",
            5000);
        return false;
    }

    easymocap_process_status_->setText("Worker: starting");
    return true;
}

void PerformanceMainWindow::launch_easymocap_solver() {
    const QString command = easymocap_command_->text().trimmed();
    if (command.isEmpty()) {
        statusBar()->showMessage(
            "Enter an EasyMocap solver command first",
            4000);
        return;
    }

    const QString cwd = easymocap_cwd_->text().trimmed();
    if (cwd.isEmpty()) {
        statusBar()->showMessage(
            "Select the EasyMocap working directory first",
            4000);
        return;
    }

    const QStringList parts = QProcess::splitCommand(command);
    if (parts.isEmpty()) {
        statusBar()->showMessage(
            "Invalid EasyMocap solver command",
            4000);
        return;
    }

    if (!ensure_easymocap_worker()) return;

    QJsonArray argv;
    for (const auto& part : parts) argv.append(part);

    QJsonObject request{
        {"command", "launch"},
        {"argv", argv},
        {"cwd", cwd},
        {"auto_restart", true},
        {"max_restarts", 3},
        {"restart_delay", 1.0}
    };

    const QByteArray line =
        QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n";
    easymocap_worker_process_->write(line);
    easymocap_worker_process_->waitForBytesWritten(1000);

    easymocap_process_status_->setText("Solver: launching");
    easymocap_stop_button_->setEnabled(true);
}

void PerformanceMainWindow::stop_easymocap_solver() {
    if (!easymocap_worker_process_ ||
        easymocap_worker_process_->state() == QProcess::NotRunning) {
        return;
    }

    const QJsonObject request{{"command", "stop_solver"}};
    easymocap_worker_process_->write(
        QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n");
    easymocap_worker_process_->waitForBytesWritten(1000);
}

void PerformanceMainWindow::shutdown_easymocap_worker() {
    if (!easymocap_worker_process_ ||
        easymocap_worker_process_->state() == QProcess::NotRunning) {
        return;
    }

    const QJsonObject request{{"command", "shutdown"}};
    easymocap_worker_process_->write(
        QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n");
    easymocap_worker_process_->waitForBytesWritten(500);

    if (!easymocap_worker_process_->waitForFinished(1500)) {
        easymocap_worker_process_->terminate();
        if (!easymocap_worker_process_->waitForFinished(1000)) {
            easymocap_worker_process_->kill();
            easymocap_worker_process_->waitForFinished(1000);
        }
    }
}

void PerformanceMainWindow::handle_easymocap_worker_output() {
    while (easymocap_worker_process_->canReadLine()) {
        const QByteArray line =
            easymocap_worker_process_->readLine().trimmed();
        if (line.isEmpty()) continue;

        QJsonParseError parse_error{};
        const auto document =
            QJsonDocument::fromJson(line, &parse_error);
        if (parse_error.error != QJsonParseError::NoError ||
            !document.isObject()) {
            continue;
        }

        const auto object = document.object();
        const auto solver = object.value("solver").toObject();
        const bool running =
            solver.value("running").toBool(false);
        const int restarts =
            solver.value("restarts").toInt(0);
        const auto pid =
            solver.value("pid").toVariant().toLongLong();

        if (running) {
            easymocap_process_status_->setText(
                QString("Solver: PID %1 · restarts %2")
                    .arg(pid)
                    .arg(restarts));
            easymocap_stop_button_->setEnabled(true);
        } else {
            const QString error =
                solver.value("last_error").toString();
            easymocap_process_status_->setText(
                error.isEmpty()
                    ? "Worker: ready"
                    : QString("Worker: %1").arg(error));
            easymocap_stop_button_->setEnabled(false);
        }
    }
}

void PerformanceMainWindow::export_current_take_bvh() {
    if (!take_reader_.loaded()) return;

    const auto path = QFileDialog::getSaveFileName(
        this,
        "Export Take to BVH",
        "capture.bvh",
        "Biovision Hierarchy (*.bvh)");
    if (path.isEmpty()) return;

    const auto skeletons =
        extract_skeleton_sequence(take_reader_.frames(), 0);

    if (skeletons.empty()) {
        statusBar()->showMessage(
            "No EasyMocap subject 0 skeleton data found in take",
            5000);
        return;
    }

    std::string error;
    if (!export_bvh(
            path.toStdString(),
            skeletons,
            {},
            &error)) {
        statusBar()->showMessage(
            QString("BVH export failed: %1")
                .arg(QString::fromStdString(error)),
            5000);
        return;
    }

    statusBar()->showMessage(
        QString("BVH exported · %1 frames")
            .arg(skeletons.size()),
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
            if (!playback_mode_) {
                const auto ids =
                    EasyMocapSkeletonMapper::subject_ids(*frame);
                if (!ids.empty()) {
                    const auto skeleton =
                        EasyMocapSkeletonMapper::map_subject(
                            *frame, ids.front());
                    if (skeleton && skeleton_view_) {
                        skeleton_view_->set_skeleton(*skeleton);
                    }
                }
            }
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

    if (playback_mode_) {
        update_playback();
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
