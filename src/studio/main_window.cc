#include "studio/main_window.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QDoubleSpinBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>
#include <QDirIterator>

#include "studio/ba_compare_tab.h"
#include "studio/colmap_model_summary.h"
#include "studio/sfm_model_view.h"

namespace studio {
namespace {

QString Native(const QString& path) {
  return QDir::toNativeSeparators(path);
}

QStringList ImageNameFilters() {
  return {"*.jpg", "*.jpeg", "*.png", "*.tif", "*.tiff", "*.bmp"};
}

QLineEdit* NewLineEdit(const QString& text = QString()) {
  QLineEdit* edit = new QLineEdit(text);
  edit->setMinimumWidth(420);
  return edit;
}

QPushButton* NewBrowseButton() {
  QPushButton* button = new QPushButton("Browse");
  button->setMaximumWidth(90);
  return button;
}

QDoubleSpinBox* NewDoubleSpinBox(double value,
                                 double minimum,
                                 double maximum,
                                 int decimals,
                                 double step) {
  QDoubleSpinBox* spin = new QDoubleSpinBox();
  spin->setRange(minimum, maximum);
  spin->setDecimals(decimals);
  spin->setSingleStep(step);
  spin->setValue(value);
  return spin;
}

QString NumberArg(double value) {
  return QString::number(value, 'g', 12);
}

void AddPathRow(QFormLayout* form,
                const QString& label,
                QLineEdit* edit,
                QPushButton* browse_button) {
  QWidget* row = new QWidget();
  QHBoxLayout* layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(edit);
  layout->addWidget(browse_button);
  form->addRow(label, row);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle("Dataset Pipeline Studio");
  resize(1180, 760);

  tabs_ = new QTabWidget(this);
  tabs_->addTab(CreateProjectTab(), "Project");
  tabs_->addTab(CreatePipelineTab(), "Pipeline");
  tabs_->addTab(CreateColmapTab(), "COLMAP SfM");
  tabs_->addTab(CreateCubeMapsTab(), "Cube Maps");
  tabs_->addTab(CreateBACompareTab(), "BA Compare");
  tabs_->addTab(CreateModelTab(), "SfM Model");
  tabs_->addTab(CreateLogTab(), "Log");
  setCentralWidget(tabs_);

  connect(&process_, SIGNAL(finished(int, QProcess::ExitStatus)),
          this, SLOT(ProcessFinished(int, QProcess::ExitStatus)));
  connect(&process_, &QProcess::readyReadStandardOutput,
          this, &MainWindow::AppendStdout);
  connect(&process_, &QProcess::readyReadStandardError,
          this, &MainWindow::AppendStderr);

  LoadSettings();
}

QWidget* MainWindow::CreateProjectTab() {
  QWidget* tab = new QWidget();
  QVBoxLayout* root = new QVBoxLayout(tab);

  QGroupBox* dataset_group = new QGroupBox("Dataset");
  QFormLayout* form = new QFormLayout(dataset_group);

  dataset_root_edit_ = NewLineEdit();
  QPushButton* browse_dataset = NewBrowseButton();
  AddPathRow(form, "Dataset root", dataset_root_edit_, browse_dataset);
  connect(browse_dataset, &QPushButton::clicked, this, &MainWindow::BrowseDatasetRoot);

  dataset_subdir_edit_ = NewLineEdit();
  QPushButton* browse_dataset_subdir = NewBrowseButton();
  AddPathRow(form, "Dataset subdir", dataset_subdir_edit_, browse_dataset_subdir);
  connect(browse_dataset_subdir, &QPushButton::clicked,
          this, &MainWindow::BrowseDatasetSubdir);

  image_dir_edit_ = NewLineEdit("dslr_images");
  form->addRow("Input image dir", image_dir_edit_);
  connect(dataset_root_edit_, &QLineEdit::editingFinished,
          this, &MainWindow::ProjectPathsChanged);
  connect(dataset_subdir_edit_, &QLineEdit::editingFinished,
          this, &MainWindow::ProjectPathsChanged);
  connect(image_dir_edit_, &QLineEdit::editingFinished,
          this, &MainWindow::ProjectPathsChanged);

  QWidget* config_buttons = new QWidget();
  QHBoxLayout* config_layout = new QHBoxLayout(config_buttons);
  config_layout->setContentsMargins(0, 0, 0, 0);
  QPushButton* export_config = new QPushButton("Export Config");
  QPushButton* import_config = new QPushButton("Import Config");
  config_layout->addWidget(export_config);
  config_layout->addWidget(import_config);
  config_layout->addStretch();
  form->addRow("", config_buttons);
  connect(export_config, &QPushButton::clicked, this, &MainWindow::ExportConfig);
  connect(import_config, &QPushButton::clicked, this, &MainWindow::ImportConfig);

  root->addWidget(dataset_group);
  root->addStretch();
  return tab;
}

QWidget* MainWindow::CreatePipelineTab() {
  QWidget* tab = new QWidget();
  QVBoxLayout* root = new QVBoxLayout(tab);

  QGroupBox* actions_group = new QGroupBox("Pipeline Actions");
  QGridLayout* actions = new QGridLayout(actions_group);
  QPushButton* refresh = new QPushButton("Refresh Status");
  QPushButton* render_cube_maps = new QPushButton("Render Cube Maps");
  QPushButton* run_icp = new QPushButton("Run ICP Scan Aligner");
  QPushButton* run_normals = new QPushButton("Run Normal Estimator");
  QPushButton* run_poisson = new QPushButton("Run Poisson Recon");
  QPushButton* run_splats = new QPushButton("Run Splat Creator");
  QPushButton* run_multi_res = new QPushButton("Build Multi-Res Point Cloud");
  QPushButton* run_image_reg = new QPushButton("Run ImageRegistrator");
  QPushButton* open_inspector = new QPushButton("Launch DatasetInspector");
  QPushButton* open_editor = new QPushButton("Launch PointCloudEditor");
  actions->addWidget(refresh, 0, 0);
  actions->addWidget(render_cube_maps, 0, 1);
  actions->addWidget(run_icp, 1, 0);
  actions->addWidget(run_normals, 1, 1);
  actions->addWidget(run_poisson, 2, 0);
  actions->addWidget(run_splats, 2, 1);
  actions->addWidget(run_multi_res, 3, 0);
  actions->addWidget(run_image_reg, 3, 1);
  actions->addWidget(open_inspector, 4, 0);
  actions->addWidget(open_editor, 4, 1);
  connect(refresh, &QPushButton::clicked, this, &MainWindow::RefreshPipelineStatus);
  connect(render_cube_maps, &QPushButton::clicked, this, &MainWindow::RenderCubeMaps);
  connect(run_icp, &QPushButton::clicked, this, &MainWindow::RunICPScanAligner);
  connect(run_normals, &QPushButton::clicked, this, &MainWindow::RunNormalEstimator);
  connect(run_poisson, &QPushButton::clicked, this, &MainWindow::RunPoissonReconstruction);
  connect(run_splats, &QPushButton::clicked, this, &MainWindow::RunSplatCreator);
  connect(run_multi_res, &QPushButton::clicked, this, &MainWindow::RunMultiResPointCloud);
  connect(run_image_reg, &QPushButton::clicked, this, &MainWindow::RunImageRegistrator);
  connect(open_inspector, &QPushButton::clicked, this, &MainWindow::LaunchDatasetInspector);
  connect(open_editor, &QPushButton::clicked, this, &MainWindow::LaunchPointCloudEditor);

  QGroupBox* parameters_group = new QGroupBox("Stage Parameters");
  QGridLayout* parameter_grid = new QGridLayout(parameters_group);

  QGroupBox* icp_group = new QGroupBox("ICP");
  QFormLayout* icp_form = new QFormLayout(icp_group);
  cube_map_size_spin_ = new QSpinBox();
  cube_map_size_spin_->setRange(256, 16384);
  cube_map_size_spin_->setValue(2048);
  icp_max_distance_spin_ = NewDoubleSpinBox(0.01, 0.000001, 10.0, 6, 0.001);
  icp_max_iterations_spin_ = new QSpinBox();
  icp_max_iterations_spin_->setRange(1, 100000);
  icp_max_iterations_spin_->setValue(100);
  icp_convergence_threshold_edit_ = new QLineEdit("1e-10");
  icp_scale_count_spin_ = new QSpinBox();
  icp_scale_count_spin_->setRange(1, 16);
  icp_scale_count_spin_->setValue(4);
  icp_form->addRow("Cube map size", cube_map_size_spin_);
  icp_form->addRow("Max distance", icp_max_distance_spin_);
  icp_form->addRow("Max iterations", icp_max_iterations_spin_);
  icp_form->addRow("Convergence threshold", icp_convergence_threshold_edit_);
  icp_form->addRow("Scales", icp_scale_count_spin_);

  QGroupBox* surface_group = new QGroupBox("Surface");
  QFormLayout* surface_form = new QFormLayout(surface_group);
  normal_neighbor_count_spin_ = new QSpinBox();
  normal_neighbor_count_spin_->setRange(1, 256);
  normal_neighbor_count_spin_->setValue(8);
  poisson_depth_spin_ = new QSpinBox();
  poisson_depth_spin_->setRange(1, 20);
  poisson_depth_spin_->setValue(13);
  poisson_data_weight_spin_ = NewDoubleSpinBox(16.0, 0.0, 1000.0, 3, 1.0);
  poisson_colors_check_ = new QCheckBox("Write colors");
  poisson_colors_check_->setChecked(true);
  poisson_density_check_ = new QCheckBox("Write density");
  poisson_density_check_->setChecked(true);
  splat_distance_threshold_spin_ =
      NewDoubleSpinBox(0.02, 0.000001, 10.0, 6, 0.001);
  surface_form->addRow("Normal neighbors", normal_neighbor_count_spin_);
  surface_form->addRow("Poisson depth", poisson_depth_spin_);
  surface_form->addRow("Poisson data", poisson_data_weight_spin_);
  surface_form->addRow("", poisson_colors_check_);
  surface_form->addRow("", poisson_density_check_);
  surface_form->addRow("Splat distance", splat_distance_threshold_spin_);

  QGroupBox* multi_res_group = new QGroupBox("Multi-Res Point Cloud");
  QFormLayout* multi_res_form = new QFormLayout(multi_res_group);
  multi_res_output_subdir_edit_ = NewLineEdit("multi_res_point_cloud_cache");
  multi_res_save_check_ = new QCheckBox("Save generated multi-res point cloud");
  multi_res_save_check_->setChecked(true);
  multi_res_form->addRow("Output subdir", multi_res_output_subdir_edit_);
  multi_res_form->addRow("", multi_res_save_check_);

  QGroupBox* image_reg_group = new QGroupBox("Image Registration");
  QFormLayout* image_reg_form = new QFormLayout(image_reg_group);
  image_reg_max_iterations_spin_ = new QSpinBox();
  image_reg_max_iterations_spin_->setRange(1, 100000);
  image_reg_max_iterations_spin_->setValue(400);
  image_reg_initial_scale_spin_ = NewDoubleSpinBox(0.0, 0.0, 16.0, 3, 1.0);
  image_reg_target_scale_spin_ = NewDoubleSpinBox(2.0, 0.0, 16.0, 3, 1.0);
  image_reg_cache_observations_check_ = new QCheckBox("Use observations cache");
  image_reg_cache_observations_check_->setChecked(true);
  image_reg_form->addRow("Max iterations", image_reg_max_iterations_spin_);
  image_reg_form->addRow("Initial scale", image_reg_initial_scale_spin_);
  image_reg_form->addRow("Target scale", image_reg_target_scale_spin_);
  image_reg_form->addRow("", image_reg_cache_observations_check_);

  parameter_grid->addWidget(icp_group, 0, 0);
  parameter_grid->addWidget(surface_group, 0, 1);
  parameter_grid->addWidget(multi_res_group, 1, 0);
  parameter_grid->addWidget(image_reg_group, 1, 1);

  pipeline_status_text_ = new QTextEdit();
  pipeline_status_text_->setReadOnly(true);
  pipeline_status_text_->setMinimumHeight(260);

  root->addWidget(actions_group);
  root->addWidget(parameters_group);
  root->addWidget(pipeline_status_text_, 1);
  return tab;
}

QWidget* MainWindow::CreateColmapTab() {
  QWidget* tab = new QWidget();
  QVBoxLayout* root = new QVBoxLayout(tab);

  QGroupBox* tools_group = new QGroupBox("External Tool");
  QFormLayout* tools_form = new QFormLayout(tools_group);
  pipeline_bin_path_edit_ = NewLineEdit();
  QPushButton* browse_pipeline_bin = NewBrowseButton();
  AddPathRow(tools_form, "Pipeline bin", pipeline_bin_path_edit_, browse_pipeline_bin);
  connect(browse_pipeline_bin, &QPushButton::clicked,
          this, &MainWindow::BrowsePipelineBinPath);

  colmap_exe_edit_ = NewLineEdit();
  QPushButton* browse_colmap = NewBrowseButton();
  AddPathRow(tools_form, "colmap.exe", colmap_exe_edit_, browse_colmap);
  connect(browse_colmap, &QPushButton::clicked, this, &MainWindow::BrowseColmapExe);

  cube_map_renderer_exe_edit_ = NewLineEdit();
  QPushButton* browse_cube_map = NewBrowseButton();
  AddPathRow(tools_form, "CubeMapRenderer.exe", cube_map_renderer_exe_edit_, browse_cube_map);
  connect(browse_cube_map, &QPushButton::clicked,
          this, &MainWindow::BrowseCubeMapRendererExe);

  scale_estimator_exe_edit_ = NewLineEdit();
  QPushButton* browse_scale = NewBrowseButton();
  AddPathRow(tools_form, "SfMScaleEstimator.exe", scale_estimator_exe_edit_, browse_scale);
  connect(browse_scale, &QPushButton::clicked,
          this, &MainWindow::BrowseScaleEstimatorExe);

  poisson_recon_exe_edit_ = NewLineEdit("E:\\sh\\tools\\AdaptiveSolvers.x64\\PoissonRecon.exe");
  QPushButton* browse_poisson = NewBrowseButton();
  AddPathRow(tools_form, "PoissonRecon.exe", poisson_recon_exe_edit_, browse_poisson);
  connect(browse_poisson, &QPushButton::clicked,
          this, &MainWindow::BrowsePoissonReconExe);

  QPushButton* test_button = new QPushButton("Test COLMAP");
  tools_form->addRow("", test_button);
  connect(test_button, &QPushButton::clicked, this, &MainWindow::TestColmap);
  root->addWidget(tools_group);

  QGroupBox* sfm_group = new QGroupBox("Sparse Reconstruction");
  QFormLayout* sfm_form = new QFormLayout(sfm_group);
  dslr_camera_model_edit_ = NewLineEdit("OPENCV");
  sfm_form->addRow("Camera model", dslr_camera_model_edit_);

  dslr_camera_params_edit_ = NewLineEdit();
  sfm_form->addRow("Camera params", dslr_camera_params_edit_);

  dslr_single_camera_check_ = new QCheckBox("Use one shared camera for input images");
  sfm_form->addRow("", dslr_single_camera_check_);

  matcher_combo_ = new QComboBox();
  matcher_combo_->addItems({"exhaustive", "sequential", "spatial", "vocab_tree"});
  sfm_form->addRow("Matcher", matcher_combo_);

  vocab_tree_edit_ = NewLineEdit();
  sfm_form->addRow("Vocab tree", vocab_tree_edit_);

  overwrite_database_check_ = new QCheckBox("Overwrite existing colmap/database.db");
  sfm_form->addRow("", overwrite_database_check_);

  model_index_spin_ = new QSpinBox();
  model_index_spin_->setRange(0, 999);
  model_index_spin_->setValue(0);
  sfm_form->addRow("Model index", model_index_spin_);

  sfm_model_subdir_edit_ = NewLineEdit("colmap/sparse_txt");
  sfm_form->addRow("SfM model subdir", sfm_model_subdir_edit_);

  scaled_output_subdir_edit_ = NewLineEdit("sparse_reconstruction_scaled");
  sfm_form->addRow("Scaled output subdir", scaled_output_subdir_edit_);

  cube_map_face_camera_id_spin_ = new QSpinBox();
  cube_map_face_camera_id_spin_->setRange(-1, 1000000);
  cube_map_face_camera_id_spin_->setValue(-1);
  cube_map_face_camera_id_spin_->setSpecialValueText("Auto");
  sfm_form->addRow("Cube map camera id", cube_map_face_camera_id_spin_);

  QWidget* buttons = new QWidget();
  QHBoxLayout* button_layout = new QHBoxLayout(buttons);
  button_layout->setContentsMargins(0, 0, 0, 0);
  run_colmap_button_ = new QPushButton("Run COLMAP SfM");
  QPushButton* scale_button = new QPushButton("Run Scale Estimator");
  stop_button_ = new QPushButton("Stop");
  stop_button_->setEnabled(false);
  button_layout->addWidget(run_colmap_button_);
  button_layout->addWidget(scale_button);
  button_layout->addWidget(stop_button_);
  button_layout->addStretch();
  sfm_form->addRow("", buttons);
  connect(run_colmap_button_, &QPushButton::clicked, this, &MainWindow::RunColmapSfm);
  connect(scale_button, &QPushButton::clicked, this, &MainWindow::RunScaleEstimator);
  connect(stop_button_, &QPushButton::clicked, this, &MainWindow::StopTask);

  root->addWidget(sfm_group);
  root->addStretch();
  return tab;
}

QWidget* MainWindow::CreateCubeMapsTab() {
  QWidget* tab = new QWidget();
  QVBoxLayout* root = new QVBoxLayout(tab);

  QGroupBox* controls_group = new QGroupBox("Cube Map Preview");
  QHBoxLayout* controls = new QHBoxLayout(controls_group);
  cube_map_scan_combo_ = new QComboBox();
  QPushButton* refresh = new QPushButton("Refresh");
  controls->addWidget(new QLabel("Scan"));
  controls->addWidget(cube_map_scan_combo_, 1);
  controls->addWidget(refresh);
  connect(refresh, &QPushButton::clicked, this, &MainWindow::RefreshCubeMapPreview);
  connect(cube_map_scan_combo_, SIGNAL(currentIndexChanged(int)),
          this, SLOT(RefreshCubeMapPreview()));

  QWidget* grid_widget = new QWidget();
  QGridLayout* grid = new QGridLayout(grid_widget);
  const QStringList faces = {"front", "left", "back", "right", "up", "down"};
  for (int i = 0; i < faces.size(); ++i) {
    QLabel* title = new QLabel(faces[i]);
    title->setAlignment(Qt::AlignCenter);
    cube_map_labels_[i] = new QLabel();
    cube_map_labels_[i]->setAlignment(Qt::AlignCenter);
    cube_map_labels_[i]->setMinimumSize(220, 220);
    cube_map_labels_[i]->setFrameShape(QFrame::StyledPanel);
    cube_map_labels_[i]->setScaledContents(false);
    const int row = (i / 3) * 2;
    const int col = i % 3;
    grid->addWidget(title, row, col);
    grid->addWidget(cube_map_labels_[i], row + 1, col);
  }

  root->addWidget(controls_group);
  root->addWidget(grid_widget, 1);
  return tab;
}

QWidget* MainWindow::CreateBACompareTab() {
  ba_compare_tab_ = new BACompareTab();
  return ba_compare_tab_;
}

QWidget* MainWindow::CreateModelTab() {
  QWidget* tab = new QWidget();
  QVBoxLayout* root = new QVBoxLayout(tab);

  QGroupBox* model_group = new QGroupBox("COLMAP TXT Model");
  QFormLayout* form = new QFormLayout(model_group);
  model_path_edit_ = NewLineEdit();
  QPushButton* browse_model = NewBrowseButton();
  AddPathRow(form, "Model path", model_path_edit_, browse_model);
  connect(browse_model, &QPushButton::clicked, this, &MainWindow::BrowseModelPath);

  model_image_base_edit_ = NewLineEdit();
  form->addRow("Image base path", model_image_base_edit_);

  QPushButton* load_button = new QPushButton("Load Summary");
  form->addRow("", load_button);
  connect(load_button, &QPushButton::clicked, this, &MainWindow::LoadModelSummary);

  model_summary_text_ = new QTextEdit();
  model_summary_text_->setReadOnly(true);
  model_summary_text_->setMinimumHeight(190);

  sfm_model_view_ = new SfmModelView();

  root->addWidget(model_group);
  root->addWidget(model_summary_text_);
  root->addWidget(sfm_model_view_, 1);
  return tab;
}

QWidget* MainWindow::CreateLogTab() {
  QWidget* tab = new QWidget();
  QVBoxLayout* root = new QVBoxLayout(tab);
  log_text_ = new QPlainTextEdit();
  log_text_->setReadOnly(true);
  log_text_->setMaximumBlockCount(20000);
  root->addWidget(log_text_);
  return tab;
}

void MainWindow::LoadSettings() {
  QSettings settings("DatasetPipeline", "DatasetPipelineStudio");
  pipeline_bin_path_edit_->setText(settings.value(
      "tools/pipeline_bin", QCoreApplication::applicationDirPath()).toString());
  colmap_exe_edit_->setText(settings.value("colmap/exe").toString());
  cube_map_renderer_exe_edit_->setText(settings.value("tools/cube_map_renderer").toString());
  scale_estimator_exe_edit_->setText(settings.value("tools/scale_estimator").toString());
  poisson_recon_exe_edit_->setText(settings.value(
      "tools/poisson_recon",
      "E:\\sh\\tools\\AdaptiveSolvers.x64\\PoissonRecon.exe").toString());
  dataset_root_edit_->setText(settings.value("project/dataset_root").toString());
  dataset_subdir_edit_->setText(settings.value("project/dataset_subdir").toString());
  image_dir_edit_->setText(settings.value("project/image_dir", "dslr_images").toString());
  cube_map_size_spin_->setValue(settings.value("colmap/cube_map_size", 2048).toInt());
  dslr_camera_model_edit_->setText(settings.value("colmap/camera_model", "OPENCV").toString());
  dslr_camera_params_edit_->setText(settings.value("colmap/camera_params").toString());
  dslr_single_camera_check_->setChecked(settings.value("colmap/single_camera", false).toBool());
  matcher_combo_->setCurrentText(settings.value("colmap/matcher", "exhaustive").toString());
  vocab_tree_edit_->setText(settings.value("colmap/vocab_tree").toString());
  model_index_spin_->setValue(settings.value("colmap/model_index", 0).toInt());
  sfm_model_subdir_edit_->setText(settings.value("scale/sfm_model_subdir", "colmap/sparse_txt").toString());
  scaled_output_subdir_edit_->setText(settings.value("scale/output_subdir", "sparse_reconstruction_scaled").toString());
  cube_map_face_camera_id_spin_->setValue(settings.value("scale/cube_map_camera_id", -1).toInt());
  icp_max_distance_spin_->setValue(settings.value("pipeline/icp_max_distance", 0.01).toDouble());
  icp_max_iterations_spin_->setValue(settings.value("pipeline/icp_max_iterations", 100).toInt());
  icp_convergence_threshold_edit_->setText(settings.value("pipeline/icp_convergence_threshold", "1e-10").toString());
  icp_scale_count_spin_->setValue(settings.value("pipeline/icp_scale_count", 4).toInt());
  normal_neighbor_count_spin_->setValue(settings.value("pipeline/normal_neighbor_count", 8).toInt());
  poisson_depth_spin_->setValue(settings.value("pipeline/poisson_depth", 13).toInt());
  poisson_data_weight_spin_->setValue(settings.value("pipeline/poisson_data_weight", 16.0).toDouble());
  poisson_colors_check_->setChecked(settings.value("pipeline/poisson_colors", true).toBool());
  poisson_density_check_->setChecked(settings.value("pipeline/poisson_density", true).toBool());
  splat_distance_threshold_spin_->setValue(settings.value("pipeline/splat_distance_threshold", 0.02).toDouble());
  multi_res_output_subdir_edit_->setText(settings.value("pipeline/multi_res_output_subdir", "multi_res_point_cloud_cache").toString());
  multi_res_save_check_->setChecked(settings.value("pipeline/multi_res_save", true).toBool());
  image_reg_max_iterations_spin_->setValue(settings.value("pipeline/image_reg_max_iterations", 400).toInt());
  image_reg_initial_scale_spin_->setValue(settings.value("pipeline/image_reg_initial_scale", 0.0).toDouble());
  image_reg_target_scale_spin_->setValue(settings.value("pipeline/image_reg_target_scale", 2.0).toDouble());
  image_reg_cache_observations_check_->setChecked(settings.value("pipeline/image_reg_cache_observations", true).toBool());
  model_path_edit_->setText(settings.value("model/path").toString());
  model_image_base_edit_->setText(settings.value("model/image_base").toString());
  UpdateDatasetDependentDefaults(false);
  ApplyPipelineBinPath(false);
  RefreshPipelineStatus();
}

void MainWindow::SaveSettings() const {
  QSettings settings("DatasetPipeline", "DatasetPipelineStudio");
  settings.setValue("tools/pipeline_bin", pipeline_bin_path_edit_->text());
  settings.setValue("colmap/exe", colmap_exe_edit_->text());
  settings.setValue("tools/cube_map_renderer", cube_map_renderer_exe_edit_->text());
  settings.setValue("tools/scale_estimator", scale_estimator_exe_edit_->text());
  settings.setValue("tools/poisson_recon", poisson_recon_exe_edit_->text());
  settings.setValue("project/dataset_root", dataset_root_edit_->text());
  settings.setValue("project/dataset_subdir", dataset_subdir_edit_->text());
  settings.setValue("project/image_dir", image_dir_edit_->text());
  settings.setValue("colmap/cube_map_size", cube_map_size_spin_->value());
  settings.setValue("colmap/camera_model", dslr_camera_model_edit_->text());
  settings.setValue("colmap/camera_params", dslr_camera_params_edit_->text());
  settings.setValue("colmap/single_camera", dslr_single_camera_check_->isChecked());
  settings.setValue("colmap/matcher", matcher_combo_->currentText());
  settings.setValue("colmap/vocab_tree", vocab_tree_edit_->text());
  settings.setValue("colmap/model_index", model_index_spin_->value());
  settings.setValue("scale/sfm_model_subdir", sfm_model_subdir_edit_->text());
  settings.setValue("scale/output_subdir", scaled_output_subdir_edit_->text());
  settings.setValue("scale/cube_map_camera_id", cube_map_face_camera_id_spin_->value());
  settings.setValue("pipeline/icp_max_distance", icp_max_distance_spin_->value());
  settings.setValue("pipeline/icp_max_iterations", icp_max_iterations_spin_->value());
  settings.setValue("pipeline/icp_convergence_threshold", icp_convergence_threshold_edit_->text());
  settings.setValue("pipeline/icp_scale_count", icp_scale_count_spin_->value());
  settings.setValue("pipeline/normal_neighbor_count", normal_neighbor_count_spin_->value());
  settings.setValue("pipeline/poisson_depth", poisson_depth_spin_->value());
  settings.setValue("pipeline/poisson_data_weight", poisson_data_weight_spin_->value());
  settings.setValue("pipeline/poisson_colors", poisson_colors_check_->isChecked());
  settings.setValue("pipeline/poisson_density", poisson_density_check_->isChecked());
  settings.setValue("pipeline/splat_distance_threshold", splat_distance_threshold_spin_->value());
  settings.setValue("pipeline/multi_res_output_subdir", multi_res_output_subdir_edit_->text());
  settings.setValue("pipeline/multi_res_save", multi_res_save_check_->isChecked());
  settings.setValue("pipeline/image_reg_max_iterations", image_reg_max_iterations_spin_->value());
  settings.setValue("pipeline/image_reg_initial_scale", image_reg_initial_scale_spin_->value());
  settings.setValue("pipeline/image_reg_target_scale", image_reg_target_scale_spin_->value());
  settings.setValue("pipeline/image_reg_cache_observations", image_reg_cache_observations_check_->isChecked());
  settings.setValue("model/path", model_path_edit_->text());
  settings.setValue("model/image_base", model_image_base_edit_->text());
}

QJsonObject MainWindow::CurrentConfig() const {
  QJsonObject project;
  project["dataset_root"] = dataset_root_edit_->text();
  project["dataset_subdir"] = dataset_subdir_edit_->text();
  project["image_dir"] = image_dir_edit_->text();

  QJsonObject colmap;
  colmap["pipeline_bin"] = pipeline_bin_path_edit_->text();
  colmap["exe"] = colmap_exe_edit_->text();
  colmap["cube_map_renderer_exe"] = cube_map_renderer_exe_edit_->text();
  colmap["scale_estimator_exe"] = scale_estimator_exe_edit_->text();
  colmap["poisson_recon_exe"] = poisson_recon_exe_edit_->text();
  colmap["cube_map_size"] = cube_map_size_spin_->value();
  colmap["camera_model"] = dslr_camera_model_edit_->text();
  colmap["camera_params"] = dslr_camera_params_edit_->text();
  colmap["single_camera"] = dslr_single_camera_check_->isChecked();
  colmap["matcher"] = matcher_combo_->currentText();
  colmap["vocab_tree"] = vocab_tree_edit_->text();
  colmap["overwrite_database"] = overwrite_database_check_->isChecked();
  colmap["model_index"] = model_index_spin_->value();

  QJsonObject scale;
  scale["sfm_model_subdir"] = sfm_model_subdir_edit_->text();
  scale["output_subdir"] = scaled_output_subdir_edit_->text();
  scale["cube_map_camera_id"] = cube_map_face_camera_id_spin_->value();

  QJsonObject model;
  model["path"] = model_path_edit_->text();
  model["image_base"] = model_image_base_edit_->text();

  QJsonObject pipeline;
  pipeline["icp_max_distance"] = icp_max_distance_spin_->value();
  pipeline["icp_max_iterations"] = icp_max_iterations_spin_->value();
  pipeline["icp_convergence_threshold"] = icp_convergence_threshold_edit_->text();
  pipeline["icp_scale_count"] = icp_scale_count_spin_->value();
  pipeline["normal_neighbor_count"] = normal_neighbor_count_spin_->value();
  pipeline["poisson_depth"] = poisson_depth_spin_->value();
  pipeline["poisson_data_weight"] = poisson_data_weight_spin_->value();
  pipeline["poisson_colors"] = poisson_colors_check_->isChecked();
  pipeline["poisson_density"] = poisson_density_check_->isChecked();
  pipeline["splat_distance_threshold"] = splat_distance_threshold_spin_->value();
  pipeline["multi_res_output_subdir"] = multi_res_output_subdir_edit_->text();
  pipeline["multi_res_save"] = multi_res_save_check_->isChecked();
  pipeline["image_reg_max_iterations"] = image_reg_max_iterations_spin_->value();
  pipeline["image_reg_initial_scale"] = image_reg_initial_scale_spin_->value();
  pipeline["image_reg_target_scale"] = image_reg_target_scale_spin_->value();
  pipeline["image_reg_cache_observations"] = image_reg_cache_observations_check_->isChecked();

  QJsonObject config;
  config["version"] = 1;
  config["project"] = project;
  config["colmap"] = colmap;
  config["scale"] = scale;
  config["pipeline"] = pipeline;
  config["model"] = model;
  return config;
}

void MainWindow::ApplyConfig(const QJsonObject& config) {
  const QJsonObject project = config["project"].toObject();
  dataset_root_edit_->setText(project["dataset_root"].toString(dataset_root_edit_->text()));
  dataset_subdir_edit_->setText(project["dataset_subdir"].toString(dataset_subdir_edit_->text()));
  image_dir_edit_->setText(project["image_dir"].toString(image_dir_edit_->text()));

  const QJsonObject colmap = config["colmap"].toObject();
  pipeline_bin_path_edit_->setText(colmap["pipeline_bin"].toString(pipeline_bin_path_edit_->text()));
  colmap_exe_edit_->setText(colmap["exe"].toString(colmap_exe_edit_->text()));
  cube_map_renderer_exe_edit_->setText(colmap["cube_map_renderer_exe"].toString(cube_map_renderer_exe_edit_->text()));
  scale_estimator_exe_edit_->setText(colmap["scale_estimator_exe"].toString(scale_estimator_exe_edit_->text()));
  poisson_recon_exe_edit_->setText(colmap["poisson_recon_exe"].toString(poisson_recon_exe_edit_->text()));
  cube_map_size_spin_->setValue(colmap["cube_map_size"].toInt(cube_map_size_spin_->value()));
  dslr_camera_model_edit_->setText(colmap["camera_model"].toString(dslr_camera_model_edit_->text()));
  dslr_camera_params_edit_->setText(colmap["camera_params"].toString(dslr_camera_params_edit_->text()));
  dslr_single_camera_check_->setChecked(colmap["single_camera"].toBool(dslr_single_camera_check_->isChecked()));
  matcher_combo_->setCurrentText(colmap["matcher"].toString(matcher_combo_->currentText()));
  vocab_tree_edit_->setText(colmap["vocab_tree"].toString(vocab_tree_edit_->text()));
  overwrite_database_check_->setChecked(colmap["overwrite_database"].toBool(overwrite_database_check_->isChecked()));
  model_index_spin_->setValue(colmap["model_index"].toInt(model_index_spin_->value()));

  const QJsonObject scale = config["scale"].toObject();
  sfm_model_subdir_edit_->setText(scale["sfm_model_subdir"].toString(sfm_model_subdir_edit_->text()));
  scaled_output_subdir_edit_->setText(scale["output_subdir"].toString(scaled_output_subdir_edit_->text()));
  cube_map_face_camera_id_spin_->setValue(scale["cube_map_camera_id"].toInt(cube_map_face_camera_id_spin_->value()));

  const QJsonObject pipeline = config["pipeline"].toObject();
  icp_max_distance_spin_->setValue(pipeline["icp_max_distance"].toDouble(icp_max_distance_spin_->value()));
  icp_max_iterations_spin_->setValue(pipeline["icp_max_iterations"].toInt(icp_max_iterations_spin_->value()));
  icp_convergence_threshold_edit_->setText(pipeline["icp_convergence_threshold"].toString(icp_convergence_threshold_edit_->text()));
  icp_scale_count_spin_->setValue(pipeline["icp_scale_count"].toInt(icp_scale_count_spin_->value()));
  normal_neighbor_count_spin_->setValue(pipeline["normal_neighbor_count"].toInt(normal_neighbor_count_spin_->value()));
  poisson_depth_spin_->setValue(pipeline["poisson_depth"].toInt(poisson_depth_spin_->value()));
  poisson_data_weight_spin_->setValue(pipeline["poisson_data_weight"].toDouble(poisson_data_weight_spin_->value()));
  poisson_colors_check_->setChecked(pipeline["poisson_colors"].toBool(poisson_colors_check_->isChecked()));
  poisson_density_check_->setChecked(pipeline["poisson_density"].toBool(poisson_density_check_->isChecked()));
  splat_distance_threshold_spin_->setValue(pipeline["splat_distance_threshold"].toDouble(splat_distance_threshold_spin_->value()));
  multi_res_output_subdir_edit_->setText(pipeline["multi_res_output_subdir"].toString(multi_res_output_subdir_edit_->text()));
  multi_res_save_check_->setChecked(pipeline["multi_res_save"].toBool(multi_res_save_check_->isChecked()));
  image_reg_max_iterations_spin_->setValue(pipeline["image_reg_max_iterations"].toInt(image_reg_max_iterations_spin_->value()));
  image_reg_initial_scale_spin_->setValue(pipeline["image_reg_initial_scale"].toDouble(image_reg_initial_scale_spin_->value()));
  image_reg_target_scale_spin_->setValue(pipeline["image_reg_target_scale"].toDouble(image_reg_target_scale_spin_->value()));
  image_reg_cache_observations_check_->setChecked(pipeline["image_reg_cache_observations"].toBool(image_reg_cache_observations_check_->isChecked()));

  const QJsonObject model = config["model"].toObject();
  model_path_edit_->setText(model["path"].toString(model_path_edit_->text()));
  model_image_base_edit_->setText(model["image_base"].toString(model_image_base_edit_->text()));
  UpdateDatasetDependentDefaults(false);
  ApplyPipelineBinPath(false);
  SaveSettings();
}

void MainWindow::UpdateDatasetDependentDefaults(bool overwrite_model_paths) {
  const QString dataset_dir = DatasetDir();
  const QDir dataset(dataset_dir);
  const QString scaled_model =
      dataset.filePath(scaled_output_subdir_edit_->text() + "/colmap_model");
  if (model_path_edit_ &&
      (overwrite_model_paths || model_path_edit_->text().trimmed().isEmpty())) {
    model_path_edit_->setText(Native(scaled_model));
  }
  if (model_image_base_edit_ &&
      (overwrite_model_paths || model_image_base_edit_->text().trimmed().isEmpty())) {
    model_image_base_edit_->setText(Native(dataset_dir));
  }
  if (ba_compare_tab_) {
    ba_compare_tab_->SetDefaultPaths(
        dataset_dir,
        multi_res_output_subdir_edit_->text(),
        cube_map_face_camera_id_spin_->value());
  }
}

void MainWindow::ApplyPipelineBinPath(bool overwrite_existing) {
  const auto fill = [&](QLineEdit* edit, const QString& executable_name) {
    if (!overwrite_existing && !edit->text().trimmed().isEmpty()) {
      return;
    }
    const QString resolved = ExecutableFromPipelineBin(executable_name);
    if (QFileInfo::exists(resolved)) {
      edit->setText(Native(resolved));
    }
  };

  fill(cube_map_renderer_exe_edit_, "CubeMapRenderer.exe");
  fill(scale_estimator_exe_edit_, "SfMScaleEstimator.exe");

  const QString colmap_from_bin = ExecutableFromPipelineBin("colmap.exe");
  if ((overwrite_existing || colmap_exe_edit_->text().trimmed().isEmpty()) &&
      QFileInfo::exists(colmap_from_bin)) {
    colmap_exe_edit_->setText(Native(colmap_from_bin));
  }
}

void MainWindow::BrowsePipelineBinPath() {
  const QString path = QFileDialog::getExistingDirectory(
      this, "Select dataset-pipeline bin directory", pipeline_bin_path_edit_->text());
  if (!path.isEmpty()) {
    pipeline_bin_path_edit_->setText(Native(path));
    ApplyPipelineBinPath(true);
    SaveSettings();
    Log(QString("Configured pipeline bin: %1").arg(Native(path)));
  }
}

void MainWindow::BrowseColmapExe() {
  const QString path = QFileDialog::getOpenFileName(
      this, "Select colmap.exe", QFileInfo(colmap_exe_edit_->text()).absolutePath(),
      "Executables (*.exe);;All files (*)");
  if (!path.isEmpty()) {
    colmap_exe_edit_->setText(QDir::toNativeSeparators(path));
    SaveSettings();
  }
}

void MainWindow::BrowseCubeMapRendererExe() {
  const QString path = QFileDialog::getOpenFileName(
      this, "Select CubeMapRenderer.exe",
      QFileInfo(cube_map_renderer_exe_edit_->text()).absolutePath(),
      "Executables (*.exe);;All files (*)");
  if (!path.isEmpty()) {
    cube_map_renderer_exe_edit_->setText(Native(path));
    SaveSettings();
  }
}

void MainWindow::BrowseScaleEstimatorExe() {
  const QString path = QFileDialog::getOpenFileName(
      this, "Select SfMScaleEstimator.exe",
      QFileInfo(scale_estimator_exe_edit_->text()).absolutePath(),
      "Executables (*.exe);;All files (*)");
  if (!path.isEmpty()) {
    scale_estimator_exe_edit_->setText(Native(path));
    SaveSettings();
  }
}

void MainWindow::BrowsePoissonReconExe() {
  const QString path = QFileDialog::getOpenFileName(
      this, "Select PoissonRecon.exe",
      QFileInfo(poisson_recon_exe_edit_->text()).absolutePath(),
      "Executables (*.exe);;All files (*)");
  if (!path.isEmpty()) {
    poisson_recon_exe_edit_->setText(Native(path));
    SaveSettings();
  }
}

void MainWindow::BrowseDatasetRoot() {
  const QString path = QFileDialog::getExistingDirectory(
      this, "Select dataset root", dataset_root_edit_->text());
  if (!path.isEmpty()) {
    dataset_root_edit_->setText(QDir::toNativeSeparators(path));
    ProjectPathsChanged();
  }
}

void MainWindow::BrowseDatasetSubdir() {
  const QString root_path = dataset_root_edit_->text().trimmed();
  const QString start_path =
      dataset_subdir_edit_->text().trimmed().isEmpty()
          ? root_path
          : QDir(root_path).filePath(dataset_subdir_edit_->text());
  const QString path = QFileDialog::getExistingDirectory(
      this, "Select dataset subdir", start_path);
  if (path.isEmpty()) {
    return;
  }

  const QDir root(root_path);
  QString subdir = root.relativeFilePath(path);
  if (subdir == "." || subdir.startsWith("..")) {
    dataset_root_edit_->setText(QDir::toNativeSeparators(path));
    dataset_subdir_edit_->clear();
  } else {
    dataset_subdir_edit_->setText(QDir::toNativeSeparators(subdir));
  }
  ProjectPathsChanged();
}

void MainWindow::ProjectPathsChanged() {
  SaveSettings();
  UpdateDatasetDependentDefaults(true);
  RefreshPipelineStatus();
  RefreshCubeMapPreview();
}

void MainWindow::BrowseModelPath() {
  const QString path = QFileDialog::getExistingDirectory(
      this, "Select COLMAP TXT model", model_path_edit_->text());
  if (!path.isEmpty()) {
    model_path_edit_->setText(QDir::toNativeSeparators(path));
    SaveSettings();
  }
}

void MainWindow::ExportConfig() {
  SaveSettings();
  const QString default_path = QDir(DatasetDir()).filePath("dataset_pipeline_studio.json");
  const QString path = QFileDialog::getSaveFileName(
      this, "Export Studio Config", default_path, "JSON files (*.json);;All files (*)");
  if (path.isEmpty()) {
    return;
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
    QMessageBox::warning(this, "Export Config",
                         QString("Cannot write %1").arg(Native(path)));
    return;
  }
  file.write(QJsonDocument(CurrentConfig()).toJson(QJsonDocument::Indented));
  Log(QString("Exported config: %1").arg(Native(path)));
}

void MainWindow::ImportConfig() {
  const QString path = QFileDialog::getOpenFileName(
      this, "Import Studio Config", DatasetDir(), "JSON files (*.json);;All files (*)");
  if (path.isEmpty()) {
    return;
  }

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QMessageBox::warning(this, "Import Config",
                         QString("Cannot read %1").arg(Native(path)));
    return;
  }
  QJsonParseError parse_error;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    QMessageBox::warning(this, "Import Config",
                         QString("Invalid JSON config: %1").arg(parse_error.errorString()));
    return;
  }

