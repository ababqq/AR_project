#pragma once

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>
#include <iostream>

#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/aruco.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/video/tracking.hpp>

#include "VisMtvApi.h"
#include "../optitrk/optitrack.h"
#include "../kar_helpers.hpp"

using namespace std;
using namespace cv;

struct EventGlobalInfo
{
	int scene_id;
	int cam_id;
	GlobalInfo& ginfo;
	EventGlobalInfo(GlobalInfo& global_info, int _scene_id, int _cam_id) : ginfo(global_info), scene_id(_scene_id), cam_id(_cam_id) {}
};

void CallBackFunc_WorldMouse(int event, int x, int y, int flags, void* userdata)
{
	EventGlobalInfo* eginfo = (EventGlobalInfo*)userdata;

	vzm::CameraParameters cam_params;
	vzm::GetCameraParameters(eginfo->scene_id, cam_params, eginfo->cam_id);

	// https://docs.opencv.org/3.4/d7/dfc/group__highgui.html
	static helpers::arcball aball_ov;
	if (event == EVENT_LBUTTONDOWN || event == EVENT_RBUTTONDOWN)
	{
		if (flags & EVENT_FLAG_CTRLKEY)
		{
			cout << "DEPRECATED OPERATION!!" << endl;
			//{
			//	vector<Point3f>& point3ds = eginfo->ginfo.otrk_data.calib_3d_pts;
			//	if (event == EVENT_LBUTTONDOWN)
			//	{
			//		int pick_obj = 0;
			//		glm::fvec3 pos_pick;
			//		vzm::PickObject(pick_obj, __FP pos_pick, x, y, eginfo->scene_id, eginfo->cam_id);
			//		cout << "PICK ID : " << pick_obj << endl;
			//
			//		if (pick_obj != 0)
			//		{
			//			glm::fvec3 mk_pt = eginfo->ginfo.vzmobjid2pos[pick_obj];
			//			cout << "----> " << eginfo->ginfo.vzmobjid2pos.size() << endl;
			//			TESTOUT("==> ", mk_pt);
			//			if (mk_pt != glm::fvec3(0))
			//			{
			//				const float zig_hight = 0.03;
			//				const float mk_r = 0.009;
			//
			//				glm::fvec3 pt = mk_pt - glm::fvec3(0, 1, 0) * (zig_hight + mk_r);
			//				TESTOUT("mk position " + to_string(point3ds.size()), pt);
			//				point3ds.push_back(Point3f(pt.x, pt.y, pt.z));
			//			}
			//
			//			cout << "# of total 3d pick positions : " << point3ds.size() << endl;
			//		}
			//	}
			//	else
			//	{
			//		if (point3ds.size() > 0)
			//			point3ds.pop_back();
			//	}
			//
			//	ofstream outfile(eginfo->ginfo.cb_positions);
			//	if (outfile.is_open())
			//	{
			//		outfile.clear();
			//		for (int i = 0; i < point3ds.size(); i++)
			//		{
			//			string line = to_string(point3ds[i].x) + " " +
			//				to_string(point3ds[i].y) + " " +
			//				to_string(point3ds[i].z);
			//			outfile << line << endl;
			//		}
			//	}
			//	outfile.close();
			//}
		}
		else
		{
			aball_ov.intializer((float*)&glm::fvec3(), 2.0f);

			helpers::cam_pose arc_cam_pose;
			glm::fvec3 pos = __cv3__ arc_cam_pose.pos = __cv3__ cam_params.pos;
			__cv3__ arc_cam_pose.up = __cv3__ cam_params.up;
			__cv3__ arc_cam_pose.view = __cv3__ cam_params.view;
			aball_ov.start((int*)&glm::ivec2(x, y), (float*)&glm::fvec2(cam_params.w, cam_params.h), arc_cam_pose);
		}
	}
	else if (event == EVENT_MBUTTONDOWN)
	{
	}
	else if (event == EVENT_MOUSEWHEEL)
	{
		if (getMouseWheelDelta(flags) > 0)
			__cv3__ cam_params.pos += 0.05f * (__cv3__ cam_params.view);
		else
			__cv3__ cam_params.pos -= 0.05f * (__cv3__ cam_params.view);
		vzm::SetCameraParameters(eginfo->scene_id, cam_params, eginfo->cam_id);
	}
	else if (event == EVENT_MOUSEMOVE && !(flags & EVENT_FLAG_CTRLKEY) )
	{
		if (flags & EVENT_FLAG_LBUTTON)
		{
			helpers::cam_pose arc_cam_pose;
			aball_ov.pan_move((int*)&glm::ivec2(x, y), arc_cam_pose);
			__cv3__ cam_params.pos = __cv3__ arc_cam_pose.pos;
			__cv3__ cam_params.up = __cv3__ arc_cam_pose.up;
			__cv3__ cam_params.view = __cv3__ arc_cam_pose.view;
			vzm::SetCameraParameters(eginfo->scene_id, cam_params, eginfo->cam_id);
		}
		else if (flags & EVENT_FLAG_RBUTTON)
		{
			helpers::cam_pose arc_cam_pose;
			aball_ov.move((int*)&glm::ivec2(x, y), arc_cam_pose);
			__cv3__ cam_params.pos = __cv3__ arc_cam_pose.pos;
			__cv3__ cam_params.up = __cv3__ arc_cam_pose.up;
			__cv3__ cam_params.view = __cv3__ arc_cam_pose.view;
			vzm::SetCameraParameters(eginfo->scene_id, cam_params, eginfo->cam_id);
		}
	}
}

