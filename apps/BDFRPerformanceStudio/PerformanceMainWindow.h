#pragma once

#include "bdfrpc/BDFRFacialUdpSource.h"
#include "bdfrpc/EasyMocapTcpSource.h"
#include "bdfrpc/FrameSynchronizer.h"
#include "bdfrpc/FusionRuntime.h"
#include "bdfrpc/OpenCVCameraSource.h"
#include "bdfrpc/OpenCVDeviceEnumerator.h"
#include "bdfrpc/TakeRecorder.h"
#include "bdfrpc/TakeReader.h"
#include "bdfrpc/Skeleton.h"
#include "bdfrpc/BvhExport.h"
#include "bdfrpc/UnrealFacialUdpSender.h"
#include "bdfrpc/TakeSessionIndex.h"

#include <QMainWindow>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QProcess;
class QSpinBox;
class QSlider;
class QTimer;
class QWidget;

namespace bdfrpc::studio {

class CalibrationWizardDialog;
class SkeletonViewportWidget;

class PerformanceMainWindow final : public QMainWindow {
public:
    PerformanceMainWindow();
    ~PerformanceMainWindow() override;

private:
    struct CameraUi {
        CaptureDeviceDescriptor descriptor;
        std::unique_ptr<OpenCVCameraSource> source;
        QWidget* card{nullptr};
        QLabel* preview{nullptr};
        QLabel* stats{nullptr};
        std::uint64_t preview_sequence{0};
    };

    void build_ui();
    void rescan_devices();
    void rebuild_camera_grid();
    void start_cameras();
    void stop_cameras();
    void start_solver_bridges();
    void stop_solver_bridges();
    void start_unreal_stream();
    void stop_unreal_stream();
    void apply_mode(int index);
    void start_recording();
    void stop_recording();
    void open_take();
    bool load_take_path(const QString& path);
    void browse_sessions();
    void open_calibration_wizard();
    void toggle_playback();
    void stop_playback();
    void seek_playback(int index);
    void update_playback();
    void display_fused_skeleton(const FusedPerformanceFrame& frame);
    void export_current_take_bvh();
    void browse_easymocap_directory();
    bool ensure_easymocap_worker();
    void launch_easymocap_solver();
    void stop_easymocap_solver();
    void shutdown_easymocap_worker();
    void handle_easymocap_worker_output();
    void tick();
    void update_sync_status();
    void update_solver_status();
    void show_frame(CameraUi& camera, const CaptureFrame& frame);

    std::vector<CameraUi> cameras_;
    std::unique_ptr<FrameSynchronizer> synchronizer_;
    FusionRuntime fusion_runtime_{OperatingMode::Hybrid};
    TakeRecorder take_recorder_;
    TakeReader take_reader_;

    std::unique_ptr<BDFRFacialUdpSource> facial_source_;
    std::unique_ptr<EasyMocapTcpSource> easymocap_source_;
    std::unique_ptr<UnrealFacialUdpSender> unreal_sender_;

    QWidget* central_{nullptr};
    QGridLayout* camera_grid_{nullptr};
    QComboBox* mode_combo_{nullptr};
    QPushButton* scan_button_{nullptr};
    QPushButton* start_button_{nullptr};
    QPushButton* stop_button_{nullptr};
    QPushButton* solver_start_button_{nullptr};
    QPushButton* solver_stop_button_{nullptr};
    QPushButton* unreal_start_button_{nullptr};
    QPushButton* unreal_stop_button_{nullptr};
    QPushButton* record_button_{nullptr};
    QPushButton* stop_record_button_{nullptr};
    QPushButton* open_take_button_{nullptr};
    QPushButton* session_browser_button_{nullptr};
    QPushButton* calibration_button_{nullptr};
    QPushButton* play_take_button_{nullptr};
    QPushButton* stop_take_button_{nullptr};
    QPushButton* export_bvh_button_{nullptr};
    QPushButton* easymocap_browse_button_{nullptr};
    QPushButton* easymocap_launch_button_{nullptr};
    QPushButton* easymocap_stop_button_{nullptr};
    QSpinBox* facial_port_{nullptr};
    QSpinBox* easymocap_port_{nullptr};
    QSpinBox* unreal_port_{nullptr};
    QLineEdit* unreal_host_{nullptr};
    QLineEdit* easymocap_cwd_{nullptr};
    QLineEdit* easymocap_command_{nullptr};
    QLabel* capture_status_{nullptr};
    QLabel* sync_status_{nullptr};
    QLabel* solver_status_{nullptr};
    QLabel* unreal_status_{nullptr};
    QLabel* fusion_status_{nullptr};
    QLabel* playback_status_{nullptr};
    QLabel* easymocap_process_status_{nullptr};
    QSlider* playback_slider_{nullptr};
    SkeletonViewportWidget* skeleton_view_{nullptr};
    QTimer* timer_{nullptr};
    QProcess* easymocap_worker_process_{nullptr};

    std::uint64_t synchronized_sets_{0};
    bool playback_mode_{false};
    bool playback_running_{false};
    std::size_t playback_index_{0};
    TimestampNs playback_wall_anchor_ns_{0};
    TimestampNs playback_take_anchor_ns_{0};
};

} // namespace bdfrpc::studio
