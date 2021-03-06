// Copyright (c) 2014- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#ifdef _WIN32
#include <windows.h>
#include <Shobjidl.h>
#include "GPU/Common/VR920.h"
#endif

#include "base/logging.h"
#include "Common/Common.h"
#include "Common/StdMutex.h"
#include "Core/Config.h"
#include "Core/CoreParameter.h"
#include "Core/System.h"
#include "GPU/Common/VR.h"
#include "GPU/Common/VROculus.h"
#include "GPU/Common/VROpenVR.h"

#ifdef HAVE_OPENVR
vr::IVRSystem *m_pHMD;
vr::IVRRenderModels *m_pRenderModels;
vr::IVRCompositor *m_pCompositor;
std::string m_strDriver;
std::string m_strDisplay;
vr::TrackedDevicePose_t m_rTrackedDevicePose[vr::k_unMaxTrackedDeviceCount];
bool m_bUseCompositor = true;
bool m_rbShowTrackedDevice[vr::k_unMaxTrackedDeviceCount];
int m_iValidPoseCount;
#endif

void ClearDebugProj();

VRPose g_eye_poses[2];

#ifdef OVR_MAJOR_VERSION
ovrHmd hmd = nullptr;
ovrHmdDesc hmdDesc;
ovrFovPort g_best_eye_fov[2], g_eye_fov[2], g_last_eye_fov[2];
ovrEyeRenderDesc g_eye_render_desc[2];
#if OVR_PRODUCT_VERSION == 0 && OVR_MAJOR_VERSION <= 7
ovrFrameTiming g_rift_frame_timing;
#endif
long long g_vr_frame_index;
#if OVR_PRODUCT_VERSION >= 1 || OVR_MAJOR_VERSION >= 7
ovrGraphicsLuid luid;
#endif
#endif

#ifdef _WIN32
LUID *g_hmd_luid = nullptr;
#endif

std::mutex g_vr_lock;

#ifdef HAVE_OCULUSSDK
#ifdef HAVE_OPENVR
#define SCM_OCULUS_STR ", Oculus SDK " OVR_VERSION_STRING " or SteamVR"
#else
#define SCM_OCULUS_STR ", Oculus SDK " OVR_VERSION_STRING
#endif
#else
#if defined(_WIN32) && defined(OVR_MAJOR_VERSION)
#ifdef HAVE_OPENVR
#define SCM_OCULUS_STR ", for Oculus DLL " OVR_VERSION_STRING " or SteamVR"
#else
#define SCM_OCULUS_STR ", for Oculus DLL " OVR_VERSION_STRING
#endif
#else
#ifdef HAVE_OPENVR
#define SCM_OCULUS_STR ", SteamVR"
#else
#define SCM_OCULUS_STR ", no Oculus SDK"
#endif
#endif
#endif

std::string g_vr_sdk_version_string = SCM_OCULUS_STR;

bool g_vr_cant_motion_blur = false, g_vr_must_motion_blur = false;
bool g_vr_has_dynamic_predict = true, g_vr_has_configure_rendering = true, g_vr_has_hq_distortion = true;
bool g_vr_should_swap_buffers = true, g_vr_dont_vsync = false;
bool g_vr_can_async_timewarp = false;
volatile bool g_vr_asyc_timewarp_active = false;

bool g_force_vr = false, g_prefer_steamvr = false;
bool g_has_hmd = false, g_has_rift = false, g_has_vr920 = false, g_has_steamvr = false;
bool g_is_direct_mode = false;
bool g_new_tracking_frame = true;
bool g_dumpThisFrame = false;
bool g_first_vr_frame = true;

Matrix44 g_head_tracking_matrix;
float g_head_tracking_position[3];
float g_left_hand_tracking_position[3], g_right_hand_tracking_position[3];

int g_hmd_window_width = 0, g_hmd_window_height = 0, g_hmd_window_x = 0, g_hmd_window_y = 0;
int g_hmd_refresh_rate = 75;
const char *g_hmd_device_name = nullptr;

float g_vr_speed = 0;
bool g_fov_changed = false, g_vr_black_screen = false;

bool g_vr_had_3D_already = false;
float vr_widest_3d_HFOV=0, vr_widest_3d_VFOV=0, vr_widest_3d_zNear=0, vr_widest_3d_zFar=0;
float this_frame_widest_HFOV=0, this_frame_widest_VFOV=0, this_frame_widest_zNear=0, this_frame_widest_zFar=0;

float g_game_camera_pos[3];
Matrix44 g_game_camera_rotmat;

bool debug_newScene = true, debug_nextScene = false;

// freelook
Matrix3x3 s_viewRotationMatrix;
Matrix3x3 s_viewInvRotationMatrix;
float s_fViewTranslationVector[3] = { 0, 0, 0 };
float s_fViewRotation[2] = { 0, 0 };
float vr_freelook_speed = 0;
bool bProjectionChanged = false;
bool bFreeLookChanged = false;

bool g_new_frame_just_rendered = false;
bool g_first_pass = true;
bool g_first_pass_vs_constants = true;
bool g_opcode_replay_frame = false;
bool g_opcode_replay_log_frame = false;
int skipped_opcode_replay_count = 0;

#ifdef _WIN32
static char hmd_device_name[MAX_PATH] = "";
#endif

bool VR_ShouldUnthrottle() {
	return g_has_rift && !g_vr_asyc_timewarp_active && g_Config.bSynchronousTimewarp;
}

void VR_NewVRFrame()
{
	//INFO_LOG(VR, "-- NewVRFrame --");
	//g_new_tracking_frame = true;
	if (!g_vr_had_3D_already)
	{
		Matrix44::LoadIdentity(g_game_camera_rotmat);
	}
	g_vr_had_3D_already = false;
	ClearDebugProj();
}

