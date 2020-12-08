#include <gtest/gtest.h>

#include <opencv2/highgui.hpp>

#include "core/file_utils.hpp"
#include "viz/visualize_matches.hpp"
#include "vo/optimization.hpp"
#include "vo/point_detector.hpp"
#include "vo/feature_matching.hpp"


using namespace bm::core;
using namespace bm::vo;
using namespace bm::viz;


TEST(VOTest, TestSeq01)
{
  const double max_epipolar_dist = 5.0;
  const double min_distance_ratio = 0.9;
  const double min_error = 1e-7;
  const double min_error_delta = 1e-9;
  const int max_iters = 20;

  const std::string data_path = "/home/milo/datasets/Unity3D/farmsim/01";
  const std::string lpath = "image_0";
  const std::string rpath = "image_1";

  std::vector<std::string> filenames_l, filenames_r;
  FilenamesInDirectory(Join(data_path, lpath), filenames_l, true);
  FilenamesInDirectory(Join(data_path, rpath), filenames_r, true);

  for (const auto& p : filenames_l) {
    std::cout << p << std::endl;
  }

  PointDetector::Options opt;
  PointDetector detector(opt);

  const PinholeCamera camera_model(415.876509, 415.876509, 376.0, 240.0, 480, 752);
  const StereoCamera stereo_cam(camera_model, camera_model, 0.2);

  assert(filenames_l.size() == filenames_r.size());

  std::vector<cv::KeyPoint> kpl_prev, kpr_prev;
  std::vector<double> disp_prev;
  cv::Mat orbl_prev, orbr_prev;

  Matrix4d T_prev = Matrix4d::Identity();
  Matrix4d T_curr = Matrix4d::Identity();

  for (int t = 0; t < filenames_l.size(); ++t) {
    printf("-----------------------------------FRAME #%d-------------------------------------\n", t);
    const Image1b iml = cv::imread(filenames_l.at(t), cv::IMREAD_GRAYSCALE);
    const Image1b imr = cv::imread(filenames_r.at(t), cv::IMREAD_GRAYSCALE);
    Image3b drawleft = cv::imread(filenames_l.at(t), cv::IMREAD_COLOR);
    Image3b drawright = cv::imread(filenames_r.at(t), cv::IMREAD_COLOR);

    std::cout << "---------------------------- STEREO MATCHING ---------------------------------\n";
    std::vector<cv::KeyPoint> kpl, kpr;
    cv::Mat orbl, orbr;

    const int npl = detector.Detect(iml, kpl, orbl);
    const int npr = detector.Detect(imr, kpr, orbr);
    printf("Detected %d|%d keypoints in left|right images\n", npl, npr);

    std::vector<int> matches_lr;
    const int Np_stereo = StereoMatchPoints(kpl, orbl, kpr, orbr, stereo_cam, max_epipolar_dist, min_distance_ratio, matches_lr);
    printf("Matched %d features from left to right\n", Np_stereo);

    const auto& dmatches = ConvertToDMatch(matches_lr);
    cv::Mat draw;
    cv::drawMatches(iml, kpl, imr, kpr, dmatches, draw);
    cv::imshow("matches", draw);
    cv::waitKey(0);

    std::cout << "----------------------------TEMPORAL MATCHING --------------------------------\n";
    if (t > 0) {
      if (kpl_prev.empty() || kpr_prev.empty()) {
        throw std::runtime_error("No keypoints for previous frame!");
      }

      assert(kpl_prev.size() == orbl_prev.rows && kpr_prev.size() == orbr_prev.rows);

      std::vector<int> matches_01;
      const int Np_temporal = TemporalMatchPoints(kpl_prev, orbl_prev, kpl, orbl, stereo_cam, min_distance_ratio, matches_01);
      printf("Matched %d temporal features (left)\n", Np_temporal);

      assert(matches_01.size() == kpl_prev.size());

      // 3D landmark points in the Camera_0 frame.
      std::vector<Vector3d> P0_list;
      std::vector<Vector2d> p1_list;
      std::vector<double> p1_sigma_list;
      for (int j0 = 0; j0 < matches_01.size(); ++j0) {
        if (matches_01.at(j0) < 0) { continue; }

        const Vector2d pl0(kpl_prev.at(j0).pt.x, kpl_prev.at(j0).pt.y);
        // const Vector2d pr0(kpr_prev.at(j).pt.x, kpr_prev.at(j).pt.y);

        const int j1 = matches_01.at(j0);
        const Vector2d pl1(kpl.at(j1).pt.x, kpl.at(j1).pt.y);
        // const double disp = std::fabs(pl0.x() - pr0.x());
        const double disp = disp_prev.at(j0);
        const double depth = camera_model.fx() * stereo_cam.Baseline() / std::max(1e-3, disp);
        P0_list.emplace_back(camera_model.Backproject(pl0, depth));
        p1_list.emplace_back(pl1);
        p1_sigma_list.emplace_back(2.0);
      }

      Matrix4d T_01;
      Matrix6d C_01;
      double error;

      const int lm_iters = OptimizePoseLevenbergMarquardt(
          P0_list, p1_list, p1_sigma_list, stereo_cam, T_01, C_01, error,
          max_iters, min_error, min_error_delta);

      const Matrix4d tmp = T_curr;
      T_curr = T_01.inverse() * T_prev;
      T_prev = tmp;
      std::cout << "Current pose:\n" << T_curr << std::endl;
    }

    // std::cout << "before resize" << std::endl;

    kpl_prev.resize(Np_stereo);
    kpr_prev.resize(Np_stereo);
    orbl_prev = cv::Mat(Np_stereo, orbl.cols, orbl.type());
    orbr_prev = cv::Mat(Np_stereo, orbr.cols, orbr.type());
    disp_prev.resize(Np_stereo);

    // std::cout << "after resize" << std::endl;

    // Only store the triangulated points.
    int match_ctr = 0;
    for (int j = 0; j < matches_lr.size(); ++j) {
      if (matches_lr.at(j) < 0) {
        continue;
      }
      // printf("j=%d match_ctr=%d kpl.size()=%zu kpr.size()=%zu Np_stereo=d\n", j, match_ctr, kpl.size(), kpr.size(), Np_stereo);

      const cv::KeyPoint& kplj = kpl.at(j);
      const cv::KeyPoint& kprj = kpr.at(matches_lr.at(j));

      const Vector2d pl(kpl.at(j).pt.x, kpl.at(j).pt.y);
      const Vector2d pr(kpr.at(matches_lr.at(j)).pt.y, kpr.at(matches_lr.at(j)).pt.y);

      disp_prev.at(match_ctr) = std::fabs(kplj.pt.x - kprj.pt.x);
      kpl_prev.at(match_ctr) = kplj;
      kpr_prev.at(match_ctr) = kprj;
      orbl_prev.row(match_ctr) = orbl.row(j);
      orbr_prev.row(match_ctr) = orbr.row(matches_lr.at(j));

      ++match_ctr;
    }
  }
}