void CallBackFunc_RsMouse(int event, int x, int y, int flags, void* userdata)
{
	EventGlobalInfo* eginfo = (EventGlobalInfo*)userdata;
	OpttrkData& otrk_data = eginfo->ginfo.otrk_data;// *(opttrk_data*)userdata;

	if (!otrk_data.trk_info.is_updated) return;

	vzm::ObjStates sobj_state;
	sobj_state.color[3] = 1.0f;
	sobj_state.emission = 0.5f;
	sobj_state.diffusion = 0.5f;
	sobj_state.specular = 0.0f;

	if (event == EVENT_LBUTTONDOWN | event == EVENT_RBUTTONDOWN)
	{
		optitrk::SetRigidBodyEnabledbyName(eginfo->ginfo.otrk_data.marker_rb_name, false);

		map<RsTouchMode, ButtonState>& rs_buttons = eginfo->ginfo.rs_buttons;
		auto disable_subbuttons = [&rs_buttons]()
		{
			for (auto it = rs_buttons.begin(); it != rs_buttons.end(); it++)
			{
				ButtonState& btn = it->second;
				if (btn.is_subbutton) btn.is_activated = false;
			}
		};
		for (auto it = rs_buttons.begin(); it != rs_buttons.end(); it++)
		{
			ButtonState& btn = it->second;
			if (btn.is_activated && btn.rect.contains(Point(x, y)))
			{
				eginfo->ginfo.touch_mode = btn.mode;
				btn.touch_count++;
			}
			else
				btn.touch_count = 0;
		}

		auto TouchOnButton = [&rs_buttons, &eginfo](int x, int y) -> bool
		{
			ButtonState& btn = rs_buttons[eginfo->ginfo.touch_mode];
			return btn.rect.contains(Point(x, y));
		};
		// to do //
		switch (eginfo->ginfo.touch_mode)
		{
		case RsTouchMode::Pick:
		{
			disable_subbuttons();
			rs_buttons[RsTouchMode::AR_Marker].is_activated = true;
			rs_buttons[RsTouchMode::DST_TOOL_E0].is_activated = true;
			rs_buttons[RsTouchMode::DST_TOOL_SE0].is_activated = true;
			rs_buttons[RsTouchMode::DST_TOOL_SE1].is_activated = true;
			rs_buttons[RsTouchMode::FIX_SCREW].is_activated = true;
			break;
		}
		case RsTouchMode::AR_Marker:
		{
			optitrk::SetRigidBodyEnabledbyName(eginfo->ginfo.otrk_data.marker_rb_name, true);
			if (TouchOnButton(x, y)) return;
			vector<Point3f>& point3ds_rsrbs = eginfo->ginfo.otrk_data.calib_3d_pts;
			if (x < eginfo->ginfo.rs_w / 2)
			{
				if (eginfo->ginfo.is_probe_detected)
				{
					glm::fvec3 ar_marker_pt = eginfo->ginfo.pos_probe_pin;
					cout << "----> " << eginfo->ginfo.vzmobjid2pos.size() << endl;
					TESTOUT("==> ", ar_marker_pt);

					TESTOUT("armk position " + to_string(point3ds_rsrbs.size()), ar_marker_pt);

					glm::fmat4x4 mat_armklf2ws;
					eginfo->ginfo.otrk_data.trk_info.GetLFrmInfo(eginfo->ginfo.otrk_data.marker_rb_name, mat_armklf2ws);
					glm::fmat4x4 mat_ws2armklf = glm::inverse(mat_armklf2ws);
					ar_marker_pt = tr_pt(mat_ws2armklf, ar_marker_pt);
					point3ds_rsrbs.push_back(Point3f(ar_marker_pt.x, ar_marker_pt.y, ar_marker_pt.z));
					cout << "# of total 3d pick positions : " << point3ds_rsrbs.size() << endl;
				}
			}
			else
			{
				if (point3ds_rsrbs.size() > 0)
					point3ds_rsrbs.pop_back();
			}

			ofstream outfile(eginfo->ginfo.cb_positions);
			if (outfile.is_open())
			{
				outfile.clear();
				for (int i = 0; i < point3ds_rsrbs.size(); i++)
				{
					string line = to_string(point3ds_rsrbs[i].x) + " " +
						to_string(point3ds_rsrbs[i].y) + " " +
						to_string(point3ds_rsrbs[i].z);
					outfile << line << endl;
				}
			}
			outfile.close();
		} break;
		case RsTouchMode::DST_TOOL_E0:
		{
			if (TouchOnButton(x, y)) return;
			SetManualProbe(eginfo->ginfo.dst_tool_name, eginfo->ginfo.src_tool_name, ONLY_PIN_POS, 0 , eginfo->ginfo);
		} break;
		case RsTouchMode::DST_TOOL_SE0:
		{
			if (TouchOnButton(x, y)) return;
			SetManualProbe(eginfo->ginfo.dst_tool_name, eginfo->ginfo.src_tool_name, ONLY_RBFRAME, 0, eginfo->ginfo);
		} break;
		case RsTouchMode::DST_TOOL_SE1:
		{
			if (TouchOnButton(x, y)) return;
			SetManualProbe(eginfo->ginfo.dst_tool_name, eginfo->ginfo.src_tool_name, ONLY_RBFRAME, 1, eginfo->ginfo);
		} break;
		case RsTouchMode::FIX_SCREW:
		{
			if (TouchOnButton(x, y)) return;
			if (eginfo->ginfo.is_probe_detected)
			{
				static vector<int> inserted_implant_ids;
				if (x < eginfo->ginfo.rs_w / 2)
				{
					glm::fvec3 pos_s = *(glm::fvec3*)&eginfo->ginfo.pos_probe_pin;
					glm::fvec3 tool_dir = eginfo->ginfo.dir_probe_se;

					const float screw_length = 0.04f;
					glm::fvec3 cyl_p[2] = { pos_s, pos_s - tool_dir * screw_length };
					glm::fvec3 cyl_rgb = glm::fvec3(1, 1, 0);
					float cyl_r = 0.003f;
					vzm::ObjStates tool_screw;
					int inserted_implant_id = 0;
					vzm::GenerateCylindersObject((float*)cyl_p, &cyl_r, __FP cyl_rgb, 1, inserted_implant_id);
					vzm::ReplaceOrAddSceneObject(eginfo->ginfo.stg_scene_id, inserted_implant_id, tool_screw);
					vzm::ReplaceOrAddSceneObject(eginfo->ginfo.rs_scene_id, inserted_implant_id, tool_screw);
					vzm::ReplaceOrAddSceneObject(eginfo->ginfo.ws_scene_id, inserted_implant_id, tool_screw);
					inserted_implant_ids.push_back(inserted_implant_id);
				}
				else
				{
					if (inserted_implant_ids.size() > 0)
					{
						vzm::DeleteObject(inserted_implant_ids[inserted_implant_ids.size() - 1]);
						inserted_implant_ids.pop_back();
					}
				}
			}
		} break;
		case RsTouchMode::Calib_TC:
		{
			optitrk::SetRigidBodyEnabledbyName(eginfo->ginfo.otrk_data.marker_rb_name, true);
			disable_subbuttons();
			rs_buttons[RsTouchMode::Pair_Clear].is_activated = true;
			//otrk_data.tc_calib_pt_pairs.clear();
			// processing during the main thread
		} break;
		case RsTouchMode::Calib_STG:
		case RsTouchMode::Calib_STG2:
		{
			disable_subbuttons();
			rs_buttons[RsTouchMode::STG_Pair_Clear].is_activated = true;
			rs_buttons[RsTouchMode::Calib_STG2].is_activated = true;
			glm::fmat4x4 mat_rbcam2ws;
			if (!otrk_data.trk_info.GetLFrmInfo("rs_cam", mat_rbcam2ws)) return;

			if (TouchOnButton(x, y)) return;

			int pick_obj = 0;
			glm::fvec3 pos_pick;
			const int r = 10;
			for (int ry = max(y - r, 0); ry < min(y + r, eginfo->ginfo.rs_h - 1); ry++)
				for (int rx = max(x - r, 0); rx < min(x + r, eginfo->ginfo.rs_w - 1); rx++)
				{
					if (vzm::PickObject(pick_obj, __FP pos_pick, rx, ry, eginfo->scene_id, eginfo->cam_id))
					{
						ry = eginfo->ginfo.rs_h;
						break;
					}
				}

			cout << "Calib_STG PICK ID : " << x << ", " << y << " ==> " << pick_obj << endl;
			if (pick_obj != 0)
			{
				glm::fvec3 mk_pt = eginfo->ginfo.vzmobjid2pos[pick_obj];
				for (int i = 0; i < (int)eginfo->ginfo.vzmobjid2pos.size(); i++)
				{
					glm::fvec3 mk_candi_pt = eginfo->ginfo.otrk_data.trk_info.GetMkPos(i);
					if (glm::length(mk_candi_pt - mk_pt) < 0.005f)
					{
						eginfo->ginfo.otrk_data.stg_calib_mk_cid = eginfo->ginfo.otrk_data.trk_info.mk_cid_list[i];
						break;
					}
				}
				cout << "Calib_STG MARKER CID : " << eginfo->ginfo.otrk_data.stg_calib_mk_cid << " / total # : " << eginfo->ginfo.vzmobjid2pos.size() << endl;
			}
			else
			{
				int stg_calib_mk_idx;
				if (!eginfo->ginfo.otrk_data.trk_info.CheckExistCID(eginfo->ginfo.otrk_data.stg_calib_mk_cid, &stg_calib_mk_idx))
				{
					eginfo->ginfo.otrk_data.stg_calib_mk_cid = 0;
					return;
				}

				const int w = eginfo->ginfo.stg_w / eginfo->ginfo.stg_display_num;
				const int h = eginfo->ginfo.stg_h;
				static Point2f pos_2d_rs[30] = {
					Point2f(w / 5.f, h / 4.f) , Point2f(w / 5.f * 2.f, h / 4.f) , Point2f(w / 5.f * 3.f, h / 4.f) , Point2f(w / 5.f * 4.f, h / 4.f),
					Point2f(w / 8.f, h / 4.f * 2.f) , Point2f(w / 8.f * 2.f, h / 4.f * 2.f) , Point2f(w / 8.f * 3.f, h / 4.f * 2.f) , Point2f(w / 8.f * 4.f, h / 4.f * 2.f),
					Point2f(w / 8.f * 5.f, h / 4.f * 2.f) , Point2f(w / 8.f * 6.f, h / 4.f * 2.f) , Point2f(w / 8.f * 7.f, h / 4.f * 2.f),
					Point2f(w / 5.f, h / 4.f * 3.f) , Point2f(w / 5.f * 2.f, h / 4.f * 3.f) , Point2f(w / 5.f * 3.f, h / 4.f * 3.f) , Point2f(w / 5.f * 4.f, h / 4.f * 3.f),

					Point2f(w / 5.f + w, h / 4.f) , Point2f(w / 5.f * 2.f + w, h / 4.f) , Point2f(w / 5.f * 3.f + w, h / 4.f) , Point2f(w / 5.f * 4.f + w, h / 4.f),
					Point2f(w / 8.f + w, h / 4.f * 2.f) , Point2f(w / 8.f * 2.f + w, h / 4.f * 2.f) , Point2f(w / 8.f * 3.f + w, h / 4.f * 2.f) , Point2f(w / 8.f * 4.f + w, h / 4.f * 2.f),
					Point2f(w / 8.f * 5.f + w, h / 4.f * 2.f) , Point2f(w / 8.f * 6.f + w, h / 4.f * 2.f) , Point2f(w / 8.f * 7.f + w, h / 4.f * 2.f),
					Point2f(w / 5.f + w, h / 4.f * 3.f) , Point2f(w / 5.f * 2.f + w, h / 4.f * 3.f) , Point2f(w / 5.f * 3.f + w, h / 4.f * 3.f) , Point2f(w / 5.f * 4.f + w, h / 4.f * 3.f) };

				vector<pair<Point2f, Point3f>>& stg_calib_pt_pairs = eginfo->ginfo.touch_mode == RsTouchMode::Calib_STG ? eginfo->ginfo.otrk_data.stg_calib_pt_pairs : eginfo->ginfo.otrk_data.stg_calib_pt_pairs_2;
				if (x < eginfo->ginfo.rs_w / 2)
				{
					if (stg_calib_pt_pairs.size() < 15)
					{
						glm::fvec3 mk_pt = eginfo->ginfo.otrk_data.trk_info.GetMkPos(stg_calib_mk_idx);
						glm::fmat4x4 mat_ws2clf = glm::inverse(mat_rbcam2ws);
						glm::fvec3 mk_pt_clf = tr_pt(mat_ws2clf, mk_pt);

						cout << "Add a STG calib marker!! ==> " << (eginfo->ginfo.touch_mode == RsTouchMode::Calib_STG ? "Display 1"  : "Display 2") << endl;
						Point2f mk_pt_2d = eginfo->ginfo.touch_mode == RsTouchMode::Calib_STG ? pos_2d_rs[stg_calib_pt_pairs.size()] :
							pos_2d_rs[stg_calib_pt_pairs.size() + 15] - Point2f(w, 0);
						stg_calib_pt_pairs.push_back(pair<Point2f, Point3f>(mk_pt_2d, Point3f(mk_pt_clf.x, mk_pt_clf.y, mk_pt_clf.z)));
					}
				}
				else
				{
					if (stg_calib_pt_pairs.size() > 0)
					{
						cout << "Remove the latest STG calib marker!! ==> " << (eginfo->ginfo.touch_mode == RsTouchMode::Calib_STG ? "Display 1" : "Display 2") << endl;
						stg_calib_pt_pairs.pop_back();
					}
				}
				cout << "# of STG calib point pairs : " << stg_calib_pt_pairs.size() << (eginfo->ginfo.touch_mode == RsTouchMode::Calib_STG ? "(Display 1)" : "(Display 2)") << endl;

				ofstream outfile(eginfo->ginfo.stg_calib);
				if (outfile.is_open())
				{
					outfile.clear();
					outfile << to_string(eginfo->ginfo.otrk_data.stg_calib_pt_pairs.size()) << endl;
					for (int i = 0; i < eginfo->ginfo.otrk_data.stg_calib_pt_pairs.size(); i++)
					{
						pair<Point2f, Point3f>& pr = eginfo->ginfo.otrk_data.stg_calib_pt_pairs[i];
						Point2f p2d = get<0>(pr);
						Point3f p3d = get<1>(pr);

						string line = to_string(p2d.x) + " " + to_string(p2d.y) + " " + to_string(p3d.x) + " " + to_string(p3d.y) + " " + to_string(p3d.z);
						outfile << line << endl;
					}

					outfile << "*** SECOND STG DISPLAY ***" << endl;
					outfile << to_string(eginfo->ginfo.otrk_data.stg_calib_pt_pairs_2.size()) << endl;
					for (int i = 0; i < eginfo->ginfo.otrk_data.stg_calib_pt_pairs_2.size(); i++)
					{
						pair<Point2f, Point3f>& pr = eginfo->ginfo.otrk_data.stg_calib_pt_pairs_2[i];
						Point2f p2d = get<0>(pr);
						Point3f p3d = get<1>(pr);

						string line = to_string(p2d.x) + " " + to_string(p2d.y) + " " + to_string(p3d.x) + " " + to_string(p3d.y) + " " + to_string(p3d.z);
						outfile << line << endl;
					}
				}
				outfile.close();
			}
		} break;
		case RsTouchMode::Align:
		{
			disable_subbuttons();
			rs_buttons[RsTouchMode::ICP].is_activated = true;
			rs_buttons[RsTouchMode::Capture].is_activated = true;
			if (eginfo->ginfo.model_ms_obj_id == 0) return;
			if (TouchOnButton(x, y))
			{

				//eginfo->ginfo.model_rbs_pick_pts.push_back(glm::fvec3(0.199279, 0.0466209, 0.177557));
				//eginfo->ginfo.model_rbs_pick_pts.push_back(glm::fvec3(0.16184, 0.0908376, 0.209669));
				//eginfo->ginfo.model_rbs_pick_pts.push_back(glm::fvec3(0.198357, 0.128109, 0.191832));
				//eginfo->ginfo.model_rbs_pick_pts.push_back(glm::fvec3(0.238769, 0.102674, 0.179341));
				//world position : 0.283975, 0.0835902, 0.216004
				//world position : 0.298452, 0.0335791, 0.254262
				return;
			}

			glm::fmat4x4 mat_rbs2ws;
			if (eginfo->ginfo.match_model_rbs_name != "")
			{
				if (!eginfo->ginfo.otrk_data.trk_info.GetLFrmInfo(eginfo->ginfo.match_model_rbs_name, mat_rbs2ws))
					return;
			}

			if (x < eginfo->ginfo.rs_w / 2)
			{
				TESTOUT("world position : ", eginfo->ginfo.pos_probe_pin);
					
				glm::fmat4x4 mat_ws2rbs = glm::inverse(mat_rbs2ws);
				eginfo->ginfo.model_rbs_pick_pts.push_back(tr_pt(mat_ws2rbs, eginfo->ginfo.pos_probe_pin));

				//static int i = 0;
				//glm::fvec3 p = eginfo->ginfo.pos_probe_pin + glm::fvec3(0.02 * (i + 2), 0.01 * i, 0.03) * (float)(i++);
				//TESTOUT("world position : ", p);
				//eginfo->ginfo.model_ws_pick_pts.push_back(p);
			}
			else // x >= eginfo->ginfo.rs_w / 2
			{
				if (eginfo->ginfo.model_rbs_pick_pts.size() > 0)
					eginfo->ginfo.model_rbs_pick_pts.pop_back();
			}

			int num_crrpts = (int)min(eginfo->ginfo.model_ms_pick_pts.size(), eginfo->ginfo.model_rbs_pick_pts.size());
			if (num_crrpts >= 4)
			{
				glm::fmat4x4 mat_ms2rbs;
				if (helpers::ComputeRigidTransform(__FP eginfo->ginfo.model_ms_pick_pts[0], __FP eginfo->ginfo.model_rbs_pick_pts[0], num_crrpts, __FP mat_ms2rbs[0]))
				{
					vzm::ObjStates model_obj_state;
					vzm::GetSceneObjectState(eginfo->ginfo.model_scene_id, eginfo->ginfo.model_ms_obj_id, model_obj_state);

					vzm::ObjStates model_ws_obj_state;
					vzm::GetSceneObjectState(eginfo->ginfo.ws_scene_id, eginfo->ginfo.model_ws_obj_id, model_ws_obj_state);
					model_ws_obj_state.is_visible = true;
					vzm::ReplaceOrAddSceneObject(eginfo->ginfo.ws_scene_id, eginfo->ginfo.model_ws_obj_id, model_ws_obj_state);

					glm::fmat4x4 mat_ms2ws = mat_rbs2ws * mat_ms2rbs;
					glm::fmat4x4 mat_match_model2ws = mat_ms2ws * (__cm4__ model_obj_state.os2ws); // the latter include scale factors
					eginfo->ginfo.mat_os2matchmodefrm = eginfo->ginfo.mat_ws2matchmodelfrm * mat_match_model2ws;
					eginfo->ginfo.mat_matchtr = mat_ms2ws;
					// current issue!
					//SetTransformMatrixOS2WS ?? SCENE PARAM ???? ??????!
					eginfo->ginfo.is_modelaligned = true;

					cout << "model matching done!" << endl;

					// store matrix....
					float* _os2matchmodefrm = glm::value_ptr(eginfo->ginfo.mat_os2matchmodefrm);
					float* _matchtr = glm::value_ptr(eginfo->ginfo.mat_matchtr);

					ofstream outfile("..\\Preset\\registration_matrix.txt");
					if (outfile.is_open())
					{
						outfile.clear();
						// os2matchmodelfrm
						for (int i = 0; i < 4; i++)
						{
							string line = to_string(_os2matchmodefrm[4 * i + 0]) + " " + to_string(_os2matchmodefrm[4 * i + 1]) + " " + to_string(_os2matchmodefrm[4 * i + 2]) + " " + to_string(_os2matchmodefrm[4 * i + 3]);
							outfile << line << endl;
						}
						// matchtr
						for (int i = 0; i < 4; i++)
						{
							string line = to_string(_matchtr[4 * i + 0]) + " " + to_string(_matchtr[4 * i + 1]) + " " + to_string(_matchtr[4 * i + 2]) + " " + to_string(_matchtr[4 * i + 3]);
							outfile << line << endl;
						}
					}
					outfile.close();
				}
			}
		} break;
		case RsTouchMode::ICP:
		{
			glm::fmat4x4 mat_tr;
			vzm::ObjStates ori_obj_state;
			vzm::GetSceneObjectState(eginfo->ginfo.model_scene_id, eginfo->ginfo.captured_model_ms_point_id, ori_obj_state);

			vzm::ObjStates tmp_obj_state = ori_obj_state;
			__cm4__ tmp_obj_state.os2ws = eginfo->ginfo.mat_matchtr;
			vzm::ReplaceOrAddSceneObject(eginfo->ginfo.model_scene_id, eginfo->ginfo.captured_model_ms_point_id, tmp_obj_state);
			vzmproc::ComputeMatchingTransform(eginfo->ginfo.captured_model_ms_point_id, eginfo->ginfo.captured_model_ws_point_id, __FP mat_tr);
			vzm::ReplaceOrAddSceneObject(eginfo->ginfo.model_scene_id, eginfo->ginfo.captured_model_ms_point_id, ori_obj_state);

			vzm::ObjStates model_obj_state;
			vzm::GetSceneObjectState(eginfo->ginfo.model_scene_id, eginfo->ginfo.model_ms_obj_id, model_obj_state);
			glm::fmat4x4 mat_match_model2ws = (mat_tr * eginfo->ginfo.mat_matchtr) * (__cm4__ model_obj_state.os2ws);
			eginfo->ginfo.mat_os2matchmodefrm = eginfo->ginfo.mat_ws2matchmodelfrm * mat_match_model2ws;
			eginfo->ginfo.mat_matchtr = mat_tr * eginfo->ginfo.mat_matchtr;
			eginfo->ginfo.is_modelaligned = true;
			cout << "model ICP matching done!" << endl;
		} break;
		case RsTouchMode::Pair_Clear:
		{
			for (int i = 0; i < eginfo->ginfo.otrk_data.calib_trial_rs_cam_frame_ids.size(); i++)
				vzm::DeleteObject(eginfo->ginfo.otrk_data.calib_trial_rs_cam_frame_ids[i]);
			eginfo->ginfo.otrk_data.calib_trial_rs_cam_frame_ids.clear();
			eginfo->ginfo.otrk_data.tc_calib_pt_pairs.clear();
			cout << "Clear point pairs!!" << endl;
		} break;
		case RsTouchMode::STG_Pair_Clear:
		{
			eginfo->ginfo.otrk_data.stg_calib_pt_pairs.clear();
			eginfo->ginfo.otrk_data.stg_calib_pt_pairs_2.clear();
			cout << "Clear STG point pairs!!" << endl;
		} break;
		case RsTouchMode::Capture:
		{
			if (TouchOnButton(x, y)) return;
			if (x < eginfo->ginfo.rs_w / 2)
			{
				if (eginfo->ginfo.rs_pc_id == 0) return;

				glm::fvec3 pos_pick;
				if(!otrk_data.trk_info.GetProbePinPoint(pos_pick)) return;

				vzm::ObjStates model_obj_state;
				vzm::GetSceneObjectState(eginfo->ginfo.ws_scene_id, eginfo->ginfo.rs_pc_id, model_obj_state);
				glm::fmat4x4 mat_ws2os = glm::inverse(*(glm::fmat4x4*)model_obj_state.os2ws);

				glm::fvec3 pos_pick_os = tr_pt(mat_ws2os, pos_pick);

				vzmproc::GenerateSamplePoints(eginfo->ginfo.rs_pc_id, (float*)&pos_pick_os, 0.02f, 0.0003f, eginfo->ginfo.captured_model_ws_point_id);
				cout << "Capturing in RS PC" << endl;

				vzm::ObjStates sobj_state;
				__cv4__ sobj_state.color = glm::fvec4(1, 1, 0, 1);
				sobj_state.emission = 0.5f;
				sobj_state.diffusion = 0.5f;
				sobj_state.specular = 0.0f;
				//sobj_state.point_thickness = 10.f;
				sobj_state.surfel_size = 0.005f;
				//*(glm::fmat4x4*)sobj_state.os2ws = *(glm::fmat4x4*)model_obj_state.os2ws;
				vzm::ReplaceOrAddSceneObject(eginfo->ginfo.ws_scene_id, eginfo->ginfo.captured_model_ws_point_id, sobj_state);
				vzm::ReplaceOrAddSceneObject(eginfo->ginfo.rs_scene_id, eginfo->ginfo.captured_model_ws_point_id, sobj_state);
				vzm::ReplaceOrAddSceneObject(eginfo->ginfo.stg_scene_id, eginfo->ginfo.captured_model_ws_point_id, sobj_state);
			}
			else
			{
				vzm::DeleteObject(eginfo->ginfo.captured_model_ws_point_id);
				cout << "Clear capture points in WS" << endl;
			}
			//eginfo->ginfo.touch_mode = RsTouchMode::Align;
		} break;
		default:
			disable_subbuttons(); return;
		}
	}
	return;
	//else if (eginfo->ginfo.touch_mode == PIN_ORIENTATION)
	//{
	//	if (event == EVENT_LBUTTONDOWN)
	//	{
	//		if (eginfo->ginfo.is_calib_rs_cam)
	//		{
	//			if (!otrk_data.trk_info.is_detected_sstool) return;
	//
	//			glm::fvec3 pt = otrk_data.trk_info.GetProbePinPoint();
	//
	//			glm::fmat4x4 mat_ws2tfrm = glm::inverse(otrk_data.trk_info.mat_tfrm2ws);
	//			eginfo->ginfo.ss_tool_info.pos_centers_tfrm.push_back(tr_pt(mat_ws2tfrm, pt));
	//
	//			ofstream outfile(eginfo->ginfo.sst_positions);
	//			if (outfile.is_open())
	//			{
	//				outfile.clear();
	//				for (int i = 0; i < eginfo->ginfo.ss_tool_info.pos_centers_tfrm.size(); i++)
	//				{
	//					string line = to_string(eginfo->ginfo.ss_tool_info.pos_centers_tfrm[i].x) + " " +
	//						to_string(eginfo->ginfo.ss_tool_info.pos_centers_tfrm[i].y) + " " +
	//						to_string(eginfo->ginfo.ss_tool_info.pos_centers_tfrm[i].z);
	//					outfile << line << endl;
	//				}
	//			}
	//			outfile.close();
	//		}
	//	}
	//}
}