#ifdef HAVE_OPENVR
//-----------------------------------------------------------------------------
// Purpose: Helper to get a string from a tracked device property and turn it
//			into a std::string
//-----------------------------------------------------------------------------
std::string GetTrackedDeviceString(vr::IVRSystem *pHmd, vr::TrackedDeviceIndex_t unDevice, vr::TrackedDeviceProperty prop, vr::TrackedPropertyError *peError = nullptr)
{
	uint32_t unRequiredBufferLen = pHmd->GetStringTrackedDeviceProperty(unDevice, prop, nullptr, 0, peError);
	if (unRequiredBufferLen == 0)
		return "";

	char *pchBuffer = new char[unRequiredBufferLen];
	unRequiredBufferLen = pHmd->GetStringTrackedDeviceProperty(unDevice, prop, pchBuffer, unRequiredBufferLen, peError);
	std::string sResult = pchBuffer;
	delete[] pchBuffer;
	return sResult;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool BInitCompositor()
{
	vr::HmdError peError = vr::HmdError_None;

	m_pCompositor = (vr::IVRCompositor*)vr::VR_GetGenericInterface(vr::IVRCompositor_Version, &peError);

	if (peError != vr::HmdError_None)
	{
		m_pCompositor = nullptr;

		NOTICE_LOG(VR, "Compositor initialization failed with error: %s\n", vr::VR_GetStringForHmdError(peError));
		return false;
	}

	uint32_t unSize = m_pCompositor->GetLastError(NULL, 0);
	if (unSize > 1)
	{
		char* buffer = new char[unSize];
		m_pCompositor->GetLastError(buffer, unSize);
		NOTICE_LOG(VR, "Compositor - %s\n", buffer);
		delete[] buffer;
		return false;
	}

	// change grid room colour
	m_pCompositor->FadeToColor(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, true);

	return true;
}
#endif

bool InitSteamVR()
{
#ifdef HAVE_OPENVR
	// Loading the SteamVR Runtime
	vr::HmdError eError = vr::HmdError_None;
	m_pHMD = vr::VR_Init(&eError, vr::VRApplication_Scene);

	if (eError != vr::HmdError_None)
	{
		m_pHMD = nullptr;
		ERROR_LOG(VR, "Unable to init SteamVR: %s", vr::VR_GetStringForHmdError(eError));
		g_has_steamvr = false;
	}
	else
	{
		m_pRenderModels = (vr::IVRRenderModels *)vr::VR_GetGenericInterface(vr::IVRRenderModels_Version, &eError);
		if (!m_pRenderModels)
		{
			m_pHMD = nullptr;
			vr::VR_Shutdown();

			ERROR_LOG(VR, "Unable to get render model interface: %s", vr::VR_GetStringForHmdError(eError));
			g_has_steamvr = false;
		}
		else
		{
			NOTICE_LOG(VR, "VR_Init Succeeded");
			g_has_steamvr = true;
			g_has_hmd = true;
		}

		u32 m_nWindowWidth = 0;
		u32 m_nWindowHeight = 0;
		m_pHMD->GetWindowBounds(&g_hmd_window_x, &g_hmd_window_y, &m_nWindowWidth, &m_nWindowHeight);
		g_hmd_window_width = m_nWindowWidth;
		g_hmd_window_height = m_nWindowHeight;
		NOTICE_LOG(VR, "SteamVR WindowBounds (%d,%d) %dx%d", g_hmd_window_x, g_hmd_window_y, g_hmd_window_width, g_hmd_window_height);

		std::string m_strDriver = "No Driver";
		std::string m_strDisplay = "No Display";
		m_strDriver = GetTrackedDeviceString(m_pHMD, vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_TrackingSystemName_String);
		m_strDisplay = GetTrackedDeviceString(m_pHMD, vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SerialNumber_String);
		vr::TrackedPropertyError error;
		g_hmd_refresh_rate = (int)(0.5f + m_pHMD->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float, &error));
		NOTICE_LOG(VR, "SteamVR strDriver = '%s'", m_strDriver.c_str());
		NOTICE_LOG(VR, "SteamVR strDisplay = '%s'", m_strDisplay.c_str());
		NOTICE_LOG(VR, "SteamVR refresh rate = %d Hz", g_hmd_refresh_rate);

		if (m_bUseCompositor)
		{
			if (!BInitCompositor())
			{
				ERROR_LOG(VR, "%s - Failed to initialize SteamVR Compositor!\n", __FUNCTION__);
				g_has_steamvr = false;
			}
		}
		if (g_has_steamvr) {
			g_vr_can_async_timewarp = false;
			g_vr_cant_motion_blur = true;
			g_vr_has_dynamic_predict = false;
			g_vr_has_configure_rendering = false;
			g_vr_has_hq_distortion = false;
			g_vr_should_swap_buffers = true; // todo: check if this is right
		}
		return g_has_steamvr;
	}
#endif
	return false;
}

bool InitOculusDebugVR()
{
#if defined(OVR_MAJOR_VERSION) && OVR_PRODUCT_VERSION == 0 && OVR_MAJOR_VERSION <= 6
	if (g_force_vr)
	{
		NOTICE_LOG(VR, "Forcing VR mode, simulating Oculus Rift DK2.");
#if OVR_MAJOR_VERSION >= 6
		if (ovrHmd_CreateDebug(ovrHmd_DK2, &hmd) != ovrSuccess)
			hmd = nullptr;
#else
		hmd = ovrHmd_CreateDebug(ovrHmd_DK2);
#endif
		if (hmd != nullptr)
			g_vr_can_async_timewarp = false;

		return (hmd != nullptr);
	}
#endif
	return false;
}

