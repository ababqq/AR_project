#include "ArSettings.h"
#include "../optitrk/optitrack.h"
#include "../aruco_marker/aruco_armarker.h"

#include <string>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>
#include "VisMtvApi.h"

using namespace std;
using namespace cv;
#include "../kar_helpers.hpp"
#include "../event_handler.hpp"

#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#define __MIRRORS

namespace rs_settings
{
	map<string, string> serials;
	rs2::decimation_filter _dec;
	rs2::disparity_transform depth2disparity;
	rs2::disparity_transform disparity2depth(false);
	rs2::spatial_filter spat;
	rs2::temporal_filter temp;
	// Spatially align all streams to depth viewport
	// We do this because:
	//   a. Usually depth has wider FOV, and we only really need depth for this demo
	//   b. We don't want to introduce new holes
	// Declare RealSense pipeline, encapsulating the actual device and sensors
	rs2::align align_to(RS2_STREAM_DEPTH);
	rs2_intrinsics rgb_intrinsics;
	rs2_intrinsics depth_intrinsics;
	rs2_extrinsics rgb_extrinsics;

	bool is_initialized = false;
	bool _use_depthsensor = false;
	bool _use_testeyecam = false;

	// Alive boolean will signal the worker threads to finish-up
	std::atomic_bool rs_alive{ false };
	std::atomic_bool eye_rs_alive{ false };
	std::thread video_processing_thread;
	std::thread eye_processing_thread;

	rs2::context* _ctx;
	rs2::pipeline* _pipe;// (ctx);
	rs2::pipeline* _eye_pipe;// (ctx);

	// Colorizer is used to visualize depth data
	rs2::colorizer color_map;
	// Declare pointcloud object, for calculating pointclouds and texture mappings
	rs2::pointcloud pc;
	// We want the points object to be persistent so we can display the last cloud when a frame drops
	rs2::points points;

	glm::fmat4x4 mat_ircs2irss, mat_irss2ircs;

	void GetRsCamParams(rs2_intrinsics& _rgb_intrinsics, rs2_intrinsics& _depth_intrinsics, rs2_extrinsics& _rgb_extrinsics)
	{
		_rgb_intrinsics = rgb_intrinsics;
		_depth_intrinsics = depth_intrinsics;
		_rgb_extrinsics = rgb_extrinsics;
	}

	void InitializeRealsense(const bool use_depthsensor, const bool use_testeyecam,
		const int rs_w, const int rs_h, const int eye_w, const int eye_h)
	{
		if (is_initialized) return;

		_use_depthsensor = use_depthsensor;
		_use_testeyecam = use_testeyecam;

		//for (auto&& dev : ctx.query_devices())
		//	serials.push_back(dev.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER));
		//cout << serials[0] << endl; // 839112061828 // 415
		//cout << serials[1] << endl; // 819312071259 // 430
		serials["RS_RBS"] = string("839112061828");
		serials["EYE"] = string("819312071259");
		//serials.insert(std::pair<std::string, std::string>("RS_RBS", "819312071259"));
		//serials.insert(std::pair<std::string, std::string>("EYE", "819312071259"));


		// Decimation filter reduces the amount of data (while preserving best samples)
		// If the demo is too slow, make sure you run in Release (-DCMAKE_BUILD_TYPE=Release)
		// but you can also increase the following parameter to decimate depth more (reducing quality)
		_dec.set_option(RS2_OPTION_FILTER_MAGNITUDE, 2);
		// Define spatial filter (edge-preserving)
		// Enable hole-filling
		// Hole filling is an agressive heuristic and it gets the depth wrong many times
		// However, this demo is not built to handle holes
		// (the shortest-path will always prefer to "cut" through the holes since they have zero 3D distance)
		spat.set_option(RS2_OPTION_FILTER_SMOOTH_ALPHA, 0.5);
		spat.set_option(RS2_OPTION_FILTER_SMOOTH_DELTA, 1.0);
		spat.set_option(RS2_OPTION_FILTER_MAGNITUDE, 2);
		spat.set_option(RS2_OPTION_HOLES_FILL, 0); // 5 = fill all the zero pixels
		// Define temporal filter
		temp.set_option(RS2_OPTION_FILTER_SMOOTH_ALPHA, 0);
		temp.set_option(RS2_OPTION_FILTER_SMOOTH_DELTA, 100.0);

		//rs2::context ctx;
		//rs2::pipeline pipe(ctx);
		//rs2::pipeline eye_pipe(ctx);

		// Use black to white color map
		color_map.set_option(RS2_OPTION_COLOR_SCHEME, 2.f);

		_ctx = new rs2::context();
		rs2::context& ctx = *_ctx;
		_pipe = new rs2::pipeline(ctx);
		_eye_pipe = new rs2::pipeline(ctx);

		rs2::pipeline& pipe = *_pipe;
		rs2::pipeline& eye_pipe = *_eye_pipe;

		rs2::config cfg;
		rs2::config eye_cfg;

		//_pipe = &pipe;
		//_eye_pipe = &eye_pipe;

		//cfg.enable_device(serials["RS_RBS"]);
		cfg.enable_stream(RS2_STREAM_DEPTH, 848, 480, RS2_FORMAT_Z16, 60); // Enable default depth

		if (!use_depthsensor)
			cfg.disable_stream(RS2_STREAM_DEPTH); // Disable default depth

		// For the color stream, set format to RGBA
		// To allow blending of the color frame on top of the depth frame
		cfg.enable_stream(RS2_STREAM_COLOR, 0, rs_w, rs_h, RS2_FORMAT_RGB8, 60);

		auto profile = pipe.start(cfg);

		auto sensor = profile.get_device().first<rs2::depth_sensor>();
		// Set the device to High Accuracy preset of the D400 stereoscopic cameras
		if (sensor && sensor.is<rs2::depth_stereo_sensor>())
		{
			sensor.set_option(RS2_OPTION_VISUAL_PRESET, RS2_RS400_VISUAL_PRESET_HIGH_ACCURACY);
			sensor.set_option(RS2_OPTION_EMITTER_ENABLED, 0);
		}

		// to get rgb sensor profile
		auto stream_rgb = profile.get_stream(RS2_STREAM_COLOR).as<rs2::video_stream_profile>();
		if (stream_rgb)
		{
			rgb_intrinsics = stream_rgb.get_intrinsics();
			cout << std::endl << "RGB Intrinsic:," << std::endl;
			cout << "Fx," << rgb_intrinsics.fx << std::endl;
			cout << "Fy," << rgb_intrinsics.fy << std::endl;
			cout << "PPx," << rgb_intrinsics.ppx << std::endl;
			cout << "PPy," << rgb_intrinsics.ppy << std::endl;
			cout << "Distorsion," << rs2_distortion_to_string(rgb_intrinsics.model) << std::endl;
		}


		if (use_depthsensor)
		{
			auto stream_depth = profile.get_stream(RS2_STREAM_DEPTH).as<rs2::video_stream_profile>();
			//rs2_extrinsics rgb_extrinsics = stream_depth.get_extrinsics_to(stream_rgb);
			if (stream_depth)
			{
				depth_intrinsics = stream_depth.get_intrinsics();
				cout << std::endl << "Depth Intrinsic:," << std::endl;
				cout << "Fx," << depth_intrinsics.fx << std::endl;
				cout << "Fy," << depth_intrinsics.fy << std::endl;
				cout << "PPx," << depth_intrinsics.ppx << std::endl;
				cout << "PPy," << depth_intrinsics.ppy << std::endl;
				cout << "Distorsion," << rs2_distortion_to_string(depth_intrinsics.model) << std::endl;
			}

			if (stream_depth)
			{
				glm::fmat3x3 mat_int;
				mat_int[0][0] = depth_intrinsics.fx;
				mat_int[1][0] = 0;
				mat_int[2][0] = depth_intrinsics.ppx;
				mat_int[1][1] = depth_intrinsics.fy;
				mat_int[2][1] = depth_intrinsics.ppy;

				glm::fmat4x4 mat_cs2ps, mat_ps2ss;
				ComputeDXProjectionMatrix((float*)&mat_cs2ps, (float*)&mat_ps2ss, (float*)&mat_int,
					0, 0, (float)rgb_intrinsics.width, (float)rgb_intrinsics.height, 0.01f, 10.f);
				mat_ircs2irss = mat_ps2ss * mat_cs2ps;
				mat_irss2ircs = glm::inverse(mat_ircs2irss);

				rgb_extrinsics = stream_depth.get_extrinsics_to(stream_rgb);
			}
		}

		if (use_testeyecam)
		{
			eye_cfg.enable_device(serials["EYE"]);
			eye_cfg.disable_stream(RS2_STREAM_DEPTH); // Disable default depth
			// For the color stream, set format to RGBA
			// To allow blending of the color frame on top of the depth frame
			eye_cfg.enable_stream(RS2_STREAM_COLOR, 0, eye_w, eye_h, RS2_FORMAT_RGB8, 60);
			eye_pipe.start(eye_cfg);
		}
		is_initialized = true;
	}

	void RunRsThread(rs2::frame_queue& original_data, rs2::frame_queue& filtered_data, rs2::frame_queue& eye_data)
	{
		if (!is_initialized | rs_alive) return;

		rs_alive = true;
		video_processing_thread = std::thread([&]() {
			while (rs_alive)
			{
				// Fetch frames from the pipeline and send them for processing
				//rs2::frameset data;
				//if (pipe.poll_for_frames(&data))
				{
					rs2::frameset data = _pipe->wait_for_frames(); // Wait for next set of frames from the camera
					rs2::frame data_depth = data.get_depth_frame();

					if (data_depth != NULL && _use_depthsensor)
					{
						// First make the frames spatially aligned
						data_depth = data_depth.apply_filter(align_to);

						// Decimation will reduce the resultion of the depth image,
						// closing small holes and speeding-up the algorithm
						data_depth = data_depth.apply_filter(_dec);

						// To make sure far-away objects are filtered proportionally
						// we try to switch to disparity domain
						data_depth = data_depth.apply_filter(depth2disparity);

						// Apply spatial filtering
						data_depth = data_depth.apply_filter(spat);

						// Apply temporal filtering
						data_depth = data_depth.apply_filter(temp);

						// If we are in disparity domain, switch back to depth
						data_depth = data_depth.apply_filter(disparity2depth);

						// Apply color map for visualization of depth
						// data = data.apply_filter(color_map);
						// data.arbitrary_depth = data_depth;
						// postprocessed_depthframes.enqueue(data_depth);
					}

					filtered_data.enqueue(data_depth);
					original_data.enqueue(data);

					// Send resulting frames for visualization in the main thread
					//original_data.enqueue(data);
				}
			}
		});

		if (_use_testeyecam)
		{
			eye_rs_alive = true;
			eye_processing_thread = std::thread([&]() {
				while (eye_rs_alive)
				{
					// Fetch frames from the pipeline and send them for processing
					{
						//rs2::frameset data = eye_pipe.wait_for_frames(); // Wait for next set of frames from the camera
						//eye_data.enqueue(data);
					}
					rs2::frameset data;
					if (_eye_pipe->poll_for_frames(&data))
					{
						eye_data.enqueue(data);
					}
				}
			});
		}
	}

	void FinishRsThreads()
	{
		rs_alive = false;
		video_processing_thread.join();
		if (_use_testeyecam)
		{
			eye_rs_alive = false;
			eye_processing_thread.join();
		}
	}

	void DeinitializeRealsense()
	{
		delete _ctx;
		delete _pipe;
		delete _eye_pipe;
		_ctx = NULL;
		_pipe = NULL;
		_eye_pipe = NULL;
		is_initialized = false;

	}
}


namespace var_settings
{
#define __NUMMARKERS 15
	GlobalInfo g_info;
	ArMarkerTracker ar_marker;
	set<int> mk_ids;
	string preset_path;
	string GetDefaultFilePath() {
		return preset_path;
	}

	int scenario = 0;
	float dicom_tr_x = 0;
	float dicom_tr_y = 0;
	float dicom_tr_z = 0;
	int dicom_flip_x = 1;
	int dicom_flip_y = 1;
	int dicom_flip_z = 1;
	
	bool _show_sectional_views = false;
	int ov_cam_id = 1; // arbitrary integer
	int model_cam_id = 1; // arbitrary integer
	int rs_cam_id = 1; // arbitrary integer
	int stg_cam_id = 1; // arbitrary integer
	int stg2_cam_id = 2; // arbitrary integer
	int znavi_cam_id = 1; // arbitrary integer
	vzm::ObjStates default_obj_state;

	bool is_rsrb_detected = false;
	glm::fmat4x4 mat_ws2clf, mat_clf2ws;

	glm::fmat4x4 mat_rscs2clf;
	glm::fmat4x4 mat_stgcs2clf;
	glm::fmat4x4 mat_stgcs2clf_2;

	// rs calib history
	vector<track_info> record_trk_info;
	vector<void*> record_rsimg;
	map<string, int> action_info;
	vector<int> record_key;

	std::string operation_name;

	void InitializeVarSettings(int _scenario, bool is_stereo_stg, const std::string& marker_rb_name)
	{
		g_info.scenario = scenario = _scenario;
		g_info.stg_display_num = 1;// is_stereo_stg ? 2 : 1;

		g_info.otrk_data.marker_rb_name = marker_rb_name;

		// set global information
		g_info.ws_scene_id = 1;
		g_info.rs_scene_id = 2;
		g_info.model_scene_id = 3;
		g_info.csection_scene_id = 4;
		g_info.stg_scene_id = 5;
		g_info.znavi_rs_scene_id = 9;
		g_info.znavi_stg_scene_id = 10;

		g_info.window_name_rs_view = "RealSense VIEW";
		g_info.window_name_ws_view = "World VIEW";
		g_info.window_name_ms_view = "Model VIEW";
		g_info.window_name_stg_view = "STG VIEW";

		char ownPth[2048];
		GetModuleFileNameA(NULL, ownPth, (sizeof(ownPth)));
		string exe_path = ownPth;
		size_t pos = 0;
		std::string token;
		string delimiter = "\\";
		preset_path = "";
		while ((pos = exe_path.find(delimiter)) != std::string::npos) {
			token = exe_path.substr(0, pos);
			if (token.find(".exe") != std::string::npos) break;
			preset_path += token + "\\";
			exe_path.erase(0, pos + delimiter.length());
		}
		preset_path += "..\\";
		//cout << hlslobj_path << endl;

		g_info.custom_pos_file_paths["preset_path"] = preset_path;


		// load file
		//	~200907
		/*
		g_info.optrack_calib = "C:\\Users\\User\\Desktop\\Preset\\Optitrack\\Calibration_200904.cal";
		g_info.optrack_env = "C:\\Users\\User\\Desktop\\Preset\\Optitrack\\Asset_200904.motive";
		g_info.cb_positions = "E:\\project_srcs\\kar\\prototype_ver1\\cb_points.txt";
		g_info.sst_positions = "E:\\project_srcs\\kar\\prototype_ver1\\ss_pin_pts.txt";
		g_info.rs_calib = "E:\\project_srcs\\kar\\prototype_ver1\\rs_calib.txt";
		g_info.stg_calib = "E:\\project_srcs\\kar\\prototype_ver1\\stg_calib.txt";
		g_info.model_predefined_pts = "E:\\project_srcs\\kar\\prototype_ver1\\mode_predefined_points.txt";
		*/
		
		g_info.optrack_calib = preset_path + "..\\Preset\\Calibration_201123.cal";
		g_info.optrack_env = preset_path + "..\\Preset\\Asset_201123.motive";
		g_info.cb_positions = preset_path + "..\\Preset\\cb_points.txt";
		g_info.sst_positions = preset_path + "..\\Preset\\ss_pin_pts.txt";
		g_info.rs_calib = preset_path + "..\\Preset\\rs_calib.txt";
		g_info.stg_calib = preset_path + "..\\Preset\\stg_calib.txt";

		if (scenario == 0)
		{
			g_info.model_path = preset_path + "..\\Data\\skin.obj";
			g_info.model_predefined_pts = preset_path + "..\\Preset\\mode_predefined_points.txt";
			//g_info.volume_model_path = "C:\\Users\\User\\source\\repos\\korfriend\\LargeData\\head\\head.x3d";
			g_info.volume_model_path = preset_path + "..\\..\\LargeData\\head_phantom_kar\\head_phantom_kar.x3d";
			g_info.model_view_preset = preset_path + "..\\Preset\\mv_preset(head).txt";
		}
		else if (scenario == 1)
		{
			//g_info.model_path = preset_path + "..\\Data\\breast\\chest_front_points(nrl)_simple1.ply";
			g_info.model_path = preset_path + "..\\Data\\breast\\chest_front_surf_simple1.stl";
			g_info.volume_model_path = preset_path + "..\\Data\\breast\\chest_x3d.x3d";
			g_info.model_predefined_pts = preset_path + "..\\Preset\\mode_predefined_points(breast).txt";
			g_info.model_view_preset = preset_path + "..\\Preset\\mv_preset(breast).txt";
		}
		else if (scenario == 2)
		{
			g_info.model_path = preset_path + "..\\Data\\spine\\lev7.stl";
			//g_info.model_path = preset_path + "..\\..\\LargeData\\Lev7\\lev7.stl";
			g_info.volume_model_path = preset_path + "..\\..\\LargeData\\den.x3d";
			//g_info.volume_model_path = preset_path + "..\\..\\LargeData\\201120_den\\201120_den.x3d";
			//g_info.volume_model_path = preset_path + "..\\Data\\spine\\chest_x3d.x3d";
			g_info.model_predefined_pts = preset_path + "..\\Preset\\mode_predefined_points(spine).txt";
			g_info.model_view_preset = preset_path + "..\\Preset\\mv_preset(spine).txt";
			//g_info.model_predefined_pts = preset_path + "..\\..\\LargeData\\";
		}

		for (int i = 1; i <= __NUMMARKERS; i++)
		{
			mk_ids.insert(i);
			ar_marker.register_marker(i, 5.15);
			//ar_marker.aruco_marker_file_out(i, "armk" + to_string(i) + ".bmp");
		}
	}

