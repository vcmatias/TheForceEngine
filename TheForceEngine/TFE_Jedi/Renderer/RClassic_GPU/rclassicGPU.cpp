#include <TFE_System/system.h>
#include <cstring>

#include <TFE_System/math.h>
#include <TFE_Jedi/Level/level.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Settings/settings.h>
#include <TFE_Game/igame.h>
#include "../redgePair.h"
#include "../rcommon.h"

// TODO: FIx
#include "../RClassic_Float/rclassicFloatSharedState.h"

namespace TFE_Jedi
{
	Mat3  s_cameraMtx;
	Mat4  s_cameraProj;
	Vec3f s_cameraPos;
	Vec3f s_cameraDir;
	Vec3f s_cameraDirXZ;
	Vec3f s_cameraRight;
	
#ifdef __ANDROID__
}
#include <tfe_xr.h>
#include <android/log.h>
namespace TFE_Jedi
{
#endif
namespace RClassic_GPU
{
#ifdef __ANDROID__
	static f32 s_vrBodyYaw = 0.0f;
	static Vec3f s_vrBodyPos = { 0 };

	// TFE view space is x right, y down, z back (world y points down); OpenXR is y up.
	void vrSetCamera(s32 eye, bool traverse)
	{
		f32 q[4], e[3], fov[4];
		if (traverse && eye >= 0)
		{
			// Portal traversal for one eye: the head orientation and wide field of view, from the
			// eye position, so occlusion splits match what that eye sees.
			f32 q2[4], fov2[4];
			tfe_xr_get_view(-1, q, e, fov);
			tfe_xr_get_view(eye, q2, e, fov2);
		}
		else { tfe_xr_get_view(eye, q, e, fov); }

		f32 sinYaw, cosYaw;
		sinCosFlt(-s_vrBodyYaw, &sinYaw, &cosYaw);
		const f32 B[3][3] = { { cosYaw, 0.0f, sinYaw }, { 0.0f, 1.0f, 0.0f }, { sinYaw, 0.0f, -cosYaw } };

		// Head rotation (columns = rotated axes) converted to TFE view axes: Q' = D Q D, D = diag(1,-1,1).
		const f32 x = q[0], y = q[1], z = q[2], w = q[3];
		f32 Q[3][3] =
		{
			{ 1 - 2*(y*y + z*z), 2*(x*y - z*w),     2*(x*z + y*w) },
			{ 2*(x*y + z*w),     1 - 2*(x*x + z*z), 2*(y*z - x*w) },
			{ 2*(x*z - y*w),     2*(y*z + x*w),     1 - 2*(x*x + y*y) },
		};
		const f32 D[3] = { 1.0f, -1.0f, 1.0f };
		for (s32 r = 0; r < 3; r++) for (s32 c = 0; c < 3; c++) { Q[r][c] *= D[r] * D[c]; }

		// M = Q'^T * B
		f32 M[3][3];
		for (s32 r = 0; r < 3; r++) for (s32 c = 0; c < 3; c++)
		{
			M[r][c] = Q[0][r]*B[0][c] + Q[1][r]*B[1][c] + Q[2][r]*B[2][c];
		}
		s_cameraMtx.m0 = { M[0][0], M[0][1], M[0][2] };
		s_cameraMtx.m1 = { M[1][0], M[1][1], M[1][2] };
		s_cameraMtx.m2 = { M[2][0], M[2][1], M[2][2] };
		s_cameraDir   = { -s_cameraMtx.m2.x, -s_cameraMtx.m2.y, -s_cameraMtx.m2.z };
		s_cameraRight = {  s_cameraMtx.m0.x,  s_cameraMtx.m0.y,  s_cameraMtx.m0.z };

		// Position: body + B^T * (D * e * unitsPerMeter)
		const f32 u = tfe_xr_units_per_meter();
		const f32 ev[3] = { e[0] * u, -e[1] * u, e[2] * u };
		s_cameraPos.x = s_vrBodyPos.x + B[0][0]*ev[0] + B[1][0]*ev[1] + B[2][0]*ev[2];
		s_cameraPos.y = s_vrBodyPos.y + B[0][1]*ev[0] + B[1][1]*ev[1] + B[2][1]*ev[2];
		s_cameraPos.z = s_vrBodyPos.z + B[0][2]*ev[0] + B[1][2]*ev[1] + B[2][2]*ev[2];

		// Projection. Eyes render straight into the OpenXR swapchain (GL orientation),
		// so the y row is negated compared to TFE's own (flipped) render targets.
		const f32 zNear = 0.01f, zFar = 4096.0f;
		const f32 l = fov[0], r = fov[1], t = fov[2], b = fov[3];
		Mat4 p = { 0 };
		p.m0 = { 2.0f / (r - l), 0.0f, (r + l) / (r - l), 0.0f };
		const f32 ys = (eye < 0 || traverse) ? 1.0f : -1.0f;
		p.m1 = { 0.0f, ys * 2.0f / (t - b), (t + b) / (t - b), 0.0f };
		p.m2 = { 0.0f, 0.0f, zFar / (zNear - zFar), zNear * zFar / (zNear - zFar) };
		p.m3 = { 0.0f, 0.0f, -1.0f, 0.0f };
		s_cameraProj = p;
	}
#endif
	void resetState()
	{
	}