  ApplyConfig(document.object());
  Log(QString("Imported config: %1").arg(Native(path)));
}

void MainWindow::TestColmap() {
  SaveSettings();
  const QString colmap_exe = ResolveExecutable(colmap_exe_edit_->text(), "colmap.exe");
  if (!QFileInfo::exists(colmap_exe)) {
    QMessageBox::warning(this, "COLMAP", "colmap.exe does not exist.");
    return;
  }
  completion_model_path_.clear();
  completion_image_base_path_.clear();
  StartCommands({{"Test COLMAP", colmap_exe, {"help"}, QFileInfo(colmap_exe).absolutePath()}});
}

void MainWindow::RenderCubeMaps() {
  SaveSettings();
  QString error;
  QList<ToolCommand> commands = BuildCubeMapCommands(&error);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, "Cube Map Rendering", error);
    Log("ERROR: " + error);
    return;
  }
  completion_model_path_.clear();
  completion_image_base_path_.clear();
  StartCommands(commands);
}

void MainWindow::RunColmapSfm() {
  SaveSettings();
  QString dslr_list_path;
  QString cubemap_list_path;
  QString error;
  if (!PrepareColmapImageLists(&dslr_list_path, &cubemap_list_path, &error)) {
    QMessageBox::warning(this, "COLMAP SfM", error);
    Log("ERROR: " + error);
    return;
  }

  QList<ToolCommand> commands = BuildColmapSfmCommands(dslr_list_path, cubemap_list_path, &error);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, "COLMAP SfM", error);
    Log("ERROR: " + error);
    return;
  }
  completion_model_path_ = QDir(ColmapDir()).filePath("sparse_txt");
  completion_image_base_path_ = DatasetDir();
  StartCommands(commands);
}

