#include "studio/ba_compare_tab.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <igl/jet.h>
#include <pcl/console/print.h>

#include "camera/camera_base.h"
#include "io/colmap_model.h"
#include "opt/image.h"
#include "opt/intrinsics.h"
#include "opt/occlusion_geometry.h"
#include "opt/problem.h"
#include "opt/util.h"

namespace studio {
namespace {

QString Native(const QString& path) {
  return QDir::toNativeSeparators(path);
}

template <class Camera>
void RasterizeDepthOverlay(const Camera& camera,
                           const opt::Image& image,
                           const pcl::PointCloud<pcl::PointXYZRGB>& scan_points,
                           QImage* depth_image) {
  const int width = camera.width();
  const int height = camera.height();
  std::vector<float> z_buffer(width * height,
                              std::numeric_limits<float>::infinity());
  float min_depth = std::numeric_limits<float>::infinity();
  float max_depth = -std::numeric_limits<float>::infinity();

  const Eigen::Matrix3f image_R_global = image.image_T_global.so3().matrix();
  const Eigen::Vector3f image_T_global = image.image_T_global.translation();
  for (const pcl::PointXYZRGB& point : scan_points.points) {
    const Eigen::Vector3f image_point =
        image_R_global * point.getVector3fMap() + image_T_global;
    if (image_point.z() <= 0) {
      continue;
    }
    const Eigen::Vector2f pxy = camera.NormalizedToImage(
        Eigen::Vector2f(image_point.x() / image_point.z(),
                        image_point.y() / image_point.z()));
    const int ix = static_cast<int>(pxy.x() + 0.5f);
    const int iy = static_cast<int>(pxy.y() + 0.5f);
    if (ix < 0 || iy < 0 || ix >= width || iy >= height) {
      continue;
    }
    float& z = z_buffer[iy * width + ix];
    if (image_point.z() < z) {
      z = image_point.z();
      min_depth = std::min(min_depth, z);
      max_depth = std::max(max_depth, z);
    }
  }

  *depth_image = QImage(width, height, QImage::Format_ARGB32);
  depth_image->fill(qRgba(0, 0, 0, 0));
  const float depth_range = max_depth - min_depth;
  if (!std::isfinite(depth_range) || depth_range <= 0) {
    return;
  }
  for (int y = 0; y < height; ++y) {
    QRgb* out = reinterpret_cast<QRgb*>(depth_image->scanLine(y));
    for (int x = 0; x < width; ++x) {
      const float depth = z_buffer[y * width + x];
      if (!std::isfinite(depth)) {
        out[x] = qRgba(0, 0, 0, 0);
        continue;
      }
      const float factor = 1.0f - (depth - min_depth) / depth_range;
      float r = 0.0f;
      float g = 0.0f;
      float b = 0.0f;
      igl::jet(factor, r, g, b);
      out[x] = qRgba(static_cast<int>(255.99f * r),
                     static_cast<int>(255.99f * g),
                     static_cast<int>(255.99f * b),
                     255);
    }
  }
}

}  // namespace

BADepthOverlayWidget::BADepthOverlayWidget(QWidget* parent) : QWidget(parent) {
  setMinimumSize(320, 240);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void BADepthOverlayWidget::SetImage(
    opt::Image* image,
    opt::Intrinsics* intrinsics,
    const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& scan_points) {
  image_ = image;
  intrinsics_ = intrinsics;
  scan_points_ = scan_points;
  cached_image_id_ = -1;
  cached_depth_image_id_ = -1;
  RebuildImage();
  RebuildDepthOverlay();
  UpdateViewTransforms();
  update();
}

void BADepthOverlayWidget::SetImageScale(int image_scale) {
  image_scale_ = image_scale;
  cached_image_scale_ = -100;
  cached_depth_image_scale_ = -100;
  RebuildImage();
  RebuildDepthOverlay();
  UpdateViewTransforms();
  update();
}

void BADepthOverlayWidget::SetOverlayOpacity(double opacity) {
  overlay_opacity_ = std::clamp(opacity, 0.0, 1.0);
  update();
}

BAImageViewState BADepthOverlayWidget::ViewState() const {
  return view_state_;
}

void BADepthOverlayWidget::SetViewState(const BAImageViewState& state) {
  view_state_ = state;
  UpdateViewTransforms();
  update();
}

void BADepthOverlayWidget::paintEvent(QPaintEvent* event) {
  QPainter painter(this);
  painter.fillRect(event->rect(), QColor(22, 24, 28));
  if (image_qimage_.isNull()) {
    painter.setPen(QColor(150, 156, 166));
    painter.drawText(rect(), Qt::AlignCenter, "No image");
    return;
  }

  painter.setClipRect(event->rect());
  painter.setTransform(image_to_viewport_);
  painter.drawImage(QPointF(0, 0), image_qimage_);
  if (!depth_qimage_.isNull()) {
    painter.setOpacity(overlay_opacity_);
    painter.drawImage(QPointF(0, 0), depth_qimage_);
    painter.setOpacity(1.0);
  }
}

void BADepthOverlayWidget::resizeEvent(QResizeEvent* event) {
  UpdateViewTransforms();
  QWidget::resizeEvent(event);
}

void BADepthOverlayWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton) {
    dragging_ = true;
    drag_start_pos_ = event->pos();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void BADepthOverlayWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!dragging_) {
    QWidget::mouseMoveEvent(event);
    return;
  }
  const QPoint delta = event->pos() - drag_start_pos_;
  view_state_.offset_x += delta.x();
  view_state_.offset_y += delta.y();
  drag_start_pos_ = event->pos();
  UpdateViewTransforms();
  update();
  emit ViewStateChanged(ViewState());
}

void BADepthOverlayWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (dragging_) {
    dragging_ = false;
    unsetCursor();
    event->accept();
    emit ViewStateChanged(ViewState());
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void BADepthOverlayWidget::wheelEvent(QWheelEvent* event) {
  if (image_qimage_.isNull()) {
    return;
  }
  const double steps = event->angleDelta().y() / 120.0;
  const double scale_factor = std::pow(std::sqrt(2.0), steps);
  const QPointF center_on_image = ViewportToImage(event->position());
  const double new_scale = std::clamp(view_state_.scale * scale_factor, 0.02, 200.0);
  view_state_.offset_x =
      event->position().x() -
      (0.5 * width() - 0.5 * image_qimage_.width() * new_scale) -
      new_scale * center_on_image.x();
  view_state_.offset_y =
      event->position().y() -
      (0.5 * height() - 0.5 * image_qimage_.height() * new_scale) -
      new_scale * center_on_image.y();
  view_state_.scale = new_scale;
  UpdateViewTransforms();
  update();
  emit ViewStateChanged(ViewState());
}

void BADepthOverlayWidget::RebuildImage() {
  if (!image_ || !intrinsics_) {
    image_qimage_ = QImage();
    return;
  }
  const int display_scale =
      image_scale_ == -1 ? intrinsics_->min_image_scale : image_scale_;
  if (cached_image_id_ == image_->image_id &&
      cached_image_scale_ == display_scale) {
    return;
  }
  cached_image_id_ = image_->image_id;
  cached_image_scale_ = display_scale;
  const cv::Mat_<uint8_t>& gray = image_->image(display_scale, *intrinsics_);
  QImage qimage(gray.data, gray.cols, gray.rows, gray.cols, QImage::Format_Indexed8);
  qimage.setColorCount(256);
  for (int i = 0; i < 256; ++i) {
    qimage.setColor(i, qRgb(i, i, i));
  }
  image_qimage_ = qimage.copy();
}

void BADepthOverlayWidget::RebuildDepthOverlay() {
  if (!image_ || !intrinsics_ || !scan_points_) {
    depth_qimage_ = QImage();
    return;
  }
  const int display_scale =
      image_scale_ == -1 ? intrinsics_->min_image_scale : image_scale_;
  if (cached_depth_image_id_ == image_->image_id &&
      cached_depth_image_scale_ == display_scale) {
    return;
  }
  cached_depth_image_id_ = image_->image_id;
  cached_depth_image_scale_ = display_scale;
  const camera::CameraBase& camera = *intrinsics_->model(display_scale);
  CHOOSE_CAMERA_TEMPLATE(
      camera,
      RasterizeDepthOverlay(_camera, *image_, *scan_points_, &depth_qimage_));
}

void BADepthOverlayWidget::UpdateViewTransforms() {
  if (image_qimage_.isNull()) {
    image_to_viewport_.reset();
    viewport_to_image_.reset();
    return;
  }
  image_to_viewport_ = QTransform(
      view_state_.scale, 0,
      0, view_state_.scale,
      view_state_.offset_x + 0.5 * width() - 0.5 * image_qimage_.width() * view_state_.scale,
      view_state_.offset_y + 0.5 * height() - 0.5 * image_qimage_.height() * view_state_.scale);
  viewport_to_image_ = image_to_viewport_.inverted();
}

QPointF BADepthOverlayWidget::ViewportToImage(const QPointF& pos) const {
  return viewport_to_image_.map(pos);
}

BACompareTab::BACompareTab(QWidget* parent) : QWidget(parent) {
  QVBoxLayout* root = new QVBoxLayout(this);

  QGroupBox* paths_group = new QGroupBox("Compare Inputs");
  QFormLayout* paths_form = new QFormLayout(paths_group);
  scan_alignment_edit_ = NewPathEdit();
  image_base_edit_ = NewPathEdit();
  before_state_edit_ = NewPathEdit();
  after_state_edit_ = NewPathEdit();
  AddPathRow(paths_form, "Scan alignment", scan_alignment_edit_);
  AddPathRow(paths_form, "Image base", image_base_edit_);
  AddPathRow(paths_form, "Before BA state", before_state_edit_);
  AddPathRow(paths_form, "After BA state", after_state_edit_);
  root->addWidget(paths_group);

  QWidget* controls = new QWidget();
  QHBoxLayout* controls_layout = new QHBoxLayout(controls);
  controls_layout->setContentsMargins(0, 0, 0, 0);
  image_scale_combo_ = new QComboBox();
  image_scale_combo_->addItem("Combined", -1);
  opacity_slider_ = new QSlider(Qt::Horizontal);
  opacity_slider_->setRange(0, 100);
  opacity_slider_->setValue(35);
  opacity_slider_->setMaximumWidth(180);
  QPushButton* load_button = new QPushButton("Load Compare");
  controls_layout->addWidget(new QLabel("Image scale"));
  controls_layout->addWidget(image_scale_combo_);
  controls_layout->addWidget(new QLabel("Depth opacity"));
  controls_layout->addWidget(opacity_slider_);
  controls_layout->addWidget(load_button);
  controls_layout->addStretch();
  root->addWidget(controls);

  QSplitter* main_splitter = new QSplitter();
  image_list_ = new QListWidget();
  image_list_->setMinimumWidth(260);
  main_splitter->addWidget(image_list_);

  QSplitter* viewer_splitter = new QSplitter();
  QWidget* before_panel = new QWidget();
  QVBoxLayout* before_layout = new QVBoxLayout(before_panel);
  before_layout->setContentsMargins(0, 0, 0, 0);
  before_title_ = new QLabel("Before BA");
  before_widget_ = new BADepthOverlayWidget();
  before_layout->addWidget(before_title_);
  before_layout->addWidget(before_widget_, 1);

  QWidget* after_panel = new QWidget();
  QVBoxLayout* after_layout = new QVBoxLayout(after_panel);
  after_layout->setContentsMargins(0, 0, 0, 0);
  after_title_ = new QLabel("After BA");
  after_widget_ = new BADepthOverlayWidget();
  after_layout->addWidget(after_title_);
  after_layout->addWidget(after_widget_, 1);

  viewer_splitter->addWidget(before_panel);
  viewer_splitter->addWidget(after_panel);
  main_splitter->addWidget(viewer_splitter);
  main_splitter->setStretchFactor(1, 1);
  root->addWidget(main_splitter, 1);

  status_text_ = new QTextEdit();
  status_text_->setReadOnly(true);
  status_text_->setMaximumHeight(90);
  root->addWidget(status_text_);

  connect(load_button, &QPushButton::clicked, this, &BACompareTab::LoadDatasets);
  connect(image_list_, &QListWidget::currentItemChanged,
          this, &BACompareTab::CurrentImageChanged);
  connect(image_scale_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &BACompareTab::ImageScaleChanged);
  connect(opacity_slider_, &QSlider::valueChanged,
          this, &BACompareTab::OpacityChanged);
  connect(before_widget_, &BADepthOverlayWidget::ViewStateChanged,
          this, &BACompareTab::SyncViewFromBefore);
  connect(after_widget_, &BADepthOverlayWidget::ViewStateChanged,
          this, &BACompareTab::SyncViewFromAfter);
}

void BACompareTab::SetDefaultPaths(const QString& dataset_dir,
                                   const QString& /*multi_res_subdir*/,
                                   int /*cube_map_camera_id*/) {
  if (dataset_dir.trimmed().isEmpty()) {
    return;
  }
  const QDir dataset(dataset_dir);
  scan_alignment_edit_->setText(Native(dataset.filePath("scan_clean/scan_alignment.mlp")));
  image_base_edit_->setText(Native(dataset.absolutePath()));
  before_state_edit_->setText(Native(dataset.filePath("sparse_reconstruction_scaled/colmap_model")));
  after_state_edit_->setText(Native(dataset.filePath("image_alignment_refined/scale_1_state")));
}

QLineEdit* BACompareTab::NewPathEdit(const QString& text) {
  QLineEdit* edit = new QLineEdit(text);
  edit->setMinimumWidth(480);
  return edit;
}

void BACompareTab::AddPathRow(QFormLayout* form,
                              const QString& label,
                              QLineEdit* edit) {
  form->addRow(label, edit);
}

void BACompareTab::LoadDatasets() {
  if (!QFileInfo::exists(scan_alignment_edit_->text())) {
    QMessageBox::warning(this, "BA Compare", "Scan alignment file does not exist.");
    return;
  }
  if (!QDir(image_base_edit_->text()).exists()) {
    QMessageBox::warning(this, "BA Compare", "Image base directory does not exist.");
    return;
  }
  if (!QDir(before_state_edit_->text()).exists()) {
    QMessageBox::warning(this, "BA Compare", "Before BA state directory does not exist.");
    return;
  }
  QString model_error;
  if (!ValidateColmapTextModel(before_state_edit_->text(), "Before BA", &model_error)) {
    QMessageBox::warning(this, "BA Compare", model_error);
    status_text_->setPlainText(model_error);
    return;
  }

  QString error;
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr scan_points = LoadAlignedScans(&error);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, "BA Compare", error);
    return;
  }

  status_text_->setPlainText("Loading before BA state ...");
  before_ = LoadOneDataset(before_state_edit_->text(), "Before BA", scan_points);
  if (!before_.error.isEmpty()) {
    QMessageBox::warning(this, "BA Compare", before_.error);
    status_text_->setPlainText(before_.error);
    return;
  }

  after_ = LoadedDataset();
  if (QDir(after_state_edit_->text()).exists()) {
    QString after_error;
    if (!ValidateColmapTextModel(after_state_edit_->text(), "After BA", &after_error)) {
      status_text_->setPlainText(after_error + "\nRight panel will stay blank.");
      PopulateImageList();
      PopulateImageScales();
      return;
    }
    status_text_->setPlainText("Loading after BA state ...");
    after_ = LoadOneDataset(after_state_edit_->text(), "After BA", scan_points);
    if (!after_.error.isEmpty()) {
      status_text_->append("After BA model could not be loaded; right panel will stay blank.");
      after_ = LoadedDataset();
    }
  } else {
    status_text_->append("After BA state does not exist; right panel will stay blank.");
  }

  PopulateImageList();
  PopulateImageScales();
  status_text_->setPlainText(
      QString("Loaded %1 before images, %2 after images, %3 scan points.")
          .arg(before_.image_ids_by_name.size())
          .arg(after_.image_ids_by_name.size())
          .arg(scan_points ? scan_points->size() : 0));
}

