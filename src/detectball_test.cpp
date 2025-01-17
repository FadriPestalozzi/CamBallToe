///////////////////////////////////////////////////////////////////////////
//
// use below function to detect position and distance of a ball to ZED 2 stereo
// camera
//
// Copyright (c) 2021, STEREOLABS.
//
// All rights reserved.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////

// ----> Includes
#include <iostream>
#include <sstream>
#include <string>

#include "videocapture.hpp"

// OpenCV includes
#include <opencv2/opencv.hpp>

//#undef HAVE_OPENCV_VIZ // Uncomment if cannot use Viz3D for point cloud
// rendering

#ifdef HAVE_OPENCV_VIZ
#include <opencv2/viz.hpp>
#include <opencv2/viz/viz3d.hpp>
#endif

// Sample includes
#include "calibration.hpp"
#include "ocv_display.hpp"
#include "stereo.hpp"
#include "stopwatch.hpp"
// <---- Includes

#define USE_OCV_TAPI // Comment to use "normal" cv::Mat instead of CV::UMat
#define USE_HALF_SIZE_DISP // Comment to compute depth matching on full image
                           // frames

int main(int argc, char *argv[]) {
  // ----> Silence unused warning
  (void)argc;
  (void)argv;
  // <---- Silence unused warning

  sl_oc::VERBOSITY verbose = sl_oc::VERBOSITY::INFO;

  // ----> Set Video parameters
  sl_oc::video::VideoParams params;
#ifdef EMBEDDED_ARM
  params.res = sl_oc::video::RESOLUTION::VGA;
#else
  params.res = sl_oc::video::RESOLUTION::HD720;
#endif
  params.fps = sl_oc::video::FPS::FPS_30;
  params.verbose = verbose;
  // <---- Set Video parameters

  // ----> Create Video Capture
  sl_oc::video::VideoCapture cap(params);
  if (!cap.initializeVideo(-1)) {
    std::cerr << "Cannot open camera video capture" << std::endl;
    std::cerr << "See verbosity level for more details." << std::endl;

    return EXIT_FAILURE;
  }
  int sn = cap.getSerialNumber();
  std::cout << "Connected to camera sn: " << sn << std::endl;
  // <---- Create Video Capture

  // ----> Retrieve calibration file from Stereolabs server
  std::string calibration_file;
  // ZED Calibration
  unsigned int serial_number = sn;
  // Download camera calibration file
  if (!sl_oc::tools::downloadCalibrationFile(serial_number, calibration_file)) {
    std::cerr << "Could not load calibration file from Stereolabs servers"
              << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "Calibration file found. Loading..." << std::endl;

  // ----> Frame size
  int w, h;
  cap.getFrameSize(w, h);
  // <---- Frame size

  // ----> Initialize calibration
  cv::Mat map_left_x, map_left_y;
  cv::Mat map_right_x, map_right_y;
  cv::Mat cameraMatrix_left, cameraMatrix_right;
  double baseline = 0;
  sl_oc::tools::initCalibration(
      calibration_file, cv::Size(w / 2, h), map_left_x, map_left_y, map_right_x,
      map_right_y, cameraMatrix_left, cameraMatrix_right, &baseline);

  double fx = cameraMatrix_left.at<double>(0, 0);
  double fy = cameraMatrix_left.at<double>(1, 1);
  double cx = cameraMatrix_left.at<double>(0, 2);
  double cy = cameraMatrix_left.at<double>(1, 2);

  std::cout << " Camera Matrix L: \n"
            << cameraMatrix_left << std::endl
            << std::endl;
  std::cout << " Camera Matrix R: \n"
            << cameraMatrix_right << std::endl
            << std::endl;

#ifdef USE_OCV_TAPI
  cv::UMat map_left_x_gpu =
      map_left_x.getUMat(cv::ACCESS_READ, cv::USAGE_ALLOCATE_DEVICE_MEMORY);
  cv::UMat map_left_y_gpu =
      map_left_y.getUMat(cv::ACCESS_READ, cv::USAGE_ALLOCATE_DEVICE_MEMORY);
  cv::UMat map_right_x_gpu =
      map_right_x.getUMat(cv::ACCESS_READ, cv::USAGE_ALLOCATE_DEVICE_MEMORY);
  cv::UMat map_right_y_gpu =
      map_right_y.getUMat(cv::ACCESS_READ, cv::USAGE_ALLOCATE_DEVICE_MEMORY);
#endif
  // ----> Initialize calibration

  // ----> Declare OpenCV images
#ifdef USE_OCV_TAPI
  cv::UMat frameYUV; // Full frame side-by-side in YUV 4:2:2 format
  cv::UMat frameBGR(
      cv::USAGE_ALLOCATE_DEVICE_MEMORY); // Full frame side-by-side in BGR
                                         // format
  cv::UMat left_raw(cv::USAGE_ALLOCATE_DEVICE_MEMORY); // Left unrectified image
  cv::UMat right_raw(
      cv::USAGE_ALLOCATE_DEVICE_MEMORY); // Right unrectified image
  cv::UMat left_rect(cv::USAGE_ALLOCATE_DEVICE_MEMORY); // Left rectified image
  cv::UMat right_rect(
      cv::USAGE_ALLOCATE_DEVICE_MEMORY); // Right rectified image
  cv::UMat left_for_matcher(
      cv::USAGE_ALLOCATE_DEVICE_MEMORY); // Left image for the stereo matcher
  cv::UMat right_for_matcher(
      cv::USAGE_ALLOCATE_DEVICE_MEMORY); // Right image for the stereo matcher
  cv::UMat left_disp_half(
      cv::USAGE_ALLOCATE_DEVICE_MEMORY); // Half sized disparity map
  cv::UMat left_disp(cv::USAGE_ALLOCATE_DEVICE_MEMORY); // Full output disparity
  cv::UMat left_disp_float(
      cv::USAGE_ALLOCATE_DEVICE_MEMORY); // Final disparity map in float32
  cv::UMat left_disp_image(
      cv::USAGE_ALLOCATE_DEVICE_MEMORY); // Normalized and color remapped
                                         // disparity map to be displayed
  cv::UMat left_depth_map(
      cv::USAGE_ALLOCATE_DEVICE_MEMORY); // Depth map in float32
#else
  cv::Mat frameBGR, left_raw, left_rect, right_raw, right_rect, frameYUV,
      left_for_matcher, right_for_matcher, left_disp_half, left_disp,
      left_disp_float, left_disp_vis;
#endif
  // <---- Declare OpenCV images

  // ----> Stereo matcher initialization
  sl_oc::tools::StereoSgbmPar stereoPar;

  // Note: you can use the tool 'zed_open_capture_depth_tune_stereo' to tune the
  // parameters and save them to YAML
  if (!stereoPar.load()) {
    stereoPar.save(); // Save default parameters.
  }

  cv::Ptr<cv::StereoSGBM> left_matcher = cv::StereoSGBM::create(
      stereoPar.minDisparity, stereoPar.numDisparities, stereoPar.blockSize);
  left_matcher->setMinDisparity(stereoPar.minDisparity);
  left_matcher->setNumDisparities(stereoPar.numDisparities);
  left_matcher->setBlockSize(stereoPar.blockSize);
  left_matcher->setP1(stereoPar.P1);
  left_matcher->setP2(stereoPar.P2);
  left_matcher->setDisp12MaxDiff(stereoPar.disp12MaxDiff);
  left_matcher->setMode(stereoPar.mode);
  left_matcher->setPreFilterCap(stereoPar.preFilterCap);
  left_matcher->setUniquenessRatio(stereoPar.uniquenessRatio);
  left_matcher->setSpeckleWindowSize(stereoPar.speckleWindowSize);
  left_matcher->setSpeckleRange(stereoPar.speckleRange);

  stereoPar.print();
  // <---- Stereo matcher initialization

  // ----> Point Cloud
  cv::Mat cloudMat;

#ifdef HAVE_OPENCV_VIZ
  cv::viz::Viz3d pc_viewer = cv::viz::Viz3d("Point Cloud");
#endif
  // <---- Point Cloud

  uint64_t last_ts = 0; // Used to check new frame arrival

  // Infinite video grabbing loop
  while (1) {

    // ----> frame buffer

    // buffer to store circles from several frames to find ball
    std::vector<cv::Vec3f> left_circles_frames;
    int n_buffer_frames = 10;

    // fill buffer to detect circle
    for (int i = 0; i < n_buffer_frames; i++) {

      // Get a new frame from camera
      const sl_oc::video::Frame frame = cap.getLastFrame();

      // ----> If the frame is valid we can convert, rectify and display it
      if (frame.data != nullptr && frame.timestamp != last_ts) {
        last_ts = frame.timestamp;

        // ----> Conversion from YUV 4:2:2 to BGR for visualization
#ifdef USE_OCV_TAPI
        cv::Mat frameYUV_cpu =
            cv::Mat(frame.height, frame.width, CV_8UC2, frame.data);
        frameYUV = frameYUV_cpu.getUMat(cv::ACCESS_READ,
                                        cv::USAGE_ALLOCATE_HOST_MEMORY);
#else
        frameYUV = cv::Mat(frame.height, frame.width, CV_8UC2, frame.data);
#endif
        cv::cvtColor(frameYUV, frameBGR, cv::COLOR_YUV2BGR_YUYV);
        // <---- Conversion from YUV 4:2:2 to BGR for visualization

        // ----> Extract left and right images from side-by-side
        left_raw = frameBGR(cv::Rect(0, 0, frameBGR.cols / 2, frameBGR.rows));
        right_raw = frameBGR(
            cv::Rect(frameBGR.cols / 2, 0, frameBGR.cols / 2, frameBGR.rows));
        // <---- Extract left and right images from side-by-side

        // ----> Apply rectification
        sl_oc::tools::StopWatch remap_clock;
#ifdef USE_OCV_TAPI
        cv::remap(left_raw, left_rect, map_left_x_gpu, map_left_y_gpu,
                  cv::INTER_AREA);
        cv::remap(right_raw, right_rect, map_right_x_gpu, map_right_y_gpu,
                  cv::INTER_AREA);
#else
        cv::remap(left_raw, left_rect, map_left_x, map_left_y, cv::INTER_AREA);
        cv::remap(right_raw, right_rect, map_right_x, map_right_y,
                  cv::INTER_AREA);
#endif
        double remap_elapsed = remap_clock.toc();
        std::stringstream remapElabInfo;
        remapElabInfo << "Rectif. processing: " << remap_elapsed
                      << " sec - Freq: " << 1. / remap_elapsed;
        // <---- Apply rectification

        // ----> Stereo matching
        sl_oc::tools::StopWatch stereo_clock;
        double resize_fact = 1.0;
#ifdef USE_HALF_SIZE_DISP
        resize_fact = 0.5;
        // Resize the original images to improve performances
        cv::resize(left_rect, left_for_matcher, cv::Size(), resize_fact,
                   resize_fact, cv::INTER_AREA);
        cv::resize(right_rect, right_for_matcher, cv::Size(), resize_fact,
                   resize_fact, cv::INTER_AREA);
#else
        left_for_matcher = left_rect;   // No data copy
        right_for_matcher = right_rect; // No data copy
#endif
        // Apply stereo matching
        left_matcher->compute(left_for_matcher, right_for_matcher,
                              left_disp_half);

        left_disp_half.convertTo(left_disp_float, CV_32FC1);
        cv::multiply(
            left_disp_float, 1. / 16.,
            left_disp_float); // Last 4 bits of SGBM disparity are decimal

#ifdef USE_HALF_SIZE_DISP
        cv::multiply(
            left_disp_float, 2.,
            left_disp_float); // Last 4 bits of SGBM disparity are decimal
        cv::UMat tmp = left_disp_float; // Required for OpenCV 3.2
        cv::resize(tmp, left_disp_float, cv::Size(), 1. / resize_fact,
                   1. / resize_fact, cv::INTER_AREA);
#else
        left_disp = left_disp_float;
#endif

        double elapsed = stereo_clock.toc();
        std::stringstream stereoElabInfo;
        stereoElabInfo << "Stereo processing: " << elapsed
                       << " sec - Freq: " << 1. / elapsed;
        // <---- Stereo matching

        // ----> Show disparity image
        cv::add(left_disp_float,
                -static_cast<double>(stereoPar.minDisparity - 1),
                left_disp_float); // Minimum disparity offset correction
        cv::multiply(left_disp_float, 1. / stereoPar.numDisparities,
                     left_disp_image, 255.,
                     CV_8UC1); // Normalization and rescaling

        cv::applyColorMap(left_disp_image, left_disp_image,
                          cv::COLORMAP_INFERNO);
        // <---- Show disparity image

        // ----> Extract Depth map
        // The DISPARITY MAP can be now transformed in DEPTH MAP using the
        // formula depth = (f * B) / disparity where 'f' is the camera focal,
        // 'B' is the camera baseline, 'disparity' is the pixel disparity

        double num = static_cast<double>(fx * baseline);
        cv::divide(num, left_disp_float, left_depth_map);

        float central_depth =
            left_depth_map.getMat(cv::ACCESS_READ)
                .at<float>(left_depth_map.rows / 2, left_depth_map.cols / 2);
        std::cout << "Depth of the central pixel: " << central_depth << " mm"
                  << std::endl;
        // <---- Extract Depth map

        // ----> Detect ball
        // tuning parameters
        int threshold_bin_min = 50;
        int threshold_bin_max = 255;
        int threshold_diameter_min = 0;
        int threshold_diameter_max = 0;
        int HoughCircles_EdgeDetect =
            100; // usually 100-200, lower = more edges detected
        int HoughCircles_CircleDetect =
            20; // usually 20-100, lower = more circles detected
        int GaussianBlur_kernel = 9; // 9 size of Gaussian kernel
        int GaussianBlur_std = 2; // 2 standard deviation in X and Y directions

        // Convert image to grayscale
        cv::Mat left_gray;
        cv::Mat right_gray;
        cv::cvtColor(right_rect, right_gray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(left_rect, left_gray, cv::COLOR_BGR2GRAY);

        // Apply a binary threshold to the grayscale image
        cv::Mat left_bin;
        cv::threshold(left_gray, left_bin, threshold_bin_min, threshold_bin_max,
                      cv::THRESH_BINARY);

        // Blur the binary grayscale image
        cv::Mat left_blurred;
        cv::GaussianBlur(left_bin, left_blurred,
                         cv::Size(GaussianBlur_kernel, GaussianBlur_kernel),
                         GaussianBlur_std, GaussianBlur_std);

        // Convert the pixel diameters to radii
        int radius_min = threshold_diameter_min / 2;
        int radius_max = threshold_diameter_max / 2;

        // detect circles (x, y, radius) by using
        // Hough Circle Transform of binary image
        std::vector<cv::Vec3f> left_circles;
        cv::HoughCircles(left_blurred, left_circles, cv::HOUGH_GRADIENT, 1,
                         left_blurred.rows / 8, HoughCircles_EdgeDetect,
                         HoughCircles_CircleDetect, radius_min, radius_max);

        // find circle and calculate its depth using the depth map
        for (size_t i = 0; i < left_circles.size(); i++) {
          cv::Point center(cvRound(left_circles[i][0]),
                           cvRound(left_circles[i][1]));
          int radius = cvRound(left_circles[i][2]);

          // Calculate the pixel diameter of the circle
          int diameter = radius * 2;

          // Check if the region of interest (ROI) is within the image
          // boundaries ignore if ball is not completely inside the image
          if (center.x - radius < 0 || center.y - radius < 0 ||
              center.x + radius >= left_depth_map.cols ||
              center.y + radius >= left_depth_map.rows) {
            std::cout << "Skipping circle " << i
                      << " because it's outside the image boundaries"
                      << std::endl;
            continue;
          }

          // using left_depth_map get depth at circle position x,y
          float depth = left_depth_map.getMat(cv::ACCESS_READ)
                            .at<float>(center.y, center.x);

          // Print circle position, diameter and distance using left_disp_image
          // xxx todo
          std::cout << "Circle " << i << " at (x,y,z) = (" << center.x << ", "
                    << center.y << depth << ") with diameter " << diameter
                    << std::endl;

          // Draw the circle on the original image
          int line_thickness = 10; // [pixels]
          int line_type = 8;       // 8-connected line
          cv::circle(left_rect, center, radius, cv::Scalar(0, 0, 255),
                     line_thickness, line_type, 0);

          // Show the original image with the circle
          sl_oc::tools::showImage("Left rect.", left_rect, params.res, true,
                                  remapElabInfo.str());
        }

        // <---- Detect ball

        // ----> Create Point Cloud
        sl_oc::tools::StopWatch pc_clock;
        size_t buf_size =
            static_cast<size_t>(left_depth_map.cols * left_depth_map.rows);
        std::vector<cv::Vec3d> buffer(
            buf_size, cv::Vec3f::all(std::numeric_limits<float>::quiet_NaN()));
        cv::Mat depth_map_cpu = left_depth_map.getMat(cv::ACCESS_READ);
        float *depth_vec = (float *)(&(depth_map_cpu.data[0]));

#pragma omp parallel for
        for (size_t idx = 0; idx < buf_size; idx++) {
          size_t r = idx / left_depth_map.cols;
          size_t c = idx % left_depth_map.cols;
          double depth = static_cast<double>(depth_vec[idx]);
          // std::cout << depth << " ";
          if (!isinf(depth) && depth >= 0 && depth > stereoPar.minDepth_mm &&
              depth < stereoPar.maxDepth_mm) {
            buffer[idx].val[2] = depth;                 // Z
            buffer[idx].val[0] = (c - cx) * depth / fx; // X
            buffer[idx].val[1] = (r - cy) * depth / fy; // Y
          }
        }

        cloudMat = cv::Mat(left_depth_map.rows, left_depth_map.cols, CV_64FC3,
                           &buffer[0])
                       .clone();

        double pc_elapsed = stereo_clock.toc();
        std::stringstream pcElabInfo;
        //            pcElabInfo << "Point cloud processing: " << pc_elapsed <<
        //            " sec - Freq: " << 1./pc_elapsed;
        // std::cout << pcElabInfo.str() << std::endl;
        // <---- Create Point Cloud
      }

      //   sl::Mat image_zed;
      //   zed.retrieveImage(image_zed, sl::VIEW::LEFT);
      //   cv::Mat image_cv = sl::toCvMat(image_zed);

      //   // Convert the image to grayscale
      //   cv::Mat gray;
      //   cv::cvtColor(image_cv, gray, cv::COLOR_BGR2GRAY);

      //   // Detect circles in the grayscale image
      //   std::vector<cv::Vec3f> detected_circles;
      //   cv::HoughCircles(gray, detected_circles, cv::HOUGH_GRADIENT, 1,
      //                    gray.rows / 8, 200, 100, 0, 0);

      //   // For each detected circle, check if it overlaps with an existing
      //   circle for (size_t j = 0; j < detected_circles.size(); j++) {
      //     cv::Point center(cvRound(detected_circles[j][0]),
      //                      cvRound(detected_circles[j][1]));
      //     int radius = cvRound(detected_circles[j][2]);

      //     bool found = false;
      //     for (Circle &circle : circles) {
      //       // Calculate the distance between the centers of the two circles
      //       double distance = cv::norm(circle.center - center);

      //       // If the circles overlap more than 90%, increment the frequency
      //       of
      //       // the existing circle
      //       if (distance < 1.1 * (circle.radius + radius)) {
      //         circle.frequency++;
      //         found = true;
      //         break;
      //       }
      //     }

      //     // If the circle does not overlap with any existing circle, add it
      //     to
      //     // the vector
      //     if (!found) {
      //       circles.push_back({center, radius, 1});
      //     }
      //   }
    }

    // // Sort the circles based on their frequency
    // std::sort(circles.begin(), circles.end(),
    //           [](const Circle &a, const Circle &b) {
    //             return a.frequency > b.frequency;
    //           });

    // // The circle that is detected the most frequently is now the first
    // element
    // // in the vector
    // Circle most_frequent_circle = circles[0];

    // // <---- frame buffer

    // ----> Keyboard handling
    int key = cv::waitKey(5);
    if (key == 'q' || key == 'Q') // Quit
      break;
      // <---- Keyboard handling

#ifdef HAVE_OPENCV_VIZ
    // ----> Show Point Cloud
    cv::viz::WCloud cloudWidget(cloudMat, left_rect);
    cloudWidget.setRenderingProperty(cv::viz::POINT_SIZE, 1);
    pc_viewer.showWidget("Point Cloud", cloudWidget);
    pc_viewer.spinOnce(1);

    if (pc_viewer.wasStopped())
      break;
      // <---- Show Point Cloud
#endif
  }

  return EXIT_SUCCESS;
}