bool InitOculusHMD()
{
#ifdef OVR_MAJOR_VERSION
	if (hmd)
	{

		g_vr_dont_vsync = true;
#if OVR_PRODUCT_VERSION >= 1 || OVR_MAJOR_VERSION >= 5 || (OVR_MINOR_VERSION == 4 && OVR_BUILD_VERSION >= 2)
		g_vr_has_hq_distortion = true;
#else
		g_vr_has_hq_distortion = false;
#endif
#if OVR_PRODUCT_VERSION == 0 && OVR_MAJOR_VERSION <= 5
		g_vr_should_swap_buffers = false;
#else
		g_vr_should_swap_buffers = true;
#endif

		// Get more details about the HMD
		//ovrHmd_GetDesc(hmd, &hmdDesc);
#if OVR_PRODUCT_VERSION >= 1
		g_vr_cant_motion_blur = true;
		g_vr_has_dynamic_predict = false;
		g_vr_has_configure_rendering = false;
		hmdDesc = ovr_GetHmdDesc(hmd);
#elif OVR_MAJOR_VERSION >= 7
		g_vr_cant_motion_blur = true;
		g_vr_has_dynamic_predict = false;
		g_vr_has_configure_rendering = false;
		hmdDesc = ovr_GetHmdDesc(hmd);
		ovr_SetEnabledCaps(hmd, ovrHmd_GetEnabledCaps(hmd) | 0);
#else
		g_vr_cant_motion_blur = false;
		g_vr_has_dynamic_predict = true;
		g_vr_has_configure_rendering = true;
		hmdDesc = *hmd;
		ovrHmd_SetEnabledCaps(hmd, ovrHmd_GetEnabledCaps(hmd) | ovrHmdCap_DynamicPrediction | ovrHmdCap_LowPersistence);
#endif



#if OVR_PRODUCT_VERSION >= 1
		// no need to configure tracking
#elif OVR_MAJOR_VERSION >= 6
		if (OVR_SUCCESS(ovrHmd_ConfigureTracking(hmd, ovrTrackingCap_Orientation | ovrTrackingCap_Position | ovrTrackingCap_MagYawCorrection, 0)))
#else
		if (ovrHmd_ConfigureTracking(hmd, ovrTrackingCap_Orientation | ovrTrackingCap_Position | ovrTrackingCap_MagYawCorrection, 0))
#endif
		{
			g_has_rift = true;
			g_has_hmd = true;
			g_vr_must_motion_blur = hmdDesc.Type <= ovrHmd_DK1;
			g_hmd_window_width = hmdDesc.Resolution.w;
			g_hmd_window_height = hmdDesc.Resolution.h;
			g_best_eye_fov[0] = hmdDesc.DefaultEyeFov[0];
			g_best_eye_fov[1] = hmdDesc.DefaultEyeFov[1];
			g_eye_fov[0] = g_best_eye_fov[0];
			g_eye_fov[1] = g_best_eye_fov[1];
			g_last_eye_fov[0] = g_eye_fov[0];
			g_last_eye_fov[1] = g_eye_fov[1];			
#if OVR_PRODUCT_VERSION == 0 && OVR_MAJOR_VERSION < 6
			// Before Oculus SDK 0.6 we had to size and position the mirror window (or actual window) correctly, at least for OpenGL.
			g_hmd_window_x = hmdDesc.WindowsPos.x;
			g_hmd_window_y = hmdDesc.WindowsPos.y;
			g_Config.iWindowX = g_hmd_window_x;
			g_Config.iWindowY = g_hmd_window_y;
			g_Config.iWindowWidth = g_hmd_window_width;
			g_Config.iWindowHeight = g_hmd_window_height;
			g_is_direct_mode = !(hmdDesc.HmdCaps & ovrHmdCap_ExtendDesktop);
			if (hmdDesc.Type < 6)
				g_hmd_refresh_rate = 60;
			else if (hmdDesc.Type > 6)
				g_hmd_refresh_rate = 90;
			else
				g_hmd_refresh_rate = 75;
#else
#if OVR_PRODUCT_VERSION == 0 && OVR_MAJOR_VERSION == 6
			g_hmd_refresh_rate = (int)(1.0f / ovrHmd_GetFloat(hmd, "VsyncToNextVsync", 0.f) + 0.5f);
#else
			g_hmd_refresh_rate = (int)(hmdDesc.DisplayRefreshRate + 0.5f);
#endif
			g_hmd_window_x = 0;
			g_hmd_window_y = 0;
			g_is_direct_mode = true;
#endif
#ifdef _WIN32
#if OVR_PRODUCT_VERSION == 0 && OVR_MAJOR_VERSION < 6
			g_hmd_device_name = hmdDesc.DisplayDeviceName;
#else
			g_hmd_device_name = nullptr;
#endif
			const char *p;
			if (g_hmd_device_name && (p = strstr(g_hmd_device_name, "\\Monitor")))
			{
				size_t n = p - g_hmd_device_name;
				if (n >= MAX_PATH)
					n = MAX_PATH - 1;
				g_hmd_device_name = strncpy(hmd_device_name, g_hmd_device_name, n);
				hmd_device_name[n] = '\0';
			}
#endif
			NOTICE_LOG(VR, "Oculus Rift head tracker started.");
		}
		return g_has_rift;
	}
#endif
	return false;
}

