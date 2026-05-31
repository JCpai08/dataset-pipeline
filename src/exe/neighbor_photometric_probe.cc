// Copyright 2017 ETH Zurich, Thomas Schops
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include <boost/filesystem.hpp>
#include <glog/logging.h>
#include <pcl/console/parse.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include "base/util.h"
#include "io/colmap_model.h"
#include "opt/interpolate_trilinear.h"
#include "opt/occlusion_geometry.h"
#include "opt/parameters.h"
#include "opt/problem.h"
#include "opt/util.h"
#include "opt/visibility_estimator.h"

namespace {

struct RunningStats {
  std::uint64_t count = 0;
  double sum_cloud = 0;
  double sum_image = 0;
  double sum_ratio = 0;
  double sum_cloud_sq = 0;
  double sum_image_sq = 0;
  double sum_cloud_image = 0;
  double min_image = std::numeric_limits<double>::infinity();
  double max_image = -std::numeric_limits<double>::infinity();
  std::vector<float> image_diff_samples;

  void Add(double cloud_diff, double image_diff) {
    ++count;
    sum_cloud += cloud_diff;
    sum_image += image_diff;
    if (cloud_diff > 0) {
      sum_ratio += image_diff / cloud_diff;
    }
    sum_cloud_sq += cloud_diff * cloud_diff;
    sum_image_sq += image_diff * image_diff;
    sum_cloud_image += cloud_diff * image_diff;
    min_image = std::min(min_image, image_diff);
    max_image = std::max(max_image, image_diff);
  }

  double MeanCloud() const { return count ? sum_cloud / count : 0; }
  double MeanImage() const { return count ? sum_image / count : 0; }
  double MeanRatio() const { return count ? sum_ratio / count : 0; }

  double Pearson() const {
    if (count < 2) {
      return 0;
    }
    const double n = static_cast<double>(count);
    const double cov = n * sum_cloud_image - sum_cloud * sum_image;
    const double var_cloud = n * sum_cloud_sq - sum_cloud * sum_cloud;
    const double var_image = n * sum_image_sq - sum_image * sum_image;
    if (var_cloud <= 0 || var_image <= 0) {
      return 0;
    }
    return cov / std::sqrt(var_cloud * var_image);
  }
};

double Quantile(std::vector<float>* values, double q) {
  if (values->empty()) {
    return 0;
  }
  std::sort(values->begin(), values->end());
  const double index = q * (values->size() - 1);
  const std::size_t lo = static_cast<std::size_t>(std::floor(index));
  const std::size_t hi = static_cast<std::size_t>(std::ceil(index));
  if (lo == hi) {
    return values->at(lo);
  }
  const double t = index - lo;
  return (1.0 - t) * values->at(lo) + t * values->at(hi);
}

void PrintUsage() {
  std::cout
      << "Usage: NeighborPhotometricProbe"
      << " --state_path <colmap_model_dir>"
      << " --image_base_path <image_dir>"
      << " --multi_res_point_cloud_directory_path <multi_res_dir>"
      << " [--scan_alignment_path <scan_alignment.mlp>]"
      << " [--occlusion_mesh_path <mesh.ply>]"
      << " [--occlusion_splats_path <splats.ply>]"
      << " [--ignore_occlusion 0]"
      << " [--output_csv_path <samples.csv>]"
      << " [--min_cloud_diff 5]"
      << " [--max_csv_rows 100000]"
      << " [--quantile_sample_count 200000]"
      << " [--max_images -1]"
      << " [--point_scale -1]"
      << " [--camera_ids_to_ignore <id0,id1>]"
      << std::endl;
}

bool InterpolateObservationIntensity(
    const opt::Image& image,
    const opt::Intrinsics& intrinsics,
    const opt::PointObservation& observation,
    float* intensity) {
  const int smaller_scale = observation.smaller_interpolation_scale();
  const int larger_scale = observation.larger_interpolation_scale();
  if (larger_scale < intrinsics.min_image_scale ||
      smaller_scale >= static_cast<int>(image.image_.size()) + intrinsics.min_image_scale) {
    return false;
  }

  const cv::Mat_<uint8_t>& smaller_image = image.image(smaller_scale, intrinsics);
  const cv::Mat_<uint8_t>& larger_image = image.image(larger_scale, intrinsics);
  const int smaller_ix = static_cast<int>(observation.smaller_scale_image_x);
  const int smaller_iy = static_cast<int>(observation.smaller_scale_image_y);
  const float larger_x = observation.larger_scale_image_x();
  const float larger_y = observation.larger_scale_image_y();
  const int larger_ix = static_cast<int>(larger_x);
  const int larger_iy = static_cast<int>(larger_y);

  if (observation.smaller_scale_image_x < 0.f ||
      observation.smaller_scale_image_y < 0.f ||
      smaller_ix >= smaller_image.cols - 1 ||
      smaller_iy >= smaller_image.rows - 1 ||
      larger_x < 0.f ||
      larger_y < 0.f ||
      larger_ix >= larger_image.cols - 1 ||
      larger_iy >= larger_image.rows - 1) {
    return false;
  }

  opt::InterpolateTrilinearNoCheck(
      smaller_image,
      larger_image,
      observation.smaller_scale_image_x,
      observation.smaller_scale_image_y,
      1 - (observation.image_scale - static_cast<int>(observation.image_scale)),
      intensity);
  return true;
}

void MaybeKeepQuantileSample(
    float value,
    std::size_t sample_limit,
    std::uint64_t seen_count,
    std::mt19937* rng,
    std::vector<float>* samples) {
  if (sample_limit == 0) {
    return;
  }
  if (samples->size() < sample_limit) {
    samples->push_back(value);
    return;
  }
  std::uniform_int_distribution<std::uint64_t> distribution(0, seen_count - 1);
  const std::uint64_t index = distribution(*rng);
  if (index < sample_limit) {
    samples->at(static_cast<std::size_t>(index)) = value;
  }
}

}  // namespace

