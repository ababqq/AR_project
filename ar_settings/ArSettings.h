#pragma once

#ifndef __dojostatic
#define __dojostatic extern "C" __declspec(dllexport)
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <windows.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/aruco.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/video/tracking.hpp>

#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
#include <librealsense2/rsutil.h>

namespace rs_settings
{
	__dojostatic void InitializeRealsense(const bool use_depthsensor, const bool use_testeyecam, const int rs_w, const int rs_h, const int eye_w, const int eye_h);
	__dojostatic void RunRsThread(rs2::frame_queue& original_data, rs2::frame_queue& filtered_data, rs2::frame_queue& eye_data);
	__dojostatic void GetRsCamParams(rs2_intrinsics& rgb_intrinsics, rs2_intrinsics& depth_intrinsics, rs2_extrinsics& rgb_extrinsics);
	__dojostatic void FinishRsThreads();
	__dojostatic void DeinitializeRealsense();
}

namespace var_settings
{
	__dojostatic void InitializeVarSettings(int scenario = 0, bool is_stereo_stg = false, const std::string& marker_rb_name = "");
	__dojostatic void SetPreoperations(const int rs_w, const int rs_h, const int ws_w, const int ws_h, const int stg_w, const int stg_h, const int eye_w, const int eye_h);
	__dojostatic void SetCvWindows();
	__dojostatic void LoadPresets();
	__dojostatic void ResetCalib();
	__dojostatic void StoreRecordInfo();
	// probe_mode [0, 1, 2] => [DEFAULT, ONLY_PIN_POS, ONLY_RBFRAME]
	__dojostatic void UpdateTrackInfo(const void* trk_info, const std::string& probe_specifier_rb_name = "probe", int probe_mode = 0);
	__dojostatic void RecordInfo(const int key_pressed, const void* color_data);
	__dojostatic void SetTcCalibMkPoints();
	__dojostatic void SetMkSpheres(bool is_visible, bool is_pickable);
	__dojostatic void GetVarInfo(void*);
	__dojostatic void GetVarInfoPtr(void**);
	__dojostatic void SetVarInfo(const void*);
	__dojostatic void TryCalibrationTC(cv::Mat& imgColor);
	__dojostatic void TryCalibrationSTG();
	__dojostatic void SetCalibFrames(bool is_visible);
	__dojostatic void SetDepthMapPC(const bool is_visible, rs2::depth_frame& depth_frame, rs2::video_frame& color_frame);
	__dojostatic void SetTargetModelAssets(const std::string& name, const int guide_line_idx = -1);
	__dojostatic void SetSectionalImageAssets(const bool show_sectional_views, const float* pos_tip, const float* pos_end, const float rot_angle_rad = 0);
	__dojostatic void RenderAndShowWindows(bool show_times, cv::Mat& img_rs, bool skip_show_rs_window = false, int addtional_scene = -1, int addtional_cam = -1);
	__dojostatic void DeinitializeVarSettings();

	__dojostatic std::string GetDefaultFilePath();

	// Spine Scenario
	__dojostatic void SetOperationDesription(std::string& operation_name);
	__dojostatic void SetProbeRigidBodyName(std::string& probe_rb_name);
	// SSU (???? prototype ver2?? ?????? ???? (???????? namespace ???? ?????? ?????? ?????? ????)
	__dojostatic int GetCameraID_SSU(int scene_id);
}