	void SetOperationDesription(std::string& operation_name)
	{
		operation_name = operation_name;
	}

	void SetPreoperations(const int rs_w, const int rs_h, const int ws_w, const int ws_h, const int stg_w, const int stg_h, const int eye_w, const int eye_h)
	{
		//printf("%s", g_info.optrack_env.c_str());
		optitrk::LoadProfileAndCalibInfo(g_info.optrack_env, g_info.optrack_calib);
		cout << "cam0 frame rate setting ==> " << optitrk::SetCameraFrameRate(0, 120) << endl;
		cout << "cam1 frame rate setting ==> " << optitrk::SetCameraFrameRate(1, 120) << endl;

		optitrk::SetCameraSettings(0, 4, 50, 100);
		optitrk::SetCameraSettings(1, 4, 50, 100);

		g_info.eye_w = eye_w;
		g_info.eye_h = eye_h;
		g_info.ws_w = ws_w;
		g_info.ws_h = rs_h;// ws_h;
		g_info.stg_w = stg_w;
		g_info.stg_h = stg_h;
		g_info.rs_w = rs_w;
		g_info.rs_h = rs_h;
		g_info.ms_w = 400;
		g_info.ms_h = 400;
		g_info.zn_w = 300;
		g_info.zn_h = 300;

		int volume_obj_id = 0;
		if (scenario == 0)
		{
			vzm::LoadModelFile(g_info.model_path, g_info.model_ms_obj_id);
			vzm::GenerateCopiedObject(g_info.model_ms_obj_id, g_info.model_ws_obj_id);
			vzm::LoadModelFile(g_info.volume_model_path, g_info.model_volume_id);
		}
		else if (scenario == 1 || scenario == 2)
		{
			vzm::LoadModelFile(g_info.model_path, g_info.model_ms_obj_id);
			vzm::GenerateCopiedObject(g_info.model_ms_obj_id, g_info.model_ws_obj_id);
			vzm::LoadModelFile(g_info.volume_model_path, g_info.model_volume_id);
		}
		vzm::ValidatePickTarget(g_info.model_ms_obj_id);

		vzm::CameraParameters cam_params;
		__cv3__ cam_params.pos = glm::fvec3(1.0, 2.0, 1.5f);
		glm::fvec3 t_up = glm::fvec3(0, 1.f, 0);
		__cv3__ cam_params.view = glm::normalize(glm::fvec3(0, 1, 0) - __cv3__ cam_params.pos);
		glm::fvec3 t_right = glm::cross(__cv3__ cam_params.view, t_up);
		__cv3__ cam_params.up = glm::normalize(glm::cross(t_right, __cv3__ cam_params.view));
		cam_params.fov_y = 3.141592654f / 4.f;
		cam_params.aspect_ratio = (float)ws_w / (float)ws_h;
		cam_params.projection_mode = 2;
		cam_params.w = ws_w;
		cam_params.h = ws_h;
		cam_params.np = 0.1f;
		cam_params.fp = 20.0f;

		vzm::SetCameraParameters(g_info.ws_scene_id, cam_params, ov_cam_id);

		vzm::CameraParameters cam_params_model = cam_params;
		cam_params_model.np = 0.01f;
		cam_params_model.fp = 10.0f;
		if (scenario == 0)
		{
			__cv3__ cam_params_model.pos = glm::fvec3(0.4f, 0, 0) + glm::fvec3(dicom_tr_x, dicom_tr_y, dicom_tr_z) * 0.001f;
			__cv3__ cam_params_model.up = glm::fvec3(0, 0.f, 1.f);
			__cv3__ cam_params_model.view = glm::fvec3(-1.f, 0, 0.f);

			//glm::fmat4x4 mat_t = glm::translate(glm::fvec3(112.896, 112.896, 91.5));
		}
		else if (scenario == 1)
		{
			__cv3__ cam_params_model.pos = glm::fvec3(0, -0.7f, 0);
			__cv3__ cam_params_model.up = glm::fvec3(0, 0, 1.f);
			__cv3__ cam_params_model.view = glm::fvec3(0, 1.f, 0.f);
		}
		else if (scenario == 2)
		{
			__cv3__ cam_params_model.pos = glm::fvec3(0, -0.4f, 0);
			__cv3__ cam_params_model.up = glm::fvec3(0, 0, 1.f);
			__cv3__ cam_params_model.view = glm::fvec3(0, 1.f, 0.f);
		}
		cam_params_model.w = g_info.ms_w;
		cam_params_model.h = g_info.ms_h;

		vzm::SetCameraParameters(g_info.model_scene_id, cam_params_model, model_cam_id);

		vzm::SceneEnvParameters scn_env_params;
		scn_env_params.is_on_camera = false;
		scn_env_params.is_pointlight = false;
		scn_env_params.effect_ssao.is_on_ssao = false;
		scn_env_params.effect_ssao.kernel_r = 0.01f;
		scn_env_params.effect_ssao.num_dirs = 16;
		scn_env_params.is_on_camera = true;
		scn_env_params.is_pointlight = true;
		__cv3__ scn_env_params.pos_light = __cv3__ cam_params.pos;
		__cv3__ scn_env_params.dir_light = __cv3__ cam_params.view;
		vzm::SetSceneEnvParameters(g_info.ws_scene_id, scn_env_params);
		scn_env_params.is_on_camera = true;
		vzm::SetSceneEnvParameters(g_info.rs_scene_id, scn_env_params);
		vzm::SceneEnvParameters ms_scn_env_params = scn_env_params;
		ms_scn_env_params.is_on_camera = true;
		vzm::SetSceneEnvParameters(g_info.model_scene_id, ms_scn_env_params);
		vzm::SceneEnvParameters csection_scn_env_params = scn_env_params;
		vzm::SetSceneEnvParameters(g_info.csection_scene_id, csection_scn_env_params);
		vzm::SceneEnvParameters stg_scn_env_params = scn_env_params;
		vzm::SetSceneEnvParameters(g_info.stg_scene_id, stg_scn_env_params);

		vzm::SceneEnvParameters znavi_scn_env_params = scn_env_params;
		vzm::SetSceneEnvParameters(g_info.znavi_rs_scene_id, znavi_scn_env_params);
		vzm::SetSceneEnvParameters(g_info.znavi_stg_scene_id, znavi_scn_env_params);

		vzm::ObjStates obj_state;
		obj_state.emission = 0.4f;
		obj_state.diffusion = 0.6f;
		obj_state.specular = 0.2f;
		obj_state.sp_pow = 30.f;
		__cv4__ obj_state.color = glm::fvec4(1.f, 1.f, 1.f, 1.f);
		__cm4__ obj_state.os2ws = glm::fmat4x4();
		default_obj_state = obj_state;

		vzm::SetRenderTestParam("_bool_UseSpinLock", false, sizeof(bool), -1, -1);
		vzm::SetRenderTestParam("_double_MergingBeta", 0.5, sizeof(double), -1, -1);
		vzm::SetRenderTestParam("_double_RobustRatio", 0.5, sizeof(double), -1, -1);
		vzm::SetRenderTestParam("_int_OitMode", (int)0, sizeof(int), -1, -1);
		vzm::SetRenderTestParam("_double_AbsVZThickness", 0.002, sizeof(double), -1, -1);
		vzm::SetRenderTestParam("_double_AbsCopVZThickness", 0.001, sizeof(double), -1, -1);

		vzm::SetRenderTestParam("_bool_UseMask3DTip", true, sizeof(bool), -1, -1);
		vzm::SetRenderTestParam("_double4_MaskCenterRadius0", glm::dvec4(-100, -100, 50, 0.5), sizeof(glm::dvec4), -1, -1);
		vzm::SetRenderTestParam("_double3_HotspotParamsTKtKs0", glm::dvec3(1, 0.5, 1.5), sizeof(glm::dvec3), -1, -1);
		vzm::SetRenderTestParam("_double_InDepthVis", 0.01, sizeof(double), -1, -1);
		vzm::SetRenderTestParam("_bool_GhostEffect", true, sizeof(bool), g_info.rs_scene_id, 1);
		vzm::SetRenderTestParam("_bool_GhostEffect", true, sizeof(bool), g_info.stg_scene_id, 1);
		vzm::SetRenderTestParam("_bool_GhostEffect", true, sizeof(bool), g_info.stg_scene_id, 2);
		vzm::SetRenderTestParam("_bool_IsGhostSurface", true, sizeof(bool), g_info.rs_scene_id, 1, g_info.model_ws_obj_id);
		vzm::SetRenderTestParam("_bool_IsGhostSurface", true, sizeof(bool), g_info.stg_scene_id, 1, g_info.model_ws_obj_id);
		vzm::SetRenderTestParam("_bool_IsGhostSurface", true, sizeof(bool), g_info.stg_scene_id, 2, g_info.model_ws_obj_id);
		vzm::SetRenderTestParam("_bool_IsOnlyHotSpotVisible", true, sizeof(bool), g_info.rs_scene_id, 1, g_info.model_ws_obj_id);
		vzm::SetRenderTestParam("_bool_IsOnlyHotSpotVisible", true, sizeof(bool), g_info.stg_scene_id, 1, g_info.model_ws_obj_id);
		vzm::SetRenderTestParam("_bool_IsOnlyHotSpotVisible", true, sizeof(bool), g_info.stg_scene_id, 2, g_info.model_ws_obj_id);

		if(scenario == 2)
			vzm::SetRenderTestParam("_bool_OnlyForemostSurfaces", true, sizeof(bool), -1, -1, g_info.model_ws_obj_id);

		//double vz = 0.0;
		//vzm::SetRenderTestParam("_double_VZThickness", vz, sizeof(double), -1, -1);
		//double cvz = 0.00001;
		//vzm::SetRenderTestParam("_double_CopVZThickness", cvz, sizeof(double), -1, -1);

		vzm::ObjStates model_state = obj_state;
		model_state.emission = 0.3f;
		model_state.diffusion = 0.5f;
		model_state.specular = 0.1f;
		model_state.color[3] = 0.8;
		double scale_factor = 0.001;
		glm::fmat4x4 mat_s = glm::scale(glm::fvec3(scale_factor));
		g_info.mat_os2matchmodefrm = __cm4__ model_state.os2ws = (__cm4__ model_state.os2ws) * mat_s;
		model_state.surfel_size = 0.01;
		vzm::ObjStates model_ws_state;
		if (g_info.model_volume_id != 0)
		{
			int vr_tmap_id = 0, vr_tmap_id1 = 0, mpr_tmap_id = 0;
			std::vector<glm::fvec2> alpha_ctrs;
			std::vector<glm::fvec4> rgb_ctrs;
			if (scenario == 0)
			{
				const int a_0 = 104;
				const int a_1 = 1249;
				//const int a_0 = 14000;
				//const int a_1 = 26000;
				alpha_ctrs.push_back(glm::fvec2(0, a_0));
				alpha_ctrs.push_back(glm::fvec2(1, a_1));
				alpha_ctrs.push_back(glm::fvec2(1, 65536));
				alpha_ctrs.push_back(glm::fvec2(0, 65537));
				rgb_ctrs.push_back(glm::fvec4(1, 1, 1, 0));
				rgb_ctrs.push_back(glm::fvec4(0.31, 0.78, 1, a_0));
				rgb_ctrs.push_back(glm::fvec4(1, 0.51, 0.49, a_1));
				rgb_ctrs.push_back(glm::fvec4(1, 1, 1, 21000));
				rgb_ctrs.push_back(glm::fvec4(1, 1, 1, 65536));
				vzm::GenerateMappingTable(65537, alpha_ctrs.size(), (float*)&alpha_ctrs[0], rgb_ctrs.size(), (float*)&rgb_ctrs[0], vr_tmap_id);
				alpha_ctrs[0] = glm::fvec2(0, a_0);
				alpha_ctrs[1] = glm::fvec2(1, a_1);
				vzm::GenerateMappingTable(65537, alpha_ctrs.size(), (float*)&alpha_ctrs[0], rgb_ctrs.size(), (float*)&rgb_ctrs[0], vr_tmap_id1);
				alpha_ctrs[0] = glm::fvec2(0, a_0);
				alpha_ctrs[1] = glm::fvec2(1, a_1);
				rgb_ctrs[1] = glm::fvec4(1);
				rgb_ctrs[2] = glm::fvec4(1);
				vzm::GenerateMappingTable(65537, alpha_ctrs.size(), (float*)&alpha_ctrs[0], rgb_ctrs.size(), (float*)&rgb_ctrs[0], mpr_tmap_id);
			}
			else if (scenario == 1)
			{
				alpha_ctrs.push_back(glm::fvec2(0, 17760));
				alpha_ctrs.push_back(glm::fvec2(1, 21700));
				alpha_ctrs.push_back(glm::fvec2(1, 65536));
				alpha_ctrs.push_back(glm::fvec2(0, 65537));
				rgb_ctrs.push_back(glm::fvec4(1, 1, 1, 0));
				rgb_ctrs.push_back(glm::fvec4(0.31, 0.78, 1, 17760));
				rgb_ctrs.push_back(glm::fvec4(1, 0.51, 0.49, 18900));
				rgb_ctrs.push_back(glm::fvec4(1, 1, 1, 21000));
				rgb_ctrs.push_back(glm::fvec4(1, 1, 1, 65536));
				vzm::GenerateMappingTable(65537, alpha_ctrs.size(), (float*)&alpha_ctrs[0], rgb_ctrs.size(), (float*)&rgb_ctrs[0], vr_tmap_id);
				alpha_ctrs[0] = glm::fvec2(0, 17760);
				alpha_ctrs[1] = glm::fvec2(1, 21700);
				vzm::GenerateMappingTable(65537, alpha_ctrs.size(), (float*)&alpha_ctrs[0], rgb_ctrs.size(), (float*)&rgb_ctrs[0], vr_tmap_id1);
				alpha_ctrs[0] = glm::fvec2(0, 100);
				alpha_ctrs[1] = glm::fvec2(1, 30000);
				rgb_ctrs[1] = glm::fvec4(1);
				rgb_ctrs[2] = glm::fvec4(1);
				vzm::GenerateMappingTable(65537, alpha_ctrs.size(), (float*)&alpha_ctrs[0], rgb_ctrs.size(), (float*)&rgb_ctrs[0], mpr_tmap_id);
			}
			else if (scenario == 2)
			{
				//ActualMinMaxValue = -1024.000000 1714.750000
				//StoredMinMaxValue = 0.000000 43820.000000
				alpha_ctrs.push_back(glm::fvec2(0, 17500));
				alpha_ctrs.push_back(glm::fvec2(1, 21200));
				alpha_ctrs.push_back(glm::fvec2(1, 65536));
				alpha_ctrs.push_back(glm::fvec2(0, 65537));
				rgb_ctrs.push_back(glm::fvec4(1, 1, 1, 0));
				rgb_ctrs.push_back(glm::fvec4(0.31, 0.78, 1, 15000));
				rgb_ctrs.push_back(glm::fvec4(1, 0.51, 0.49, 19000));
				rgb_ctrs.push_back(glm::fvec4(1, 1, 1, 22000));
				rgb_ctrs.push_back(glm::fvec4(1, 1, 1, 65536));
				vzm::GenerateMappingTable(65537, alpha_ctrs.size(), (float*)&alpha_ctrs[0], rgb_ctrs.size(), (float*)&rgb_ctrs[0], vr_tmap_id);
				alpha_ctrs[0] = glm::fvec2(0, 17500);
				alpha_ctrs[1] = glm::fvec2(1, 21200);
				vzm::GenerateMappingTable(65537, alpha_ctrs.size(), (float*)&alpha_ctrs[0], rgb_ctrs.size(), (float*)&rgb_ctrs[0], vr_tmap_id1);
				alpha_ctrs[0] = glm::fvec2(0, 10000);
				alpha_ctrs[1] = glm::fvec2(1, 40000);
				rgb_ctrs[1] = glm::fvec4(1);
				rgb_ctrs[2] = glm::fvec4(1);
				vzm::GenerateMappingTable(65537, alpha_ctrs.size(), (float*)&alpha_ctrs[0], rgb_ctrs.size(), (float*)&rgb_ctrs[0], mpr_tmap_id);
			}

			vzm::ObjStates volume_ws_state = model_state;
			volume_ws_state.associated_obj_ids[vzm::ObjStates::USAGE::VR_OTF] = vr_tmap_id;
			volume_ws_state.associated_obj_ids[vzm::ObjStates::USAGE::MPR_WINDOWING] = mpr_tmap_id;

			volume_ws_state.associated_obj_ids[vzm::ObjStates::USAGE::VR_OTF] = vr_tmap_id1;

			double sample_rate = 1. / scale_factor;
			vzm::SetRenderTestParam("_double_UserSampleRate", sample_rate, sizeof(double), -1, -1);// g_info.model_scene_id, model_cam_id);
			bool apply_samplerate2grad = true;
			vzm::SetRenderTestParam("_bool_ApplySampleRateToGradient", apply_samplerate2grad, sizeof(bool), -1, -1);//g_info.model_scene_id, model_cam_id);

			volume_ws_state.is_visible = false;
			vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, g_info.model_volume_id, volume_ws_state);
			vzm::ReplaceOrAddSceneObject(g_info.model_scene_id, g_info.model_volume_id, volume_ws_state);
		}
		vzm::ReplaceOrAddSceneObject(g_info.model_scene_id, g_info.model_ms_obj_id, model_state);