bool InitOculusVR()
{
#ifdef OVR_MAJOR_VERSION
#if OVR_PRODUCT_VERSION == 0 && OVR_MAJOR_VERSION <= 7
	memset(&g_rift_frame_timing, 0, sizeof(g_rift_frame_timing));
#endif

#if OVR_PRODUCT_VERSION >= 1 || OVR_MAJOR_VERSION >= 7
	ovr_Initialize(nullptr);
	ovrGraphicsLuid luid;
	if (ovr_Create(&hmd, &luid) != ovrSuccess)
		hmd = nullptr;
#ifdef _WIN32
	else
		g_hmd_luid = reinterpret_cast<LUID*>(&luid);
#endif
	if (hmd != nullptr)
#if OVR_PRODUCT_VERSION >= 1
		g_vr_can_async_timewarp = false;
#else
		g_vr_can_async_timewarp = !g_Config.bNoAsyncTimewarp;
#endif
#elif OVR_MAJOR_VERSION >= 6
	ovr_Initialize(nullptr);
	if (ovrHmd_Create(0, &hmd) != ovrSuccess)
		hmd = nullptr;
	if (hmd != nullptr)
		g_vr_can_async_timewarp = !g_Config.bNoAsyncTimewarp;
#else
	ovr_Initialize();
	hmd = ovrHmd_Create(0);
	g_vr_can_async_timewarp = false;
#endif

	if (!hmd)
		WARN_LOG(VR, "Oculus Rift not detected. Oculus Rift support will not be available.");
	return (hmd != nullptr);
#else
	return false;
#endif
}

bool VR_Init920VR()
{
#ifdef _WIN32
	LoadVR920();
	if (g_has_vr920)
	{
		g_has_hmd = true;
		g_hmd_window_width = 800;
		g_hmd_window_height = 600;
		// Todo: find vr920
		g_hmd_window_x = 0;
		g_hmd_window_y = 0;
		g_vr_can_async_timewarp = false;
		g_hmd_refresh_rate = 60; // or 30, depending on how we implement it
		g_vr_must_motion_blur = true;
		g_vr_has_dynamic_predict = false;
		g_vr_has_configure_rendering = false;
		g_vr_has_hq_distortion = false;
		g_vr_should_swap_buffers = true;
		return true;
	}
#endif
	return false;
}

void VR_Init()
{
	NOTICE_LOG(VR, "VR_Init()");
	g_has_hmd = false;
	g_is_direct_mode = false;
	g_hmd_device_name = nullptr;
	g_has_steamvr = false;
	g_vr_can_async_timewarp = false;
	g_vr_asyc_timewarp_active = false;
	g_vr_dont_vsync = false;
#ifdef _WIN32
	g_hmd_luid = nullptr;
#endif

	if (g_prefer_steamvr)
	{
		if (!InitSteamVR() && !InitOculusVR() && !VR_Init920VR() && !InitOculusDebugVR())
			g_has_hmd = g_force_vr;
	}
	else
	{
		if (!InitOculusVR() && !InitSteamVR() && !VR_Init920VR() && !InitOculusDebugVR())
			g_has_hmd = g_force_vr;
	}
	InitOculusHMD();

	if (g_has_hmd)
	{
		NOTICE_LOG(VR, "HMD detected and initialised");
		//SConfig::GetInstance().strFullscreenResolution =
		//	StringFromFormat("%dx%d", g_hmd_window_width, g_hmd_window_height);
		//SConfig::GetInstance().iRenderWindowXPos = g_hmd_window_x;
		//SConfig::GetInstance().iRenderWindowYPos = g_hmd_window_y;
		//SConfig::GetInstance().iRenderWindowWidth = g_hmd_window_width;
		//SConfig::GetInstance().iRenderWindowHeight = g_hmd_window_height;
		//SConfig::GetInstance().m_special_case = true;
	}
	else
	{
		ERROR_LOG(VR, "No HMD detected!");
		//SConfig::GetInstance().m_special_case = false;
	}
}

void VR_StopRendering()
{
#ifdef _WIN32
	if (g_has_vr920)
	{
		VR920_StopStereo3D();
	}
#endif
#ifdef OVR_MAJOR_VERSION
	// Shut down rendering and release resources (by passing NULL)
	if (g_has_rift)
	{
#if OVR_PRODUCT_VERSION >= 1 || OVR_MAJOR_VERSION >= 6
		for (int i = 0; i < ovrEye_Count; ++i)
			g_eye_render_desc[i] = ovrHmd_GetRenderDesc(hmd, (ovrEyeType)i, g_eye_fov[i]);
#else
		ovrHmd_ConfigureRendering(hmd, nullptr, 0, g_eye_fov, g_eye_render_desc);
#endif
	}
#endif
}

void VR_Shutdown()
{
	g_vr_can_async_timewarp = false;
#ifdef HAVE_OPENVR
	if (g_has_steamvr && m_pHMD)
	{
		g_has_steamvr = false;
		m_pHMD = nullptr;
		// crashes if OpenGL
		vr::VR_Shutdown();
	}
#endif
#ifdef OVR_MAJOR_VERSION
	if (hmd)
	{
#if OVR_PRODUCT_VERSION == 0 && OVR_MAJOR_VERSION < 6
		// on my computer, on runtime 0.4.2, the Rift won't switch itself off without this:
		if (g_is_direct_mode)
			ovrHmd_SetEnabledCaps(hmd, ovrHmdCap_DisplayOff);
#endif
		ovrHmd_Destroy(hmd);
		g_has_rift = false;
		g_has_hmd = false;
		g_is_direct_mode = false;
		NOTICE_LOG(VR, "Oculus Rift shut down.");
	}
	ovr_Shutdown();
#endif
}