void MainWindow::RunScaleEstimator() {
  SaveSettings();
  QString error;
  ToolCommand command = BuildScaleEstimatorCommand(&error);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, "SfM Scale Estimator", error);
    Log("ERROR: " + error);
    return;
  }
  completion_model_path_ =
      QDir(QDir(DatasetDir()).filePath(scaled_output_subdir_edit_->text())).filePath("colmap_model");
  completion_image_base_path_ = DatasetDir();
  StartCommands({command});
}

void MainWindow::RunICPScanAligner() {
  SaveSettings();
  QString error;
  ToolCommand command = BuildICPScanAlignerCommand(&error);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, "ICP Scan Aligner", error);
    Log("ERROR: " + error);
    return;
  }
  StartCommands({command});
}

void MainWindow::RunNormalEstimator() {
  SaveSettings();
  QString error;
  ToolCommand command = BuildNormalEstimatorCommand(&error);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, "Normal Estimator", error);
    Log("ERROR: " + error);
    return;
  }
  StartCommands({command});
}

void MainWindow::RunSplatCreator() {
  SaveSettings();
  QString error;
  ToolCommand command = BuildSplatCreatorCommand(&error);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, "Splat Creator", error);
    Log("ERROR: " + error);
    return;
  }
  StartCommands({command});
}