BACompareTab::LoadedDataset BACompareTab::LoadOneDataset(
    const QString& state_path,
    const QString& side_name,
    const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& scan_points) const {
  LoadedDataset loaded;
  loaded.scan_points = scan_points;

  std::shared_ptr<opt::OcclusionGeometry> dummy_occlusion_geometry(
      new opt::OcclusionGeometry());
  loaded.problem.reset(new opt::Problem(dummy_occlusion_geometry));
  if (!io::InitializeStateFromColmapModel(state_path.toStdString(),
                                          image_base_edit_->text().toStdString(),
                                          {},
                                          loaded.problem.get())) {
    loaded.error = side_name + ": cannot initialize COLMAP state.";
    return loaded;
  }
  loaded.problem->InitializeImages();
  loaded.problem->LoadImages(image_base_edit_->text().toStdString());
  loaded.problem->SetImageScale(0);

  for (const auto& entry : loaded.problem->images()) {
    loaded.image_ids_by_name.insert(
        NormalizeImageName(entry.second.file_path), entry.first);
  }
  return loaded;
}

bool BACompareTab::ValidateColmapTextModel(const QString& state_path,
                                           const QString& side_name,
                                           QString* error) const {
  const QDir state_dir(state_path);
  const QString cameras_path = state_dir.filePath("cameras.txt");
  const QString images_path = state_dir.filePath("images.txt");
  if (!QFileInfo::exists(cameras_path) || !QFileInfo::exists(images_path)) {
    *error = QString("%1 state is not a COLMAP TXT model: %2")
                 .arg(side_name, Native(state_path));
    return false;
  }

  auto count_data_lines = [](const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      return -1;
    }
    int count = 0;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
      const QString line = stream.readLine().trimmed();
      if (!line.isEmpty() && !line.startsWith("#")) {
        ++count;
      }
    }
    return count;
  };

  const int camera_count = count_data_lines(cameras_path);
  const int image_line_count = count_data_lines(images_path);
  if (camera_count <= 0 || image_line_count <= 0) {
    *error = QString("%1 state has no cameras/images: %2\n"
                     "cameras.txt data lines: %3, images.txt data lines: %4")
                 .arg(side_name,
                      Native(state_path),
                      QString::number(camera_count),
                      QString::number(image_line_count));
    return false;
  }
  return true;
}