void VR_RecenterHMD()
{
#ifdef HAVE_OPENVR
	if (g_has_steamvr && m_pHMD)
	{
		m_pHMD->ResetSeatedZeroPose();
	}
#endif
#ifdef OVR_MAJOR_VERSION
	if (g_has_rift)
	{
		ovrHmd_RecenterPose(hmd);
	}
#endif
}

void VR_ConfigureHMDTracking()
{
#if defined(OVR_MAJOR_VERSION) && OVR_PRODUCT_VERSION == 0
	if (g_has_rift)
	{
		int cap = 0;
		if (g_Config.bOrientationTracking)
			cap |= ovrTrackingCap_Orientation;
		if (g_Config.bMagYawCorrection)
			cap |= ovrTrackingCap_MagYawCorrection;
		if (g_Config.bPositionTracking)
			cap |= ovrTrackingCap_Position;
		ovrHmd_ConfigureTracking(hmd, cap, 0);
}
#endif
}

void VR_ConfigureHMDPrediction()
{
#if defined(OVR_MAJOR_VERSION) && OVR_PRODUCT_VERSION == 0
	if (g_has_rift)
	{
#if OVR_MAJOR_VERSION <= 5
		int caps = ovrHmd_GetEnabledCaps(hmd) & ~(ovrHmdCap_DynamicPrediction | ovrHmdCap_LowPersistence | ovrHmdCap_NoMirrorToWindow);
#else
#if OVR_MAJOR_VERSION >= 7
		int caps = ovrHmd_GetEnabledCaps(hmd) & ~(0);
#else
		int caps = ovrHmd_GetEnabledCaps(hmd) & ~(ovrHmdCap_DynamicPrediction | ovrHmdCap_LowPersistence);
#endif
#endif
#if OVR_MAJOR_VERSION <= 6
		if (g_Config.bLowPersistence)
			caps |= ovrHmdCap_LowPersistence;
		if (g_Config.bDynamicPrediction)
			caps |= ovrHmdCap_DynamicPrediction;
#if OVR_MAJOR_VERSION <= 5
		if (g_Config.bNoMirrorToWindow)
			caps |= ovrHmdCap_NoMirrorToWindow;
#endif
#endif
		ovrHmd_SetEnabledCaps(hmd, caps);
	}
#endif
}