		vzm::CameraParameters rs_cam_params;
		__cv3__ rs_cam_params.pos = glm::fvec3(0);
		__cv3__ rs_cam_params.up = glm::fvec3(0, 1, 0);
		__cv3__ rs_cam_params.view = glm::fvec3(0, 0, 1);

		rs_cam_params.fx = rs_settings::rgb_intrinsics.fx;
		rs_cam_params.fy = rs_settings::rgb_intrinsics.fy;
		rs_cam_params.cx = rs_settings::rgb_intrinsics.ppx;
		rs_cam_params.cy = rs_settings::rgb_intrinsics.ppy;
		rs_cam_params.sc = 0;
		rs_cam_params.w = rs_settings::rgb_intrinsics.width;
		rs_cam_params.h = rs_settings::rgb_intrinsics.height;
		rs_cam_params.np = 0.1f;
		rs_cam_params.fp = 20.0f;
		rs_cam_params.projection_mode = 3;

		vzm::SetCameraParameters(g_info.rs_scene_id, rs_cam_params, rs_cam_id);

		Show_Window_with_Info(g_info.window_name_ms_view, g_info.model_scene_id, model_cam_id, g_info);

		optitrk::UpdateFrame();
		glm::fmat4x4 mat_cam0_to_ws, mat_cam1_to_ws;
		optitrk::GetCameraLocation(0, (float*)&mat_cam0_to_ws);
		optitrk::GetCameraLocation(1, (float*)&mat_cam1_to_ws);
		Update_CamModel(g_info.ws_scene_id, mat_cam0_to_ws, "IR CAM 0", 0);
		Update_CamModel(g_info.ws_scene_id, mat_cam1_to_ws, "IR CAM 1", 1);

		// make 3d ui widgets
		GenWorldGrid(g_info.ws_scene_id, ov_cam_id);

		//vzm::SetRenderTestParam("_double_AbsCopVZThickness", 0.001, sizeof(double), -1, -1);

		//{
		//	int coord_grid_obj_id = 0, axis_lines_obj_id = 0, axis_texX_obj_id = 0, axis_texZ_obj_id = 0;
		//	World_GridAxis_Gen(coord_grid_obj_id, axis_lines_obj_id, axis_texX_obj_id, axis_texZ_obj_id);
		//	vzm::ObjStates grid_obj_state;
		//	grid_obj_state.color[3] = 0.7f;
		//	grid_obj_state.line_thickness = 0;
		//	vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, coord_grid_obj_id, grid_obj_state);
		//	bool foremost_surf_rendering = false;
		//	vzm::SetRenderTestParam("_bool_OnlyForemostSurfaces", foremost_surf_rendering, sizeof(bool), g_info.ws_scene_id, ov_cam_id, coord_grid_obj_id);
		//	grid_obj_state.color[3] = 0.9f;
		//	vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, axis_lines_obj_id, grid_obj_state);
		//	*(glm::fvec4*) grid_obj_state.color = glm::fvec4(1, 0.3, 0.3, 0.6);
		//	vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, axis_texX_obj_id, grid_obj_state);
		//	*(glm::fvec4*) grid_obj_state.color = glm::fvec4(0.3, 0.3, 1, 0.6);
		//	vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, axis_texZ_obj_id, grid_obj_state);
		//	SetDashEffectInRendering(g_info.ws_scene_id, ov_cam_id, coord_grid_obj_id, 0.01);
		//
		//	vzm::SetRenderTestParam("_bool_IsDashed", true, sizeof(bool), g_info.ws_scene_id, ov_cam_id, coord_grid_obj_id);
		//	vzm::SetRenderTestParam("_bool_IsInvertColorDashLine", false, sizeof(bool), g_info.ws_scene_id, ov_cam_id, coord_grid_obj_id);
		//	vzm::SetRenderTestParam("_double_LineDashInterval", 2.0, sizeof(double), g_info.ws_scene_id, ov_cam_id, coord_grid_obj_id);
		//}


