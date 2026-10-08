#include "SkeletonViewportWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>

namespace bdfrpc::studio {
namespace {

std::array<float,3> rotate_view(
    const std::array<float,3>& p,
    float yaw,
    float pitch) {

    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);

    const float x1 = cy * p[0] + sy * p[2];
    const float z1 = -sy * p[0] + cy * p[2];

    return {
        x1,
        cp * p[1] - sp * z1,
        sp * p[1] + cp * z1
    };
}

} // namespace

SkeletonViewportWidget::SkeletonViewportWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(340, 360);
    setMouseTracking(true);
    setAutoFillBackground(false);
}

void SkeletonViewportWidget::set_skeleton(const SkeletonPose& pose) {
    pose_ = SkeletonKinematics::solve(pose);
    model_ = pose.model;
    has_pose_ = !pose_.joints.empty();
    update();
}

void SkeletonViewportWidget::clear_skeleton() {
    pose_ = {};
    model_ = SkeletonModel::Unknown;
    has_pose_ = false;
    update();
}

void SkeletonViewportWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(18, 20, 24));

    painter.setPen(QColor(210, 214, 220));
    painter.drawText(
        QRect(14, 12, width() - 28, 30),
        Qt::AlignLeft | Qt::AlignVCenter,
        has_pose_
            ? QString("3D Skeleton · %1")
                  .arg(EasyMocapSkeletonMapper::model_name(model_))
            : QString("3D Skeleton · waiting for body data"));

    if (!has_pose_) {
        painter.setPen(QColor(115, 122, 132));
        painter.drawText(
            rect().adjusted(20, 50, -20, -20),
            Qt::AlignCenter,
            "Start EasyMocap or open a recorded take.");
        return;
    }

    std::array<float,3> origin{0.0F,0.0F,0.0F};
    if (const auto* pelvis = pose_.find_joint("pelvis")) {
        origin = pelvis->position;
    }

    const float base_scale =
        std::min(width(), height()) * 0.70F * zoom_;

    std::vector<QPointF> projected;
    projected.reserve(pose_.joints.size());

    for (const auto& joint : pose_.joints) {
        std::array<float,3> relative{
            joint.position[0] - origin[0],
            joint.position[1] - origin[1],
            joint.position[2] - origin[2]
        };
        const auto p = rotate_view(relative, yaw_, pitch_);

        const float perspective =
            1.0F / std::max(1.5F, 3.0F - p[2]);
        projected.emplace_back(
            width() * 0.5F + p[0] * base_scale * perspective,
            height() * 0.58F - p[1] * base_scale * perspective);
    }

    QPen bone_pen(QColor(112, 198, 255));
    bone_pen.setWidthF(2.4);
    bone_pen.setCapStyle(Qt::RoundCap);
    painter.setPen(bone_pen);

    for (std::size_t i = 0; i < pose_.joints.size(); ++i) {
        const int parent = pose_.joints[i].parent;
        if (parent < 0 ||
            static_cast<std::size_t>(parent) >= projected.size()) {
            continue;
        }
        painter.drawLine(
            projected[static_cast<std::size_t>(parent)],
            projected[i]);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(238, 241, 245));
    for (const auto& point : projected) {
        painter.drawEllipse(point, 3.2, 3.2);
    }

    painter.setPen(QColor(110, 116, 126));
    painter.drawText(
        QRect(14, height() - 30, width() - 28, 20),
        Qt::AlignLeft | Qt::AlignVCenter,
        "Drag: orbit   ·   Wheel: zoom");
}

void SkeletonViewportWidget::mousePressEvent(QMouseEvent* event) {
    last_mouse_ = event->position().toPoint();
    QWidget::mousePressEvent(event);
}

void SkeletonViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        const QPoint current = event->position().toPoint();
        const QPoint delta = current - last_mouse_;
        last_mouse_ = current;

        yaw_ += static_cast<float>(delta.x()) * 0.008F;
        pitch_ += static_cast<float>(delta.y()) * 0.008F;
        pitch_ = std::clamp(pitch_, -1.35F, 1.35F);
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void SkeletonViewportWidget::wheelEvent(QWheelEvent* event) {
    const float steps =
        static_cast<float>(event->angleDelta().y()) / 120.0F;
    zoom_ *= std::pow(1.10F, steps);
    zoom_ = std::clamp(zoom_, 0.45F, 2.5F);
    update();
    event->accept();
}

} // namespace bdfrpc::studio
