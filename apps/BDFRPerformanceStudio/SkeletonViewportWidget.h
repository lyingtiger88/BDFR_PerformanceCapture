#pragma once

#include "bdfrpc/Kinematics.h"
#include "bdfrpc/Skeleton.h"

#include <QPoint>
#include <QWidget>

namespace bdfrpc::studio {

class SkeletonViewportWidget final : public QWidget {
public:
    explicit SkeletonViewportWidget(QWidget* parent = nullptr);

    void set_skeleton(const SkeletonPose& pose);
    void clear_skeleton();
    bool has_skeleton() const noexcept { return has_pose_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    KinematicPose pose_;
    SkeletonModel model_{SkeletonModel::Unknown};
    bool has_pose_{false};
    float yaw_{-0.35F};
    float pitch_{-0.10F};
    float zoom_{1.0F};
    QPoint last_mouse_;
};

} // namespace bdfrpc::studio