//void CallBackFunc_StgMouse(int event, int x, int y, int flags, void* userdata)
//{
//	EventGlobalInfo* eginfo = (EventGlobalInfo*)userdata;
//	OpttrkData& otrk_data = eginfo->ginfo.otrk_data;// *(opttrk_data*)userdata;
//
//	//vector<Point3f>& point3ds = otrk_data.stg_calib_pt_pairs;
//
//	if (!otrk_data.trk_info.is_updated) return;
//
//	if (eginfo->ginfo.touch_mode == Calib_STG)
//	{
//		int stg_calib_mk_idx;
//		if (!eginfo->ginfo.otrk_data.trk_info.CheckExistCID(eginfo->ginfo.otrk_data.stg_calib_mk_cid, &stg_calib_mk_idx)) return;
//
//		glm::fmat4x4 mat_rbcam2ws;
//		if (otrk_data.trk_info.GetLFrmInfo("rs_cam", mat_rbcam2ws))
//		{
//			if (event == EVENT_LBUTTONDOWN)
//			{
//				glm::fvec3 mk_pt = eginfo->ginfo.otrk_data.trk_info.GetMkPos(stg_calib_mk_idx);
//				glm::fmat4x4 mat_ws2clf = glm::inverse(mat_rbcam2ws);
//				glm::fvec3 mk_pt_clf = tr_pt(mat_ws2clf, mk_pt);
//
//				otrk_data.stg_calib_pt_pairs.push_back(pair<Point2f, Point3f>(Point2f(x, y), Point3f(mk_pt_clf.x, mk_pt_clf.y, mk_pt_clf.z)));
//				cout << "# of STG calib point pairs : " << otrk_data.stg_calib_pt_pairs.size() << endl;
//			}
//			else if(event == EVENT_RBUTTONDOWN)
//			{
//				if (otrk_data.stg_calib_pt_pairs.size() > 0)
//				{
//					cout << "Remove the latest STG calib marker!!" << endl;
//					otrk_data.stg_calib_pt_pairs.pop_back();
//				}
//			}
//
//			ofstream outfile(eginfo->ginfo.stg_calib);
//			if (outfile.is_open())
//			{
//				outfile.clear();
//				outfile << to_string(eginfo->ginfo.otrk_data.stg_calib_pt_pairs.size()) << endl;
//				for (int i = 0; i < eginfo->ginfo.otrk_data.stg_calib_pt_pairs.size(); i++)
//				{
//					pair<Point2f, Point3f>& pr = eginfo->ginfo.otrk_data.stg_calib_pt_pairs[i];
//					Point2f p2d = get<0>(pr);
//					Point3f p3d = get<1>(pr);
//
//					string line = to_string(p2d.x) + " " + to_string(p2d.y) + " " + to_string(p3d.x) + " " + to_string(p3d.y) + " " + to_string(p3d.z);
//					outfile << line << endl;
//				}
//			}
//			outfile.close();
//		}
//	}
//}

