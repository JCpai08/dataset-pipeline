#pragma once

#include <QColor>
#include <QMatrix4x4>
#include <QOpenGLWidget>
#include <QVector>
#include <QVector3D>

class QComboBox;

namespace studio {

class SfmModelView : public QOpenGLWidget {
  Q_OBJECT

 public:
  struct CameraPose {
    QVector3D center;
    QVector3D right;
    QVector3D up;
    QVector3D forward;
  };

  explicit SfmModelView(QWidget* parent = nullptr);

  bool LoadModel(const QString& model_path, QString* error);
  void Clear();

 protected:
  void initializeGL() override;
  void resizeGL(int width, int height) override;
  void paintGL() override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

 private slots:
  void ProjectionChanged(int index);

 private:
  QPointF Project(const QVector3D& point) const;
  bool Project3D(const QVector3D& point, QPointF* screen, double* depth) const;
  QVector3D PositionToArcballVector(float x, float y) const;
  void RotateView(const QPoint& current, const QPoint& previous);
  void TranslateView(const QPoint& current, const QPoint& previous);
  void ChangeFocusDistance(float delta);
  float ZoomScale() const;
  void ResetView();
  void RecomputeBounds();
  double SceneRadius() const;

  QVector<QVector3D> points_;
  QVector<QColor> point_colors_;
  QVector<CameraPose> cameras_;
  QComboBox* projection_combo_ = nullptr;
  QPoint last_mouse_pos_;
  QPointF pan_;
  double zoom_ = 1.0;
  float focus_distance_ = 3.2f;
  QMatrix4x4 model_view_matrix_;
  double min_u_ = 0.0;
  double min_v_ = 0.0;
  double max_u_ = 1.0;
  double max_v_ = 1.0;
  QVector3D min_bound_;
  QVector3D max_bound_;
  QVector3D scene_center_;
  double scene_radius_ = 1.0;
};

}  // namespace studio
