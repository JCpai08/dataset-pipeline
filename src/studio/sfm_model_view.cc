#include "studio/sfm_model_view.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QTextStream>
#include <QVBoxLayout>
#include <QVector4D>
#include <QWheelEvent>

namespace studio {
namespace {

QStringList Tokens(const QString& line) {
  return line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
}

bool IsDataLine(const QString& line) {
  const QString trimmed = line.trimmed();
  return !trimmed.isEmpty() && !trimmed.startsWith("#");
}

SfmModelView::CameraPose CameraPoseFromColmap(double qw,
                                              double qx,
                                              double qy,
                                              double qz,
                                              double tx,
                                              double ty,
                                              double tz) {
  const double r00 = 1.0 - 2.0 * qy * qy - 2.0 * qz * qz;
  const double r01 = 2.0 * qx * qy - 2.0 * qz * qw;
  const double r02 = 2.0 * qx * qz + 2.0 * qy * qw;
  const double r10 = 2.0 * qx * qy + 2.0 * qz * qw;
  const double r11 = 1.0 - 2.0 * qx * qx - 2.0 * qz * qz;
  const double r12 = 2.0 * qy * qz - 2.0 * qx * qw;
  const double r20 = 2.0 * qx * qz - 2.0 * qy * qw;
  const double r21 = 2.0 * qy * qz + 2.0 * qx * qw;
  const double r22 = 1.0 - 2.0 * qx * qx - 2.0 * qy * qy;

  SfmModelView::CameraPose pose;
  // COLMAP stores world-to-camera as x_cam = R * x_world + t.
  // Camera center is -R^T * t.
  pose.center = QVector3D(
      static_cast<float>(-(r00 * tx + r10 * ty + r20 * tz)),
      static_cast<float>(-(r01 * tx + r11 * ty + r21 * tz)),
      static_cast<float>(-(r02 * tx + r12 * ty + r22 * tz)));
  pose.right = QVector3D(static_cast<float>(r00),
                         static_cast<float>(r01),
                         static_cast<float>(r02)).normalized();
  pose.up = QVector3D(static_cast<float>(r10),
                      static_cast<float>(r11),
                      static_cast<float>(r12)).normalized();
  pose.forward = QVector3D(static_cast<float>(r20),
                           static_cast<float>(r21),
                           static_cast<float>(r22)).normalized();
  return pose;
}

}  // namespace

SfmModelView::SfmModelView(QWidget* parent) : QOpenGLWidget(parent) {
  setMinimumHeight(360);
  setMouseTracking(true);

  QVBoxLayout* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  QHBoxLayout* toolbar = new QHBoxLayout();
  toolbar->setContentsMargins(0, 0, 0, 0);
  toolbar->addStretch();
  projection_combo_ = new QComboBox();
  projection_combo_->addItems({"3D", "XY", "XZ", "YZ"});
  projection_combo_->setMaximumWidth(100);
  toolbar->addWidget(projection_combo_);
  root->addLayout(toolbar);
  root->addStretch();

  connect(projection_combo_, SIGNAL(currentIndexChanged(int)),
          this, SLOT(ProjectionChanged(int)));
}

void SfmModelView::initializeGL() {
}

void SfmModelView::resizeGL(int /*width*/, int /*height*/) {}

bool SfmModelView::LoadModel(const QString& model_path, QString* error) {
  Clear();

  const QDir model_dir(model_path);
  QFile points_file(model_dir.filePath("points3D.txt"));
  if (!points_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    *error = QString("Cannot read %1").arg(QDir::toNativeSeparators(points_file.fileName()));
    return false;
  }

  QTextStream points_stream(&points_file);
  while (!points_stream.atEnd()) {
    const QString line = points_stream.readLine();
    if (!IsDataLine(line)) {
      continue;
    }
    const QStringList parts = Tokens(line);
    if (parts.size() < 4) {
      continue;
    }
    bool ok_x = false;
    bool ok_y = false;
    bool ok_z = false;
    const float x = parts[1].toFloat(&ok_x);
    const float y = parts[2].toFloat(&ok_y);
    const float z = parts[3].toFloat(&ok_z);
    if (ok_x && ok_y && ok_z) {
      points_.push_back(QVector3D(x, y, z));
      bool ok_r = false;
      bool ok_g = false;
      bool ok_b = false;
      const int r = parts.value(4).toInt(&ok_r);
      const int g = parts.value(5).toInt(&ok_g);
      const int b = parts.value(6).toInt(&ok_b);
      point_colors_.push_back(ok_r && ok_g && ok_b ? QColor(r, g, b)
                                                   : QColor(75, 90, 110));
    }
  }

  QFile images_file(model_dir.filePath("images.txt"));
  if (!images_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    *error = QString("Cannot read %1").arg(QDir::toNativeSeparators(images_file.fileName()));
    return false;
  }

  QTextStream images_stream(&images_file);
  while (!images_stream.atEnd()) {
    const QString image_line = images_stream.readLine();
    if (!IsDataLine(image_line)) {
      continue;
    }
    const QStringList parts = Tokens(image_line);
    if (parts.size() >= 9) {
      bool ok_qw = false;
      bool ok_qx = false;
      bool ok_qy = false;
      bool ok_qz = false;
      bool ok_tx = false;
      bool ok_ty = false;
      bool ok_tz = false;
      const double qw = parts[1].toDouble(&ok_qw);
      const double qx = parts[2].toDouble(&ok_qx);
      const double qy = parts[3].toDouble(&ok_qy);
      const double qz = parts[4].toDouble(&ok_qz);
      const double tx = parts[5].toDouble(&ok_tx);
      const double ty = parts[6].toDouble(&ok_ty);
      const double tz = parts[7].toDouble(&ok_tz);
      if (ok_qw && ok_qx && ok_qy && ok_qz && ok_tx && ok_ty && ok_tz) {
        cameras_.push_back(CameraPoseFromColmap(qw, qx, qy, qz, tx, ty, tz));
      }
    }
    if (!images_stream.atEnd()) {
      images_stream.readLine();
    }
  }

  RecomputeBounds();
  ResetView();
  update();
  return true;
}

void SfmModelView::Clear() {
  points_.clear();
  point_colors_.clear();
  cameras_.clear();
  RecomputeBounds();
  ResetView();
  update();
}

void SfmModelView::paintGL() {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(248, 249, 250));
  painter.setRenderHint(QPainter::Antialiasing, true);