void VR_GetEyePoses()
{
#ifdef OVR_MAJOR_VERSION
	if (g_has_rift)
	{
#ifdef OCULUSSDK042
		((ovrPosef*)g_eye_poses)[ovrEye_Left] = ovrHmd_GetEyePose(hmd, ovrEye_Left);
		((ovrPosef*)g_eye_poses)[ovrEye_Right] = ovrHmd_GetEyePose(hmd, ovrEye_Right);
#elif OVR_PRODUCT_VERSION >= 1
		ovrVector3f useHmdToEyeViewOffset[2] = { g_eye_render_desc[0].HmdToEyeOffset, g_eye_render_desc[1].HmdToEyeOffset };
#else
		ovrVector3f useHmdToEyeViewOffset[2] = { g_eye_render_desc[0].HmdToEyeViewOffset, g_eye_render_desc[1].HmdToEyeViewOffset };
#if OVR_MAJOR_VERSION >= 7
#if OVR_MAJOR_VERSION <= 7
		ovr_GetEyePoses(hmd, g_vr_frame_index, useHmdToEyeViewOffset, (ovrPosef *)g_eye_poses, nullptr);
#endif
#else
		ovrHmd_GetEyePoses(hmd, g_vr_frame_index, useHmdToEyeViewOffset, (ovrPosef *)g_eye_poses, nullptr);
#endif
#endif
	}
#endif
#if HAVE_OPENVR
	if (g_has_steamvr)
	{
		if (m_pCompositor)
		{
			m_pCompositor->WaitGetPoses(m_rTrackedDevicePose, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
		}
	}
#endif
}

#ifdef HAVE_OPENVR
//-----------------------------------------------------------------------------
// Purpose: Processes a single VR event
//-----------------------------------------------------------------------------
void ProcessVREvent(const vr::VREvent_t & event)
{
	switch (event.eventType)
	{
	case vr::VREvent_TrackedDeviceActivated:
	{
		//SetupRenderModelForTrackedDevice(event.trackedDeviceIndex);
		NOTICE_LOG(VR, "Device %u attached. Setting up render model.\n", event.trackedDeviceIndex);
		break;
	}
	case vr::VREvent_TrackedDeviceDeactivated:
	{
		NOTICE_LOG(VR, "Device %u detached.\n", event.trackedDeviceIndex);
		break;
	}
	case vr::VREvent_TrackedDeviceUpdated:
	{
		NOTICE_LOG(VR, "Device %u updated.\n", event.trackedDeviceIndex);
		break;
	}
	}
}
#endif

#ifdef OVR_MAJOR_VERSION
void UpdateOculusHeadTracking()
{
	// On Oculus SDK 0.6 and above, we start the next frame the first time we read the head tracking.
	// On SDK 0.5 and below, this is done in BeginFrame instead.
#if OVR_PRODUCT_VERSION >= 1 || OVR_MAJOR_VERSION >= 6
	++g_vr_frame_index;
#endif
	// we can only call GetEyePose between BeginFrame and EndFrame
#ifdef OCULUSSDK042
	g_vr_lock.lock();
	((ovrPosef*)g_eye_poses)[ovrEye_Left] = ovrHmd_GetEyePose(hmd, ovrEye_Left);
	((ovrPosef*)g_eye_poses)[ovrEye_Right] = ovrHmd_GetEyePose(hmd, ovrEye_Right);
	g_vr_lock.unlock();
	OVR::Posef pose = ((ovrPosef*)g_eye_poses)[ovrEye_Left];
#else
#if OVR_PRODUCT_VERSION >= 1
	ovrVector3f useHmdToEyeViewOffset[2] = { g_eye_render_desc[0].HmdToEyeOffset, g_eye_render_desc[1].HmdToEyeOffset };
#else
	ovrVector3f useHmdToEyeViewOffset[2] = { g_eye_render_desc[0].HmdToEyeViewOffset, g_eye_render_desc[1].HmdToEyeViewOffset };
#endif
#if OVR_PRODUCT_VERSION >= 1 || OVR_MAJOR_VERSION >= 8
	double display_time = ovr_GetPredictedDisplayTime(hmd, g_vr_frame_index);
	ovrTrackingState state = ovr_GetTrackingState(hmd, display_time, false);
	ovr_CalcEyePoses(state.HeadPose.ThePose, useHmdToEyeViewOffset, (ovrPosef *)g_eye_poses);
	OVR::Posef pose = state.HeadPose.ThePose;
#elif OVR_MAJOR_VERSION >= 6
	ovrFrameTiming timing = ovrHmd_GetFrameTiming(hmd, g_vr_frame_index);
	ovrTrackingState state = ovrHmd_GetTrackingState(hmd, timing.DisplayMidpointSeconds);
	ovr_CalcEyePoses(state.HeadPose.ThePose, useHmdToEyeViewOffset, (ovrPosef *)g_eye_poses);
	OVR::Posef pose = state.HeadPose.ThePose;
#else
	ovrHmd_GetEyePoses(hmd, g_vr_frame_index, useHmdToEyeViewOffset, (ovrPosef *)g_eye_poses, nullptr);
	OVR::Posef pose = ((ovrPosef *)g_eye_poses)[ovrEye_Left];
#endif
#endif
	//ovrTrackingState ss = ovrHmd_GetTrackingState(hmd, g_rift_frame_timing.ScanoutMidpointSeconds);
	//if (ss.StatusFlags & (ovrStatus_OrientationTracked | ovrStatus_PositionTracked))
	{
		//OVR::Posef pose = ss.HeadPose.ThePose;
		float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
		pose.Rotation.GetEulerAngles<OVR::Axis_Y, OVR::Axis_X, OVR::Axis_Z>(&yaw, &pitch, &roll);

		float x = 0, y = 0, z = 0;
		roll = -RADIANS_TO_DEGREES(roll);  // ???
		pitch = -RADIANS_TO_DEGREES(pitch); // should be degrees down
		yaw = -RADIANS_TO_DEGREES(yaw);   // should be degrees right
		x = pose.Translation.x;
		y = pose.Translation.y;
		z = pose.Translation.z;
		g_head_tracking_position[0] = -x;
		g_head_tracking_position[1] = -y;
#if OVR_PRODUCT_VERSION == 0 && OVR_MAJOR_VERSION <= 4
		g_head_tracking_position[2] = 0.06f - z;
#else
		g_head_tracking_position[2] = -z;
#endif
		Matrix33 m, yp, ya, p, r;
		Matrix33::RotateY(ya, DEGREES_TO_RADIANS(yaw));
		Matrix33::RotateX(p, DEGREES_TO_RADIANS(pitch));
		Matrix33::Multiply(p, ya, yp);
		Matrix33::RotateZ(r, DEGREES_TO_RADIANS(roll));
		Matrix33::Multiply(r, yp, m);
		Matrix44::LoadMatrix33(g_head_tracking_matrix, m);
	}
}
#endif

#ifdef HAVE_OPENVR
void UpdateSteamVRHeadTracking()
{
	// Process SteamVR events
	vr::VREvent_t event;
	while (m_pHMD->PollNextEvent(&event))
	{
		ProcessVREvent(event);
	}

	// Process SteamVR controller state
	for (vr::TrackedDeviceIndex_t unDevice = 0; unDevice < vr::k_unMaxTrackedDeviceCount; unDevice++)
	{
		vr::VRControllerState_t state;
		if (m_pHMD->GetControllerState(unDevice, &state))
		{
			m_rbShowTrackedDevice[unDevice] = state.ulButtonPressed == 0;
		}
	}
	float fSecondsUntilPhotons = 0.0f;
	m_pHMD->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseSeated, fSecondsUntilPhotons, m_rTrackedDevicePose, vr::k_unMaxTrackedDeviceCount);
	m_iValidPoseCount = 0;
	//for ( int nDevice = 0; nDevice < vr::k_unMaxTrackedDeviceCount; ++nDevice )
	//{
	//	if ( m_rTrackedDevicePose[nDevice].bPoseIsValid )
	//	{
	//		m_iValidPoseCount++;
	//		//m_rTrackedDevicePose[nDevice].mDeviceToAbsoluteTracking;
	//	}
	//}

	if (m_rTrackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)
	{
		float x = m_rTrackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking.m[0][3];
		float y = m_rTrackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking.m[1][3];
		float z = m_rTrackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking.m[2][3];
		g_head_tracking_position[0] = -x;
		g_head_tracking_position[1] = -y;
		g_head_tracking_position[2] = -z;
		Matrix33 m;
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++)
				m.data[r * 3 + c] = m_rTrackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking.m[c][r];
		Matrix44::LoadMatrix33(g_head_tracking_matrix, m);
	}
}
#endif

