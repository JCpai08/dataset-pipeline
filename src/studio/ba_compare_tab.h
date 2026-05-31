#pragma once

#include <memory>
#include <vector>

#include <QMap>
#include <QWidget>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

class QComboBox;
class QFormLayout;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QSlider;
class QTextEdit;

namespace opt {
struct Image;
struct Intrinsics;
class Problem;
}  // namespace opt

namespace studio {

struct BAImageViewState {
  double scale = 1.0;
  double offset_x = 0.0;
  double offset_y = 0.0;
};

class BADepthOverlayWidget : public QWidget {
  Q_OBJECT

 public:
  explicit BADepthOverlayWidget(QWidget* parent = nullptr);

  void SetImage(opt::Image* image,
                opt::Intrinsics* intrinsics,
                const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& scan_points);
  void SetImageScale(int image_scale);
  void SetOverlayOpacity(double opacity);
  BAImageViewState ViewState() const;
  void SetViewState(const BAImageViewState& state);

 signals:
  void ViewStateChanged(const studio::BAImageViewState& state);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  void RebuildImage();
  void RebuildDepthOverlay();
  void UpdateViewTransforms();
  QPointF ViewportToImage(const QPointF& pos) const;

  opt::Image* image_ = nullptr;
  opt::Intrinsics* intrinsics_ = nullptr;
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr scan_points_;
  int image_scale_ = -1;
  double overlay_opacity_ = 0.35;
  QImage image_qimage_;
  QImage depth_qimage_;
  int cached_image_id_ = -1;
  int cached_image_scale_ = -100;
  int cached_depth_image_id_ = -1;
  int cached_depth_image_scale_ = -100;
  BAImageViewState view_state_;
  QTransform image_to_viewport_;
  QTransform viewport_to_image_;
  bool dragging_ = false;
  QPoint drag_start_pos_;
};

class BACompareTab : public QWidget {
  Q_OBJECT

 public:
  explicit BACompareTab(QWidget* parent = nullptr);

  void SetDefaultPaths(const QString& dataset_dir,
                       const QString& multi_res_subdir,
                       int cube_map_camera_id);

 private slots:
  void LoadDatasets();
  void CurrentImageChanged(QListWidgetItem* current, QListWidgetItem* previous);
  void ImageScaleChanged(int index);
  void OpacityChanged(int value);
  void SyncViewFromBefore(const studio::BAImageViewState& state);
  void SyncViewFromAfter(const studio::BAImageViewState& state);

 private:
  struct LoadedDataset {
    std::shared_ptr<opt::Problem> problem;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr scan_points;
    QMap<QString, int> image_ids_by_name;
    QString error;
  };

  QLineEdit* NewPathEdit(const QString& text = QString());
  void AddPathRow(QFormLayout* form, const QString& label, QLineEdit* edit);
  LoadedDataset LoadOneDataset(const QString& state_path,
                               const QString& side_name,
                               const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& scan_points) const;
  bool ValidateColmapTextModel(const QString& state_path,
                               const QString& side_name,
                               QString* error) const;
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr LoadAlignedScans(QString* error) const;
  void PopulateImageList();
  void SelectImage(const QString& image_name);
  void SetWidgetImage(BADepthOverlayWidget* widget,
                      LoadedDataset* dataset,
                      const QString& image_name,
                      QLabel* title_label);
  void PopulateImageScales();
  QString NormalizeImageName(const std::string& image_name) const;

  QLineEdit* scan_alignment_edit_ = nullptr;
  QLineEdit* image_base_edit_ = nullptr;
  QLineEdit* before_state_edit_ = nullptr;
  QLineEdit* after_state_edit_ = nullptr;
  QComboBox* image_scale_combo_ = nullptr;
  QSlider* opacity_slider_ = nullptr;
  QListWidget* image_list_ = nullptr;
  QLabel* before_title_ = nullptr;
  QLabel* after_title_ = nullptr;
  QTextEdit* status_text_ = nullptr;
  BADepthOverlayWidget* before_widget_ = nullptr;
  BADepthOverlayWidget* after_widget_ = nullptr;

  LoadedDataset before_;
  LoadedDataset after_;
  bool syncing_view_ = false;
};

}  // namespace studio