  const QRectF view_rect = rect().adjusted(12, 42, -12, -12);
  painter.setPen(QPen(QColor(210, 215, 220)));
  painter.drawRect(view_rect);

  if (points_.empty() && cameras_.empty()) {
    painter.setPen(QColor(90, 98, 106));
    painter.drawText(view_rect, Qt::AlignCenter,
                     "Load a COLMAP TXT model to view sparse points and cameras.");
    return;
  }

  painter.setClipRect(view_rect.adjusted(1, 1, -1, -1));
  if (projection_combo_->currentIndex() == 0) {
    struct ProjectedPoint {
      QPointF screen;
      QColor color;
      double depth;
    };
    QVector<ProjectedPoint> projected_points;
    const int stride = std::max(1, points_.size() / 140000);
    for (int i = 0; i < points_.size(); i += stride) {
      QPointF screen;
      double depth = 0.0;
      if (Project3D(points_[i], &screen, &depth) && view_rect.contains(screen)) {
        projected_points.push_back(
            {screen, point_colors_.value(i, QColor(75, 90, 110)), depth});
      }
    }
    std::sort(projected_points.begin(), projected_points.end(),
              [](const ProjectedPoint& a, const ProjectedPoint& b) {
                return a.depth > b.depth;
              });
    for (const ProjectedPoint& point : projected_points) {
      QColor color = point.color;
      color.setAlpha(185);
      painter.setPen(QPen(color, 1));
      painter.drawPoint(point.screen);
    }

    const double frustum_size = SceneRadius() * 0.018;
    painter.setPen(QPen(QColor(205, 55, 55), 1.0));
    painter.setBrush(QColor(220, 70, 70));
    for (const CameraPose& camera : cameras_) {
      QPointF c;
      double depth = 0.0;
      if (!Project3D(camera.center, &c, &depth)) {
        continue;
      }
      painter.drawEllipse(c, 2.2, 2.2);

      const QVector3D fc =
          camera.center + camera.forward * static_cast<float>(frustum_size * 1.8);
      const QVector3D ru = camera.right * static_cast<float>(frustum_size);
      const QVector3D uu = camera.up * static_cast<float>(frustum_size * 0.7);
      const QVector<QVector3D> corners = {
          fc - ru - uu, fc + ru - uu, fc + ru + uu, fc - ru + uu};
      QVector<QPointF> projected_corners;
      bool visible = true;
      for (const QVector3D& corner : corners) {
        QPointF p;
        double d = 0.0;
        if (!Project3D(corner, &p, &d)) {
          visible = false;
          break;
        }
        projected_corners.push_back(p);
      }
      if (!visible) {
        continue;
      }
      for (const QPointF& p : projected_corners) {
        painter.drawLine(c, p);
      }
      painter.drawPolygon(QPolygonF(projected_corners));
    }
  } else {
    const double data_w = std::max(1e-9, max_u_ - min_u_);
    const double data_h = std::max(1e-9, max_v_ - min_v_);
    const double base_scale =
        0.92 * std::min(view_rect.width() / data_w, view_rect.height() / data_h);
    const QPointF center = view_rect.center() + pan_;
    auto to_screen = [&](const QVector3D& p) {
      const QPointF uv = Project(p);
      const double x =
          center.x() + ((uv.x() - (min_u_ + max_u_) * 0.5) * base_scale * zoom_);
      const double y =
          center.y() - ((uv.y() - (min_v_ + max_v_) * 0.5) * base_scale * zoom_);
      return QPointF(x, y);
    };

    painter.setPen(QPen(QColor(70, 85, 105, 80), 1));
    const int stride = std::max(1, points_.size() / 100000);
    for (int i = 0; i < points_.size(); i += stride) {
      painter.drawPoint(to_screen(points_[i]));
    }

    painter.setPen(QPen(QColor(190, 55, 55), 1.5));
    painter.setBrush(QColor(220, 70, 70));
    for (const CameraPose& camera : cameras_) {
      const QPointF c = to_screen(camera.center);
      static constexpr double kSize = 5.0;
      QPolygonF triangle;
      triangle << QPointF(c.x(), c.y() - kSize)
               << QPointF(c.x() - kSize, c.y() + kSize)
               << QPointF(c.x() + kSize, c.y() + kSize);
      painter.drawPolygon(triangle);
    }
  }
  painter.setClipping(false);

