/*+-------------------------------------------------------------------------+
  |                       MultiVehicle simulator (libmvsim)                 |
  |                                                                         |
  | Copyright (C) 2014-2023  Jose Luis Blanco Claraco                       |
  | Copyright (C) 2017  Borys Tymchenko (Odessa Polytechnic University)     |
  | Distributed under 3-clause BSD License                                  |
  |   See COPYING                                                           |
  +-------------------------------------------------------------------------+ */

#pragma once

#include <mrpt/obs/CObservationPointCloud.h>
#include <mrpt/obs/CObservationRotatingScan.h>
#include <mrpt/opengl/CFBORender.h>
#include <mrpt/opengl/CPointCloudColoured.h>
#include <mrpt/poses/CPose2D.h>
#include <mvsim/Sensors/SensorBase.h>

#include <mutex>

namespace mvsim
{
/**
 * @brief A 3D LiDAR sensor, with 360 degrees horizontal fielf-of-view, and a
 * configurable vertical FOV.
 * The number of rays in the vertical FOV and the number of samples in each
 * horizontal row are configurable.
 */
class Lidar3D : public SensorBase
{
	DECLARES_REGISTER_SENSOR(Lidar3D)
   public:
	Lidar3D(Simulable& parent, const rapidxml::xml_node<char>* root);
	virtual ~Lidar3D();

	// See docs in base class
	virtual void loadConfigFrom(const rapidxml::xml_node<char>* root) override;

	virtual void simul_pre_timestep(const TSimulContext& context) override;
	virtual void simul_post_timestep(const TSimulContext& context) override;

	void simulateOn3DScene(mrpt::opengl::COpenGLScene& gl_scene) override;
	void freeOpenGLResources() override;

   protected:
	virtual void internalGuiUpdate(
		const mrpt::optional_ref<mrpt::opengl::COpenGLScene>& viz,
		const mrpt::optional_ref<mrpt::opengl::COpenGLScene>& physical,
		bool childrenOnly) override;

	mrpt::poses::CPose3D sensorPoseOnVeh_;

	double rangeStdNoise_ = 0.01;
	bool ignore_parent_body_ = false;

	float viz_pointSize_ = 3.0f;
	float minRange_ = 0.01f;
	float maxRange_ = 80.0f;
	double vertical_fov_ = mrpt::DEG2RAD(30.0);
	int vertNumRays_ = 16, horzNumRays_ = 180;
	int fbo_nrows_ = vertNumRays_ * 20;

	/** Last simulated scan */
	mrpt::obs::CObservationPointCloud::Ptr last_scan2gui_, last_scan_;
	std::mutex last_scan_cs_;

	/** Whether gl_scan_ has to be updated upon next call of
	 * internalGuiUpdate() from last_scan2gui_ */
	bool gui_uptodate_ = false;

	mrpt::opengl::CPointCloudColoured::Ptr glPoints_;
	mrpt::opengl::CSetOfObjects::Ptr gl_sensor_origin_,
		gl_sensor_origin_corner_;
	mrpt::opengl::CSetOfObjects::Ptr gl_sensor_fov_;

	std::optional<TSimulContext> has_to_render_;
	std::mutex has_to_render_mtx_;

	std::shared_ptr<mrpt::opengl::CFBORender> fbo_renderer_depth_;

	struct PerRayLUT
	{
		int u = 0, v = 0;  //!< Pixel coords
		float depth2range = 0;
	};
	struct PerHorzAngleLUT
	{
		std::vector<PerRayLUT> column;
	};

	std::vector<PerHorzAngleLUT> lut_;
};
}  // namespace mvsim