#ifdef _WIN32
void UpdateVuzixHeadTracking()
{
	LONG ya = 0, p = 0, r = 0;
	if (Vuzix_GetTracking(&ya, &p, &r) == ERROR_SUCCESS)
	{
		float yaw = -ya * 180.0f / 32767.0f;
		float pitch = p * -180.0f / 32767.0f;
		float roll = r * 180.0f / 32767.0f;
		// todo: use head and neck model
		float x = 0;
		float y = 0;
		float z = 0;
		Matrix33 m, yp, ya, p, r;
		Matrix33::RotateY(ya, DEGREES_TO_RADIANS(yaw));
		Matrix33::RotateX(p, DEGREES_TO_RADIANS(pitch));
		Matrix33::Multiply(p, ya, yp);
		Matrix33::RotateZ(r, DEGREES_TO_RADIANS(roll));
		Matrix33::Multiply(r, yp, m);
		Matrix44::LoadMatrix33(g_head_tracking_matrix, m);
		g_head_tracking_position[0] = -x;
		g_head_tracking_position[1] = -y;
		g_head_tracking_position[2] = -z;
	}
}
#endif

bool VR_UpdateHeadTrackingIfNeeded()
{
	if (g_new_tracking_frame) {
		g_new_tracking_frame = false;
#ifdef _WIN32
		if (g_has_vr920 && Vuzix_GetTracking)
			UpdateVuzixHeadTracking();
#endif
#ifdef HAVE_OPENVR
		if (g_has_steamvr)
			UpdateSteamVRHeadTracking();
#endif
#ifdef OVR_MAJOR_VERSION
		if (g_has_rift)
			UpdateOculusHeadTracking();
#endif
		return true;
	} else {
		return false;
	}
}

void VR_GetProjectionHalfTan(float &hmd_halftan)
{
#ifdef OVR_MAJOR_VERSION
	if (g_has_rift)
	{
		hmd_halftan = fabs(g_eye_fov[0].LeftTan);
		if (fabs(g_eye_fov[0].RightTan) > hmd_halftan)
			hmd_halftan = fabs(g_eye_fov[0].RightTan);
		if (fabs(g_eye_fov[0].UpTan) > hmd_halftan)
			hmd_halftan = fabs(g_eye_fov[0].UpTan);
		if (fabs(g_eye_fov[0].DownTan) > hmd_halftan)
			hmd_halftan = fabs(g_eye_fov[0].DownTan);
	}
	else
#endif
		if (g_has_steamvr)
		{
			// rough approximation, can't be bothered to work this out properly
			hmd_halftan = tan(DEGREES_TO_RADIANS(100.0f / 2));
		}
		else
		{
			hmd_halftan = tan(DEGREES_TO_RADIANS(32.0f / 2))*3.0f / 4.0f;
		}
}

void VR_GetProjectionMatrices(Matrix4x4 &left_eye, Matrix4x4 &right_eye, float znear, float zfar, bool isOpenGL)
{
#ifdef OVR_MAJOR_VERSION
	if (g_has_rift)
	{
#if OVR_PRODUCT_VERSION >= 1 || OVR_MAJOR_VERSION >= 5
		unsigned flags = ovrProjection_None;
#if OVR_PRODUCT_VERSION == 0		
		flags |= ovrProjection_RightHanded; // is this right for Dolphin VR?
#endif
		if (isOpenGL)
			flags |= ovrProjection_ClipRangeOpenGL;
		if (isinf(zfar))
			flags |= ovrProjection_FarClipAtInfinity;
#else
		bool flags = true; // right handed
#endif
		//INFO_LOG(VR, "GetProjectionMatrices(%g, %g, %d)", znear, zfar, flags);
		ovrMatrix4f rift_left = ovrMatrix4f_Projection(g_eye_fov[0], znear, zfar, flags);
		ovrMatrix4f rift_right = ovrMatrix4f_Projection(g_eye_fov[1], znear, zfar, flags);
		Matrix44::Set(left_eye, rift_left.M[0]);
		Matrix44::Set(right_eye, rift_right.M[0]);
		// Oculus don't give us the correct z values for infinite zfar
		if (isinf(zfar))
		{
			left_eye.zz = -1.0f;
			left_eye.zw = -2.0f * znear;
			right_eye.zz = left_eye.zz;
			right_eye.zw = left_eye.zw;
		}
	}
	else
#endif
#ifdef HAVE_OPENVR
		if (g_has_steamvr)
		{
			vr::GraphicsAPIConvention flags = isOpenGL ? vr::API_OpenGL : vr::API_DirectX;
			vr::HmdMatrix44_t mat = m_pHMD->GetProjectionMatrix(vr::Eye_Left, znear, zfar, flags);
			for (int r = 0; r < 4; ++r)
				for (int c = 0; c < 4; ++c)
					left_eye.data[r * 4 + c] = mat.m[r][c];
			mat = m_pHMD->GetProjectionMatrix(vr::Eye_Right, znear, zfar, vr::API_DirectX);
			for (int r = 0; r < 4; ++r)
				for (int c = 0; c < 4; ++c)
					right_eye.data[r * 4 + c] = mat.m[r][c];
		}
		else
#endif
		{
			Matrix44::LoadIdentity(left_eye);
			left_eye.data[10] = -znear / (zfar - znear);
			left_eye.data[11] = -zfar*znear / (zfar - znear);
			left_eye.data[14] = -1.0f;
			left_eye.data[15] = 0.0f;
			// 32 degrees HFOV, 4:3 aspect ratio
			left_eye.data[0 * 4 + 0] = 1.0f / tan(32.0f / 2.0f * 3.1415926535f / 180.0f);
			left_eye.data[1 * 4 + 1] = 4.0f / 3.0f * left_eye.data[0 * 4 + 0];
			Matrix44::Set(right_eye, left_eye.data);
		}
}