void MainWindow::RunPoissonReconstruction() {
  SaveSettings();
  QString error;
  ToolCommand command = BuildPoissonReconstructionCommand(&error);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, "Poisson Reconstruction", error);
    Log("ERROR: " + error);
    return;
  }
  StartCommands({command});
}

void MainWindow::RunMultiResPointCloud() {
  SaveSettings();
  QString error;
  ToolCommand command = BuildMultiResPointCloudCommand(&error);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, "Multi-Res Point Cloud", error);
    Log("ERROR: " + error);
    return;
  }
  StartCommands({command});
}

void MainWindow::RunImageRegistrator() {
  SaveSettings();
  QString error;
  ToolCommand command = BuildImageRegistratorCommand(&error);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, "ImageRegistrator", error);
    Log("ERROR: " + error);
    return;
  }
  completion_model_path_ = QDir(DatasetDir()).filePath("image_alignment_refined/scale_1_state");
  completion_image_base_path_ = DatasetDir();
  StartCommands({command});
}

void MainWindow::LaunchDatasetInspector() {
  SaveSettings();
  QString error;
  ToolCommand command = BuildDatasetInspectorCommand(&error);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, "DatasetInspector", error);
    Log("ERROR: " + error);
    return;
  }
  StartDetachedCommand(command);
}