  painter.setPen(QColor(45, 52, 60));
  painter.drawText(16, 24,
                   QString("Points: %1  Cameras: %2  View: %3")
                       .arg(points_.size())
                       .arg(cameras_.size())
                       .arg(projection_combo_->currentText()));
}

void SfmModelView::wheelEvent(QWheelEvent* event) {
  const float delta = static_cast<float>(event->angleDelta().x() + event->angleDelta().y());
  if (projection_combo_->currentIndex() == 0) {
    ChangeFocusDistance(delta);
  } else {
    const double factor = delta > 0 ? 1.15 : 1.0 / 1.15;
    zoom_ = std::max(0.05, std::min(200.0, zoom_ * factor));
  }
  update();
}

void SfmModelView::mousePressEvent(QMouseEvent* event) {
  last_mouse_pos_ = event->pos();
}

void SfmModelView::mouseMoveEvent(QMouseEvent* event) {
  if (projection_combo_->currentIndex() == 0) {
    if ((event->buttons() & Qt::RightButton) ||
        ((event->buttons() & Qt::LeftButton) &&
         (event->modifiers() & Qt::ControlModifier))) {
      TranslateView(event->pos(), last_mouse_pos_);
    } else if (event->buttons() & Qt::LeftButton) {
      RotateView(event->pos(), last_mouse_pos_);
    } else if (event->buttons() & Qt::MiddleButton) {
      TranslateView(event->pos(), last_mouse_pos_);
    }
  } else {
    const QPoint delta = event->pos() - last_mouse_pos_;
    if (event->buttons() & (Qt::MiddleButton | Qt::RightButton | Qt::LeftButton)) {
      pan_ += delta;
      update();
    }
  }
  last_mouse_pos_ = event->pos();
}

