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
	__dojostatic void InitializeVarSettings();
	__dojostatic void SetPreoperations(const int rs_w, const int rs_h, const int ws_w, const int ws_h, const int stg_w, const int stg_h, const int eye_w, const int eye_h);
	__dojostatic void SetCvWindows();
	__dojostatic void LoadPresets();
	__dojostatic void ResetCalib();
	__dojostatic void StoreRecordInfo();
	__dojostatic void UpdateTrackInfo(const void* trk_info);
	__dojostatic void RecordInfo(const int key_pressed, const void* color_data);
	__dojostatic void SetTcCalibMkPoints(bool is_visible);
	__dojostatic void SetMkSpheres(bool is_visible, bool is_pickable);
	__dojostatic void GetVarInfo(void*);
	__dojostatic void SetVarInfo(const void*);
	__dojostatic void TryCalibrationTC(cv::Mat& imgColor);
	__dojostatic void TryCalibrationSTG();
	__dojostatic void SetCalibFrames(bool is_visible);
	__dojostatic void SetDepthMapPC(const bool is_visible, rs2::depth_frame& depth_frame, rs2::video_frame& color_frame);
	__dojostatic void SetTargetModelAssets(const std::string& name, const bool show_sectional_views);
	__dojostatic void RenderAndShowWindows(bool show_times, cv::Mat& img_rs);
	__dojostatic void DeinitializeVarSettings();

	// SSU (���� prototype ver2�� ������ ���� (�ܺο��� namespace ���� ������ ����� �ӽ÷� �۾�)
	__dojostatic int GetCameraID_SSU(int scene_id);
}