void MainWindow::LaunchPointCloudEditor() {
  SaveSettings();
  QString error;
  ToolCommand command = BuildPointCloudEditorCommand(&error);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, "PointCloudEditor", error);
    Log("ERROR: " + error);
    return;
  }
  StartDetachedCommand(command);
}

void MainWindow::RefreshPipelineStatus() {
  const QString dataset_dir = DatasetDir();
  const QDir dataset(dataset_dir);
  QString text;
  QTextStream stream(&text);
  stream << "Dataset: " << Native(dataset_dir) << "\n\n";
  stream << ExistingPathLabel(dataset.filePath("scan_clean")) << " scan_clean\n";
  stream << ExistingPathLabel(dataset.filePath("cube_maps")) << " cube_maps\n";
  stream << ExistingPathLabel(dataset.filePath("colmap/sparse_txt/cameras.txt")) << " COLMAP sparse TXT cameras.txt\n";
  stream << ExistingPathLabel(dataset.filePath("colmap/sparse_txt/images.txt")) << " COLMAP sparse TXT images.txt\n";
  stream << ExistingPathLabel(dataset.filePath("colmap/sparse_txt/points3D.txt")) << " COLMAP sparse TXT points3D.txt\n";
  stream << ExistingPathLabel(dataset.filePath("sparse_reconstruction_scaled/colmap_model/cameras.txt")) << " scaled COLMAP model\n";
  stream << ExistingPathLabel(dataset.filePath("sparse_reconstruction_scaled/meshlab_project.mlp")) << " scaled MeshLab project\n";
  stream << ExistingPathLabel(dataset.filePath("scan_clean/scan_alignment.mlp")) << " ICP scan alignment\n";
  stream << ExistingPathLabel(dataset.filePath("surface_reconstruction/point_cloud_with_normals.ply")) << " point cloud with normals\n";
  stream << ExistingPathLabel(dataset.filePath("surface_reconstruction/surface.ply")) << " surface mesh\n";
  stream << ExistingPathLabel(dataset.filePath("surface_reconstruction/splats.ply")) << " splats\n";
  stream << ExistingPathLabel(dataset.filePath(multi_res_output_subdir_edit_->text())) << " multi-res point cloud cache\n";
  stream << ExistingPathLabel(dataset.filePath("image_alignment_refined/scale_1_state/cameras.txt")) << " refined image registration state\n";
  if (!stage_elapsed_seconds_.isEmpty()) {
    stream << "\nStage timings:\n";
    for (auto it = stage_elapsed_seconds_.constBegin();
         it != stage_elapsed_seconds_.constEnd();
         ++it) {
      stream << QString("  %1: %2 s\n").arg(it.key()).arg(it.value(), 0, 'f', 2);
    }
  }

  if (pipeline_status_text_) {
    pipeline_status_text_->setPlainText(text);
  }
}