pcl::PointCloud<pcl::PointXYZRGB>::Ptr BACompareTab::LoadAlignedScans(
    QString* error) const {
  std::vector<pcl::PointCloud<pcl::PointXYZRGB>::Ptr> scans;
  pcl::console::setVerbosityLevel(pcl::console::L_ERROR);
  if (!opt::LoadPointClouds(scan_alignment_edit_->text().toStdString(), &scans)) {
    pcl::console::setVerbosityLevel(pcl::console::L_ALWAYS);
    *error = "Cannot load aligned scan point clouds.";
    return {};
  }
  pcl::console::setVerbosityLevel(pcl::console::L_ALWAYS);
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr merged(
      new pcl::PointCloud<pcl::PointXYZRGB>());
  for (const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& scan : scans) {
    *merged += *scan;
  }
  return merged;
}

void BACompareTab::PopulateImageList() {
  image_list_->clear();
  QStringList names = before_.image_ids_by_name.keys();
  names.sort(Qt::CaseInsensitive);
  image_list_->addItems(names);
  if (image_list_->count() > 0) {
    image_list_->setCurrentRow(0);
  }
}

void BACompareTab::CurrentImageChanged(QListWidgetItem* current,
                                       QListWidgetItem* /*previous*/) {
  if (!current) {
    return;
  }
  SelectImage(current->text());
}