	void setupInitCameraAndLights(s32 width, s32 height)
	{
	}

	void changeResolution(s32 width, s32 height)
	{
		s_width = width;
		s_height = height;
		s_rcfltState.halfWidth      = f32(width >> 1);
		s_rcfltState.focalLength    = s_rcfltState.halfWidth;
		s_rcfltState.focalLenAspect = s_rcfltState.halfWidth;
		s_rcfltState.aspectScaleY   = 1.0f;

		if (TFE_RenderBackend::getWidescreen())
		{
			// 200p and 400p get special handling because they are 16:10 resolutions in 4:3.
			if (s_height == 200 || s_height == 400)
			{
				s_rcfltState.focalLenAspect = (s_height == 200) ? 160.0f : 320.0f;
			}
			else
			{
				s_rcfltState.focalLenAspect = (s_height * 4 / 3) * 0.5f;
			}

			// The (4/3) or (16/10) factor removes the 4:3 or 16:10 aspect ratio already factored in 's_halfWidth' 
			// The (height/width) factor adjusts for the resolution pixel aspect ratio.
			const f32 scaleFactor = (s_height == 200 || s_height == 400) ? (16.0f / 10.0f) : (4.0f / 3.0f);
			s_rcfltState.focalLength = s_rcfltState.halfWidth * scaleFactor * f32(s_height) / f32(s_width);
		}
		if (s_height != 200 && s_height != 400)
		{
			// Scale factor to account for converting from rectangular pixels to square pixels when computing flat texture coordinates.
			// Factor = (16/10) / (4/3)
			s_rcfltState.aspectScaleY = 1.2f;
		}
		s_rcfltState.focalLenAspect *= s_rcfltState.aspectScaleY;

		// Allow for FOV changes, assumes the base horizontal FOV when using 4:3 is 90 degrees.
		// FOV scale = tan(FOV/2)
		const s32 fov = TFE_Settings::getGraphicsSettings()->fov;
		if (fov != 90 && fov > 0 && fov < 180)
		{
			const f32 fovScale = 1.0f / tanf(f32(fov) * 0.5f * PI / 180.0f);
			s_rcfltState.focalLength *= fovScale;
			s_rcfltState.focalLenAspect *= fovScale;
		}

		s_cameraProj = TFE_Math::computeProjMatrixExplicit(2.0f*s_rcfltState.focalLength / f32(s_width),
			2.0f*s_rcfltState.focalLenAspect / f32(s_height), 0.01f, 4096.0f);
	}

	void computeCameraTransform(RSector* sector, f32 pitch, f32 yaw, f32 camX, f32 camY, f32 camZ)
	{
		s_cameraPos = { camX, camY, camZ };
		s_cameraProj = TFE_Math::computeProjMatrixExplicit(2.0f*s_rcfltState.focalLength / f32(s_width),
			2.0f*s_rcfltState.focalLenAspect / f32(s_height), 0.01f, 4096.0f);

		f32 sinYaw, cosYaw, sinPitch, cosPitch;
		sinCosFlt(-yaw, &sinYaw, &cosYaw);
		sinCosFlt(-pitch, &sinPitch, &cosPitch);

		s_cameraMtx.m0 = { cosYaw, 0.0f, sinYaw };
		s_cameraMtx.m1 = { sinYaw * sinPitch,  cosPitch, -cosYaw * sinPitch };
		s_cameraMtx.m2 = { sinYaw * cosPitch, -sinPitch, -cosYaw * cosPitch };
		s_cameraDir    = { -s_cameraMtx.m2.x, -s_cameraMtx.m2.y, -s_cameraMtx.m2.z };
		s_cameraRight  = {  s_cameraMtx.m0.x,  s_cameraMtx.m0.y,  s_cameraMtx.m0.z };
#ifdef __ANDROID__
		s_vrBodyYaw = yaw;
		s_vrBodyPos = s_cameraPos;
		{
			static s32 s_posLog = 0;
			if ((++s_posLog % 72) == 0)
			{
				__android_log_print(ANDROID_LOG_INFO, "TFEQuestPos", "body yaw=%.0f pos=%.2f %.2f %.2f", yaw, camX, camY, camZ);
				TFE_System::logWrite(LOG_MSG, "VRCam", "body yaw=%.0f eye=%.2f %.2f %.2f", yaw, camX, camY, camZ);
			}
		}
		if (tfe_xr_world_ready())
		{
			// Portal traversal uses the head with a field of view covering both eyes.
			vrSetCamera(-1, false);
		}
#endif
	}

	void transformPointByCamera(vec3_float* worldPoint, vec3_float* viewPoint)
	{
	}

	void computeSkyOffsets()
	{
	}
}  // RClassic_GPU

}  // TFE_Jedi