void SfmModelView::mouseDoubleClickEvent(QMouseEvent* /*event*/) {
  ResetView();
  update();
}

void SfmModelView::ProjectionChanged(int /*index*/) {
  RecomputeBounds();
  ResetView();
  update();
}

QPointF SfmModelView::Project(const QVector3D& point) const {
  switch (projection_combo_->currentIndex()) {
    case 2:
      return QPointF(point.x(), point.z());
    case 3:
      return QPointF(point.y(), point.z());
    default:
      return QPointF(point.x(), point.y());
  }
}

bool SfmModelView::Project3D(const QVector3D& point,
                             QPointF* screen,
                             double* depth) const {
  const QRectF view_rect = rect().adjusted(12, 42, -12, -12);
  const double radius = SceneRadius();
  const QVector3D normalized =
      (point - scene_center_) / static_cast<float>(radius);
  const QVector4D eye_point =
      model_view_matrix_ * QVector4D(normalized, 1.0f);
  const double z = -eye_point.z();
  if (z <= 0.03) {
    return false;
  }
  const double focal = std::min(view_rect.width(), view_rect.height()) * 0.7;
  *screen = view_rect.center() + pan_ +
            QPointF(focal * eye_point.x() / z, -focal * eye_point.y() / z);
  *depth = z;
  return true;
}

QVector3D SfmModelView::PositionToArcballVector(float x, float y) const {
  QVector3D vec(2.0f * x / std::max(1, width()) - 1.0f,
                1.0f - 2.0f * y / std::max(1, height()),
                0.0f);
  const float norm2 = vec.lengthSquared();
  if (norm2 <= 1.0f) {
    vec.setZ(std::sqrt(1.0f - norm2));
  } else {
    vec.normalize();
  }
  return vec;
}

void SfmModelView::RotateView(const QPoint& current, const QPoint& previous) {
  if (current == previous) {
    return;
  }

  const QVector3D u = PositionToArcballVector(current.x(), current.y());
  const QVector3D v = PositionToArcballVector(previous.x(), previous.y());
  const float dot = std::max(-1.0f, std::min(1.0f, QVector3D::dotProduct(u, v)));
  const float angle = 2.0f * std::acos(dot);
  if (angle <= 1e-3f) {
    return;
  }

  QVector3D axis = QVector3D::crossProduct(v, u);
  if (axis.lengthSquared() < 1e-8f) {
    return;
  }
  axis.normalize();

  QMatrix4x4 rotation;
  rotation.rotate(angle * 180.0f / static_cast<float>(M_PI),
                  axis.x(), axis.y(), axis.z());
  model_view_matrix_ = rotation * model_view_matrix_;
  update();
}

void SfmModelView::TranslateView(const QPoint& current, const QPoint& previous) {
  if (current == previous) {
    return;
  }

  const QPoint delta = current - previous;
  const float scale = ZoomScale();
  QMatrix4x4 translation;
  translation.translate(delta.x() * scale, -delta.y() * scale, 0.0f);
  model_view_matrix_ = translation * model_view_matrix_;
  update();
}