		// touch interface buttons
		Make_Buttons(g_info.rs_w, g_info.rs_h, g_info.rs_buttons);
	}

	void SetCvWindows()
	{
//#define __DOJO_PC
#define __DEMO_PC
#ifdef __DOJO_PC
		//Create a window
		cv::namedWindow(g_info.window_name_rs_view, WINDOW_NORMAL);
		cv::namedWindow(g_info.window_name_ws_view, WINDOW_AUTOSIZE | WINDOW_NORMAL);
		cv::namedWindow(g_info.window_name_ms_view, WINDOW_AUTOSIZE | WINDOW_NORMAL);
		cv::namedWindow(g_info.window_name_stg_view, WINDOW_AUTOSIZE | WINDOW_NORMAL);

		cv::moveWindow(g_info.window_name_ws_view, 2560, 0);
		cv::moveWindow(g_info.window_name_ms_view, 2560 + 1000, 0);

		cv::moveWindow(g_info.window_name_rs_view, 2560 * 3, 0);
		cv::moveWindow(g_info.window_name_stg_view, 2560 * 2, 700);
		//cv::moveWindow(g_info.window_name_rs_view, 0 * 3, 0);
		//cv::moveWindow(g_info.window_name_stg_view, 0 * 3 + 1024, 0);

		cv::setWindowProperty(g_info.window_name_rs_view, WND_PROP_FULLSCREEN, WINDOW_FULLSCREEN);
		//cv::setWindowProperty(g_info.window_name_stg_view, WND_PROP_FULLSCREEN, WINDOW_FULLSCREEN);
#endif

#ifdef __DEMO_PC
		const int display1_w = 1680 + 2;// 1920;
		const int display2_w = 2000 + 2;
		// for demo PC
		//Create a window
		cv::namedWindow(g_info.window_name_rs_view, WINDOW_NORMAL);
		cv::namedWindow(g_info.window_name_ws_view, WINDOW_NORMAL);
		cv::namedWindow(g_info.window_name_ms_view, WINDOW_NORMAL);
		cv::namedWindow(g_info.window_name_stg_view, WINDOW_NORMAL);

		//cv::moveWindow(g_info.window_name_rs_view, 0, 0);
		//cv::moveWindow(g_info.window_name_stg_view, 0, 500);
		//cv::moveWindow(g_info.window_name_ws_view, 900, 0);
		//cv::moveWindow(g_info.window_name_ms_view, 900, 500);
		//cv::setWindowProperty(g_info.window_name_rs_view, WND_PROP_AUTOSIZE, WINDOW_NORMAL);
		//cv::setWindowProperty(g_info.window_name_stg_view, WND_PROP_AUTOSIZE, WINDOW_NORMAL);
		//cv::setWindowProperty(g_info.window_name_ws_view, WND_PROP_AUTOSIZE, WINDOW_NORMAL);
		//cv::setWindowProperty(g_info.window_name_ms_view, WND_PROP_AUTOSIZE, WINDOW_NORMAL);

#ifdef __MIRRORS
		cv::namedWindow("rs mirror", WINDOW_NORMAL);
		cv::namedWindow("stg mirror", WINDOW_NORMAL);
		//cv::moveWindow("rs mirror", display1_w, 30);
		//cv::moveWindow("stg mirror", display1_w + display2_w, 30);
		//cv::setWindowProperty("rs mirror", WND_PROP_FULLSCREEN, WINDOW_NORMAL | WINDOW_FULLSCREEN);
		//cv::setWindowProperty("stg mirror", WND_PROP_FULLSCREEN, WINDOW_NORMAL | WINDOW_FULLSCREEN);
#endif
		//cv::moveWindow(g_info.window_name_rs_view, 0, 0);
		//cv::moveWindow(g_info.window_name_stg_view, display_w + 1025, 0);

		////cv::moveWindow(g_info.window_name_rs_view, 0 * 3, 0);
		////cv::moveWindow(g_info.window_name_stg_view, 0 * 3 + 1024, 0);

		//cv::setWindowProperty(g_info.window_name_stg_view, WND_PROP_FULLSCREEN, WINDOW_FULLSCREEN);
#endif

		// for developers
		//cv::namedWindow(g_info.window_name_rs_view, WINDOW_AUTOSIZE);
		//cv::namedWindow(g_info.window_name_ws_view, WINDOW_AUTOSIZE);
		//cv::namedWindow(g_info.window_name_ms_view, WINDOW_AUTOSIZE);
		//cv::namedWindow(g_info.window_name_stg_view, WINDOW_AUTOSIZE);

		static EventGlobalInfo rg_info_world(g_info, g_info.ws_scene_id, ov_cam_id);
		cv::setMouseCallback(g_info.window_name_ws_view, CallBackFunc_WorldMouse, &rg_info_world);
		static EventGlobalInfo rg_info_model(g_info, g_info.model_scene_id, model_cam_id);
		cv::setMouseCallback(g_info.window_name_ms_view, CallBackFunc_ModelMouse, &rg_info_model);

		static EventGlobalInfo rg_info_rs(g_info, g_info.rs_scene_id, rs_cam_id);
		cv::setMouseCallback(g_info.window_name_rs_view, CallBackFunc_RsMouse, &rg_info_rs);
		cv::setMouseCallback("rs mirror", CallBackFunc_RsMouse, &rg_info_rs);
		
		//static EventGlobalInfo rg_info_stg(g_info, 0, 0);
		//cv::setMouseCallback(g_info.window_name_stg_view, CallBackFunc_StgMouse, &rg_info_stg);
	}

	void LoadPresets()
	{
		// loading 3d points
		g_info.otrk_data.calib_3d_pts.clear();
		std::ifstream infile(g_info.cb_positions);
		string line;
		if (infile.is_open())
		{
			while (getline(infile, line))
			{
				std::istringstream iss(line);
				float a, b, c;
				if (!(iss >> a >> b >> c)) { break; } // error
				g_info.otrk_data.calib_3d_pts.push_back(Point3f(a, b, c));
				// process pair (a,b)
			}
			infile.close();
		}

		// loading tool_se points
		g_info.otrk_data.custom_pos_map.clear();
		for (auto it : g_info.custom_pos_file_paths)
		{
			string file_path = it.second;
			infile = std::ifstream(file_path);
			if (infile.is_open())
			{
				vector<Point3f>& custom_pos_list = g_info.otrk_data.custom_pos_map[it.first];
				while (getline(infile, line))
				{
					std::istringstream iss(line);
					float a, b, c;
					if (!(iss >> a >> b >> c)) { break; } // error
					custom_pos_list.push_back(Point3f(a, b, c));
					// process pair (a,b)
				}
				infile.close();
			}
		}

		// loading rs calib pairs and matrix
		infile = std::ifstream(g_info.rs_calib);
		if (infile.is_open())
		{
			g_info.otrk_data.tc_calib_pt_pairs.clear();
			float* mat_data = glm::value_ptr(mat_rscs2clf);
			int line_idx = 0, line_pairs = 100000;
			while (getline(infile, line))
			{
				std::istringstream iss(line);
				if (line_idx == 0)
				{
					iss >> line_pairs;
				}
				else if (line_idx < line_pairs + 1)
				{
					glm::fvec2 p2d;
					glm::fvec3 p3d;
					iss >> p2d.x >> p2d.y >> p3d.x >> p3d.y >> p3d.z;
					g_info.otrk_data.tc_calib_pt_pairs.push_back(PAIR_MAKE(p2d, p3d));
				}
				else
				{
					iss >> mat_data[line_idx - line_pairs - 1];
				}
				// process pair (a,b)
				line_idx++;
			}
			g_info.is_calib_rs_cam = true;
			infile.close();


			// loading model point pairs
			infile = std::ifstream(g_info.model_predefined_pts);
			if (infile.is_open())
			{
				g_info.model_ms_pick_pts.clear();
				while (getline(infile, line))
				{
					std::istringstream iss(line);
					float a, b, c;
					if (!(iss >> a >> b >> c)) { break; } // error
					g_info.model_ms_pick_pts.push_back(glm::fvec3(a, b, c));
				}
				infile.close();

				if (g_info.model_ms_pick_pts.size() > 0)
				{
					vector<glm::fvec4> spheres_xyzr;
					vector<glm::fvec3> spheres_rgb;
					for (int i = 0; i < (int)g_info.model_ms_pick_pts.size(); i++)
					{
						glm::fvec4 sphere_xyzr = glm::fvec4(g_info.model_ms_pick_pts[i], 0.002);
						spheres_xyzr.push_back(sphere_xyzr);
						glm::fvec3 sphere_rgb = glm::fvec3(0.2, 0.3, 1);
						spheres_rgb.push_back(sphere_rgb);
					}
					vzm::ObjStates sobj_state;
					sobj_state.color[3] = 1.0f;
					sobj_state.emission = 0.5f;
					sobj_state.diffusion = 0.5f;
					sobj_state.specular = 0.0f;
					vzm::GenerateSpheresObject(__FP spheres_xyzr[0], __FP spheres_rgb[0], (int)g_info.model_ms_pick_pts.size(), g_info.model_ms_pick_spheres_id);
					vzm::ReplaceOrAddSceneObject(g_info.model_scene_id, g_info.model_ms_pick_spheres_id, sobj_state);
					Show_Window_with_Info(g_info.window_name_ms_view, g_info.model_scene_id, model_cam_id, g_info);
				}
			}
		}

		// loading stg calib points
		infile = std::ifstream(g_info.stg_calib);
		if (infile.is_open())
		{
			g_info.otrk_data.stg_calib_pt_pairs.clear();
			g_info.otrk_data.stg_calib_pt_pairs_2.clear();
			int line_idx = 0, line_pairs = 100000;
			bool is_second_stg_pairs = false;
			while (getline(infile, line))
			{
				std::istringstream iss(line);
				if (line == "*** SECOND STG DISPLAY ***")
				{
					line_idx = 0;
					is_second_stg_pairs = true;
					continue;
				}
				if (line_idx == 0)
				{
					iss >> line_pairs;
				}
				else if (line_idx < line_pairs + 1)
				{
					Point2f p2d;
					Point3f p3d;
					iss >> p2d.x >> p2d.y >> p3d.x >> p3d.y >> p3d.z;
					if (!is_second_stg_pairs)
						g_info.otrk_data.stg_calib_pt_pairs.push_back(pair<Point2f, Point3f>(p2d, p3d));
					else
						g_info.otrk_data.stg_calib_pt_pairs_2.push_back(pair<Point2f, Point3f>(p2d, p3d));
				}
				// process pair (a,b)
				line_idx++;
			}
			infile.close();
		}

		infile = std::ifstream(g_info.model_view_preset);
		if (infile.is_open())
		{
			vzm::CameraParameters cam_params;
			vzm::GetCameraParameters(g_info.model_scene_id, cam_params, 1);
			int cam_loaded = 0;
			while (getline(infile, line))
			{
				istringstream iss(line);
				string param_name;
				iss >> param_name;
#define SET_CAM(A) A[0] >> A[1] >> A[2]

				if (param_name == "cam_pos")
				{
					cam_loaded++;
					iss >> SET_CAM(cam_params.pos);
				}
				else if (param_name == "cam_up")
				{
					cam_loaded++;
					iss >> SET_CAM(cam_params.up);
				}
				else if (param_name == "cam_view")
				{
					cam_loaded++;
					iss >> SET_CAM(cam_params.view);
				}
			}
			if (cam_loaded == 3)
			{
				vzm::SetCameraParameters(g_info.model_scene_id, cam_params, model_cam_id);
				Show_Window_with_Info(g_info.window_name_ms_view, g_info.model_scene_id, model_cam_id, g_info);
			}
		}

		infile = std::ifstream(g_info.custom_pos_file_paths["guide_lines"]);
		if (infile.is_open())
		{
			getline(infile, line);
			std::istringstream iss_num(line);

			int screwcount;
			iss_num >> screwcount;

			while (getline(infile, line))
			{
				std::istringstream iss(line);
				float a, b, c, d, e, f, g;
				if (!(iss >> a >> b >> c >> d >> e >> f)) { break; } // error

				const float line_leng = 10.f;
				glm::fvec3 p = glm::fvec3(a, b, c);
				glm::fvec3 dir = glm::normalize(glm::fvec3(d, e, f) - p);

				g_info.guide_lines_target_rbs.push_back(std::pair<glm::fvec3, glm::fvec3>(p, dir));
			}
			infile.close();

			//vzm::GenerateCylindersObject((float*)&needles_pos[0], &needles_radii[0], (float*)&needles_clr[0], screwcount, needles_guide_id);

		}
	}

	/////////////////////////////////////////////////////////////
	int GetCameraID_SSU(const int scene_id)
	{
		// ov_cam_id
		// model_cam_id
		// rs_cam_id
		// stg_cam_id
		// zoom_cam_id
		if (scene_id == g_info.ws_scene_id) {
			return ov_cam_id;
		}
		else if (scene_id == g_info.rs_scene_id) {
			return rs_cam_id;
		}
		else if (scene_id == g_info.model_scene_id) {
			return model_cam_id;
		}
		else if (scene_id == g_info.stg_scene_id) {
			return stg_cam_id;
		}

		return -1;
	}

	/////////////////////////////////////////////////////////////

	auto clear_record_info = [&]()
	{
		for (int i = 0; i < (int)record_rsimg.size(); i++)
		{
			delete[] record_rsimg[i];
		}
		record_trk_info.clear();
		record_rsimg.clear();
		action_info.clear();
		record_key.clear();
	};

	void ResetCalib()
	{
		g_info.otrk_data.calib_3d_pts.clear();
		g_info.otrk_data.stg_calib_pt_pairs.clear();
		g_info.otrk_data.stg_calib_pt_pairs_2.clear();
		g_info.otrk_data.tc_calib_pt_pairs.clear();
		g_info.is_calib_stg_cam = g_info.is_calib_stg_cam_2 = g_info.is_calib_rs_cam = false;
		g_info.model_predefined_pts.clear();
		cout << "CLEAR calibration points" << endl;
	}

	void StoreRecordInfo()
	{
		const int w = g_info.rs_w;
		const int h = g_info.rs_h;
		cout << "WRITE RECODING INFO of " << record_trk_info.size() << " frames" << endl;

		std::fstream file_imgdata;
		file_imgdata.open("imgdata.bin", std::ios::app | std::ios::binary);
		file_imgdata.clear();
		file_imgdata.write(reinterpret_cast<char*>(&record_rsimg[0]), w * h * 3 * sizeof(char) * record_rsimg.size());
		file_imgdata.close();

		std::fstream file_trkdata;
		file_trkdata.open("trkdata.bin", std::ios::app | std::ios::binary);
		file_trkdata.clear();
		for (int i = 0; i < (int)record_trk_info.size(); i++)
		{
			size_t buf_size_bytes;
			char* _buf = record_trk_info[i].GetSerialBuffer(buf_size_bytes);
			file_trkdata.write(_buf, buf_size_bytes);
			delete[] _buf;
		}
		file_trkdata.close();

		ofstream file_keydata("keyrecord.txt");
		if (file_keydata.is_open())
		{
			file_keydata.clear();
			file_keydata << to_string(record_key.size()) << endl;
			for (int i = 0; i < (int)record_key.size(); i++)
			{
				file_keydata << to_string(record_key[i]) << endl;
			}
		}
		file_keydata.close();

		clear_record_info();
	}

	static int probe_line_id = 0, probe_tip_id = 0;
	void UpdateTrackInfo(const void* trk_info, const std::string& probe_specifier_rb_name, int _probe_mode)
	{
		PROBE_MODE probe_mode = (PROBE_MODE)_probe_mode;
		g_info.probe_rb_name = probe_specifier_rb_name;
		g_info.otrk_data.trk_info = *(track_info*)trk_info;
		is_rsrb_detected = g_info.otrk_data.trk_info.GetLFrmInfo("rs_cam", mat_clf2ws);
		mat_ws2clf = glm::inverse(mat_clf2ws);

		glm::fmat4x4 mat_opti_probe2ws;
		//bool is_probe_detected = g_info.otrk_data.trk_info.GetLFrmInfo("probe", mat_opti_probe2ws);
		bool is_probe_detected = g_info.otrk_data.trk_info.GetLFrmInfo(probe_specifier_rb_name, mat_opti_probe2ws);
		if (is_probe_detected)
		{
			//g_info.pos_probe_pin = tr_pt(mat_opti_probe2ws, glm::fvec3(0));
			glm::fvec3 probe_tip = tr_pt(mat_opti_probe2ws, glm::fvec3(0));
			glm::fvec3 probe_dir_se;
			if (probe_mode == ONLY_PIN_POS)
			{
				vector<Point3f>& custom_pos_list = g_info.otrk_data.custom_pos_map[probe_specifier_rb_name];
				if (custom_pos_list.size() > 0)
				{
					glm::fvec3 pos_e = *(glm::fvec3*)&custom_pos_list[0];
					probe_dir_se = glm::normalize(tr_pt(mat_opti_probe2ws, pos_e) - probe_tip);
				}
			}
			else if (probe_mode == ONLY_RBFRAME)
			{
				vector<Point3f>& custom_pos_list = g_info.otrk_data.custom_pos_map[probe_specifier_rb_name];
				if (custom_pos_list.size() >= 2)
				{
					glm::fvec3 pos_s = *(glm::fvec3*)&custom_pos_list[0];
					probe_tip = tr_pt(mat_opti_probe2ws, pos_s);
					glm::fvec3 pos_e = *(glm::fvec3*)&custom_pos_list[1];
					probe_dir_se = glm::normalize(tr_pt(mat_opti_probe2ws, pos_e) - probe_tip);
				}
			}
			else
			{
				probe_dir_se = glm::normalize(tr_vec(mat_opti_probe2ws, glm::fvec3(0, 0, 1)));
			}
			g_info.pos_probe_pin = probe_tip;
			g_info.dir_probe_se = probe_dir_se;

			glm::fvec3 cyl_p01[2] = { probe_tip, probe_tip + probe_dir_se * 0.2f };
			float cyl_r = 0.0015f;	// 0.002f
			vzm::GenerateCylindersObject((float*)cyl_p01, &cyl_r, NULL, 1, probe_line_id);
			vzm::ObjStates probe_state = default_obj_state;
			__cv4__ probe_state.color = glm::fvec4(0, 1, 1, 1);
			vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, probe_line_id, probe_state);
			vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, probe_line_id, probe_state);
			vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, probe_line_id, probe_state);

			vzm::GenerateSpheresObject(__FP glm::fvec4(probe_tip, 0.0045f), NULL, 1, probe_tip_id);
			__cv4__ probe_state.color = glm::fvec4(1, 1, 1, 1);

			vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, probe_tip_id, probe_state);
			vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, probe_tip_id, probe_state);
			vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, probe_tip_id, probe_state);
		}
		else
		{
			vzm::ObjStates cobj_state = default_obj_state;
			cobj_state.is_visible = false;
			vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, probe_line_id, cobj_state);
			vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, probe_line_id, cobj_state);
			vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, probe_line_id, cobj_state);
			vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, probe_tip_id, cobj_state);
			vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, probe_tip_id, cobj_state);
			vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, probe_tip_id, cobj_state);
		}

		//g_info.is_probe_detected = g_info.otrk_data.trk_info.GetLFrmInfo(probe_specifier_rb_name, g_info.mat_probe2ws);
		g_info.is_probe_detected = is_probe_detected;
		g_info.mat_probe2ws = mat_opti_probe2ws;

		vzm::SetRenderTestParam("_double3_3DTipPos", glm::dvec3(g_info.pos_probe_pin), sizeof(glm::dvec3), -1, -1);

		//if (g_info.is_probe_detected)
		//{
		//	g_info.pos_probe_pin = tr_pt(g_info.mat_probe2ws, glm::fvec3(0));
		//}

		auto set_rb_axis = [](const bool is_detected, const glm::fmat4x4& mat_frm2ws, int& obj_id)
		{
			if (is_detected)
			{
				Axis_Gen(mat_frm2ws, 0.07, obj_id);
				vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, obj_id, default_obj_state);
			}
			else if (obj_id != 0)
			{
				vzm::ObjStates ostate = default_obj_state;
				ostate.is_visible = false;
				vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, obj_id, ostate);
			}
		};
		set_rb_axis(is_rsrb_detected, mat_clf2ws, g_info.otrk_data.rs_lf_axis_id);
		//set_rb_axis(g_info.is_probe_detected, g_info.mat_probe2ws, g_info.otrk_data.probe_lf_axis_id);
	}

	void RecordInfo(const int key_pressed, const void* color_data)
	{
		record_trk_info.push_back(g_info.otrk_data.trk_info);
		char* img_data = new char[g_info.rs_w * g_info.rs_h * 3];
		memcpy(img_data, color_data, sizeof(char) * 3 * g_info.rs_w * g_info.rs_h);
		record_rsimg.push_back(img_data);
		record_key.push_back(key_pressed);
	}

	void SetTcCalibMkPoints()
	{
		bool is_visible = g_info.touch_mode == RsTouchMode::AR_Marker || g_info.touch_mode == RsTouchMode::Calib_TC || g_info.touch_mode == RsTouchMode::Pair_Clear;

		//auto marker_color = [](int idx, int w)
		//{
		//	return glm::fvec3((idx % max(w, 1)) / (float)max(w - 1, 1), (idx / max(w, 1)) / (float)max(w - 1, 1), 1);
		//};
		glm::fmat4x4 mat_armklf2ws;
		bool is_armk_detected = g_info.otrk_data.trk_info.GetLFrmInfo(g_info.otrk_data.marker_rb_name, mat_armklf2ws);

		static int armk_frame_id = 0, cb_spheres_id = 0;
		if (is_armk_detected)
		{
			Axis_Gen(mat_armklf2ws, 0.05f, armk_frame_id);
			vzm::ObjStates cstate = default_obj_state;
			vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, armk_frame_id, cstate);
		}
		else
		{
			vzm::ObjStates cstate = default_obj_state;
			cstate.is_visible = false;
			vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, armk_frame_id, cstate);
		}

		if (is_visible && is_armk_detected)
		{
			vector<glm::fvec4> sphers_xyzr;
			vector<glm::fvec3> sphers_rgb;
			for (int i = 0; i < g_info.otrk_data.calib_3d_pts.size(); i++)
			{
				glm::fvec3 pt = tr_pt(mat_armklf2ws, *(glm::fvec3*)&g_info.otrk_data.calib_3d_pts[i]);
				//sphers_xyzr.push_back(glm::fvec4(pt.x, pt.y, pt.z, 0.008));
				//sphers_rgb.push_back(marker_color(i, (int)g_info.otrk_data.calib_3d_pts.size() / 2));
				sphers_xyzr.push_back(glm::fvec4(pt, 0.01));
				sphers_rgb.push_back(glm::fvec3(0, 1, 1));

				int text_id = 0;
				if (g_info.otrk_data.armk_text_ids.size() > i)
					text_id = g_info.otrk_data.armk_text_ids[i];
				std::vector<glm::fvec3> lt_v_u(3);
				lt_v_u[0] = glm::fvec3(pt.x, pt.y, pt.z) + glm::fvec3(0, 1, 0) * 0.02f;
				lt_v_u[1] = glm::fvec3(0, -1, 0);
				lt_v_u[2] = glm::fvec3(1, 0, 0);
				vzm::GenerateTextObject(__FP lt_v_u, to_string(1), 0.03, false, false, text_id, true);
				if (g_info.otrk_data.armk_text_ids.size() <= i)
					g_info.otrk_data.armk_text_ids.push_back(text_id);
			}
			if (sphers_xyzr.size() > 0)
			{
				vzm::GenerateSpheresObject(__FP sphers_xyzr[0], __FP sphers_rgb[0],
					g_info.otrk_data.calib_3d_pts.size(), cb_spheres_id);
				vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, cb_spheres_id, default_obj_state);
				vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, cb_spheres_id, default_obj_state);

				for (int i = 0; i < (int)g_info.otrk_data.calib_3d_pts.size(); i++)
					vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, g_info.otrk_data.armk_text_ids[i], default_obj_state);
				vzm::ObjStates cstate = default_obj_state;
				cstate.is_visible = false;
				for (int i = g_info.otrk_data.calib_3d_pts.size(); i < (int)g_info.otrk_data.armk_text_ids.size(); i++)
					vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, g_info.otrk_data.armk_text_ids[i], cstate);
			}
			else
			{
				vzm::ObjStates cstate = default_obj_state;
				cstate.is_visible = false;
				vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, cb_spheres_id, cstate);
				vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, cb_spheres_id, cstate);

				for (int i = 0; i < (int)g_info.otrk_data.armk_text_ids.size(); i++)
					vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, g_info.otrk_data.armk_text_ids[i], cstate);
			}
		}
		else
		{
			vzm::ObjStates cstate = default_obj_state;
			cstate.is_visible = false;
			vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, cb_spheres_id, cstate);
			vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, cb_spheres_id, cstate);

			for (int i = 0; i < (int)g_info.otrk_data.armk_text_ids.size(); i++)
				vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, g_info.otrk_data.armk_text_ids[i], cstate);
		}
	}

	void SetMkSpheres(bool is_visible, bool is_pickable)
	{
		auto marker_color = [](int idx, int w)
		{
			return glm::fvec3(1, (idx % max(w, 1)) / (float)max(w - 1, 1), (idx / max(w, 1)) / (float)max(w - 1, 1));
		};

		auto register_mks = [](const glm::fvec3* pos_list, const glm::fvec3& color, const int num_mks, const float r, int& mks_id)
		{
			vector<glm::fvec4> sphers_xyzr;
			vector<glm::fvec3> sphers_rgb;
			for (int i = 0; i < num_mks; i++)
			{
				glm::fvec3 pt = pos_list[i];
				sphers_xyzr.push_back(glm::fvec4(pt.x, pt.y, pt.z, r));
				sphers_rgb.push_back(color);
			}

			if (num_mks > 0)
				vzm::GenerateSpheresObject(__FP sphers_xyzr[0], __FP sphers_rgb[0], num_mks, mks_id);
			else
			{
				vzm::DeleteObject(mks_id);
				mks_id = 0;
			}
		};

		is_pickable |= g_info.touch_mode == RsTouchMode::Calib_STG || g_info.touch_mode == RsTouchMode::Calib_STG2;

		static int mks_spheres_id = 0;
		if (is_visible && !is_pickable)
		{
			register_mks((glm::fvec3*)&g_info.otrk_data.trk_info.mk_xyz_list[0], glm::fvec3(1, 1, 0), g_info.otrk_data.trk_info.mk_xyz_list.size() / 3, 0.005, mks_spheres_id);
			if (mks_spheres_id != 0)
			{
				vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, mks_spheres_id, default_obj_state);
				vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, mks_spheres_id, default_obj_state);
				vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, mks_spheres_id, default_obj_state);
			}
		}
		else
		{
			vzm::ObjStates cstate = default_obj_state;
			cstate.is_visible = false;
			{
				vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, mks_spheres_id, cstate);
				vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, mks_spheres_id, cstate);
				vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, mks_spheres_id, cstate);
			}
		}

		if (is_pickable)
		{
			int num_mks = g_info.otrk_data.trk_info.mk_xyz_list.size() / 3;
			g_info.vzmobjid2pos.clear();
			for (int i = 0; i < (int)g_info.otrk_data.mk_pickable_sphere_ids.size(); i++)
				vzm::DeleteObject(g_info.otrk_data.mk_pickable_sphere_ids[i]);
			g_info.otrk_data.mk_pickable_sphere_ids.clear();

			for (int i = 0; i < num_mks; i++)
			{
				g_info.otrk_data.mk_pickable_sphere_ids.push_back(0);
				int& pickable_mk_id = g_info.otrk_data.mk_pickable_sphere_ids[i];
				glm::fvec3 pt = g_info.otrk_data.trk_info.GetMkPos(i);
				vzm::GenerateSpheresObject(__FP glm::fvec4(pt.x, pt.y, pt.z, 0.015), __FP marker_color(i, 7), 1, pickable_mk_id);
				g_info.vzmobjid2pos[pickable_mk_id] = pt;
				vzm::ValidatePickTarget(pickable_mk_id);
				vzm::ObjStates cstate = default_obj_state;
				if (g_info.touch_mode == RsTouchMode::Calib_STG || g_info.touch_mode == RsTouchMode::Calib_STG2)
					vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, pickable_mk_id, cstate);
				else
					vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, pickable_mk_id, cstate);
			}
		}
		else
		{
			for (int i = 0; i < (int)g_info.otrk_data.mk_pickable_sphere_ids.size(); i++)
				vzm::DeleteObject(g_info.otrk_data.mk_pickable_sphere_ids[i]);
			g_info.otrk_data.mk_pickable_sphere_ids.clear();
			g_info.vzmobjid2pos.clear();
		}

		//
		static int dst_custom_spheres_id = 0;
		bool hide_dst_custom_spheres = true;
		if (g_info.touch_mode == RsTouchMode::DST_TOOL_E0 || g_info.touch_mode == RsTouchMode::DST_TOOL_SE0 || g_info.touch_mode == RsTouchMode::DST_TOOL_SE1)
		{
			vector<Point3f>& dst_pos_list = g_info.otrk_data.custom_pos_map[g_info.dst_tool_name];
			glm::fmat4x4 mat_dstfrm2ws;
			bool is_tool_dst_tracked = g_info.otrk_data.trk_info.GetLFrmInfo(g_info.dst_tool_name, mat_dstfrm2ws);
			if (is_tool_dst_tracked && dst_pos_list.size() > 0)
			{
				hide_dst_custom_spheres = false;
				register_mks((glm::fvec3*)&dst_pos_list[0], glm::fvec3(1, 0, 1), dst_pos_list.size(), 0.005, dst_custom_spheres_id);
				vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, dst_custom_spheres_id, default_obj_state);
				vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, dst_custom_spheres_id, default_obj_state);
				vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, dst_custom_spheres_id, default_obj_state);
			}
		}
		if (hide_dst_custom_spheres || (dst_custom_spheres_id == 0))
		{
			vzm::ObjStates cstate = default_obj_state;
			cstate.is_visible = false;
			vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, dst_custom_spheres_id, cstate);
			vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, dst_custom_spheres_id, cstate);
			vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, dst_custom_spheres_id, cstate);
		}
	}

	void GetVarInfo(void* ginfo)
	{
		*(GlobalInfo*)ginfo = g_info;
	}

	void GetVarInfoPtr(void** ginfo_ptr)
	{
		*ginfo_ptr = &g_info;
	}

	void SetVarInfo(const void* ginfo)
	{
		g_info = *(GlobalInfo*)ginfo;
	}

	void TryCalibrationTC(Mat& imgColor)
	{
		auto marker_color = [](int idx, int w)
		{
			return glm::fvec3((idx % max(w, 1)) / (float)max(w - 1, 1), (idx / max(w, 1)) / (float)max(w - 1, 1), 1);
		};
		if (g_info.touch_mode == RsTouchMode::Calib_TC && is_rsrb_detected && g_info.otrk_data.calib_3d_pts.size() > 0)
		{
			// calibration routine
			Mat viewGray;
			cvtColor(imgColor, viewGray, COLOR_BGR2GRAY);

			std::vector<__MarkerDetInfo> list_det_armks;
			ar_marker.track_markers(list_det_armks, viewGray.data, viewGray.cols, viewGray.rows, mk_ids);

			for (int i = 0; i < (int)list_det_armks.size(); i++)
			{
				__MarkerDetInfo& armk = list_det_armks[i];

				int id = armk.id;
				glm::fvec3 _rgb = marker_color(id - 1, g_info.otrk_data.calib_3d_pts.size() / 2);

				for (int j = 0; j < 4; j++)
					circle(imgColor, Point(armk.corners2d[2 * j + 0], armk.corners2d[2 * j + 1]), 1, CV_RGB(_rgb.r * 255, _rgb.g * 255, _rgb.b * 255), 2);
			}

			if (list_det_armks.size() > 0)
			{
				vector<Point2f> point2d;
				vector<Point3f> point3dws;

				glm::fmat4x4 mat_armklf2ws;
				bool marker_rb_detected = g_info.otrk_data.trk_info.GetLFrmInfo(g_info.otrk_data.marker_rb_name, mat_armklf2ws);
				if (g_info.otrk_data.marker_rb_name != "" && !marker_rb_detected) return;

				for (int i = 0; i < (int)list_det_armks.size(); i++)
				{
					__MarkerDetInfo& armk = list_det_armks[i];
					if (armk.id > g_info.otrk_data.calib_3d_pts.size()) continue;

					Point2f pt2d = Point2f(0, 0);
					vector<float>& cpts = armk.corners2d;
					for (int k = 0; k < 4; k++)
						pt2d += Point2f(cpts[k * 2 + 0], cpts[k * 2 + 1]);

					point2d.push_back(pt2d / 4.f);
					const glm::fvec3& pos_3d = *(glm::fvec3*)&g_info.otrk_data.calib_3d_pts[armk.id - 1];
					const Point3f& pos_3dws = *(Point3f*)&tr_pt(mat_armklf2ws, pos_3d);
					point3dws.push_back(pos_3dws);
				}


				static glm::fmat4x4 prev_mat_clf2ws = mat_clf2ws;
				static glm::fmat4x4 prev_mat_armklf2ws = mat_armklf2ws;
				float diff_len = glm::length(tr_pt(mat_clf2ws, glm::fvec3()) - tr_pt(prev_mat_clf2ws, glm::fvec3()))
					+ glm::length(tr_pt(mat_armklf2ws, glm::fvec3()) - tr_pt(prev_mat_armklf2ws, glm::fvec3()));

				//bitset
				//auto get_quter = [](const glm::fmat4x4& tr)
				//{
				//	//glm::vec3 scale;
				//	//glm::quat rotation;
				//	//glm::vec3 translation;
				//	//glm::vec3 skew;
				//	//glm::vec4 perspective;
				//	//glm::decompose(tr, scale, rotation, translation, skew, perspective);
				//	glm::quat rotation = glm::toQuat(tr);
				//};
				//glm::quat q_c = glm::toQuat(mat_clf2ws);
				//glm::quat q_prev = glm::toQuat(prev_mat_clf2ws);
				//glm::fvec3 rx
				/**/

				if (diff_len > 0.05 && point2d.size() > 0)
				{
					prev_mat_clf2ws = mat_clf2ws;
					prev_mat_armklf2ws = mat_armklf2ws;
					float pnp_err = -1.f;
					int calib_samples = 0;
					glm::fmat4x4 __mat_rscs2clf = mat_rscs2clf;

					static int err_count = 0;
					bool is_success = CalibrteCamLocalFrame(*(vector<glm::fvec2>*)&point2d, *(vector<glm::fvec3>*)&point3dws, mat_ws2clf,
						rs_settings::rgb_intrinsics.fx, rs_settings::rgb_intrinsics.fy, rs_settings::rgb_intrinsics.ppx, rs_settings::rgb_intrinsics.ppy,
						__mat_rscs2clf, &pnp_err, &calib_samples, g_info.otrk_data.tc_calib_pt_pairs);
					if (pnp_err >= 5 && calib_samples >= 12)
					{
						//calib_samples == 12 ? g_info.otrk_data.tc_calib_pt_pairs.clear() : 
							g_info.otrk_data.tc_calib_pt_pairs.pop_back();
					}
					else if (is_success)
					{
						//static int calib_frame_id = 0;
						//Axis_Gen(mat_clf2ws, 0.05f, calib_frame_id);
						//vzm::ObjStates cstate = default_obj_state;
						//cstate.color[3] = 0.3f;
						//vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, calib_frame_id, cstate);
						//g_info.otrk_data.calib_trial_rs_cam_frame_ids.push_back(calib_frame_id);

						mat_rscs2clf = __mat_rscs2clf;

						g_info.is_calib_rs_cam = true;
						// 
						ofstream outfile(g_info.rs_calib);
						if (outfile.is_open())
						{
							outfile.clear();

							outfile << to_string(point2d.size()) << endl;
							for (int i = 0; i < (int)point2d.size(); i++)
							{
								pair<Point2f, Point3f>& _pair = g_info.otrk_data.tc_calib_pt_pairs[i];
								Point2d p2d = std::get<0>(_pair);
								Point3d p3d_clf = std::get<1>(_pair);
								string line = to_string(p2d.x) + " " + to_string(p2d.y) + " " + to_string(p3d_clf.x) + " " + to_string(p3d_clf.y) + " " + to_string(p3d_clf.z);
								outfile << line << endl;
							}

							float* d = glm::value_ptr(mat_rscs2clf);
							for (int i = 0; i < 16; i++)
							{
								string line = to_string(d[i]);
								outfile << line << endl;
							}
						}
						outfile.close();
					}
				}
			}
		}

		// display samples //
		if (g_info.touch_mode == RsTouchMode::AR_Marker || g_info.touch_mode == RsTouchMode::Calib_TC || g_info.touch_mode == RsTouchMode::Pair_Clear)
		{
			if (g_info.otrk_data.marker_rb_name == "")
			{
				for (int i = 0; i < g_info.otrk_data.calib_trial_rs_cam_frame_ids.size(); i++)
				{
					vzm::ObjStates cstate;
					vzm::GetSceneObjectState(g_info.ws_scene_id, g_info.otrk_data.calib_trial_rs_cam_frame_ids[i], cstate);
					cstate.is_visible = true;
					vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, g_info.otrk_data.calib_trial_rs_cam_frame_ids[i], cstate);
				}
			}
			else if(g_info.otrk_data.tc_calib_pt_pairs.size() > 0)
			{
				vector<glm::fvec4> ws_armk_spheres_xyzr;
				vector<glm::fvec3> ws_armk_spheres_rgb;
				for (int i = 0; i < (int)g_info.otrk_data.tc_calib_pt_pairs.size(); i++)
				{
					const glm::fvec3 pos_3d = tr_pt(mat_clf2ws ,*(glm::fvec3*)&g_info.otrk_data.tc_calib_pt_pairs[i].second);

					ws_armk_spheres_xyzr.push_back(glm::fvec4(pos_3d, 0.007));
					ws_armk_spheres_rgb.push_back(glm::fvec3(1));
				}

				static int calib_armks_id = 0;
				vzm::GenerateSpheresObject(__FP ws_armk_spheres_xyzr[0], __FP ws_armk_spheres_rgb[0], g_info.otrk_data.tc_calib_pt_pairs.size(), calib_armks_id);
				vzm::ObjStates cstate = default_obj_state;
				cstate.color[3] = 0.3f;
				vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, calib_armks_id, cstate);
			}
		}

		if (g_info.is_calib_rs_cam)
		{
			glm::fmat4x4 mat_rscs2ws = mat_clf2ws * mat_rscs2clf;

			//rs_cam_tris_id, rs_cam_lines_id, rs_cam_txt_id
			if (is_rsrb_detected)
				Update_CamModel(g_info.ws_scene_id, mat_rscs2ws, "RS CAM 0", 2);

			vzm::CameraParameters _rs_cam_params;
			vzm::GetCameraParameters(g_info.rs_scene_id, _rs_cam_params, rs_cam_id);
			ComputeCameraStates(mat_rscs2clf, mat_clf2ws, _rs_cam_params);
			vzm::SetCameraParameters(g_info.rs_scene_id, _rs_cam_params, rs_cam_id);

			vzm::SceneEnvParameters scn_env_params;
			vzm::GetSceneEnvParameters(g_info.ws_scene_id, scn_env_params);
			//scn_env_params.is_on_camera = false;
			__cv3__ scn_env_params.pos_light = __cv3__ _rs_cam_params.pos;
			__cv3__ scn_env_params.dir_light = __cv3__ _rs_cam_params.view;
			vzm::SetSceneEnvParameters(g_info.ws_scene_id, scn_env_params);
		}
	}

	void TryCalibrationSTG()
	{
		static int mk_stg_calib_sphere_id = 0;
		static int clf_mk_stg_calib_spheres_id = 0;
		static int last_calib_pair = 0;
		static int last_calib_pair_2 = 0;

#ifdef STG_LINE_CALIB
		static Point2d pos_calib_lines[4] = { Point2d(100, 100), Point2d(400, 400), Point2d(400, 100), Point2d(100, 400) };
#endif
		if (g_info.touch_mode == RsTouchMode::Calib_STG || g_info.touch_mode == RsTouchMode::Calib_STG2)
		{
			int stg_calib_mk_idx;
			bool exist_mk_cid = g_info.otrk_data.trk_info.CheckExistCID(g_info.otrk_data.stg_calib_mk_cid, &stg_calib_mk_idx);
			if (exist_mk_cid)
			{
				glm::fvec3 pos_stg_calib_mk = g_info.otrk_data.trk_info.GetMkPos(stg_calib_mk_idx);
				vzm::GenerateSpheresObject(__FP glm::fvec4(pos_stg_calib_mk, 0.02), __FP glm::fvec3(1), 1, mk_stg_calib_sphere_id);
				vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, mk_stg_calib_sphere_id, default_obj_state);
				vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, mk_stg_calib_sphere_id, default_obj_state);
			}
			else
			{
				vzm::ObjStates cstate;
				vzm::GetSceneObjectState(g_info.ws_scene_id, mk_stg_calib_sphere_id, cstate);
				cstate.is_visible = false;
				vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, mk_stg_calib_sphere_id, cstate);
				vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, mk_stg_calib_sphere_id, cstate);
			}

			vector<glm::fvec4> ws_mk_spheres_xyzr;
			vector<glm::fvec3> ws_mk_spheres_rgb;
			glm::fvec3 color_pick[2] = { glm::fvec3(1, 0, 0) , glm::fvec3(0, 0, 1) };
			for (int i = 0; i < g_info.stg_display_num; i++)
			{
				vector<pair<Point2f, Point3f>>& stg_calib_pt_pairs = i == 0? g_info.otrk_data.stg_calib_pt_pairs : g_info.otrk_data.stg_calib_pt_pairs_2;

				int num_stg_calib_pairs = (int)stg_calib_pt_pairs.size();

				vector<Point2f> point2d;
				vector<Point3f> point3d;
				const glm::fvec3 color_pick_mks = color_pick[i];
				for (int j = 0; j < num_stg_calib_pairs; j++)
				{
					pair<Point2f, Point3f>& pr = stg_calib_pt_pairs[j];
					point2d.push_back(get<0>(pr));
					Point3f pos_pt = get<1>(pr);
					point3d.push_back(pos_pt);

					glm::fvec3 pos_mk_ws = tr_pt(mat_clf2ws, __cv3__ &pos_pt);
					ws_mk_spheres_xyzr.push_back(glm::fvec4(pos_mk_ws, 0.007));
					ws_mk_spheres_rgb.push_back(color_pick_mks);
				}

				int& __last_calib_pair = i == 0 ? last_calib_pair : last_calib_pair_2;
				if (num_stg_calib_pairs >= 12 && __last_calib_pair != num_stg_calib_pairs)
				{
					cout << "compute STG calibration : " << i << " display" << endl;
					__last_calib_pair = num_stg_calib_pairs;
					vzm::CameraParameters cam_state_calbirated;

					// calculate intrinsics and extrinsics
					// crbs means Camera RigidBody Space
					{
						glm::fvec3 pos_crbs, view_crbs, up_crbs;
						glm::fmat4x4 mat_clf2clf;
						helpers::ComputeArCameraCalibrateInfo(__FP mat_clf2clf, __FP point3d[0], __FP point2d[0], num_stg_calib_pairs,
							__FP (i == 0 ? mat_stgcs2clf : mat_stgcs2clf_2), &cam_state_calbirated);

						//float* pv = glm::value_ptr(mat_stgcs2clf);
						//for (int i = 0; i < 16; i++)
						//	std::cout << i << " : " << pv[i] << std::endl;

						// why divide by 4?!
						//cam_state_calbirated.fx /= 4.f;
						//cam_state_calbirated.fy /= 4.f;
						//cam_state_calbirated.cx /= 4.f;
						//cam_state_calbirated.cy /= 4.f;
						//cam_state_calbirated.sc /= 4.f;
					}

					cam_state_calbirated.w = g_info.stg_w / g_info.stg_display_num;
					cam_state_calbirated.h = g_info.stg_h;
					cam_state_calbirated.np = 0.1f;
					cam_state_calbirated.fp = 20.0f;
					cam_state_calbirated.projection_mode = 3;

					// 
					__cv3__ cam_state_calbirated.pos = tr_pt(mat_clf2ws, __cv3__ cam_state_calbirated.pos);
					__cv3__ cam_state_calbirated.up = glm::normalize(tr_vec(mat_clf2ws, __cv3__ cam_state_calbirated.up));
					__cv3__ cam_state_calbirated.view = glm::normalize(tr_vec(mat_clf2ws, __cv3__ cam_state_calbirated.view));

					vzm::SetCameraParameters(g_info.stg_scene_id, cam_state_calbirated, i == 0 ? stg_cam_id : stg2_cam_id);
					i == 0? g_info.is_calib_stg_cam = true : g_info.is_calib_stg_cam_2 = true;
					// TO DO //
					//bool is_success = CalibrteCamLocalFrame(*(vector<glm::fvec2>*)&point2d, *(vector<glm::fvec3>*)&point3d, mat_ws2clf,
					//	rgb_intrinsics.fx, rgb_intrinsics.fy, rgb_intrinsics.ppx, rgb_intrinsics.ppy,
					//	mat_rscs2clf, &pnp_err, &calib_samples, 3d, 2d);
				}
			}

			if (ws_mk_spheres_xyzr.size() > 0)
			{
				vzm::GenerateSpheresObject(__FP ws_mk_spheres_xyzr[0], __FP ws_mk_spheres_rgb[0], ws_mk_spheres_xyzr.size(), clf_mk_stg_calib_spheres_id);
				vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, clf_mk_stg_calib_spheres_id, default_obj_state);
				vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, clf_mk_stg_calib_spheres_id, default_obj_state);
			}
			else
			{
				vzm::ObjStates cstate = default_obj_state;
				cstate.is_visible = false;
				vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, clf_mk_stg_calib_spheres_id, cstate);
				vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, clf_mk_stg_calib_spheres_id, cstate);
			}
		}
		else
		{
			last_calib_pair = 0;
			vzm::ObjStates cstate = default_obj_state;
			cstate.is_visible = false;
			vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, mk_stg_calib_sphere_id, cstate);
			vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, mk_stg_calib_sphere_id, cstate);

			vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, clf_mk_stg_calib_spheres_id, cstate);
			vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, clf_mk_stg_calib_spheres_id, cstate);
		}

		for (int i = 0; i < g_info.stg_display_num; i++)
		{
			if (i == 0 ? g_info.is_calib_stg_cam : g_info.is_calib_stg_cam_2)
			{
				glm::fmat4x4 mat_stgcs2ws = mat_clf2ws * (i == 0 ? mat_stgcs2clf : mat_stgcs2clf_2);

				//rs_cam_tris_id, rs_cam_lines_id, rs_cam_txt_id
				if (is_rsrb_detected)
					Update_CamModel(g_info.ws_scene_id, mat_stgcs2ws, "STG CAM " + to_string(i), 3 + i);

				vzm::CameraParameters _stg_cam_params;
				int cam_id = i == 0 ? stg_cam_id : stg2_cam_id;
				vzm::GetCameraParameters(g_info.stg_scene_id, _stg_cam_params, cam_id);
				ComputeCameraStates(i == 0 ? mat_stgcs2clf : mat_stgcs2clf_2, mat_clf2ws, _stg_cam_params);
				vzm::SetCameraParameters(g_info.stg_scene_id, _stg_cam_params, cam_id);
			}
		}
	}

	void SetCalibFrames(bool is_visible)
	{
		vzm::ObjStates cstate = default_obj_state;
		cstate.color[3] = 0.3f;
		cstate.is_visible = is_visible;
		for (int i = 0; i < g_info.otrk_data.calib_trial_rs_cam_frame_ids.size(); i++)
		{
			vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, g_info.otrk_data.calib_trial_rs_cam_frame_ids[i], cstate);
		}
	}

	void SetDepthMapPC(const bool is_visible, rs2::depth_frame& depth_frame, rs2::video_frame& color_frame)
	{
		if (is_visible && depth_frame)
		{
			//rs2::depth_frame depth_frame = depth_frame;// .get_depth_frame();

			vzm::ObjStates obj_state_pts = default_obj_state;
			obj_state_pts.color[3] = 1.f;
			obj_state_pts.emission = 0.3f;
			obj_state_pts.diffusion = 1.f;
			obj_state_pts.surfel_size = 0.005f;
			// Generate the pointcloud and texture mappings
			rs_settings::pc.map_to(color_frame); // before pc.calculate, which generates texture_coordinates
			rs_settings::points = rs_settings::pc.calculate(depth_frame);
			if (rs_settings::points)
			{
				auto vertices = rs_settings::points.get_vertices();              // get vertices
				auto tex_coords = rs_settings::points.get_texture_coordinates(); // and texture coordinates

				vzm::CameraParameters _cam_params;
				vzm::GetCameraParameters(g_info.ws_scene_id, _cam_params, ov_cam_id);

				//vector<glm::fvec3> vtx;// (points.size());
				//memcpy(&vtx[0], &vertices[0], sizeof(glm::fvec3) * points.size());
				glm::fmat4x4 mat_r = glm::rotate(-glm::pi<float>(), glm::fvec3(1, 0, 0));
				glm::fmat4x4 mat_rscs2ws = mat_clf2ws * mat_rscs2clf;
				glm::fmat4x4 mat_os2ws;


				{
					glm::fmat4x4 mat_rs2ws = mat_rscs2ws * mat_r;
					const float *rv = rs_settings::rgb_extrinsics.rotation;
					glm::fmat4x4 mat_rt(rv[0], rv[1], rv[2], 0, rv[3], rv[4], rv[5], 0, rv[6], rv[7], rv[8], 0,
						rs_settings::rgb_extrinsics.translation[0], rs_settings::rgb_extrinsics.translation[1], rs_settings::rgb_extrinsics.translation[2], 1); // ignore 4th row 
					//*(glm::fmat4x4*) obj_state_pts.os2ws = mat_rs2ws * mat_rt;
					mat_os2ws = mat_rs2ws * mat_rt; // depth to rgb (external)
				}
				glm::fvec3* normalmap = NULL;
				const int _w = depth_frame.as<rs2::video_frame>().get_width();
				const int _h = depth_frame.as<rs2::video_frame>().get_height();
				{
					// compute face normal
					normalmap = new glm::fvec3[_w * _h];
					auto depth_color = depth_frame.apply_filter(rs_settings::color_map);
					Mat image_depth(Size(_w, _h), CV_8UC3, (void*)depth_color.get_data(), Mat::AUTO_STEP);
					imshow("test depth", image_depth);

					// 
					glm::fmat4x4 mat_irss2os = mat_r * rs_settings::mat_irss2ircs;
					glm::fmat4x4 mat_irss2ws = mat_os2ws * mat_irss2os;
					glm::fvec3 pos_ir_cam_ws = tr_pt(mat_os2ws, glm::fvec3(0));
					auto ComputePos_SSZ2WS = [](const int x, const int y, const float z, const glm::fvec3& pos_ir_cam_ws, const glm::fmat4x4& mat_irss2ws)
					{
						// g_cbCamState.mat_ss2ws
						// g_cbCamState.dir_view_ws
						// g_cbCamState.pos_cam_ws
						// g_cbCamState.cam_flag & 0x1d
						// g_cbCamState.rt_width
						// deep_k_buf
						glm::fvec3 pos_ip_ws = tr_pt(mat_irss2ws, glm::fvec3(x, y, 0));
						// always perspective (do not consider orthogonal viewing)
						glm::fvec3 view_dir_ws = pos_ip_ws - pos_ir_cam_ws;

						view_dir_ws = normalize(view_dir_ws);
						return pos_ip_ws + view_dir_ws * z;
					};

					//int z_valid_count = 0;
					//float zmin = 1000000;
					//float zmax = 0;
					for (int i = 0; i < _h; i++)
						for (int j = 0; j < _w; j++)
						{
							float z = depth_frame.get_distance(j, i);
							//if (z > 0 && z < 100) 
							//	z_valid_count++;
							float z_dxR = depth_frame.get_distance(min(j + 1, _w - 1), i);
							float z_dxL = depth_frame.get_distance(max(j - 1, 0), i);
							float z_dyR = depth_frame.get_distance(j, min(i + 1, _h - 1));
							float z_dyL = depth_frame.get_distance(j, max(i - 1, 0));
							float zRx_diff = z - z_dxR;
							float zLx_diff = z - z_dxL;
							float zRy_diff = z - z_dyR;
							float zLy_diff = z - z_dyL;
							float z_dx = z_dxR, z_dy = z_dyR;
							float x_offset = 1, y_offset = 1;
							if (zRx_diff*zRx_diff > zLx_diff*zLx_diff)
							{
								z_dx = z_dxL;
								x_offset *= -1;
							}
							if (zRy_diff*zRy_diff > zLy_diff*zLy_diff)
							{
								z_dy = z_dyL;
								y_offset *= -1;
							}
							glm::fvec3 p = ComputePos_SSZ2WS(j, i, z, pos_ir_cam_ws, mat_irss2ws);
							glm::fvec3 p_dx = ComputePos_SSZ2WS(j + x_offset, i, z_dx, pos_ir_cam_ws, mat_irss2ws);
							glm::fvec3 p_dy = ComputePos_SSZ2WS(j, i + y_offset, z_dy, pos_ir_cam_ws, mat_irss2ws);
							glm::fvec3 p_ddx = p_dx - p;
							glm::fvec3 p_ddy = p_dy - p;
							glm::fvec3 face_normal = glm::normalize(glm::cross(p_ddx, p_ddy));
							normalmap[j + i * _w] = face_normal;
						}

				}
				vector<glm::fvec2> tex(rs_settings::points.size());
				memcpy(&tex[0], &tex_coords[0], sizeof(glm::fvec2) * rs_settings::points.size());
				vector<glm::fvec3> color_pts(rs_settings::points.size());
				vector<glm::fvec3> pos_pts(rs_settings::points.size());
				vector<glm::fvec3> nrl_pts(rs_settings::points.size());
				for (int i = 0; i < (int)rs_settings::points.size(); i++)
				{
					float tx = tex[i].x * g_info.rs_w;
					float ty = tex[i].y * g_info.rs_h;
					int _tx = (int)tx;
					int _ty = (int)ty;
					if (_tx < 0 || _ty < 0 || _tx >= g_info.rs_w || _ty >= g_info.rs_h)
						continue;
					glm::u8vec3* _data = (glm::u8vec3*)color_frame.get_data();
					glm::u8vec3 _color0 = _data[_tx + _ty * g_info.rs_w];
					//glm::u8vec3 _color1 = _data[_tx + _ty * g_info.rs_w];
					//glm::u8vec3 _color2 = _data[_tx + _ty * g_info.rs_w];
					//glm::u8vec3 _color3 = _data[_tx + _ty * g_info.rs_w];
					color_pts[i] = glm::fvec3(_color0.x / 255.f, _color0.y / 255.f, 1.f);// _color0.z / 255.f);
					pos_pts[i] = tr_pt(mat_os2ws, *(glm::fvec3*)&vertices[i]);
					if (normalmap) nrl_pts[i] = normalmap[i % _w + (i / _w) * _w];
				}
				if (normalmap) delete[] normalmap;
				//vzm::GeneratePointCloudObject(__FP pos_pts[0], NULL, __FP color_pts[0], (int)points.size(), g_info.rs_pc_id);
				vzm::GeneratePointCloudObject(__FP pos_pts[0], __FP nrl_pts[0], NULL, (int)rs_settings::points.size(), g_info.rs_pc_id);
				vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, g_info.rs_pc_id, obj_state_pts);
				vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, g_info.rs_pc_id, obj_state_pts);
				vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, g_info.rs_pc_id, obj_state_pts);
				bool foremost_surf_rendering = false;
				vzm::SetRenderTestParam("_bool_OnlyForemostSurfaces", foremost_surf_rendering, sizeof(bool), g_info.ws_scene_id, ov_cam_id, g_info.rs_pc_id);
			}
		}
		else
		{
			vzm::ObjStates obj_state_pts;
			vzm::GetSceneObjectState(g_info.ws_scene_id, g_info.rs_pc_id, obj_state_pts);
			obj_state_pts.is_visible = false;
			vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, g_info.rs_pc_id, obj_state_pts);
			vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, g_info.rs_pc_id, obj_state_pts);
			vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, g_info.rs_pc_id, obj_state_pts);
		}
	}

	void SetTargetModelAssets(const std::string& name, const int guide_line_idx)
	{
		g_info.match_model_rbs_name = name;
		glm::fmat4x4 mat_matchmodelfrm2ws;
		bool model_match_rb = g_info.otrk_data.trk_info.GetLFrmInfo(name, mat_matchmodelfrm2ws); 
		g_info.mat_ws2matchmodelfrm = glm::inverse(mat_matchmodelfrm2ws);	
		//if (g_info.model_rbs_pick_pts.size() > 0)
		{
			static int model_ws_pick_spheres_id = 0;
			vzm::ObjStates cstate;
			cstate.is_visible = g_info.touch_mode == RsTouchMode::Align
				&& g_info.model_rbs_pick_pts.size() > 0;
			if (cstate.is_visible)
			{
				vector<glm::fvec4> spheres_xyzr;
				vector<glm::fvec3> spheres_rgb;
				for (int i = 0; i < (int)g_info.model_rbs_pick_pts.size(); i++)
				{
					glm::fvec4 sphere_xyzr = glm::fvec4(tr_pt(mat_matchmodelfrm2ws, g_info.model_rbs_pick_pts[i]), 0.005);
					spheres_xyzr.push_back(sphere_xyzr);
					glm::fvec3 sphere_rgb = glm::fvec3(0, 1, 0);
					spheres_rgb.push_back(sphere_rgb);
				}
				vzm::GenerateSpheresObject(__FP spheres_xyzr[0], __FP spheres_rgb[0], (int)g_info.model_rbs_pick_pts.size(), model_ws_pick_spheres_id);
			}
			vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, model_ws_pick_spheres_id, cstate);
			vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, model_ws_pick_spheres_id, cstate);
			vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, model_ws_pick_spheres_id, cstate);
		}

		if (/*model_match_rb && */g_info.is_modelaligned)
		{
			// model
			vzm::ObjStates model_ws_obj_state;
			vzm::ObjStates volume_ws_obj_state;
			{
				vzm::GetSceneObjectState(g_info.ws_scene_id, g_info.model_ws_obj_id, model_ws_obj_state);

				__cm4__ model_ws_obj_state.os2ws = mat_matchmodelfrm2ws * g_info.mat_os2matchmodefrm;
				//if (scenario == 1 || scenario == 2)
				//{
				vzm::GetSceneObjectState(g_info.ws_scene_id, g_info.model_volume_id, volume_ws_obj_state);

				// TEMP
				if (scenario == 0)
				{
					glm::fmat4x4 mat_s = glm::scale(glm::fvec3(dicom_flip_x, dicom_flip_y, dicom_flip_z));
					glm::fmat4x4 mat_t = glm::translate(glm::fvec3(dicom_tr_x, dicom_tr_y, dicom_tr_z));
					__cm4__ volume_ws_obj_state.os2ws = mat_matchmodelfrm2ws * g_info.mat_os2matchmodefrm * mat_t * mat_s;
				}
				else
				{
					__cm4__ volume_ws_obj_state.os2ws = mat_matchmodelfrm2ws * g_info.mat_os2matchmodefrm;
				}

				volume_ws_obj_state.is_visible = false;

				vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, g_info.model_volume_id, volume_ws_obj_state);
				vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, g_info.model_volume_id, volume_ws_obj_state);
				vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, g_info.model_volume_id, volume_ws_obj_state);
				//
				//	model_ws_obj_state.color[3] = 0.05f;
				//	//model_ws_obj_state.point_thickness = 15;

				__cv4__ model_ws_obj_state.color = glm::fvec4(0.9, 0.7, 0.3, 1.0);
				model_ws_obj_state.surfel_size = 0.003f;
				if (g_info.touch_mode == RsTouchMode::Align)
				{
					model_ws_obj_state.color[3] = 0.1f;
					model_ws_obj_state.show_outline = true;
					vzm::SetRenderTestParam("_bool_IsGhostSurface", false, sizeof(bool), g_info.rs_scene_id, 1, g_info.model_ws_obj_id);
					vzm::SetRenderTestParam("_bool_IsOnlyHotSpotVisible", false, sizeof(bool), g_info.rs_scene_id, 1, g_info.model_ws_obj_id);
				}
				else
				{
					model_ws_obj_state.color[3] = 1.f;
					model_ws_obj_state.show_outline = false;
					vzm::SetRenderTestParam("_bool_IsGhostSurface", true, sizeof(bool), g_info.rs_scene_id, 1, g_info.model_ws_obj_id);
					vzm::SetRenderTestParam("_bool_IsOnlyHotSpotVisible", true, sizeof(bool), g_info.rs_scene_id, 1, g_info.model_ws_obj_id);
				}

				vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, g_info.model_ws_obj_id, model_ws_obj_state);
				vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, g_info.model_ws_obj_id, model_ws_obj_state);
				vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, g_info.model_ws_obj_id, model_ws_obj_state);
			}
			// guide lines
			g_info.guide_line_idx = guide_line_idx;
			if(g_info.guide_lines_target_rbs.size() > 0 && guide_line_idx >= 0)
			{
				vzm::ObjStates line_state, cyl_state;
				line_state.line_thickness = 5;
				line_state.is_visible = false;
				cyl_state.is_visible = false;
				__cv4__ cyl_state.color = glm::fvec4(0, 1, 1, 0.3);
				static int closest_dist_line_id = 0, closest_dist_text_id = 0;
				static int angle_id = 0, angle_text_id = 0, angle_text_id_stg = 0, angle_text_id_ws = 0;
				int guide_objs[] = { closest_dist_line_id , closest_dist_text_id , 
					angle_id , angle_text_id , angle_text_id_stg, angle_text_id_ws };
				vzm::ObjStates guide_obj_state;
				guide_obj_state.is_visible = false;
				for (int i = 0; i < (int)(sizeof(guide_objs) / sizeof(int)); i++)
				{
					int obj_id = guide_objs[i];
					vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, obj_id, guide_obj_state);
					vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, obj_id, guide_obj_state);
					vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, obj_id, guide_obj_state);
					vzm::ReplaceOrAddSceneObject(g_info.znavi_rs_scene_id, obj_id, guide_obj_state);
					vzm::ReplaceOrAddSceneObject(g_info.znavi_stg_scene_id, obj_id, guide_obj_state);
				}

				int num_guide_lines = (int)g_info.guide_lines_target_rbs.size();
				if (guide_line_idx < num_guide_lines)
				{
					static vector<int> guide_line_obj_ids;
					static vector<int> guide_cylinder_obj_ids;
					//for (int i = 0; i < guide_line_obj_ids.size(); i++)
					//{
					//	int obj_id = guide_line_obj_ids[i];
					//	if (obj_id != 0)
					//	{
					//		vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, obj_id, guide_obj_state);
					//		vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, obj_id, guide_obj_state);
					//		vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, obj_id, guide_obj_state);
					//		vzm::ReplaceOrAddSceneObject(g_info.znavi_rs_scene_id, obj_id, guide_obj_state);
					//		vzm::ReplaceOrAddSceneObject(g_info.znavi_stg_scene_id, obj_id, guide_obj_state);
					//	}
					//
					//	obj_id = guide_cylinder_obj_ids[i];
					//	if (obj_id != 0)
					//	{
					//		vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, obj_id, guide_obj_state);
					//		vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, obj_id, guide_obj_state);
					//		vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, obj_id, guide_obj_state);
					//		vzm::ReplaceOrAddSceneObject(g_info.znavi_rs_scene_id, obj_id, guide_obj_state);
					//		vzm::ReplaceOrAddSceneObject(g_info.znavi_stg_scene_id, obj_id, guide_obj_state);
					//	}
					//}

					glm::fmat4x4& tr = __cm4__ volume_ws_obj_state.os2ws;
					//if (scenario == 0)
					//{
					//	glm::fmat4x4 mat_s = glm::scale(glm::fvec3(-1, -1, 1));
					//	glm::fmat4x4 mat_t = glm::translate(glm::fvec3(dicom_tr_x, dicom_tr_y, dicom_tr_z));
					//	tr = tr * mat_t * mat_s;
					//}
					const pair< glm::fvec3, glm::fvec3>& guide_line = g_info.guide_lines_target_rbs[guide_line_idx];
					glm::fvec3 pos_guide_line = tr_pt(tr, get<0>(guide_line));
					glm::fvec3 dir_guide_line = glm::normalize(tr_vec2(tr, get<1>(guide_line)));

					glm::fvec3 closetPoint;
					ComputeClosestPointBetweenLineAndPoint(pos_guide_line, dir_guide_line, g_info.pos_probe_pin, closetPoint);

					// guide line
					for (int i = guide_line_obj_ids.size(); i < num_guide_lines; i++) guide_line_obj_ids.push_back(0);
					for (int i = guide_cylinder_obj_ids.size(); i < num_guide_lines; i++) guide_cylinder_obj_ids.push_back(0);

					glm::fvec3 line_pos[2] = { pos_guide_line, pos_guide_line + dir_guide_line * 1.f };
					int& guide_line_id = guide_line_obj_ids[guide_line_idx];
					vzm::GenerateLinesObject(__FP line_pos[0], NULL, 1, guide_line_id);

					glm::fvec3 cyl_pos[2] = { pos_guide_line, pos_guide_line + dir_guide_line * 0.3f };
					float cyl_r = 0.004;
					int& guide_cyl_id = guide_cylinder_obj_ids[guide_line_idx];
					vzm::GenerateCylindersObject(__FP cyl_pos[0], __FP cyl_r, NULL, 1, guide_cyl_id);

					static int guide_cyl_zoom_id = 0;
					//cyl_r = 0.0015;
					vzm::GenerateCylindersObject(__FP cyl_pos[0], __FP cyl_r, NULL, 1, guide_cyl_zoom_id);
					
					// 

					//cyl_state.is_visible = true;
					//
					//vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, guide_cyl_id, cyl_state);
					//vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, guide_cyl_id, cyl_state);
					//vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, guide_cyl_id, cyl_state);
					//
					//line_state.is_visible = true;
					//
					//vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, guide_line_id, line_state);
					//vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, guide_line_id, line_state);
					//vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, guide_line_id, line_state);

					// show dist line
					g_info.closest_dist = MakeDistanceLine(-1, g_info.pos_probe_pin, closetPoint, 0.05, closest_dist_line_id, closest_dist_text_id);
					g_info.guide_probe_closest_point = closetPoint;
					vzm::ObjStates closest_dist_line_state;
					closest_dist_line_state.line_thickness = 5;
					vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, closest_dist_line_id, closest_dist_line_state);
					vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, closest_dist_line_id, closest_dist_line_state);
					vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, closest_dist_line_id, closest_dist_line_state);

					SetDashEffectInRendering(g_info.stg_scene_id, 1, closest_dist_line_id, 0.01, true);
					SetDashEffectInRendering(g_info.stg_scene_id, 2, closest_dist_line_id, 0.01, true);
					SetDashEffectInRendering(g_info.rs_scene_id, 1, closest_dist_line_id, 0.01, true);
					SetDashEffectInRendering(g_info.ws_scene_id, 1, closest_dist_line_id, 0.01, true);

					// show angle
					g_info.angle = MakeAngle3(g_info.dir_probe_se, dir_guide_line, closetPoint, 0.05, 0.1, angle_id, 
						g_info.rs_scene_id, angle_text_id, g_info.stg_scene_id, angle_text_id_stg,
						g_info.ws_scene_id, angle_text_id_ws);
					vzm::ObjStates angle_state, angle_text_state;
					angle_state.emission = 2.f;
					angle_state.diffusion = 0.f;
					angle_state.specular = 0.f;
					angle_state.color[3] = 0.8;
					__cv4__ angle_text_state.color = glm::fvec4(1);
					vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, angle_id, angle_state);
					vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, angle_text_id_stg, angle_text_state);
					vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, angle_id, angle_state);
					vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, angle_text_id_ws, angle_text_state);
					vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, angle_id, angle_state);
					vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, angle_text_id, angle_text_state);


					// color coding w.r.t. distance and angle. //
					// guide
					if (g_info.angle * 180.f / glm::pi<float>() <= 5) {
						cyl_state.color[3] = 0.15;
						float r = 0 / 255.0;
						float g = 255 / 255.0;
						float b = 0 / 255.0;
						float o = 0.5;
						__cv4__ line_state.color = glm::fvec4(r, g, b, o);
						__cv4__ cyl_state.color = glm::fvec4(r, g, b, o);
						cyl_state.color[3] = o;
			
					}
					else {
						//cyl_state.is_visible = false;
						float r = 255 / 255.0;
						float g = 127 / 255.0;
						float b = 39 / 255.0;
						float o = 0.5;
						__cv4__ line_state.color = glm::fvec4(r, g, b, o);
						__cv4__ cyl_state.color = glm::fvec4(r, g, b, o);
						cyl_state.color[3] = o;
					}

					// tool tip
					vzm::ObjStates tooltipState;
					//cout << probe_tip_id << endl;
					vzm::GetSceneObjectState(g_info.ws_scene_id, probe_tip_id, tooltipState);

					if (g_info.closest_dist <= 0.004) {
						float r = 0 / 255.0;
						float g = 255 / 255.0;
						float b = 0 / 255.0;
						float o = 0.8;
						__cv3__ tooltipState.color = glm::fvec3(r, g, b);
						tooltipState.color[3] = o;
					}
					else {
						float r = 255 / 255.0;
						float g = 127 / 255.0;
						float b = 39 / 255.0;
						float o = 0.8;
						__cv3__ tooltipState.color = glm::fvec3(r, g, b);
						tooltipState.color[3] =o;
					}
					vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, probe_tip_id, tooltipState);
					vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, probe_tip_id, tooltipState);
					vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, probe_tip_id, tooltipState);
					

					SetDashEffectInRendering(g_info.stg_scene_id, 1, guide_line_id, 0.01, false);
					SetDashEffectInRendering(g_info.stg_scene_id, 2, guide_line_id, 0.01, false);
					SetDashEffectInRendering(g_info.rs_scene_id, 1, guide_line_id, 0.01, false);
					SetDashEffectInRendering(g_info.ws_scene_id, 1, guide_line_id, 0.01, false);
					for (int i = 0; i < (int)guide_line_obj_ids.size(); i++)
					{
						int line_obj_id = guide_line_obj_ids[i];
						line_state.is_visible = cyl_state.is_visible = i == guide_line_idx;
	
						vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, line_obj_id, line_state);
						vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, line_obj_id, line_state);
						vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, line_obj_id, line_state);
						//vzm::ReplaceOrAddSceneObject(g_info.znavi_rs_scene_id, line_obj_id, line_state);
						//vzm::ReplaceOrAddSceneObject(g_info.znavi_stg_scene_id, line_obj_id, line_state);

						int cyl_obj_id = guide_cylinder_obj_ids[i];
						vzm::ReplaceOrAddSceneObject(g_info.stg_scene_id, cyl_obj_id, cyl_state);
						vzm::ReplaceOrAddSceneObject(g_info.rs_scene_id, cyl_obj_id, cyl_state);
						vzm::ReplaceOrAddSceneObject(g_info.ws_scene_id, cyl_obj_id, cyl_state);
						//vzm::ReplaceOrAddSceneObject(g_info.znavi_rs_scene_id, cyl_obj_id, cyl_state);
						//vzm::ReplaceOrAddSceneObject(g_info.znavi_stg_scene_id, cyl_obj_id, cyl_state);

						line_state.is_visible = true;
						cyl_state.is_visible = true;
						vzm::ReplaceOrAddSceneObject(g_info.znavi_rs_scene_id, guide_cyl_zoom_id, cyl_state);
						vzm::ReplaceOrAddSceneObject(g_info.znavi_stg_scene_id, guide_cyl_zoom_id, cyl_state);
					}

					// zoom navi view //
					{
						// probe tip
						vzm::ReplaceOrAddSceneObject(g_info.znavi_rs_scene_id, probe_tip_id, tooltipState);
						vzm::ReplaceOrAddSceneObject(g_info.znavi_stg_scene_id, probe_tip_id, tooltipState);

						// displacement arrow
						static int guide_dist_arrow_id = 0;
						vzm::ObjStates guide_dist_arrow_state;
						__cv4__ guide_dist_arrow_state.color = glm::fvec4(1, 0.5, 1, 0.5);

						vzm::GenerateArrowObject(__FP g_info.pos_probe_pin, __FP closetPoint, 0.0015f, 0.003f, guide_dist_arrow_id);
						vzm::ReplaceOrAddSceneObject(g_info.znavi_rs_scene_id, guide_dist_arrow_id, guide_dist_arrow_state);
						vzm::ReplaceOrAddSceneObject(g_info.znavi_stg_scene_id, guide_dist_arrow_id, guide_dist_arrow_state);
					}
				}
			}
		}
	}

	void SetSectionalImageAssets(const bool show_sectional_views, const float* _pos_tip, const float* _pos_end, const float rot_angle_rad)
	{
		// after calling SetTargetModelAssets
		_show_sectional_views = show_sectional_views;
		if (show_sectional_views && _pos_tip && _pos_end && g_info.is_modelaligned)
		{
			// ws
			glm::fvec3 pos_tip = __cv3__ _pos_tip;
			glm::fvec3 pos_end = __cv3__ _pos_end;

			if (g_info.model_volume_id == 0)
			{
				vzm::ObjStates model_ws_obj_state;
				vzm::GetSceneObjectState(g_info.ws_scene_id, g_info.model_ws_obj_id, model_ws_obj_state);
				vzm::ReplaceOrAddSceneObject(g_info.csection_scene_id, g_info.model_ws_obj_id, model_ws_obj_state);
			}
			else
			{
				vzm::ObjStates volume_ws_obj_state;
				vzm::GetSceneObjectState(g_info.ws_scene_id, g_info.model_volume_id, volume_ws_obj_state);
				volume_ws_obj_state.is_visible = true;
				vzm::ReplaceOrAddSceneObject(g_info.csection_scene_id, g_info.model_volume_id, volume_ws_obj_state);
			}

			vzm::CameraParameters csection_cam_params_model;
			csection_cam_params_model.np = 0.0f;
			csection_cam_params_model.fp = 10.0f;
			csection_cam_params_model.projection_mode = 4;
			csection_cam_params_model.ip_w = 0.1;
			csection_cam_params_model.ip_h = 0.1;
			csection_cam_params_model.w = 100;
			csection_cam_params_model.h = 100;

			__cv3__ csection_cam_params_model.pos = pos_tip; // g_info.pos_probe_pin;
			glm::fvec3 cs_up = glm::normalize(pos_end - pos_tip);// tr_vec(mat_section_probe2ws, glm::fvec3(0, 0, -1));
			__cv3__ csection_cam_params_model.up = cs_up;

			glm::fvec3 cs_view = glm::rotate(glm::fvec3(0, 0, 1), rot_angle_rad, glm::fvec3(0, 1, 0));// glm::fvec3(0, 0, 1);
			glm::fvec3 cs_right = glm::cross(cs_view, cs_up);
			cs_view = glm::normalize(glm::cross(cs_up, cs_right));
			__cv3__ csection_cam_params_model.view = cs_view;
			vzm::SetCameraParameters(g_info.csection_scene_id, csection_cam_params_model, 0);

			cs_view = glm::rotate(glm::fvec3(1, 0, 0), rot_angle_rad, glm::fvec3(0, 1, 0)); //glm::fvec3(1, 0, 0);
			cs_right = glm::cross(cs_view, cs_up);
			cs_view = glm::normalize(glm::cross(cs_up, cs_right));
			__cv3__ csection_cam_params_model.view = cs_view;
			vzm::SetCameraParameters(g_info.csection_scene_id, csection_cam_params_model, 1);
		}
	}

	void RenderAndShowWindows(bool show_times, Mat& img_rs, bool skip_show_rs_window, int addtional_scene, int addtional_cam)
	{
		auto DisplayTimes = [&show_times](const LARGE_INTEGER lIntCntStart, const string& _test)
		{
			return;
			if (!show_times) return;
			LARGE_INTEGER lIntFreq, lIntCntEnd;
			QueryPerformanceFrequency(&lIntFreq);
			QueryPerformanceCounter(&lIntCntEnd);
			double dRunTime1 = (lIntCntEnd.QuadPart - lIntCntStart.QuadPart) / (double)(lIntFreq.QuadPart);

			cout << _test << " : " << 1. / dRunTime1 << " fps" << endl;
		};
		auto GetPerformanceFreq = []() -> LARGE_INTEGER
		{
			LARGE_INTEGER lIntCntFreq;
			QueryPerformanceCounter(&lIntCntFreq);
			return lIntCntFreq;
		};

		auto RenderToolNavi = []()
		{
			using namespace glm;

			bool draw_znavi = g_info.is_probe_detected && g_info.is_modelaligned && g_info.guide_lines_target_rbs.size() > 0 && g_info.guide_line_idx >= 0;
			if (draw_znavi)
			{
				vzm::CameraParameters zoom_cam_params;

				zoom_cam_params.fov_y = 3.141592654f / 4.f;
				zoom_cam_params.aspect_ratio = (float)g_info.zn_w / (float)g_info.zn_h;
				//cout << g_info.zn_w << ", " << g_info.zn_h << endl;
				zoom_cam_params.projection_mode = 2;
				zoom_cam_params.w = g_info.zn_w;
				zoom_cam_params.h = g_info.zn_h;
				zoom_cam_params.np = 0.01f;
				zoom_cam_params.fp = 10.0f;
				__cv3__ zoom_cam_params.pos = g_info.pos_probe_pin + g_info.dir_probe_se * 0.1f;
				__cv3__ zoom_cam_params.view = -g_info.dir_probe_se;
				glm::fvec3 right = glm::normalize(glm::cross(-g_info.dir_probe_se, glm::fvec3(0, 1, 0)));
				__cv3__ zoom_cam_params.up = glm::normalize(glm::cross(right, -g_info.dir_probe_se));

				vzm::SetCameraParameters(g_info.znavi_rs_scene_id, zoom_cam_params, znavi_cam_id);
				vzm::SetCameraParameters(g_info.znavi_stg_scene_id, zoom_cam_params, znavi_cam_id);

				vzm::RenderScene(g_info.znavi_rs_scene_id, znavi_cam_id);
				vzm::RenderScene(g_info.znavi_stg_scene_id, znavi_cam_id);
			}
			return draw_znavi;
		};

		auto WorldCamSet = []()
		{
			using namespace glm;
			fvec3 up = fvec3(0, 1, 0);
			fvec3 view = -g_info.dir_probe_se;
			fvec3 left = normalize(cross(up, view));
			fvec3 pos_lookat = g_info.pos_probe_pin;// +g_info.dir_probe_se * 0.1f;

			vzm::CameraParameters cam_param;
			vzm::GetCameraParameters(g_info.ws_scene_id, cam_param, ov_cam_id);

			__cv3__ cam_param.pos = pos_lookat + left * 0.3f;
			__cv3__ cam_param.up = up;
			__cv3__ cam_param.view = -left;

			vzm::SetCameraParameters(g_info.ws_scene_id, cam_param, ov_cam_id);
		};

		if (!g_info.skip_call_render)
		{
#define ENABLE_STG
#define SHOW_WS_VIEW
#define SHOW_RS_VIEW
#define SHOW_MS_VIEW
#define SHOW_SECTION_VIEW
#define SHOW_STG_VIEW
#ifdef SHOW_WS_VIEW
			LARGE_INTEGER frq_render_ws = GetPerformanceFreq();

			if (g_info.is_modelaligned && g_info.is_probe_detected)
				WorldCamSet();

			string tc_calib_info = "# of current 3D pick positions : " + to_string(g_info.otrk_data.calib_3d_pts.size());
			Show_Window(g_info.window_name_ws_view, g_info.ws_scene_id, ov_cam_id, &tc_calib_info);
			DisplayTimes(frq_render_ws, "ws render : ");
#endif

#ifdef SHOW_RS_VIEW
			if (g_info.is_calib_rs_cam)
			{
				LARGE_INTEGER frq_render_rs = GetPerformanceFreq();
				vzm::RenderScene(g_info.rs_scene_id, rs_cam_id);
				DisplayTimes(frq_render_rs, "rs render : ");
				unsigned char* ptr_rgba;
				float* ptr_zdepth;
				int rs_w, rs_h;
				LARGE_INTEGER frq_render_cb = GetPerformanceFreq();
				if (vzm::GetRenderBufferPtrs(g_info.rs_scene_id, &ptr_rgba, &ptr_zdepth, &rs_w, &rs_h, rs_cam_id))
					copy_back_ui_buffer(img_rs.data, ptr_rgba, rs_w, rs_h, false);

				if (g_info.touch_mode == RsTouchMode::Align)
				{
					unsigned char* ms_ptr_rgba;
					float* ms_ptr_zdepth;
					int ms_w, ms_h;
					if (vzm::GetRenderBufferPtrs(g_info.model_scene_id, &ms_ptr_rgba, &ms_ptr_zdepth, &ms_w, &ms_h, model_cam_id))
					{
						cv::Mat cs_cvmat(ms_h, ms_w, CV_8UC4, ms_ptr_rgba);

						copy_back_ui_buffer_local(img_rs.data, rs_w, rs_h, ms_ptr_rgba, ms_w, ms_h, 10, 100, false, true, 0.2f, 50.f, false);
					}
				}
#ifdef SHOW_RS_VIEW
				else if (g_info.is_modelaligned)
				{
					if (_show_sectional_views)
					{
						vzm::RenderScene(g_info.csection_scene_id, 0);
						vzm::RenderScene(g_info.csection_scene_id, 1);

						for (int i = 0; i < 2; i++)
						{
							unsigned char* cs_ptr_rgba;
							float* cs_ptr_zdepth;
							int cs_w, cs_h;
							vzm::GetRenderBufferPtrs(g_info.csection_scene_id, &cs_ptr_rgba, &cs_ptr_zdepth, &cs_w, &cs_h, i);
							cv::Mat cs_cvmat(cs_h, cs_w, CV_8UC4, cs_ptr_rgba);
							cv::line(cs_cvmat, cv::Point(cs_w / 2, cs_h / 2), cv::Point(cs_w / 2, 0), cv::Scalar(255, 255, 0, 255), 2, LineTypes::LINE_AA);
							cv::circle(cs_cvmat, cv::Point(cs_w / 2, cs_h / 2), 2, cv::Scalar(255, 0, 0, 255), 2, LineTypes::LINE_AA);
							//cv::rectangle(cs_cvmat, Rect(10, 100, 10 + cs_w * (i + 1), 100 + cs_h), Scalar(200, 200, 200, 255), 1, LineTypes::LINE_AA);

							copy_back_ui_buffer_local(img_rs.data, rs_w, rs_h, cs_ptr_rgba, cs_w, cs_h, 10 + cs_w * i, 100, false, true, 3.f, 5.f, true);
						}
					}

					if (RenderToolNavi())
					{
						unsigned char* znavi_rs_ptr_rgba;
						float* znavi_rs_ptr_zdepth;
						int znavi_rs_w, znavi_rs_h;
						vzm::GetRenderBufferPtrs(g_info.znavi_rs_scene_id, &znavi_rs_ptr_rgba, &znavi_rs_ptr_zdepth, &znavi_rs_w, &znavi_rs_h, 1);
						cv::Mat znavi_rs_cvmat(znavi_rs_w, znavi_rs_h, CV_8UC4, znavi_rs_ptr_rgba);

						glm::fmat4x4 tr;
						//{
						//	glm::fmat4x4 mat_matchmodelfrm2ws;
						//	g_info.otrk_data.trk_info.GetLFrmInfo(g_info.match_model_rbs_name, mat_matchmodelfrm2ws);
						//	tr = mat_matchmodelfrm2ws * g_info.mat_os2matchmodefrm;
						//	if (scenario == 0)
						//	{
						//		glm::fmat4x4 mat_s = glm::scale(glm::fvec3(-1, -1, 1));
						//		glm::fmat4x4 mat_t = glm::translate(glm::fvec3(dicom_tr_x, dicom_tr_y, dicom_tr_z));
						//		tr = tr * mat_t * mat_s;
						//	}
						//}
						{
							vzm::ObjStates volume_ws_obj_state;
							vzm::GetSceneObjectState(g_info.ws_scene_id, g_info.model_volume_id, volume_ws_obj_state);
							tr = __cm4__ volume_ws_obj_state.os2ws;
						}
						const pair< glm::fvec3, glm::fvec3>& guide_line = g_info.guide_lines_target_rbs[g_info.guide_line_idx];
						glm::fvec3 pos_guide_line = tr_pt(tr, get<0>(guide_line));
						glm::fvec3 dir_guide_line = glm::normalize(tr_vec2(tr, get<1>(guide_line)));

						glm::fvec3 guide_probe_closest_point = g_info.guide_probe_closest_point;

						float maxDistance = 99;
						float currentDistance = glm::length(guide_probe_closest_point - pos_guide_line) * 1000;
						if (currentDistance >= maxDistance) { currentDistance = maxDistance; }

						int znavi_x_pos = 10;
						int znavi_y_pos = 250;
						int barX = 10 + znavi_x_pos, barY = 10 + znavi_y_pos, textOffset = 5;
						float barMaxLength = 200;
						float currentBarLength = (currentDistance / maxDistance) * barMaxLength;

						//cv::line(znavi_rs_cvmat, cv::Point(barX, barY), cv::Point(barX, barY + barMaxLength), cv::Scalar(125, 125, 125, 255), 10, LineTypes::LINE_AA);
						//cv::line(znavi_rs_cvmat, cv::Point(barX, barY), cv::Point(barX, barMaxLength - (barY + currentBarLength)), cv::Scalar(255, 255, 0, 255), 10, LineTypes::LINE_AA);
						//string distanceText = to_string_with_precision(currentDistance, 2) + " mm";
						//cv::putText(znavi_rs_cvmat, distanceText, cv::Point(barX + textOffset, barMaxLength - (barY + currentBarLength)), cv::FONT_HERSHEY_DUPLEX, 0.7, Scalar(255, 255, 255, 255), 1, LineTypes::LINE_AA);

						copy_back_ui_buffer_local(img_rs.data, rs_w, rs_h, znavi_rs_ptr_rgba, znavi_rs_w, znavi_rs_h, znavi_x_pos, znavi_y_pos, false, true, 0.4f, 20.f, false);

						cv::line(img_rs, cv::Point(barX, barY), cv::Point(barX, barY + barMaxLength), cv::Scalar(125, 125, 125, 255), 10, LineTypes::LINE_AA);
						cv::line(img_rs, cv::Point(barX, barY), cv::Point(barX, barY + barMaxLength - currentBarLength), cv::Scalar(255, 255, 0, 255), 10, LineTypes::LINE_AA);
						string distanceText = to_string_with_precision(currentDistance, 2) + " mm";
						cv::putText(img_rs, distanceText, cv::Point(barX + textOffset, barY + barMaxLength - currentBarLength), cv::FONT_HERSHEY_DUPLEX, 0.7, Scalar(255, 255, 255, 255), 1, LineTypes::LINE_AA);
					}
				}
#endif

				DisplayTimes(frq_render_rs, "rs copy-back : ");
			}

			// draw buttons
			Draw_TouchButtons(img_rs, g_info.rs_buttons, g_info.touch_mode);

			if (g_info.is_calib_rs_cam && !is_rsrb_detected)
				cv::putText(img_rs, "RS Cam is out of tracking volume !!", cv::Point(0, 150), cv::FONT_HERSHEY_DUPLEX, 2.0, CV_RGB(255, 0, 0), 3, LineTypes::LINE_AA);
			else
			{
				if (operation_name == "Picking AR Markers"
					|| operation_name == "Picking Tool Tip and End")
				{
					cv::putText(img_rs, operation_name, cv::Point(0, 150), cv::FONT_HERSHEY_DUPLEX, 2.0, CV_RGB(255, 0, 0), 2, LineTypes::LINE_AA);
				}
			}

			if(!skip_show_rs_window)
				imshow(g_info.window_name_rs_view, img_rs);

#ifdef __MIRRORS
			//if(scenario != 0)
			{
				cv::Mat img_rs_mirror(g_info.rs_h, g_info.rs_w, CV_8UC3, img_rs.data);
				imshow("rs mirror", img_rs_mirror);
			}
#endif
#endif

#if defined(ENABLE_STG) && defined(SHOW_STG_VIEW)
			auto Draw_STG_Calib_Point = [](Mat& img)
			{
				if (g_info.touch_mode == RsTouchMode::Calib_STG || g_info.touch_mode == RsTouchMode::Calib_STG2)
				{
					const int w = g_info.stg_w / g_info.stg_display_num;
					const int h = g_info.stg_h;
					static Point2f pos_2d_rs[30] = {
						Point2f(w / 5.f, h / 4.f) , Point2f(w / 5.f * 2.f, h / 4.f) , Point2f(w / 5.f * 3.f, h / 4.f) , Point2f(w / 5.f * 4.f, h / 4.f),
						Point2f(w / 8.f, h / 4.f * 2.f) , Point2f(w / 8.f * 2.f, h / 4.f * 2.f) , Point2f(w / 8.f * 3.f, h / 4.f * 2.f) , Point2f(w / 8.f * 4.f, h / 4.f * 2.f),
						Point2f(w / 8.f * 5.f, h / 4.f * 2.f) , Point2f(w / 8.f * 6.f, h / 4.f * 2.f) , Point2f(w / 8.f * 7.f, h / 4.f * 2.f),
						Point2f(w / 5.f, h / 4.f * 3.f) , Point2f(w / 5.f * 2.f, h / 4.f * 3.f) , Point2f(w / 5.f * 3.f, h / 4.f * 3.f) , Point2f(w / 5.f * 4.f, h / 4.f * 3.f),

						Point2f(w / 5.f + w, h / 4.f) , Point2f(w / 5.f * 2.f + w, h / 4.f) , Point2f(w / 5.f * 3.f + w, h / 4.f) , Point2f(w / 5.f * 4.f + w, h / 4.f),
						Point2f(w / 8.f + w, h / 4.f * 2.f) , Point2f(w / 8.f * 2.f + w, h / 4.f * 2.f) , Point2f(w / 8.f * 3.f + w, h / 4.f * 2.f) , Point2f(w / 8.f * 4.f + w, h / 4.f * 2.f),
						Point2f(w / 8.f * 5.f + w, h / 4.f * 2.f) , Point2f(w / 8.f * 6.f + w, h / 4.f * 2.f) , Point2f(w / 8.f * 7.f + w, h / 4.f * 2.f),
						Point2f(w / 5.f + w, h / 4.f * 3.f) , Point2f(w / 5.f * 2.f + w, h / 4.f * 3.f) , Point2f(w / 5.f * 3.f + w, h / 4.f * 3.f) , Point2f(w / 5.f * 4.f + w, h / 4.f * 3.f) };


					for (int i = 0; i < g_info.stg_display_num; i++)
					{
						vector<pair<Point2f, Point3f>>& stg_calib_pt_pairs = i == 0 ? g_info.otrk_data.stg_calib_pt_pairs : g_info.otrk_data.stg_calib_pt_pairs_2;
						if (stg_calib_pt_pairs.size() < 15)
							cv::drawMarker(img, pos_2d_rs[stg_calib_pt_pairs.size() + i * 15], Scalar(255, 100, 255), MARKER_CROSS, 30, 7);
						else
							for (int i = 0; i < stg_calib_pt_pairs.size(); i++)
							{
								pair<Point2f, Point3f>& pair_pts = stg_calib_pt_pairs[i];
								cv::drawMarker(img, get<0>(pair_pts), Scalar(255, 255, 100), MARKER_STAR, 30, 3);
							}
					}
#ifdef STG_LINE_CALIB
					cv::line(image_stg, pos_calib_lines[0], pos_calib_lines[1], Scalar(255, 255, 0), 2);
					cv::line(image_stg, pos_calib_lines[2], pos_calib_lines[3], Scalar(255, 255, 0), 2);
#endif
				}
			};
			if (g_info.stg_display_num == 1 ? g_info.is_calib_stg_cam : g_info.is_calib_stg_cam && g_info.is_calib_stg_cam_2)
			{
				LARGE_INTEGER frq_render_stg = GetPerformanceFreq();
				if (g_info.stg_display_num == 1)
				{
					vzm::RenderScene(g_info.stg_scene_id, stg_cam_id);
					unsigned char* ptr_rgba;
					float* ptr_zdepth;
					int _stg_w, _stg_h;
					if (vzm::GetRenderBufferPtrs(g_info.stg_scene_id, &ptr_rgba, &ptr_zdepth, &_stg_w, &_stg_h, stg_cam_id))
					{

						Mat image_stg(Size(_stg_w, _stg_h), CV_8UC4, (void*)ptr_rgba, Mat::AUTO_STEP);
						cv::drawMarker(image_stg, Point(_stg_w / 2, _stg_h / 2), Scalar(255, 255, 255), MARKER_CROSS, 30, 3);
						cv::rectangle(image_stg, Point(0, 0), Point(g_info.stg_w - 10, g_info.stg_h - 5), Scalar(255, 255, 255), 3);
						Draw_STG_Calib_Point(image_stg);

						imshow(g_info.window_name_stg_view, image_stg);

#ifdef __MIRRORS
						cv::Mat img_stg_mirror(Size(_stg_w, _stg_h), CV_8UC4, image_stg.data);
						imshow("stg mirror", img_stg_mirror);
#endif
					}
				}
				else // g_info.stg_display_num == 2
				{
					vzm::RenderScene(g_info.stg_scene_id, stg_cam_id);
					vzm::RenderScene(g_info.stg_scene_id, stg2_cam_id);
					unsigned char* ptr_rgba[2];
					float* ptr_zdepth[2];
					int _stg_w[2], _stg_h[2];
					if (vzm::GetRenderBufferPtrs(g_info.stg_scene_id, &ptr_rgba[0], &ptr_zdepth[0], &_stg_w[0], &_stg_h[0], stg_cam_id)
						&& vzm::GetRenderBufferPtrs(g_info.stg_scene_id, &ptr_rgba[1], &ptr_zdepth[1], &_stg_w[1], &_stg_h[1], stg2_cam_id))
					{
						Mat image_stg(Size(g_info.stg_w, g_info.stg_h), CV_8UC4);
						unsigned char* rgba_fb = image_stg.data;
						for (int row = 0; row < g_info.stg_h; row++)
						{
							memcpy(&rgba_fb[row * g_info.stg_w * 4], &ptr_rgba[0][row * _stg_w[0] * 4], sizeof(int) * _stg_w[0]);
							memcpy(&rgba_fb[row * g_info.stg_w * 4 + _stg_w[0] * 4], &ptr_rgba[1][row * _stg_w[1] * 4], sizeof(int) * _stg_w[1]);
						}

						cv::drawMarker(image_stg, Point(_stg_w[0] / 2 + g_info.stg_focus_offset_w, g_info.stg_h / 2), Scalar(255, 255, 255), MARKER_CROSS, 30, 3);
						cv::drawMarker(image_stg, Point(_stg_w[0] + _stg_w[1] / 2 - g_info.stg_focus_offset_w, g_info.stg_h / 2), Scalar(255, 255, 255), MARKER_CROSS, 30, 3);

						cv::rectangle(image_stg, Point(2, 2), Point(_stg_w[0] - 2, g_info.stg_h - 2), Scalar(255, 255, 255), 3);
						cv::rectangle(image_stg, Point(_stg_w[0] + 2, 2), Point(_stg_w[0] + _stg_w[1] - 2, g_info.stg_h - 2), Scalar(255, 255, 255), 3);
						Draw_STG_Calib_Point(image_stg);

						imshow(g_info.window_name_stg_view, image_stg);

#ifdef __MIRRORS
						cv::Mat img_stg_mirror(Size(g_info.stg_w, g_info.stg_h), CV_8UC4, image_stg.data);
						imshow("stg mirror", img_stg_mirror);
#endif
					}
				}

				DisplayTimes(frq_render_stg, "stg render : ");
			}
			else
			{
				static Mat image_stg(Size(g_info.stg_w, g_info.stg_h), CV_8UC4, Mat::AUTO_STEP);
				image_stg = cv::Mat::zeros(image_stg.size(), image_stg.type());
				if (g_info.stg_display_num == 1)
				{
					cv::drawMarker(image_stg, Point(g_info.stg_w / 2, g_info.stg_h / 2), Scalar(100, 100, 255), MARKER_CROSS, 30, 3);
					cv::rectangle(image_stg, Point(0, 0), Point(g_info.stg_w - 10, g_info.stg_h - 5), Scalar(255, 255, 255), 3);
				}
				else
				{
					int w = g_info.stg_w / 2;
					cv::drawMarker(image_stg, Point(w / 2 + g_info.stg_focus_offset_w, g_info.stg_h / 2), Scalar(100, 100, 255), MARKER_CROSS, 30, 3);
					cv::drawMarker(image_stg, Point(w + w / 2 - g_info.stg_focus_offset_w, g_info.stg_h / 2), Scalar(100, 100, 255), MARKER_CROSS, 30, 3);

					cv::rectangle(image_stg, Point(2, 2), Point(w - 2, g_info.stg_h - 2), Scalar(255, 255, 255), 3);
					cv::rectangle(image_stg, Point(w + 2, 2), Point(w + w - 2, g_info.stg_h - 2), Scalar(255, 255, 255), 3);
				}
				Draw_STG_Calib_Point(image_stg);
				imshow(g_info.window_name_stg_view, image_stg);

#ifdef __MIRRORS
				cv::Mat img_stg_mirror(Size(g_info.stg_w, g_info.stg_h), CV_8UC4, image_stg.data);
				imshow("stg mirror", img_stg_mirror);
#endif
			}
#endif
		}

		static bool once_prob_set = true;
		if (once_prob_set)
		{
			once_prob_set = false;
			cv::moveWindow(g_info.window_name_rs_view, 0, 0);
			cv::waitKey(100);
			cv::moveWindow(g_info.window_name_stg_view, 0, g_info.rs_h + 5);
			cv::waitKey(100);
			cv::moveWindow(g_info.window_name_ws_view, g_info.rs_w + 5, 0);
			cv::waitKey(100);
			cv::moveWindow(g_info.window_name_ms_view, g_info.rs_w + g_info.ws_w + 10, 0);
			cv::waitKey(100);

			const int display1_w = 1680 + 2;// 1920;
			const int display2_w = 2000 + 2;
#ifdef __MIRRORS
			cv::moveWindow("rs mirror", display1_w, 30);
			cv::waitKey(1);
			cv::moveWindow("stg mirror", display1_w + display2_w, 30);
			cv::waitKey(1);
			cv::setWindowProperty("rs mirror", WND_PROP_FULLSCREEN, WINDOW_FULLSCREEN);
			cv::setWindowProperty("stg mirror", WND_PROP_FULLSCREEN, WINDOW_FULLSCREEN);
#endif
			cv::resizeWindow(g_info.window_name_rs_view, cv::Size(g_info.rs_w, g_info.rs_h));
			cv::waitKey(10);
			cv::resizeWindow(g_info.window_name_stg_view, cv::Size(g_info.stg_w, g_info.stg_h));
			cv::waitKey(10);
			cv::resizeWindow(g_info.window_name_ws_view, cv::Size(g_info.ws_w, g_info.ws_h));
			cv::waitKey(10);
			cv::resizeWindow(g_info.window_name_ms_view, cv::Size(g_info.ws_w, g_info.ws_h));
			cv::waitKey(10);
		}
	}

	void DeinitializeVarSettings()
	{
		clear_record_info();
	}
}