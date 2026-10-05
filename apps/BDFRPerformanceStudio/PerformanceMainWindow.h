#pragma once

#include "bdfrpc/BDFRFacialUdpSource.h"
#include "bdfrpc/EasyMocapTcpSource.h"
#include "bdfrpc/FrameSynchronizer.h"
#include "bdfrpc/FusionRuntime.h"
#include "bdfrpc/OpenCVCameraSource.h"
#include "bdfrpc/OpenCVDeviceEnumerator.h"

#include <QMainWindow>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;
class QWidget;

namespace bdfrpc::studio {

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
    void apply_mode(int index);
    void tick();
    void update_sync_status();
    void update_solver_status();
    void show_frame(CameraUi& camera, const CaptureFrame& frame);

    std::vector<CameraUi> cameras_;
    std::unique_ptr<FrameSynchronizer> synchronizer_;
    FusionRuntime fusion_runtime_{OperatingMode::Hybrid};

    std::unique_ptr<BDFRFacialUdpSource> facial_source_;
    std::unique_ptr<EasyMocapTcpSource> easymocap_source_;

    QWidget* central_{nullptr};
    QGridLayout* camera_grid_{nullptr};
    QComboBox* mode_combo_{nullptr};
    QPushButton* scan_button_{nullptr};
    QPushButton* start_button_{nullptr};
    QPushButton* stop_button_{nullptr};
    QPushButton* solver_start_button_{nullptr};
    QPushButton* solver_stop_button_{nullptr};
    QSpinBox* facial_port_{nullptr};
    QSpinBox* easymocap_port_{nullptr};
    QLabel* capture_status_{nullptr};
    QLabel* sync_status_{nullptr};
    QLabel* solver_status_{nullptr};
    QLabel* fusion_status_{nullptr};
    QTimer* timer_{nullptr};

    std::uint64_t synchronized_sets_{0};
};

} // namespace bdfrpc::studio
