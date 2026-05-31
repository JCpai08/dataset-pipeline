#pragma once

#include <QMainWindow>
#include <QElapsedTimer>
#include <QMap>
#include <QProcess>
#include <QJsonObject>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QSpinBox;
class QTabWidget;
class QTextEdit;

namespace studio {

class BACompareTab;
class SfmModelView;

struct ToolCommand {
  QString title;
  QString program;
  QStringList arguments;
  QString working_directory;
};

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

 private slots:
  void BrowsePipelineBinPath();
  void BrowseColmapExe();
  void BrowseCubeMapRendererExe();
  void BrowseScaleEstimatorExe();
  void BrowsePoissonReconExe();
  void BrowseDatasetRoot();
  void BrowseDatasetSubdir();
  void ProjectPathsChanged();
  void BrowseModelPath();
  void ExportConfig();
  void ImportConfig();
  void TestColmap();
  void RenderCubeMaps();
  void RunColmapSfm();
  void RunScaleEstimator();
  void RunICPScanAligner();
  void RunNormalEstimator();
  void RunSplatCreator();
  void RunPoissonReconstruction();
  void RunMultiResPointCloud();
  void RunImageRegistrator();
  void LaunchDatasetInspector();
  void LaunchPointCloudEditor();
  void RefreshPipelineStatus();
  void RefreshCubeMapPreview();
  void StopTask();
  void LoadModelSummary();
  void ProcessFinished(int exit_code, QProcess::ExitStatus exit_status);
  void AppendStdout();
  void AppendStderr();

 private:
  QWidget* CreateProjectTab();
  QWidget* CreatePipelineTab();
  QWidget* CreateColmapTab();
  QWidget* CreateCubeMapsTab();
  QWidget* CreateBACompareTab();
  QWidget* CreateModelTab();
  QWidget* CreateLogTab();

  void LoadSettings();
  void SaveSettings() const;
  QJsonObject CurrentConfig() const;
  void ApplyConfig(const QJsonObject& config);
  void UpdateDatasetDependentDefaults(bool overwrite_model_paths);
  void ApplyPipelineBinPath(bool overwrite_existing);
  void Log(const QString& text);
  void LogCommand(const ToolCommand& command);
  void StartCommands(const QList<ToolCommand>& commands);
  void StartNextCommand();
  bool PrepareColmapImageLists(QString* dslr_list_path,
                               QString* cubemap_list_path,
                               QString* error);
  QList<ToolCommand> BuildCubeMapCommands(QString* error) const;
  QList<ToolCommand> BuildColmapSfmCommands(const QString& dslr_list_path,
                                            const QString& cubemap_list_path,
                                            QString* error) const;
  ToolCommand BuildScaleEstimatorCommand(QString* error) const;
  ToolCommand BuildICPScanAlignerCommand(QString* error) const;
  ToolCommand BuildNormalEstimatorCommand(QString* error) const;
  ToolCommand BuildSplatCreatorCommand(QString* error) const;
  ToolCommand BuildPoissonReconstructionCommand(QString* error) const;
  ToolCommand BuildMultiResPointCloudCommand(QString* error) const;
  ToolCommand BuildImageRegistratorCommand(QString* error) const;
  ToolCommand BuildDatasetInspectorCommand(QString* error) const;
  ToolCommand BuildPointCloudEditorCommand(QString* error) const;
  void StartDetachedCommand(const ToolCommand& command);
  int FindCubeMapCameraId(const QString& cameras_path, QString* error) const;
  QString ExistingPathLabel(const QString& path) const;
  QString ResolveExecutable(const QString& explicit_path,
                            const QString& executable_name) const;
  QString ExecutableFromPipelineBin(const QString& executable_name) const;
  QString FindDatasetDirectory(const QString& root_path) const;
  QString DatasetDir() const;
  QString ColmapDir() const;

  QTabWidget* tabs_ = nullptr;
  QLineEdit* pipeline_bin_path_edit_ = nullptr;
  QLineEdit* colmap_exe_edit_ = nullptr;
  QLineEdit* cube_map_renderer_exe_edit_ = nullptr;
  QLineEdit* scale_estimator_exe_edit_ = nullptr;
  QLineEdit* poisson_recon_exe_edit_ = nullptr;
  QLineEdit* dataset_root_edit_ = nullptr;
  QLineEdit* dataset_subdir_edit_ = nullptr;
  QLineEdit* image_dir_edit_ = nullptr;
  QSpinBox* cube_map_size_spin_ = nullptr;
  QLineEdit* dslr_camera_model_edit_ = nullptr;
  QLineEdit* dslr_camera_params_edit_ = nullptr;
  QCheckBox* dslr_single_camera_check_ = nullptr;
  QComboBox* matcher_combo_ = nullptr;
  QLineEdit* vocab_tree_edit_ = nullptr;
  QCheckBox* overwrite_database_check_ = nullptr;
  QSpinBox* model_index_spin_ = nullptr;
  QSpinBox* cube_map_face_camera_id_spin_ = nullptr;
  QLineEdit* sfm_model_subdir_edit_ = nullptr;
  QLineEdit* scaled_output_subdir_edit_ = nullptr;
  QDoubleSpinBox* icp_max_distance_spin_ = nullptr;
  QSpinBox* icp_max_iterations_spin_ = nullptr;
  QLineEdit* icp_convergence_threshold_edit_ = nullptr;
  QSpinBox* icp_scale_count_spin_ = nullptr;
  QSpinBox* normal_neighbor_count_spin_ = nullptr;
  QSpinBox* poisson_depth_spin_ = nullptr;
  QDoubleSpinBox* poisson_data_weight_spin_ = nullptr;
  QCheckBox* poisson_colors_check_ = nullptr;
  QCheckBox* poisson_density_check_ = nullptr;
  QDoubleSpinBox* splat_distance_threshold_spin_ = nullptr;
  QLineEdit* multi_res_output_subdir_edit_ = nullptr;
  QCheckBox* multi_res_save_check_ = nullptr;
  QSpinBox* image_reg_max_iterations_spin_ = nullptr;
  QDoubleSpinBox* image_reg_initial_scale_spin_ = nullptr;
  QDoubleSpinBox* image_reg_target_scale_spin_ = nullptr;
  QCheckBox* image_reg_cache_observations_check_ = nullptr;
  QLineEdit* model_path_edit_ = nullptr;
  QLineEdit* model_image_base_edit_ = nullptr;
  QTextEdit* model_summary_text_ = nullptr;
  SfmModelView* sfm_model_view_ = nullptr;
  QTextEdit* pipeline_status_text_ = nullptr;
  QComboBox* cube_map_scan_combo_ = nullptr;
  BACompareTab* ba_compare_tab_ = nullptr;
  QLabel* cube_map_labels_[6] = {};
  QPlainTextEdit* log_text_ = nullptr;
  QPushButton* run_colmap_button_ = nullptr;
  QPushButton* stop_button_ = nullptr;

  QProcess process_;
  QList<ToolCommand> command_queue_;
  ToolCommand current_command_;
  QElapsedTimer current_stage_timer_;
  QMap<QString, double> stage_elapsed_seconds_;
  QString completion_model_path_;
  QString completion_image_base_path_;
};

}  // namespace studio
