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

#include <chrono>
#include <iostream>
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
#include "opt/occlusion_geometry.h"
#include "opt/parameters.h"
#include "opt/problem.h"
#include "opt/util.h"
#include "opt/visibility_estimator.h"

namespace {

using Clock = std::chrono::steady_clock;

double SecondsSince(const Clock::time_point& start_time) {
  return std::chrono::duration<double>(Clock::now() - start_time).count();
}

void PrintUsage() {
  std::cout
      << "Usage: MultiResPointCloudBenchmark"
      << " --scan_alignment_path <scan_alignment.mlp>"
      << " --image_base_path <image_dir>"
      << " --state_path <colmap_model_dir>"
      << " [--occlusion_mesh_path <mesh.ply>]"
      << " [--occlusion_splats_path <splats.ply>]"
      << " [--camera_ids_to_ignore <id0,id1>]"
      << " [--multi_res_point_cloud_directory_path <output_dir>]"
      << " [--save_multi_res_point_cloud 1]"
      << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
  FLAGS_logtostderr = 1;
  google::InitGoogleLogging(argv[0]);
  pcl::console::setVerbosityLevel(pcl::console::L_ALWAYS);

  std::string scan_alignment_path;
  pcl::console::parse_argument(
      argc, argv, "--scan_alignment_path", scan_alignment_path);

  std::string image_base_path;
  pcl::console::parse_argument(
      argc, argv, "--image_base_path", image_base_path);

  std::string state_path;
  pcl::console::parse_argument(argc, argv, "--state_path", state_path);

  std::string occlusion_mesh_path;
  pcl::console::parse_argument(
      argc, argv, "--occlusion_mesh_path", occlusion_mesh_path);

  std::string occlusion_splats_path;
  pcl::console::parse_argument(
      argc, argv, "--occlusion_splats_path", occlusion_splats_path);

  std::string multi_res_point_cloud_directory_path;
  pcl::console::parse_argument(
      argc, argv, "--multi_res_point_cloud_directory_path",
      multi_res_point_cloud_directory_path);

  bool save_multi_res_point_cloud = false;
  pcl::console::parse_argument(
      argc, argv, "--save_multi_res_point_cloud",
      save_multi_res_point_cloud);

  std::string camera_ids_to_ignore_string;
  pcl::console::parse_argument(
      argc, argv, "--camera_ids_to_ignore", camera_ids_to_ignore_string);
  std::unordered_set<std::string> camera_ids_to_ignore_split =
      util::SplitStringIntoSet(',', camera_ids_to_ignore_string);
  std::unordered_set<int> camera_ids_to_ignore;
  for (const std::string& id_to_ignore : camera_ids_to_ignore_split) {
    if (!id_to_ignore.empty()) {
      camera_ids_to_ignore.insert(atoi(id_to_ignore.c_str()));
    }
  }

  opt::GlobalParameters().SetFromArguments(argc, argv);

  if (scan_alignment_path.empty() ||
      image_base_path.empty() ||
      state_path.empty()) {
    PrintUsage();
    LOG(ERROR) << "Please specify all required paths.";
    return EXIT_FAILURE;
  }
  if (save_multi_res_point_cloud &&
      multi_res_point_cloud_directory_path.empty()) {
    LOG(ERROR) << "--save_multi_res_point_cloud requires "
               << "--multi_res_point_cloud_directory_path.";
    return EXIT_FAILURE;
  }

  const Clock::time_point total_start_time = Clock::now();

  LOG(INFO) << "Loading scan point clouds ...";
  const Clock::time_point load_scans_start_time = Clock::now();
  std::vector<pcl::PointCloud<pcl::PointXYZRGB>::Ptr> colored_scans;
  if (!opt::LoadPointClouds(scan_alignment_path, &colored_scans)) {
    LOG(ERROR) << "Cannot load scan point clouds.";
    return EXIT_FAILURE;
  }
  std::size_t total_input_point_count = 0;
  for (const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& scan : colored_scans) {
    total_input_point_count += scan->size();
  }
  LOG(INFO) << "Loaded " << colored_scans.size() << " scans with "
            << total_input_point_count << " points in "
            << SecondsSince(load_scans_start_time) << " s.";

  pcl::PointCloud<pcl::PointXYZ>::Ptr occlusion_point_cloud(
      new pcl::PointCloud<pcl::PointXYZ>());
  occlusion_point_cloud->resize(total_input_point_count);
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

  LOG(INFO) << "Creating occlusion geometry ...";
  const Clock::time_point occlusion_start_time = Clock::now();
  std::shared_ptr<opt::OcclusionGeometry> occlusion_geometry(
      new opt::OcclusionGeometry());
  if (occlusion_mesh_path.empty() && occlusion_splats_path.empty()) {
    LOG(WARNING) << "No occlusion geometry specified, using 2D splats from "
                 << "the scan point cloud.";
    occlusion_geometry->SetSplatPoints(occlusion_point_cloud);
  } else {
    if (!occlusion_mesh_path.empty()) {
      occlusion_geometry->AddMesh(occlusion_mesh_path);
    }
    if (!occlusion_splats_path.empty()) {
      occlusion_geometry->AddSplats(occlusion_splats_path);
    }
  }
  LOG(INFO) << "Created occlusion geometry in "
            << SecondsSince(occlusion_start_time) << " s.";

  opt::Problem problem(occlusion_geometry);

  LOG(INFO) << "Loading COLMAP state ...";
  const Clock::time_point state_start_time = Clock::now();
  if (!io::InitializeStateFromColmapModel(
          state_path, image_base_path, camera_ids_to_ignore, &problem)) {
    return EXIT_FAILURE;
  }
  LOG(INFO) << "Loaded " << problem.intrinsics_list().size()
            << " intrinsics and " << problem.images().size()
            << " images in " << SecondsSince(state_start_time) << " s.";

  LOG(INFO) << "Initializing image pyramids ...";
  const Clock::time_point image_start_time = Clock::now();
  problem.InitializeImages();
  problem.LoadImages(image_base_path);
  LOG(INFO) << "Initialized and loaded images in "
            << SecondsSince(image_start_time) << " s.";

  LOG(INFO) << "Computing multi-res point cloud ...";
  const Clock::time_point compute_start_time = Clock::now();
  opt::VisibilityEstimator visibility_estimator(&problem);
  std::vector<std::vector<float>> multi_res_colors;
  problem.ComputeMultiResPointCloud(
      colored_scans, visibility_estimator, &multi_res_colors);
  const double compute_seconds = SecondsSince(compute_start_time);
  LOG(INFO) << "Computed multi-res point cloud in "
            << compute_seconds << " s.";

  std::size_t total_output_point_count = 0;
  for (int point_scale = 0;
       point_scale < static_cast<int>(problem.points().size());
       ++point_scale) {
    const std::size_t point_count = problem.points()[point_scale]->size();
    total_output_point_count += point_count;
    LOG(INFO) << "Scale " << point_scale
              << ": radius " << problem.point_radius(point_scale)
              << ", points " << point_count;
  }
  LOG(INFO) << "Output scale count: " << problem.points().size();
  LOG(INFO) << "Total output points across scales: "
            << total_output_point_count;

  if (save_multi_res_point_cloud) {
    LOG(INFO) << "Saving multi-res point cloud ...";
    const Clock::time_point save_start_time = Clock::now();
    boost::filesystem::create_directories(
        multi_res_point_cloud_directory_path);
    problem.SaveMultiResPointCloud(
        multi_res_point_cloud_directory_path, multi_res_colors, false);
    LOG(INFO) << "Saved multi-res point cloud in "
              << SecondsSince(save_start_time) << " s.";
  }

  LOG(INFO) << "Total benchmark time: "
            << SecondsSince(total_start_time) << " s.";
  LOG(INFO) << "Pure ComputeMultiResPointCloud time: "
            << compute_seconds << " s.";

  return EXIT_SUCCESS;
}