void CallBackFunc_ModelMouse(int event, int x, int y, int flags, void* userdata)
{
	EventGlobalInfo* eginfo = (EventGlobalInfo*)userdata;
	vzm::CameraParameters cam_params;
	vzm::GetCameraParameters(eginfo->scene_id, cam_params, eginfo->cam_id);

	static int x_old = -1;
	static int y_old = -1;
	//if ((x - x_old) * (x - x_old) + (y - y_old) * (y - y_old) < 1) return;

	x_old = x;
	y_old = y;

	eginfo->ginfo.skip_call_render = true;
	// https://docs.opencv.org/3.4/d7/dfc/group__highgui.html
	static helpers::arcball aball_ov;
	if (flags & EVENT_FLAG_CTRLKEY)
	{
		if (event == EVENT_LBUTTONDOWN || event == EVENT_RBUTTONDOWN)
		{
			if (eginfo->ginfo.model_ms_obj_id == 0)
				Show_Window_with_Texts(eginfo->ginfo.window_name_ms_view, eginfo->scene_id, eginfo->cam_id, "NO MESH!!");
			if (flags & EVENT_FLAG_CTRLKEY)
			{
				if (event == EVENT_LBUTTONDOWN)
				{
					glm::fvec3 pos_pick;
					if (GetSufacePickPos(pos_pick, eginfo->scene_id, eginfo->cam_id, eginfo->ginfo.model_volume_id == 0, x, y))
					{
						eginfo->ginfo.model_ms_pick_pts.push_back(pos_pick);
					}
				}
				else if (event == EVENT_RBUTTONDOWN)
				{
					if (eginfo->ginfo.model_ms_pick_pts.size() > 0)
						eginfo->ginfo.model_ms_pick_pts.pop_back();
				}
				if (eginfo->ginfo.model_ms_pick_pts.size() > 0)
				{
					vector<glm::fvec4> spheres_xyzr;
					vector<glm::fvec3> spheres_rgb;
					for (int i = 0; i < (int)eginfo->ginfo.model_ms_pick_pts.size(); i++)
					{
						glm::fvec4 sphere_xyzr = glm::fvec4(eginfo->ginfo.model_ms_pick_pts[i], 0.002);
						spheres_xyzr.push_back(sphere_xyzr);
						glm::fvec3 sphere_rgb = glm::fvec3(1, 0, 1);
						spheres_rgb.push_back(sphere_rgb);
					}
					vzm::ObjStates sobj_state;
					sobj_state.color[3] = 1.0f;
					sobj_state.emission = 0.5f;
					sobj_state.diffusion = 0.5f;
					sobj_state.specular = 0.0f;
					vzm::GenerateSpheresObject(__FP spheres_xyzr[0], __FP spheres_rgb[0], (int)eginfo->ginfo.model_ms_pick_pts.size(), eginfo->ginfo.model_ms_pick_spheres_id);
					vzm::ReplaceOrAddSceneObject(eginfo->scene_id, eginfo->ginfo.model_ms_pick_spheres_id, sobj_state);
				}
				else
				{
					vzm::DeleteObject(eginfo->ginfo.model_ms_pick_spheres_id);
					eginfo->ginfo.model_ms_pick_spheres_id = 0;
				}

				Show_Window_with_Info(eginfo->ginfo.window_name_ms_view, eginfo->scene_id, eginfo->cam_id, eginfo->ginfo);

				ofstream outfile(eginfo->ginfo.model_predefined_pts);
				if (outfile.is_open())
				{
					outfile.clear();
					//outfile << to_string(eginfo->ginfo.model_ms_pick_pts.size()) << endl;
					for (int i = 0; i < eginfo->ginfo.model_ms_pick_pts.size(); i++)
					{
						string line = to_string(eginfo->ginfo.model_ms_pick_pts[i].x) + " " +
							to_string(eginfo->ginfo.model_ms_pick_pts[i].y) + " " +
							to_string(eginfo->ginfo.model_ms_pick_pts[i].z);
						outfile << line << endl;
					}
				}
				outfile.close();
			}
		}
	}
	else if (flags & EVENT_FLAG_ALTKEY)
	{
		if (event == EVENT_LBUTTONDOWN)
		{
			if (eginfo->ginfo.model_ms_obj_id == 0)
				Show_Window_with_Texts(eginfo->ginfo.window_name_ms_view, eginfo->scene_id, eginfo->cam_id, "NO MESH!!");

			glm::fvec3 pos_pick;
			if (!GetSufacePickPos(pos_pick, eginfo->scene_id, eginfo->cam_id, eginfo->ginfo.model_volume_id == 0, x, y)) return;

			vzm::ObjStates model_obj_state;
			vzm::GetSceneObjectState(eginfo->scene_id, eginfo->ginfo.model_ms_obj_id, model_obj_state);
			glm::fmat4x4 mat_ws2os = glm::inverse(*(glm::fmat4x4*)model_obj_state.os2ws);

			glm::fvec3 pos_pick_os = tr_pt(mat_ws2os, pos_pick);
			vzmproc::GenerateSamplePoints(eginfo->ginfo.model_ms_obj_id, (float*)&pos_pick_os, 20.f, 0.3f, eginfo->ginfo.captured_model_ms_point_id);

			vzm::ObjStates sobj_state;
			__cv4__ sobj_state.color = glm::fvec4(1, 1, 0, 1);
			sobj_state.emission = 0.5f;
			sobj_state.diffusion = 0.5f;
			sobj_state.specular = 0.0f;
			//sobj_state.point_thickness = 10.f;
			sobj_state.surfel_size = 0.005f;
			//*(glm::fmat4x4*)sobj_state.os2ws = *(glm::fmat4x4*)model_obj_state.os2ws;
			vzm::ReplaceOrAddSceneObject(eginfo->scene_id, eginfo->ginfo.captured_model_ms_point_id, sobj_state);
			
			Show_Window_with_Info(eginfo->ginfo.window_name_ms_view, eginfo->scene_id, eginfo->cam_id, eginfo->ginfo);
		}
		else if(event == EVENT_RBUTTONDOWN)
		{
			vzm::DeleteObject(eginfo->ginfo.captured_model_ms_point_id);

			Show_Window_with_Info(eginfo->ginfo.window_name_ms_view, eginfo->scene_id, eginfo->cam_id, eginfo->ginfo);
		}
	}
	else
	{
		// manipulating camera location
		if (event == EVENT_LBUTTONDOWN || event == EVENT_RBUTTONDOWN)
		{
			//if(eginfo->ginfo.scenario != 0)
				aball_ov.intializer((float*)&glm::fvec3(), 0.10f);
			//else
			//	aball_ov.intializer((float*)&(glm::fvec3(112.896, 112.896, 91.5) * 0.001f), 0.20f);

			helpers::cam_pose arc_cam_pose;
			glm::fvec3 pos = __cv3__ arc_cam_pose.pos = __cv3__ cam_params.pos;
			__cv3__ arc_cam_pose.up = __cv3__ cam_params.up;
			__cv3__ arc_cam_pose.view = __cv3__ cam_params.view;
			aball_ov.start((int*)&glm::ivec2(x, y), (float*)&glm::fvec2(cam_params.w, cam_params.h), arc_cam_pose);
		}
		else if (event == EVENT_MOUSEWHEEL)
		{
			if (getMouseWheelDelta(flags) > 0)
				__cv3__ cam_params.pos += 0.01f * (__cv3__ cam_params.view);
			else
				__cv3__ cam_params.pos -= 0.01f * (__cv3__ cam_params.view);
			vzm::SetCameraParameters(eginfo->scene_id, cam_params, eginfo->cam_id);
			Show_Window_with_Info(eginfo->ginfo.window_name_ms_view, eginfo->scene_id, eginfo->cam_id, eginfo->ginfo);
		}
		else if (event == EVENT_MOUSEMOVE)
		{
			if (flags & EVENT_FLAG_LBUTTON)
			{
				helpers::cam_pose arc_cam_pose;
				aball_ov.pan_move((int*)&glm::ivec2(x, y), arc_cam_pose);
				__cv3__ cam_params.pos = __cv3__ arc_cam_pose.pos;
				__cv3__ cam_params.up = __cv3__ arc_cam_pose.up;
				__cv3__ cam_params.view = __cv3__ arc_cam_pose.view;
				vzm::SetCameraParameters(eginfo->scene_id, cam_params, eginfo->cam_id);
				Show_Window_with_Info(eginfo->ginfo.window_name_ms_view, eginfo->scene_id, eginfo->cam_id, eginfo->ginfo);
			}
			else if (flags & EVENT_FLAG_RBUTTON)
			{
				helpers::cam_pose arc_cam_pose;
				aball_ov.move((int*)&glm::ivec2(x, y), arc_cam_pose);
				__cv3__ cam_params.pos = __cv3__ arc_cam_pose.pos;
				__cv3__ cam_params.up = __cv3__ arc_cam_pose.up;
				__cv3__ cam_params.view = __cv3__ arc_cam_pose.view;
				vzm::SetCameraParameters(eginfo->scene_id, cam_params, eginfo->cam_id);
				Show_Window_with_Info(eginfo->ginfo.window_name_ms_view, eginfo->scene_id, eginfo->cam_id, eginfo->ginfo);
			}
		}
		else if (event == EVENT_LBUTTONUP || event == EVENT_RBUTTONUP)
		{
			vzm::CameraParameters cam_params;
			vzm::GetCameraParameters(eginfo->scene_id, cam_params, eginfo->cam_id);
			ofstream fileout(eginfo->ginfo.model_view_preset);
			if (!fileout.is_open()) return;

			fileout << "cam_pos " << __PR(cam_params.pos, " ") << endl;
			fileout << "cam_up " << __PR(cam_params.up, " ") << endl;
			fileout << "cam_view " << __PR(cam_params.view, " ") << endl;
			fileout.close();
		}
	}

	//int key_pressed = cv::waitKey(10);
	eginfo->ginfo.skip_call_render = false;
}