void SfmModelView::ChangeFocusDistance(float delta) {
  if (delta == 0.0f) {
    return;
  }
  float diff = delta * ZoomScale() * 0.75f;
  const float previous_focus_distance = focus_distance_;
  focus_distance_ = std::max(0.18f, std::min(80.0f, focus_distance_ - diff));
  diff = previous_focus_distance - focus_distance_;

  QMatrix4x4 translation;
  translation.translate(0.0f, 0.0f, diff);
  model_view_matrix_ = translation * model_view_matrix_;
  update();
}

float SfmModelView::ZoomScale() const {
  static constexpr float kFieldOfView = 25.0f;
  return 2.0f * std::tan(kFieldOfView * static_cast<float>(M_PI) / 360.0f) *
         std::abs(focus_distance_) / std::max(1, height());
}

void SfmModelView::ResetView() {
  pan_ = QPointF(0.0, 0.0);
  zoom_ = 1.0;
  focus_distance_ = 2.1f;
  model_view_matrix_.setToIdentity();
  model_view_matrix_.translate(0.0f, 0.0f, -focus_distance_);
  model_view_matrix_.rotate(225.0f, 1.0f, 0.0f, 0.0f);
  model_view_matrix_.rotate(-45.0f, 0.0f, 1.0f, 0.0f);
}

void SfmModelView::RecomputeBounds() {
  bool have_value = false;
  min_u_ = min_v_ = std::numeric_limits<double>::max();
  max_u_ = max_v_ = std::numeric_limits<double>::lowest();
  auto include = [&](const QVector3D& point) {
    const QPointF uv = Project(point);
    min_u_ = std::min(min_u_, uv.x());
    max_u_ = std::max(max_u_, uv.x());
    min_v_ = std::min(min_v_, uv.y());
    max_v_ = std::max(max_v_, uv.y());
    have_value = true;
  };

  bool have_3d_value = false;
  min_bound_ = QVector3D(std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max());
  max_bound_ = QVector3D(std::numeric_limits<float>::lowest(),
                         std::numeric_limits<float>::lowest(),
                         std::numeric_limits<float>::lowest());
  auto include_3d = [&](const QVector3D& point) {
    min_bound_.setX(std::min(min_bound_.x(), point.x()));
    min_bound_.setY(std::min(min_bound_.y(), point.y()));
    min_bound_.setZ(std::min(min_bound_.z(), point.z()));
    max_bound_.setX(std::max(max_bound_.x(), point.x()));
    max_bound_.setY(std::max(max_bound_.y(), point.y()));
    max_bound_.setZ(std::max(max_bound_.z(), point.z()));
    have_3d_value = true;
  };

  for (const QVector3D& point : points_) {
    include(point);
    include_3d(point);
  }
  for (const CameraPose& camera : cameras_) {
    include(camera.center);
    include_3d(camera.center);
  }
  if (!have_value) {
    min_u_ = min_v_ = 0.0;
    max_u_ = max_v_ = 1.0;
  }
  if (!have_3d_value) {
    min_bound_ = QVector3D(0, 0, 0);
    max_bound_ = QVector3D(1, 1, 1);
  }

  if (!cameras_.empty()) {
    QVector3D camera_sum(0, 0, 0);
    for (const CameraPose& camera : cameras_) {
      camera_sum += camera.center;
    }
    scene_center_ = camera_sum / static_cast<float>(cameras_.size());

    double camera_radius = 0.0;
    for (const CameraPose& camera : cameras_) {
      camera_radius =
          std::max(camera_radius, static_cast<double>((camera.center - scene_center_).length()));
    }
    scene_radius_ = std::max(1e-6, camera_radius);
  } else {
    scene_center_ = (min_bound_ + max_bound_) * 0.5f;
    scene_radius_ = std::max(1e-6, static_cast<double>((max_bound_ - min_bound_).length()) * 0.5);
  }

  if (scene_radius_ <= 1e-5) {
    scene_radius_ =
        std::max(1e-6, static_cast<double>((max_bound_ - min_bound_).length()) * 0.5);
  }
}

double SfmModelView::SceneRadius() const {
  return std::max(1e-6, scene_radius_);
}

}  // namespace studio