void MainWindow::RefreshCubeMapPreview() {
  const QDir cube_dir(QDir(DatasetDir()).filePath("cube_maps"));
  const QStringList faces = {"front", "left", "back", "right", "up", "down"};

  if (cube_map_scan_combo_) {
    const QString previous = cube_map_scan_combo_->currentText();
    cube_map_scan_combo_->blockSignals(true);
    cube_map_scan_combo_->clear();
    QSet<QString> scan_names;
    for (const QFileInfo& file : cube_dir.entryInfoList({"scan*.ply.*.png"}, QDir::Files, QDir::Name)) {
      const QString name = file.fileName();
      const int suffix_start = name.lastIndexOf(".ply.");
      if (suffix_start > 0) {
        scan_names.insert(name.left(suffix_start + 4));
      }
    }
    QStringList sorted = scan_names.values();
    sorted.sort(Qt::CaseInsensitive);
    cube_map_scan_combo_->addItems(sorted);
    const int previous_index = cube_map_scan_combo_->findText(previous);
    if (previous_index >= 0) {
      cube_map_scan_combo_->setCurrentIndex(previous_index);
    }
    cube_map_scan_combo_->blockSignals(false);
  }

  const QString scan_name = cube_map_scan_combo_ ? cube_map_scan_combo_->currentText() : QString();
  for (int i = 0; i < faces.size(); ++i) {
    QLabel* label = cube_map_labels_[i];
    if (!label) {
      continue;
    }
    const QString path = cube_dir.filePath(scan_name + "." + faces[i] + ".png");
    QPixmap pixmap(path);
    if (scan_name.isEmpty() || pixmap.isNull()) {
      label->setPixmap(QPixmap());
      label->setText("missing");
      continue;
    }
    label->setText(QString());
    label->setPixmap(pixmap.scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }
}

void MainWindow::StopTask() {
  command_queue_.clear();
  completion_model_path_.clear();
  completion_image_base_path_.clear();
  if (process_.state() != QProcess::NotRunning) {
    Log("Stopping current task.");
    process_.kill();
  }
}

void MainWindow::LoadModelSummary() {
  SaveSettings();
  ColmapModelSummary summary =
      ReadColmapModelSummary(model_path_edit_->text(), model_image_base_edit_->text());
  model_summary_text_->setPlainText(FormatColmapModelSummary(summary));
  QString error;
  if (summary.valid && !sfm_model_view_->LoadModel(model_path_edit_->text(), &error)) {
    Log("ERROR: " + error);
  }
}

void MainWindow::AppendStdout() {
  Log(QString::fromLocal8Bit(process_.readAllStandardOutput()));
}

void MainWindow::AppendStderr() {
  Log(QString::fromLocal8Bit(process_.readAllStandardError()));
}

void MainWindow::ProcessFinished(int exit_code, QProcess::ExitStatus exit_status) {
  const bool ok = exit_status == QProcess::NormalExit && exit_code == 0;
  const double elapsed_seconds =
      current_stage_timer_.isValid() ? current_stage_timer_.elapsed() / 1000.0 : 0.0;
  stage_elapsed_seconds_[current_command_.title] = elapsed_seconds;
  Log(QString("Finished %1 with exit code %2")
          .arg(current_command_.title)
          .arg(exit_code));
  Log(QString("Stage wall time: %1 s").arg(elapsed_seconds, 0, 'f', 2));
  RefreshPipelineStatus();
  if (!ok) {
    command_queue_.clear();
    completion_model_path_.clear();
    completion_image_base_path_.clear();
    run_colmap_button_->setEnabled(true);
    stop_button_->setEnabled(false);
    return;
  }
  StartNextCommand();
}

void MainWindow::Log(const QString& text) {
  if (!log_text_) {
    return;
  }
  const QStringList lines =
      text.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts);
  if (lines.empty()) {
    return;
  }
  for (const QString& line : lines) {
    log_text_->appendPlainText(
        QString("[%1] %2")
            .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
            .arg(line));
  }
  tabs_->setCurrentWidget(log_text_->parentWidget());
}

void MainWindow::LogCommand(const ToolCommand& command) {
  QStringList quoted_args;
  for (const QString& arg : command.arguments) {
    quoted_args.push_back(arg.contains(' ') ? QString("\"%1\"").arg(arg) : arg);
  }
  Log(QString("Running %1").arg(command.title));
  Log(QString("%1 %2").arg(Native(command.program), quoted_args.join(" ")));
}

void MainWindow::StartCommands(const QList<ToolCommand>& commands) {
  if (process_.state() != QProcess::NotRunning) {
    QMessageBox::warning(this, "Task running", "A task is already running.");
    return;
  }
  command_queue_ = commands;
  run_colmap_button_->setEnabled(false);
  stop_button_->setEnabled(true);
  StartNextCommand();
}

void MainWindow::StartNextCommand() {
  if (command_queue_.empty()) {
    Log("Task sequence finished.");
    if (!completion_model_path_.isEmpty()) {
      model_path_edit_->setText(Native(completion_model_path_));
      model_image_base_edit_->setText(Native(completion_image_base_path_));
      SaveSettings();
    }
    RefreshPipelineStatus();
    completion_model_path_.clear();
    completion_image_base_path_.clear();
    run_colmap_button_->setEnabled(true);
    stop_button_->setEnabled(false);
    return;
  }
  current_command_ = command_queue_.takeFirst();
  process_.setWorkingDirectory(current_command_.working_directory);
  process_.setProgram(current_command_.program);
  process_.setArguments(current_command_.arguments);
  LogCommand(current_command_);
  current_stage_timer_.start();
  process_.start();
}

bool MainWindow::PrepareColmapImageLists(QString* dslr_list_path,
                                         QString* cubemap_list_path,
                                         QString* error) {
  const QString dataset_dir = DatasetDir();
  if (!QDir(dataset_dir).exists()) {
    *error = QString("Dataset directory does not exist: %1").arg(Native(dataset_dir));
    return false;
  }

  const QDir dataset(dataset_dir);
  const QString image_dir_path = dataset.filePath(image_dir_edit_->text());
  if (!QDir(image_dir_path).exists()) {
    *error = QString("Input image directory does not exist: %1").arg(Native(image_dir_path));
    return false;
  }
  const QString cubemap_dir_path = dataset.filePath("cube_maps");
  if (!QDir(cubemap_dir_path).exists()) {
    *error = QString("cube_maps directory does not exist: %1").arg(Native(cubemap_dir_path));
    return false;
  }

  const QString image_lists_dir = dataset.filePath("colmap/image_lists");
  QDir().mkpath(image_lists_dir);

  QStringList dslr_images;
  QDirIterator image_it(image_dir_path, ImageNameFilters(), QDir::Files, QDirIterator::Subdirectories);
  while (image_it.hasNext()) {
    dslr_images.push_back(QDir(dataset_dir).relativeFilePath(image_it.next()).replace("\\", "/"));
  }
  dslr_images.sort(Qt::CaseInsensitive);

  const QRegularExpression cubemap_pattern(
      "^scan\\d+\\.ply\\.(front|left|back|right|up|down)\\.png$");
  QStringList cubemap_images;
  QDirIterator cubemap_it(cubemap_dir_path, {"*.png"}, QDir::Files);
  while (cubemap_it.hasNext()) {
    const QString path = cubemap_it.next();
    if (cubemap_pattern.match(QFileInfo(path).fileName()).hasMatch()) {
      cubemap_images.push_back(QDir(dataset_dir).relativeFilePath(path).replace("\\", "/"));
    }
  }
  cubemap_images.sort(Qt::CaseInsensitive);

  if (dslr_images.empty()) {
    *error = QString("No input images found under: %1").arg(Native(image_dir_path));
    return false;
  }
  if (cubemap_images.empty()) {
    *error = QString("No cube map images found under: %1").arg(Native(cubemap_dir_path));
    return false;
  }

  *dslr_list_path = QDir(image_lists_dir).filePath("dslr_images.txt");
  *cubemap_list_path = QDir(image_lists_dir).filePath("cubemap_images.txt");
  const QString all_list_path = QDir(image_lists_dir).filePath("all_images.txt");

  auto write_lines = [](const QString& path, const QStringList& lines, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
      *error = QString("Cannot write %1").arg(Native(path));
      return false;
    }
    QTextStream stream(&file);
    for (const QString& line : lines) {
      stream << line << "\n";
    }
    return true;
  };

  if (!write_lines(*dslr_list_path, dslr_images, error) ||
      !write_lines(*cubemap_list_path, cubemap_images, error) ||
      !write_lines(all_list_path, dslr_images + cubemap_images, error)) {
    return false;
  }

  Log(QString("Prepared COLMAP image lists: %1 camera images, %2 cube map faces")
          .arg(dslr_images.size())
          .arg(cubemap_images.size()));
  return true;
}

QList<ToolCommand> MainWindow::BuildCubeMapCommands(QString* error) const {
  QList<ToolCommand> commands;
  const QString renderer_exe =
      ResolveExecutable(cube_map_renderer_exe_edit_->text(), "CubeMapRenderer.exe");
  if (!QFileInfo::exists(renderer_exe)) {
    *error = QString("CubeMapRenderer.exe does not exist: %1").arg(Native(renderer_exe));
    return commands;
  }

  const QString dataset_dir = DatasetDir();
  const QString scans_dir = QDir(dataset_dir).filePath("scan_clean");
  if (!QDir(scans_dir).exists()) {
    *error = QString("scan_clean does not exist: %1").arg(Native(scans_dir));
    return commands;
  }

  const QString cube_maps_dir = QDir(dataset_dir).filePath("cube_maps");
  QDir().mkpath(cube_maps_dir);

  QDir scan_dir(scans_dir);
  QStringList scan_files = scan_dir.entryList({"scan*.ply"}, QDir::Files, QDir::Name);
  if (scan_files.empty()) {
    *error = QString("No scan*.ply files found in: %1").arg(Native(scans_dir));
    return commands;
  }

  for (const QString& scan_file : scan_files) {
    const QString scan_path = scan_dir.filePath(scan_file);
    const QString output_base = QDir(cube_maps_dir).filePath(scan_file);
    commands.push_back({
        QString("Render cube map %1").arg(scan_file),
        renderer_exe,
        {"-c", scan_path,
         "-o", output_base,
         "--size", QString::number(cube_map_size_spin_->value())},
        dataset_dir});
  }
  return commands;
}