void VR_GetEyePos(float *posLeft, float *posRight)
{
#ifdef OVR_MAJOR_VERSION
	if (g_has_rift)
	{
#ifdef OCULUSSDK042
		posLeft[0] = g_eye_render_desc[0].ViewAdjust.x;
		posLeft[1] = g_eye_render_desc[0].ViewAdjust.y;
		posLeft[2] = g_eye_render_desc[0].ViewAdjust.z;
		posRight[0] = g_eye_render_desc[1].ViewAdjust.x;
		posRight[1] = g_eye_render_desc[1].ViewAdjust.y;
		posRight[2] = g_eye_render_desc[1].ViewAdjust.z;
#elif OVR_PRODUCT_VERSION >= 1
		posLeft[0] = g_eye_render_desc[0].HmdToEyeOffset.x;
		posLeft[1] = g_eye_render_desc[0].HmdToEyeOffset.y;
		posLeft[2] = g_eye_render_desc[0].HmdToEyeOffset.z;
		posRight[0] = g_eye_render_desc[1].HmdToEyeOffset.x;
		posRight[1] = g_eye_render_desc[1].HmdToEyeOffset.y;
		posRight[2] = g_eye_render_desc[1].HmdToEyeOffset.z;
#else
		posLeft[0] = g_eye_render_desc[0].HmdToEyeViewOffset.x;
		posLeft[1] = g_eye_render_desc[0].HmdToEyeViewOffset.y;
		posLeft[2] = g_eye_render_desc[0].HmdToEyeViewOffset.z;
		posRight[0] = g_eye_render_desc[1].HmdToEyeViewOffset.x;
		posRight[1] = g_eye_render_desc[1].HmdToEyeViewOffset.y;
		posRight[2] = g_eye_render_desc[1].HmdToEyeViewOffset.z;
#endif
#if OVR_PRODUCT_VERSION >= 1 || OVR_MAJOR_VERSION >= 6
		for (int i = 0; i<3; ++i)
		{
			posLeft[i] = -posLeft[i];
			posRight[i] = -posRight[i];
		}
#endif
	}
	else
#endif
#ifdef HAVE_OPENVR
		if (g_has_steamvr)
		{
			// assume 62mm IPD
			posLeft[0] = 0.031f;
			posRight[0] = -0.031f;
			posLeft[1] = posRight[1] = 0;
			posLeft[2] = posRight[2] = 0;
		}
		else
#endif
		{
			// assume 62mm IPD
			posLeft[0] = 0.031f;
			posRight[0] = -0.031f;
			posLeft[1] = posRight[1] = 0;
			posLeft[2] = posRight[2] = 0;
		}
}

void VR_GetFovTextureSize(int *width, int *height)
{
#if defined(OVR_MAJOR_VERSION)
	if (g_has_rift)
	{
		ovrSizei size = ovrHmd_GetFovTextureSize(hmd, ovrEye_Left, g_eye_fov[0], 1.0f);
		*width = size.w;
		*height = size.h;
	}
#endif
}

void VR_SetGame(std::string id)
{
}

void ScaleView(float scale)
{
	// keep the camera in the same virtual world location when scaling the virtual world
	for (int i = 0; i < 3; i++)
		s_fViewTranslationVector[i] *= scale;

	if (s_fViewTranslationVector[0] || s_fViewTranslationVector[1] || s_fViewTranslationVector[2])
		bFreeLookChanged = true;
	else
		bFreeLookChanged = false;

	bProjectionChanged = true;
}

// Moves the freelook camera a number of scaled metres relative to the current freelook camera direction
void TranslateView(float left_metres, float forward_metres, float down_metres)
{
	float result[3];
	float vector[3] = { left_metres, down_metres, forward_metres };

	// use scaled metres in VR, or real metres otherwise
	if (g_has_hmd && g_Config.bEnableVR && g_Config.bScaleFreeLook)
		for (int i = 0; i < 3; ++i)
			vector[i] *= g_Config.fScale;

	Matrix33::Multiply(s_viewInvRotationMatrix, vector, result);

	for (int i = 0; i < 3; i++)
	{
		s_fViewTranslationVector[i] += result[i];
		vr_freelook_speed += result[i];
	}

	if (s_fViewTranslationVector[0] || s_fViewTranslationVector[1] || s_fViewTranslationVector[2])
		bFreeLookChanged = true;
	else
		bFreeLookChanged = false;

	bProjectionChanged = true;
}

void RotateView(float x, float y)
{
	s_fViewRotation[0] += x;
	s_fViewRotation[1] += y;

	Matrix33 mx;
	Matrix33 my;
	Matrix33::RotateX(mx, s_fViewRotation[1]);
	Matrix33::RotateY(my, s_fViewRotation[0]);
	Matrix33::Multiply(mx, my, s_viewRotationMatrix);

	// reverse rotation
	Matrix33::RotateX(mx, -s_fViewRotation[1]);
	Matrix33::RotateY(my, -s_fViewRotation[0]);
	Matrix33::Multiply(my, mx, s_viewInvRotationMatrix);

	if (s_fViewRotation[0] || s_fViewRotation[1])
		bFreeLookChanged = true;
	else
		bFreeLookChanged = false;

	bProjectionChanged = true;
}

void ResetView()
{
	memset(s_fViewTranslationVector, 0, sizeof(s_fViewTranslationVector));
	Matrix33::LoadIdentity(s_viewRotationMatrix);
	Matrix33::LoadIdentity(s_viewInvRotationMatrix);
	s_fViewRotation[0] = s_fViewRotation[1] = 0.0f;

	bFreeLookChanged = false;
	bProjectionChanged = true;
}