int main(int argc, char** argv) {
  FLAGS_logtostderr = 1;
  google::InitGoogleLogging(argv[0]);
  pcl::console::setVerbosityLevel(pcl::console::L_ALWAYS);

  std::string state_path;
  std::string image_base_path;
  std::string multi_res_point_cloud_directory_path;
  std::string scan_alignment_path;
  std::string occlusion_mesh_path;
  std::string occlusion_splats_path;
  std::string output_csv_path;
  std::string camera_ids_to_ignore_string;
  bool ignore_occlusion = false;
  float min_cloud_diff = 5.f;
  int max_csv_rows = 100000;
  int quantile_sample_count = 200000;
  int max_images = -1;
  int selected_point_scale = -1;

  pcl::console::parse_argument(argc, argv, "--state_path", state_path);
  pcl::console::parse_argument(argc, argv, "--image_base_path", image_base_path);
  pcl::console::parse_argument(
      argc, argv, "--multi_res_point_cloud_directory_path",
      multi_res_point_cloud_directory_path);
  pcl::console::parse_argument(
      argc, argv, "--scan_alignment_path", scan_alignment_path);
  pcl::console::parse_argument(
      argc, argv, "--occlusion_mesh_path", occlusion_mesh_path);
  pcl::console::parse_argument(
      argc, argv, "--occlusion_splats_path", occlusion_splats_path);
  pcl::console::parse_argument(argc, argv, "--output_csv_path", output_csv_path);
  pcl::console::parse_argument(
      argc, argv, "--camera_ids_to_ignore", camera_ids_to_ignore_string);
  pcl::console::parse_argument(argc, argv, "--ignore_occlusion", ignore_occlusion);
  pcl::console::parse_argument(argc, argv, "--min_cloud_diff", min_cloud_diff);
  pcl::console::parse_argument(argc, argv, "--max_csv_rows", max_csv_rows);
  pcl::console::parse_argument(
      argc, argv, "--quantile_sample_count", quantile_sample_count);
  pcl::console::parse_argument(argc, argv, "--max_images", max_images);
  pcl::console::parse_argument(argc, argv, "--point_scale", selected_point_scale);
  opt::GlobalParameters().SetFromArguments(argc, argv);

  if (state_path.empty() ||
      image_base_path.empty() ||
      multi_res_point_cloud_directory_path.empty()) {
    PrintUsage();
    LOG(ERROR) << "Please specify all required paths.";
    return EXIT_FAILURE;
  }
  if (!ignore_occlusion &&
      scan_alignment_path.empty() &&
      occlusion_mesh_path.empty() &&
      occlusion_splats_path.empty()) {
    PrintUsage();
    LOG(ERROR) << "For visibility-aware probing, provide scan or occlusion geometry. "
               << "Use --ignore_occlusion 1 for a projection-only probe.";
    return EXIT_FAILURE;
  }

  std::unordered_set<int> camera_ids_to_ignore;
  for (const std::string& id_to_ignore :
       util::SplitStringIntoSet(',', camera_ids_to_ignore_string)) {
    if (!id_to_ignore.empty()) {
      camera_ids_to_ignore.insert(atoi(id_to_ignore.c_str()));
    }
  }

  std::shared_ptr<opt::OcclusionGeometry> occlusion_geometry(
      new opt::OcclusionGeometry());
  if (!ignore_occlusion) {
    if (!scan_alignment_path.empty() &&
        occlusion_mesh_path.empty() &&
        occlusion_splats_path.empty()) {
      LOG(INFO) << "Loading scans for splat occlusion geometry ...";
      std::vector<pcl::PointCloud<pcl::PointXYZRGB>::Ptr> colored_scans;
      if (!opt::LoadPointClouds(scan_alignment_path, &colored_scans)) {
        LOG(ERROR) << "Cannot load scan point clouds.";
        return EXIT_FAILURE;
      }
      std::size_t total_point_count = 0;
      for (const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& scan_cloud :
           colored_scans) {
        total_point_count += scan_cloud->size();
      }
      pcl::PointCloud<pcl::PointXYZ>::Ptr occlusion_point_cloud(
          new pcl::PointCloud<pcl::PointXYZ>());
      occlusion_point_cloud->resize(total_point_count);
      std::size_t occlusion_point_index = 0;
      for (const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& scan_cloud :
           colored_scans) {
        for (std::size_t scan_point_index = 0;
             scan_point_index < scan_cloud->size();
             ++scan_point_index) {
          occlusion_point_cloud->at(occlusion_point_index).getVector3fMap() =
              scan_cloud->at(scan_point_index).getVector3fMap();
          ++occlusion_point_index;
        }
      }
      occlusion_geometry->SetSplatPoints(occlusion_point_cloud);
    } else {
      if (!occlusion_mesh_path.empty()) {
        occlusion_geometry->AddMesh(occlusion_mesh_path);
      }
      if (!occlusion_splats_path.empty()) {
        occlusion_geometry->AddSplats(occlusion_splats_path);
      }
    }
  }

  opt::Problem problem(occlusion_geometry);
  if (!io::InitializeStateFromColmapModel(
          state_path, image_base_path, camera_ids_to_ignore, &problem)) {
    return EXIT_FAILURE;
  }
  problem.InitializeImages();
  problem.LoadImages(image_base_path);

  std::vector<std::vector<float>> multi_res_colors;
  if (!problem.LoadMultiResPointCloud(
          multi_res_point_cloud_directory_path, &multi_res_colors)) {
    LOG(ERROR) << "Cannot load multi-resolution point cloud from "
               << multi_res_point_cloud_directory_path;
    return EXIT_FAILURE;
  }

  std::ofstream csv_stream;
  std::uint64_t csv_rows = 0;
  if (!output_csv_path.empty()) {
    const boost::filesystem::path csv_parent =
        boost::filesystem::path(output_csv_path).parent_path();
    if (!csv_parent.empty()) {
      boost::filesystem::create_directories(csv_parent);
    }
    csv_stream.open(output_csv_path, std::ios::out);
    if (!csv_stream) {
      LOG(ERROR) << "Cannot open CSV output path: " << output_csv_path;
      return EXIT_FAILURE;
    }
    csv_stream
        << "image_id,point_scale,point_index,neighbor_slot,neighbor_point_index,"
        << "cloud_i_p,cloud_i_q,cloud_abs_diff,"
        << "img_i_p,img_i_q,img_abs_diff,"
        << "u_p,v_p,u_q,v_q,pixel_distance\n";
  }

  opt::VisibilityEstimator visibility_estimator(&problem);
  std::vector<RunningStats> scale_stats(problem.point_scale_count());
  RunningStats total_stats;
  std::vector<std::uint64_t> cloud_bin_counts(6, 0);
  std::vector<double> cloud_bin_image_sums(6, 0);
  std::mt19937 rng(0);

  std::vector<std::vector<std::size_t>> all_point_indices;
  if (ignore_occlusion) {
    all_point_indices.resize(problem.point_scale_count());
    for (int point_scale = 0; point_scale < problem.point_scale_count();
         ++point_scale) {
      if (selected_point_scale >= 0 && point_scale != selected_point_scale) {
        continue;
      }
      std::vector<std::size_t>& indices = all_point_indices[point_scale];
      indices.resize(problem.points()[point_scale]->size());
      for (std::size_t i = 0; i < indices.size(); ++i) {
        indices[i] = i;
      }
    }
  }

  int processed_image_count = 0;
  for (const auto& id_and_image : problem.images()) {
    if (max_images >= 0 && processed_image_count >= max_images) {
      break;
    }
    const opt::Image& image = id_and_image.second;
    const opt::Intrinsics& intrinsics =
        problem.intrinsics_list()[image.intrinsics_id];
    LOG(INFO) << "Processing image " << image.image_id << ": "
              << image.file_path;
    ++processed_image_count;

    opt::ScaleObservationsVectors observations_by_scale;
    if (ignore_occlusion) {
      visibility_estimator.AppendObservationsForIndexedPointsVisibleInImage(
          image, intrinsics, all_point_indices, 1, &observations_by_scale);
    } else {
      visibility_estimator.AppendObservationsForImage(
          image, intrinsics, 1, &observations_by_scale);
    }

    for (int point_scale = 0; point_scale < problem.point_scale_count();
         ++point_scale) {
      if (selected_point_scale >= 0 && point_scale != selected_point_scale) {
        continue;
      }
      const opt::ObservationsVector& observations =
          observations_by_scale[point_scale];
      if (observations.empty()) {
        continue;
      }

      std::vector<int> observation_index_by_point(
          problem.points()[point_scale]->size(), -1);
      std::vector<float> image_intensities(
          problem.points()[point_scale]->size(), -1.f);

      for (std::size_t observation_index = 0;
           observation_index < observations.size();
           ++observation_index) {
        const opt::PointObservation& observation =
            observations[observation_index];
        float intensity = -1.f;
        if (InterpolateObservationIntensity(
                image, intrinsics, observation, &intensity)) {
          observation_index_by_point[observation.point_index] =
              static_cast<int>(observation_index);
          image_intensities[observation.point_index] = intensity;
        }
      }

      for (const opt::PointObservation& observation : observations) {
        if (observation_index_by_point[observation.point_index] < 0) {
          continue;
        }
        const float point_image_intensity =
            image_intensities[observation.point_index];
        const float point_cloud_intensity =
            multi_res_colors[point_scale][observation.point_index];
        const float u_p =
            observation.image_x_at_scale(observation.smaller_interpolation_scale());
        const float v_p =
            observation.image_y_at_scale(observation.smaller_interpolation_scale());

        for (int k = 0; k < opt::GlobalParameters().point_neighbor_count; ++k) {
          const std::size_t neighbor_point_index =
              problem.neighbor_point_index(
                  point_scale, observation.point_index, k);
          if (neighbor_point_index >= observation_index_by_point.size() ||
              observation_index_by_point[neighbor_point_index] < 0) {
            continue;
          }

          const float neighbor_cloud_intensity =
              multi_res_colors[point_scale][neighbor_point_index];
          const float cloud_diff =
              std::abs(point_cloud_intensity - neighbor_cloud_intensity);
          if (cloud_diff < min_cloud_diff) {
            continue;
          }

          const int neighbor_observation_index =
              observation_index_by_point[neighbor_point_index];
          const opt::PointObservation& neighbor_observation =
              observations[neighbor_observation_index];
          const float neighbor_image_intensity =
              image_intensities[neighbor_point_index];
          const float image_diff =
              std::abs(point_image_intensity - neighbor_image_intensity);
          const float u_q =
              neighbor_observation.image_x_at_scale(
                  observation.smaller_interpolation_scale());
          const float v_q =
              neighbor_observation.image_y_at_scale(
                  observation.smaller_interpolation_scale());
          const float du = u_p - u_q;
          const float dv = v_p - v_q;
          const float pixel_distance = std::sqrt(du * du + dv * dv);

          RunningStats& stats = scale_stats[point_scale];
          stats.Add(cloud_diff, image_diff);
          total_stats.Add(cloud_diff, image_diff);
          MaybeKeepQuantileSample(
              image_diff,
              static_cast<std::size_t>(std::max(0, quantile_sample_count)),
              stats.count,
              &rng,
              &stats.image_diff_samples);
          MaybeKeepQuantileSample(
              image_diff,
              static_cast<std::size_t>(std::max(0, quantile_sample_count)),
              total_stats.count,
              &rng,
              &total_stats.image_diff_samples);

          int bin = 5;
          if (cloud_diff < 5.f) {
            bin = 0;
          } else if (cloud_diff < 10.f) {
            bin = 1;
          } else if (cloud_diff < 20.f) {
            bin = 2;
          } else if (cloud_diff < 40.f) {
            bin = 3;
          } else if (cloud_diff < 80.f) {
            bin = 4;
          }
          ++cloud_bin_counts[bin];
          cloud_bin_image_sums[bin] += image_diff;

          if (csv_stream &&
              (max_csv_rows < 0 ||
               csv_rows < static_cast<std::uint64_t>(max_csv_rows))) {
            csv_stream
                << image.image_id << ','
                << point_scale << ','
                << observation.point_index << ','
                << k << ','
                << neighbor_point_index << ','
                << point_cloud_intensity << ','
                << neighbor_cloud_intensity << ','
                << cloud_diff << ','
                << point_image_intensity << ','
                << neighbor_image_intensity << ','
                << image_diff << ','
                << u_p << ','
                << v_p << ','
                << u_q << ','
                << v_q << ','
                << pixel_distance << '\n';
            ++csv_rows;
          }
        }
      }
    }
  }

  std::cout << std::fixed << std::setprecision(4);
  std::cout
      << "scale,valid_edges,mean_cloud_diff,mean_image_diff,"
      << "median_image_diff,p90_image_diff,pearson,mean_ratio,min_image_diff,"
      << "max_image_diff\n";
  for (int point_scale = 0; point_scale < problem.point_scale_count();
       ++point_scale) {
    RunningStats& stats = scale_stats[point_scale];
    std::vector<float> quantiles = stats.image_diff_samples;
    std::cout
        << point_scale << ','
        << stats.count << ','
        << stats.MeanCloud() << ','
        << stats.MeanImage() << ','
        << Quantile(&quantiles, 0.5) << ','
        << Quantile(&quantiles, 0.9) << ','
        << stats.Pearson() << ','
        << stats.MeanRatio() << ','
        << (stats.count ? stats.min_image : 0) << ','
        << (stats.count ? stats.max_image : 0)
        << '\n';
  }

  std::vector<float> total_quantiles = total_stats.image_diff_samples;
  std::cout
      << "total," << total_stats.count << ','
      << total_stats.MeanCloud() << ','
      << total_stats.MeanImage() << ','
      << Quantile(&total_quantiles, 0.5) << ','
      << Quantile(&total_quantiles, 0.9) << ','
      << total_stats.Pearson() << ','
      << total_stats.MeanRatio() << ','
      << (total_stats.count ? total_stats.min_image : 0) << ','
      << (total_stats.count ? total_stats.max_image : 0)
      << '\n';

  const char* bin_names[] = {"0-5", "5-10", "10-20", "20-40", "40-80", "80+"};
  std::cout << "cloud_diff_bin,valid_edges,mean_image_diff\n";
  for (std::size_t i = 0; i < cloud_bin_counts.size(); ++i) {
    const double mean_image =
        cloud_bin_counts[i] ? cloud_bin_image_sums[i] / cloud_bin_counts[i] : 0;
    std::cout << bin_names[i] << ','
              << cloud_bin_counts[i] << ','
              << mean_image << '\n';
  }

  if (!output_csv_path.empty() && csv_stream) {
    LOG(INFO) << "Wrote " << csv_rows << " CSV rows to " << output_csv_path;
  }

  return EXIT_SUCCESS;
}