QList<ToolCommand> MainWindow::BuildColmapSfmCommands(const QString& dslr_list_path,
                                                      const QString& cubemap_list_path,
                                                      QString* error) const {
  QList<ToolCommand> commands;
  const QString colmap_exe = ResolveExecutable(colmap_exe_edit_->text(), "colmap.exe");
  if (!QFileInfo::exists(colmap_exe)) {
    *error = QString("colmap.exe does not exist: %1").arg(Native(colmap_exe));
    return commands;
  }

  const QString dataset_dir = DatasetDir();
  const QString colmap_dir = ColmapDir();
  const QString sparse_dir = QDir(colmap_dir).filePath("sparse");
  const QString sparse_text_dir = QDir(colmap_dir).filePath("sparse_txt");
  const QString database_path = QDir(colmap_dir).filePath("database.db");
  QDir().mkpath(sparse_dir);
  QDir().mkpath(sparse_text_dir);

  if (QFileInfo::exists(database_path)) {
    if (!overwrite_database_check_->isChecked()) {
      *error = QString("Database already exists: %1").arg(Native(database_path));
      return commands;
    }
    if (!QFile::remove(database_path)) {
      *error = QString("Cannot remove existing database: %1").arg(Native(database_path));
      return commands;
    }
  }

  const int cube_map_size = cube_map_size_spin_->value();
  const int cube_map_focal = cube_map_size / 2;
  const QString cube_map_params =
      QString("%1,%1,%1,%1").arg(cube_map_focal);

  QStringList dslr_args = {
      "feature_extractor",
      "--database_path", database_path,
      "--image_path", dataset_dir,
      "--image_list_path", dslr_list_path,
      "--ImageReader.camera_model", dslr_camera_model_edit_->text()};
  if (!dslr_camera_params_edit_->text().isEmpty()) {
    dslr_args << "--ImageReader.camera_params" << dslr_camera_params_edit_->text();
  }
  if (dslr_single_camera_check_->isChecked()) {
    dslr_args << "--ImageReader.single_camera" << "1";
  }
  commands.push_back({"Extract camera image features", colmap_exe, dslr_args, dataset_dir});

  commands.push_back({
      "Extract cube map features",
      colmap_exe,
      {"feature_extractor",
       "--database_path", database_path,
       "--image_path", dataset_dir,
       "--image_list_path", cubemap_list_path,
       "--ImageReader.camera_model", "PINHOLE",
       "--ImageReader.camera_params", cube_map_params,
       "--ImageReader.single_camera", "1",
       "--SiftExtraction.max_image_size", QString::number(cube_map_size)},
      dataset_dir});

  const QString matcher = matcher_combo_->currentText();
  if (matcher == "vocab_tree" && vocab_tree_edit_->text().isEmpty()) {
    *error = "vocab_tree matcher requires a vocab tree path.";
    return commands;
  }
  QStringList matcher_args = {matcher + "_matcher", "--database_path", database_path};
  if (matcher == "vocab_tree") {
    matcher_args << "--VocabTreeMatching.vocab_tree_path" << vocab_tree_edit_->text();
  }
  commands.push_back({"Match features", colmap_exe, matcher_args, dataset_dir});

  commands.push_back({
      "Run mapper",
      colmap_exe,
      {"mapper",
       "--database_path", database_path,
       "--image_path", dataset_dir,
       "--output_path", sparse_dir,
       "--Mapper.multiple_models", "0"},
      dataset_dir});

  const QString selected_sparse_model =
      QDir(sparse_dir).filePath(QString::number(model_index_spin_->value()));
  QDir text_dir(sparse_text_dir);
  for (const QFileInfo& entry :
       text_dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (entry.isDir()) {
      QDir(entry.absoluteFilePath()).removeRecursively();
    } else {
      QFile::remove(entry.absoluteFilePath());
    }
  }
  commands.push_back({
      "Convert sparse model to TXT",
      colmap_exe,
      {"model_converter",
       "--input_path", selected_sparse_model,
       "--output_path", sparse_text_dir,
       "--output_type", "TXT"},
      dataset_dir});

  return commands;
}

ToolCommand MainWindow::BuildScaleEstimatorCommand(QString* error) const {
  const QString scale_estimator_exe =
      ResolveExecutable(scale_estimator_exe_edit_->text(), "SfMScaleEstimator.exe");
  if (!QFileInfo::exists(scale_estimator_exe)) {
    *error = QString("SfMScaleEstimator.exe does not exist: %1").arg(Native(scale_estimator_exe));
    return {};
  }

  const QString dataset_dir = DatasetDir();
  const QString sfm_model_path = QDir(dataset_dir).filePath(sfm_model_subdir_edit_->text());
  const QString scans_path = QDir(dataset_dir).filePath("scan_clean");
  const QString output_path = QDir(dataset_dir).filePath(scaled_output_subdir_edit_->text());
  for (const QString& required : {
           QDir(sfm_model_path).filePath("cameras.txt"),
           QDir(sfm_model_path).filePath("images.txt"),
           QDir(sfm_model_path).filePath("points3D.txt")}) {
    if (!QFileInfo::exists(required)) {
      *error = QString("Required COLMAP text model file does not exist: %1")
                   .arg(Native(required));
      return {};
    }
  }
  if (!QDir(scans_path).exists()) {
    *error = QString("scan_clean does not exist: %1").arg(Native(scans_path));
    return {};
  }

  int cube_map_camera_id = cube_map_face_camera_id_spin_->value();
  if (cube_map_camera_id < 0) {
    cube_map_camera_id =
        FindCubeMapCameraId(QDir(sfm_model_path).filePath("cameras.txt"), error);
    if (!error->isEmpty()) {
      return {};
    }
  }

  QDir().mkpath(output_path);
  const QString scaled_model_path = QDir(output_path).filePath("colmap_model");
  if (QDir(scaled_model_path).exists()) {
    QDir scaled_dir(scaled_model_path);
    if (!scaled_dir.removeRecursively()) {
      *error = QString("Cannot remove previous scaled COLMAP model: %1")
                   .arg(Native(scaled_model_path));
      return {};
    }
  }

  return {
      "Run SfM scale estimator",
      scale_estimator_exe,
      {"-s", sfm_model_path,
       "-si", dataset_dir,
       "-i", scans_path,
       "-o", output_path,
       "--cube_map_face_camera_id", QString::number(cube_map_camera_id)},
      dataset_dir};
}

ToolCommand MainWindow::BuildICPScanAlignerCommand(QString* error) const {
  const QString exe = ResolveExecutable(QString(), "ICPScanAligner.exe");
  if (!QFileInfo::exists(exe)) {
    *error = QString("ICPScanAligner.exe does not exist: %1").arg(Native(exe));
    return {};
  }
  const QDir dataset(DatasetDir());
  const QString input = dataset.filePath("sparse_reconstruction_scaled/meshlab_project.mlp");
  const QString output = dataset.filePath("scan_clean/scan_alignment.mlp");
  if (!QFileInfo::exists(input)) {
    *error = QString("Missing scaled MeshLab project: %1").arg(Native(input));
    return {};
  }
  return {"Run ICP scan aligner",
          exe,
          {"-i", input,
           "-o", output,
           "-d", NumberArg(icp_max_distance_spin_->value()),
           "--max_iterations", QString::number(icp_max_iterations_spin_->value()),
           "--convergence_threshold", icp_convergence_threshold_edit_->text().trimmed(),
           "--number_of_scales", QString::number(icp_scale_count_spin_->value())},
          dataset.absolutePath()};
}

ToolCommand MainWindow::BuildNormalEstimatorCommand(QString* error) const {
  const QString exe = ResolveExecutable(QString(), "NormalEstimator.exe");
  if (!QFileInfo::exists(exe)) {
    *error = QString("NormalEstimator.exe does not exist: %1").arg(Native(exe));
    return {};
  }
  const QDir dataset(DatasetDir());
  const QString input = dataset.filePath("scan_clean/scan_alignment.mlp");
  const QString output_dir = dataset.filePath("surface_reconstruction");
  const QString output = QDir(output_dir).filePath("point_cloud_with_normals.ply");
  if (!QFileInfo::exists(input)) {
    *error = QString("Missing scan alignment: %1").arg(Native(input));
    return {};
  }
  QDir().mkpath(output_dir);
  return {"Run normal estimator",
          exe,
          {"-i", input,
           "-o", output,
           "--neighbor_count", QString::number(normal_neighbor_count_spin_->value())},
          dataset.absolutePath()};
}

ToolCommand MainWindow::BuildSplatCreatorCommand(QString* error) const {
  const QString exe = ResolveExecutable(QString(), "SplatCreator.exe");
  if (!QFileInfo::exists(exe)) {
    *error = QString("SplatCreator.exe does not exist: %1").arg(Native(exe));
    return {};
  }
  const QDir dataset(DatasetDir());
  const QDir surface_dir(dataset.filePath("surface_reconstruction"));
  const QString point_normals = surface_dir.filePath("point_cloud_with_normals.ply");
  const QString mesh = surface_dir.filePath("surface.ply");
  const QString output = surface_dir.filePath("splats.ply");
  if (!QFileInfo::exists(point_normals)) {
    *error = QString("Missing point cloud with normals: %1").arg(Native(point_normals));
    return {};
  }
  if (!QFileInfo::exists(mesh)) {
    *error = QString("Missing surface mesh: %1").arg(Native(mesh));
    return {};
  }
  return {"Run splat creator",
          exe,
          {"--point_normal_cloud_path", point_normals,
           "--mesh_path", mesh,
           "--output_path", output,
           "--distance_threshold", NumberArg(splat_distance_threshold_spin_->value())},
          dataset.absolutePath()};
}

ToolCommand MainWindow::BuildPoissonReconstructionCommand(QString* error) const {
  const QString exe = poisson_recon_exe_edit_->text().trimmed();
  if (!QFileInfo::exists(exe)) {
    *error = QString("PoissonRecon.exe does not exist: %1").arg(Native(exe));
    return {};
  }

  const QDir dataset(DatasetDir());
  const QDir surface_dir(dataset.filePath("surface_reconstruction"));
  const QString input = surface_dir.filePath("point_cloud_with_normals.ply");
  const QString output = surface_dir.filePath("surface.ply");
  if (!QFileInfo::exists(input)) {
    *error = QString("Missing point cloud with normals: %1").arg(Native(input));
    return {};
  }
  QDir().mkpath(surface_dir.absolutePath());
  if (QFileInfo::exists(output)) {
    QFile::remove(output);
  }

  QStringList args = {"--in", input,
                      "--out", output,
                      "--depth", QString::number(poisson_depth_spin_->value()),
                      "--data", NumberArg(poisson_data_weight_spin_->value())};
  if (poisson_colors_check_->isChecked()) {
    args << "--colors";
  }
  if (poisson_density_check_->isChecked()) {
    args << "--density";
  }

  return {"Run Poisson reconstruction", exe, args, dataset.absolutePath()};
}

ToolCommand MainWindow::BuildMultiResPointCloudCommand(QString* error) const {
  const QString exe = ResolveExecutable(QString(), "MultiResPointCloudBenchmark.exe");
  if (!QFileInfo::exists(exe)) {
    *error = QString("MultiResPointCloudBenchmark.exe does not exist: %1")
                 .arg(Native(exe));
    return {};
  }

  const QDir dataset(DatasetDir());
  const QString scan_alignment = dataset.filePath("scan_clean/scan_alignment.mlp");
  const QString image_base = dataset.absolutePath();
  const QString state =
      dataset.filePath(scaled_output_subdir_edit_->text() + "/colmap_model");
  const QString output_dir = dataset.filePath(multi_res_output_subdir_edit_->text());
  const QString mesh = dataset.filePath("surface_reconstruction/surface.ply");
  const QString splats = dataset.filePath("surface_reconstruction/splats.ply");

  if (!QFileInfo::exists(scan_alignment)) {
    *error = QString("Missing scan alignment: %1").arg(Native(scan_alignment));
    return {};
  }
  if (!QDir(state).exists()) {
    *error = QString("Missing COLMAP state directory: %1").arg(Native(state));
    return {};
  }
  if (!QDir(image_base).exists()) {
    *error = QString("Missing image base path: %1").arg(Native(image_base));
    return {};
  }
  if (multi_res_save_check_->isChecked()) {
    QDir().mkpath(output_dir);
  }

  QStringList args = {
      "--scan_alignment_path", scan_alignment,
      "--image_base_path", image_base,
      "--state_path", state};
  if (QFileInfo::exists(mesh)) {
    args << "--occlusion_mesh_path" << mesh;
  }
  if (QFileInfo::exists(splats)) {
    args << "--occlusion_splats_path" << splats;
  }

  int cube_map_camera_id = cube_map_face_camera_id_spin_->value();
  if (cube_map_camera_id < 0) {
    QString camera_error;
    cube_map_camera_id =
        FindCubeMapCameraId(QDir(state).filePath("cameras.txt"), &camera_error);
  }
  if (cube_map_camera_id >= 0) {
    args << "--camera_ids_to_ignore" << QString::number(cube_map_camera_id);
  }
  if (multi_res_save_check_->isChecked()) {
    args << "--multi_res_point_cloud_directory_path" << output_dir
         << "--save_multi_res_point_cloud" << "1";
  }

  return {"Build multi-res point cloud", exe, args, dataset.absolutePath()};
}

