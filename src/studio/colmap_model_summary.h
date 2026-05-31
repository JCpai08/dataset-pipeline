#pragma once

#include <QString>
#include <QStringList>

namespace studio {

struct ColmapModelSummary {
  bool valid = false;
  QString error;
  QString model_path;
  int camera_count = 0;
  int image_count = 0;
  int registered_image_count = 0;
  int point_count = 0;
  int observation_count = 0;
  QStringList camera_models;
  QStringList missing_files;
  double min_x = 0.0;
  double min_y = 0.0;
  double min_z = 0.0;
  double max_x = 0.0;
  double max_y = 0.0;
  double max_z = 0.0;
};

ColmapModelSummary ReadColmapModelSummary(const QString& model_path,
                                          const QString& image_base_path);

QString FormatColmapModelSummary(const ColmapModelSummary& summary);

}  // namespace studio
