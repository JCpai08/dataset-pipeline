#include "studio/colmap_model_summary.h"

#include <algorithm>
#include <limits>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

namespace studio {
namespace {

QStringList Tokens(const QString& line) {
  return line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
}

bool IsDataLine(const QString& line) {
  const QString trimmed = line.trimmed();
  return !trimmed.isEmpty() && !trimmed.startsWith("#");
}

bool OpenTextFile(const QString& path, QFile* file, QString* error) {
  file->setFileName(path);
  if (!file->open(QIODevice::ReadOnly | QIODevice::Text)) {
    *error = QString("Cannot read %1").arg(QDir::toNativeSeparators(path));
    return false;
  }
  return true;
}

}  // namespace

ColmapModelSummary ReadColmapModelSummary(const QString& model_path,
                                          const QString& image_base_path) {
  ColmapModelSummary summary;
  summary.model_path = QDir::cleanPath(model_path);

  const QDir model_dir(summary.model_path);
  const QString cameras_path = model_dir.filePath("cameras.txt");
  const QString images_path = model_dir.filePath("images.txt");
  const QString points_path = model_dir.filePath("points3D.txt");

  for (const QString& path : {cameras_path, images_path, points_path}) {
    if (!QFileInfo::exists(path)) {
      summary.error = QString("Required COLMAP TXT file is missing: %1")
                          .arg(QDir::toNativeSeparators(path));
      return summary;
    }
  }

  QFile cameras_file;
  if (!OpenTextFile(cameras_path, &cameras_file, &summary.error)) {
    return summary;
  }
  QTextStream cameras_stream(&cameras_file);
  QSet<QString> camera_models;
  while (!cameras_stream.atEnd()) {
    const QString line = cameras_stream.readLine();
    if (!IsDataLine(line)) {
      continue;
    }
    const QStringList parts = Tokens(line);
    if (parts.size() >= 2) {
      ++summary.camera_count;
      camera_models.insert(parts[1]);
    }
  }
  summary.camera_models = camera_models.values();
  std::sort(summary.camera_models.begin(), summary.camera_models.end());

  QFile images_file;
  if (!OpenTextFile(images_path, &images_file, &summary.error)) {
    return summary;
  }
  QTextStream images_stream(&images_file);
  QDir image_base_dir(image_base_path.isEmpty() ? model_path : image_base_path);
  while (!images_stream.atEnd()) {
    const QString image_line = images_stream.readLine();
    if (!IsDataLine(image_line)) {
      continue;
    }
    const QStringList image_parts = Tokens(image_line);
    if (image_parts.size() >= 10) {
      ++summary.image_count;
      ++summary.registered_image_count;
      const QString image_name = image_parts.mid(9).join(" ");
      const QString image_path = image_base_dir.filePath(image_name);
      if (!QFileInfo::exists(image_path) && summary.missing_files.size() < 10) {
        summary.missing_files.push_back(image_name);
      }
    }

    if (images_stream.atEnd()) {
      break;
    }
    const QString observations_line = images_stream.readLine();
    if (IsDataLine(observations_line)) {
      summary.observation_count += Tokens(observations_line).size() / 3;
    }
  }

  QFile points_file;
  if (!OpenTextFile(points_path, &points_file, &summary.error)) {
    return summary;
  }
  QTextStream points_stream(&points_file);
  bool have_bounds = false;
  double min_x = std::numeric_limits<double>::max();
  double min_y = std::numeric_limits<double>::max();
  double min_z = std::numeric_limits<double>::max();
  double max_x = std::numeric_limits<double>::lowest();
  double max_y = std::numeric_limits<double>::lowest();
  double max_z = std::numeric_limits<double>::lowest();
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
    const double x = parts[1].toDouble(&ok_x);
    const double y = parts[2].toDouble(&ok_y);
    const double z = parts[3].toDouble(&ok_z);
    if (!ok_x || !ok_y || !ok_z) {
      continue;
    }
    ++summary.point_count;
    have_bounds = true;
    min_x = std::min(min_x, x);
    min_y = std::min(min_y, y);
    min_z = std::min(min_z, z);
    max_x = std::max(max_x, x);
    max_y = std::max(max_y, y);
    max_z = std::max(max_z, z);
  }

  if (have_bounds) {
    summary.min_x = min_x;
    summary.min_y = min_y;
    summary.min_z = min_z;
    summary.max_x = max_x;
    summary.max_y = max_y;
    summary.max_z = max_z;
  }

  summary.valid = true;
  return summary;
}

QString FormatColmapModelSummary(const ColmapModelSummary& summary) {
  if (!summary.valid) {
    return summary.error;
  }

  QString text;
  QTextStream stream(&text);
  stream << "COLMAP model: " << QDir::toNativeSeparators(summary.model_path) << "\n";
  stream << "Cameras: " << summary.camera_count << "\n";
  stream << "Registered images: " << summary.registered_image_count << "\n";
  stream << "Sparse points: " << summary.point_count << "\n";
  stream << "2D observations: " << summary.observation_count << "\n";
  stream << "Camera models: " << summary.camera_models.join(", ") << "\n";
  if (summary.point_count > 0) {
    stream << "Point bounds:\n";
    stream << "  X [" << summary.min_x << ", " << summary.max_x << "]\n";
    stream << "  Y [" << summary.min_y << ", " << summary.max_y << "]\n";
    stream << "  Z [" << summary.min_z << ", " << summary.max_z << "]\n";
  }
  if (!summary.missing_files.empty()) {
    stream << "\nMissing referenced images (first "
           << summary.missing_files.size() << "):\n";
    for (const QString& file : summary.missing_files) {
      stream << "  " << file << "\n";
    }
  }
  return text;
}

}  // namespace studio