ToolCommand MainWindow::BuildImageRegistratorCommand(QString* error) const {
  const QString exe = ResolveExecutable(QString(), "ImageRegistrator.exe");
  if (!QFileInfo::exists(exe)) {
    *error = QString("ImageRegistrator.exe does not exist: %1").arg(Native(exe));
    return {};
  }

  const QDir dataset(DatasetDir());
  const QString scan_alignment = dataset.filePath("scan_clean/scan_alignment.mlp");
  const QString multi_res = dataset.filePath(multi_res_output_subdir_edit_->text());
  const QString image_base = dataset.absolutePath();
  const QString state = dataset.filePath(scaled_output_subdir_edit_->text() + "/colmap_model");
  const QString output_folder = dataset.filePath("image_alignment_refined");
  const QString observations_cache = dataset.filePath("observations_cache");
  const QString mesh = dataset.filePath("surface_reconstruction/surface.ply");
  const QString splats = dataset.filePath("surface_reconstruction/splats.ply");

  if (!QFileInfo::exists(scan_alignment)) {
    *error = QString("Missing scan alignment: %1").arg(Native(scan_alignment));
    return {};
  }
  if (!QDir(state).exists()) {
    *error = QString("Missing COLMAP state directory: %1").arg(Native(state));
    return {};
  }

  if (!QDir(multi_res).exists()) {
    *error = QString("Missing multi-res point cloud cache. Run Build Multi-Res "
                     "Point Cloud first: %1")
                 .arg(Native(multi_res));
    return {};
  }
  QDir().mkpath(output_folder);
  if (image_reg_cache_observations_check_->isChecked() &&
      QDir(observations_cache).exists()) {
    const QDir cache_dir(observations_cache);
    if (cache_dir.entryList({"*.observed_indices"}, QDir::Files).isEmpty()) {
      QDir(observations_cache).removeRecursively();
    }
  }

  QStringList args = {
      "--scan_alignment_path", scan_alignment,
      "--multi_res_point_cloud_directory_path", multi_res,
      "--image_base_path", image_base,
      "--state_path", state,
      "--output_folder_path", output_folder,
      "--observations_cache_path", observations_cache,
      "--max_iterations", QString::number(image_reg_max_iterations_spin_->value()),
      "--initial_scaling_factor", NumberArg(image_reg_initial_scale_spin_->value()),
      "--target_scaling_factor", NumberArg(image_reg_target_scale_spin_->value())};

  if (image_reg_cache_observations_check_->isChecked()) {
    args << "--cache_observations" << "1";
  }

  if (QFileInfo::exists(mesh)) {
    args << "--occlusion_mesh_path" << mesh;
  }
  if (QFileInfo::exists(splats)) {
    args << "--occlusion_splats_path" << splats;
  }

  int cube_map_camera_id = cube_map_face_camera_id_spin_->value();
  if (cube_map_camera_id < 0) {
    QString camera_error;
    cube_map_camera_id = FindCubeMapCameraId(QDir(state).filePath("cameras.txt"), &camera_error);
  }
  if (cube_map_camera_id >= 0) {
    args << "--camera_ids_to_ignore" << QString::number(cube_map_camera_id);
  }

  return {"Run ImageRegistrator", exe, args, dataset.absolutePath()};
}

ToolCommand MainWindow::BuildDatasetInspectorCommand(QString* error) const {
  const QString exe = ResolveExecutable(QString(), "DatasetInspector.exe");
  if (!QFileInfo::exists(exe)) {
    *error = QString("DatasetInspector.exe does not exist: %1").arg(Native(exe));
    return {};
  }
  const QDir dataset(DatasetDir());
  const QString scan_alignment = dataset.filePath("scan_clean/scan_alignment.mlp");
  const QString image_base = dataset.absolutePath();
  const QString state = dataset.filePath(scaled_output_subdir_edit_->text() + "/colmap_model");
  const QString mesh = dataset.filePath("surface_reconstruction/surface.ply");
  const QString splats = dataset.filePath("surface_reconstruction/splats.ply");
  const QString multi_res = dataset.filePath(multi_res_output_subdir_edit_->text());
  if (!QFileInfo::exists(scan_alignment)) {
    *error = QString("Missing scan alignment: %1").arg(Native(scan_alignment));
    return {};
  }
  if (!QDir(state).exists()) {
    *error = QString("Missing COLMAP state directory: %1").arg(Native(state));
    return {};
  }
  QDir().mkpath(multi_res);

  QStringList args = {
      "--scan_alignment_path", scan_alignment,
      "--image_base_path", image_base,
      "--state_path", state,
      "--multi_res_point_cloud_directory_path", multi_res};
  if (QFileInfo::exists(mesh)) {
    args << "--occlusion_mesh_path" << mesh;
  }
  if (QFileInfo::exists(splats)) {
    args << "--occlusion_splats_path" << splats;
  }

  int cube_map_camera_id = cube_map_face_camera_id_spin_->value();
  if (cube_map_camera_id < 0) {
    QString camera_error;
    cube_map_camera_id = FindCubeMapCameraId(QDir(state).filePath("cameras.txt"), &camera_error);
  }
  if (cube_map_camera_id >= 0) {
    args << "--camera_ids_to_ignore" << QString::number(cube_map_camera_id);
  }

  return {"Launch DatasetInspector", exe, args, dataset.absolutePath()};
}

ToolCommand MainWindow::BuildPointCloudEditorCommand(QString* error) const {
  const QString exe = ResolveExecutable(QString(), "PointCloudEditor.exe");
  if (!QFileInfo::exists(exe)) {
    *error = QString("PointCloudEditor.exe does not exist: %1").arg(Native(exe));
    return {};
  }
  return {"Launch PointCloudEditor", exe, {}, DatasetDir()};
}

void MainWindow::StartDetachedCommand(const ToolCommand& command) {
  LogCommand(command);
  QString error;
  if (!QProcess::startDetached(
          command.program, command.arguments, command.working_directory)) {
    Log(QString("ERROR: Failed to launch %1").arg(command.title));
  }
}

int MainWindow::FindCubeMapCameraId(const QString& cameras_path, QString* error) const {
  QFile file(cameras_path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    *error = QString("Cannot read %1").arg(Native(cameras_path));
    return -1;
  }

  QString last_camera_line;
  QTextStream stream(&file);
  while (!stream.atEnd()) {
    const QString line = stream.readLine().trimmed();
    if (!line.isEmpty() && !line.startsWith("#")) {
      last_camera_line = line;
    }
  }
  const QStringList parts =
      last_camera_line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
  if (parts.size() < 4) {
    *error = QString("The last camera entry in cameras.txt is malformed: %1")
                 .arg(last_camera_line);
    return -1;
  }
  if (parts[1] != "PINHOLE") {
    *error = QString("The last camera entry in cameras.txt is not PINHOLE: %1")
                 .arg(last_camera_line);
    return -1;
  }
  bool ok = false;
  const int camera_id = parts[0].toInt(&ok);
  if (!ok) {
    *error = QString("Cannot parse cube map camera id from: %1").arg(last_camera_line);
    return -1;
  }
  return camera_id;
}

QString MainWindow::ExistingPathLabel(const QString& path) const {
  const QFileInfo info(path);
  if (!info.exists()) {
    return "[ ]";
  }
  if (info.isDir()) {
    const QDir dir(path);
    const int count = dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).size();
    return QString("[x] (%1 items)").arg(count);
  }
  return QString("[x] (%1 KB)").arg(std::max<qint64>(1, info.size() / 1024));
}

QString MainWindow::ResolveExecutable(const QString& explicit_path,
                                      const QString& executable_name) const {
  if (!explicit_path.trimmed().isEmpty()) {
    return explicit_path;
  }
  const QString pipeline_bin_candidate = ExecutableFromPipelineBin(executable_name);
  if (QFileInfo::exists(pipeline_bin_candidate)) {
    return pipeline_bin_candidate;
  }
  const QString app_dir_candidate =
      QDir(QCoreApplication::applicationDirPath()).filePath(executable_name);
  if (QFileInfo::exists(app_dir_candidate)) {
    return app_dir_candidate;
  }
  return executable_name;
}

QString MainWindow::ExecutableFromPipelineBin(const QString& executable_name) const {
  const QString bin_path = pipeline_bin_path_edit_->text().trimmed();
  if (bin_path.isEmpty()) {
    return QString();
  }
  return QDir(bin_path).filePath(executable_name);
}

QString MainWindow::FindDatasetDirectory(const QString& root_path) const {
  const QDir root(root_path);
  if (QDir(root.filePath("scan_clean")).exists()) {
    return root.absolutePath();
  }
  QDirIterator it(root.absolutePath(), {"scan_clean"}, QDir::Dirs,
                  QDirIterator::Subdirectories);
  QString best;
  while (it.hasNext()) {
    it.next();
    const QString dataset_dir = QFileInfo(it.filePath()).absolutePath();
    if (best.isEmpty() || dataset_dir.size() < best.size() ||
        (dataset_dir.size() == best.size() && dataset_dir < best)) {
      best = dataset_dir;
    }
  }
  return best.isEmpty() ? root.absolutePath() : best;
}

QString MainWindow::DatasetDir() const {
  QDir root(dataset_root_edit_->text());
  if (dataset_subdir_edit_->text().isEmpty()) {
    return FindDatasetDirectory(root.absolutePath());
  }
  return root.filePath(dataset_subdir_edit_->text());
}

QString MainWindow::ColmapDir() const {
  return QDir(DatasetDir()).filePath("colmap");
}

}  // namespace studio