void BACompareTab::SelectImage(const QString& image_name) {
  SetWidgetImage(before_widget_, &before_, image_name, before_title_);
  SetWidgetImage(after_widget_, &after_, image_name, after_title_);
}

void BACompareTab::SetWidgetImage(BADepthOverlayWidget* widget,
                                  LoadedDataset* dataset,
                                  const QString& image_name,
                                  QLabel* title_label) {
  if (!dataset->problem || !dataset->image_ids_by_name.contains(image_name)) {
    widget->SetImage(nullptr, nullptr, {});
    title_label->setText("Blank: " + image_name);
    return;
  }
  const int image_id = dataset->image_ids_by_name.value(image_name);
  opt::Image* image = dataset->problem->image_mutable(image_id);
  widget->SetImage(image,
                   dataset->problem->intrinsics_mutable(image->intrinsics_id),
                   dataset->scan_points);
  widget->SetImageScale(image_scale_combo_->currentData().toInt());
  widget->SetOverlayOpacity(opacity_slider_->value() / 100.0);
  title_label->setText(QString("%1  image_id=%2").arg(image_name).arg(image_id));
}

void BACompareTab::PopulateImageScales() {
  const int previous = image_scale_combo_->currentData().toInt();
  image_scale_combo_->blockSignals(true);
  image_scale_combo_->clear();
  image_scale_combo_->addItem("Combined", -1);
  if (before_.problem && !before_.problem->intrinsics_list().empty()) {
    const int max_scale = before_.problem->max_image_scale();
    for (int scale = 0; scale <= max_scale; ++scale) {
      image_scale_combo_->addItem(QString("Scale %1").arg(scale), scale);
    }
  }
  const int index = image_scale_combo_->findData(previous);
  image_scale_combo_->setCurrentIndex(index >= 0 ? index : 0);
  image_scale_combo_->blockSignals(false);
}

void BACompareTab::ImageScaleChanged(int /*index*/) {
  const int scale = image_scale_combo_->currentData().toInt();
  before_widget_->SetImageScale(scale);
  after_widget_->SetImageScale(scale);
}

void BACompareTab::OpacityChanged(int value) {
  const double opacity = value / 100.0;
  before_widget_->SetOverlayOpacity(opacity);
  after_widget_->SetOverlayOpacity(opacity);
}

void BACompareTab::SyncViewFromBefore(const BAImageViewState& state) {
  if (syncing_view_) {
    return;
  }
  syncing_view_ = true;
  after_widget_->SetViewState(state);
  syncing_view_ = false;
}

void BACompareTab::SyncViewFromAfter(const BAImageViewState& state) {
  if (syncing_view_) {
    return;
  }
  syncing_view_ = true;
  before_widget_->SetViewState(state);
  syncing_view_ = false;
}

QString BACompareTab::NormalizeImageName(const std::string& image_name) const {
  return QString::fromStdString(image_name).replace("\\", "/");
}

}  // namespace studio
