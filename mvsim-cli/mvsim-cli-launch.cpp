/*+-------------------------------------------------------------------------+
  |                       MultiVehicle simulator (libmvsim)                 |
  |                                                                         |
  | Copyright (C) 2014-2022  Jose Luis Blanco Claraco                       |
  | Copyright (C) 2017  Borys Tymchenko (Odessa Polytechnic University)     |
  | Distributed under 3-clause BSD License                                  |
  |   See COPYING                                                           |
  +-------------------------------------------------------------------------+ */

#include <mrpt/core/exceptions.h>
#include <mvsim/World.h>

#include <rapidxml_utils.hpp>
#include <thread>

#include "mvsim-cli.h"

struct TThreadParams
{
	mvsim::World* world = nullptr;
	std::mutex closingMtx;

	TThreadParams() = default;

	bool isClosing()
	{
		closingMtx.lock();
		bool ret = closing_;
		closingMtx.unlock();
		return ret;
	}
	void closing(bool v)
	{
		closingMtx.lock();
		closing_ = v;
		closingMtx.unlock();
	}

   private:
	bool closing_ = false;
};

static void mvsim_server_thread_update_GUI(TThreadParams& thread_params);
mvsim::World::TGUIKeyEvent gui_key_events;
std::mutex gui_key_events_mtx;
std::string msg2gui;

int launchSimulation()
{
	using namespace mvsim;

	// check args:
	bool badArgs = false;
	const auto& unlabeledArgs = cli->argCmd.getValue();
	if (unlabeledArgs.size() != 2) badArgs = true;

	if (cli->argHelp.isSet() || badArgs)
	{
		fprintf(
			stdout,
			R"XXX(Usage: mvsim launch <WORLD_MODEL.xml>

Available options:
  -v, --verbosity      Set verbosity level: DEBUG, INFO (default), WARN, ERROR
  --full-profiler      Enable full profiling (generates file with all timings)
)XXX");
		return 0;
	}

	const auto sXMLfilename = unlabeledArgs.at(1);

	// Start network server:
	commonLaunchServer();

	mvsim::World world;

	world.setMinLoggingLevel(
		mrpt::typemeta::TEnumType<mrpt::system::VerbosityLevel>::name2value(
			cli->argVerbosity.getValue()));

	if (cli->argFullProfiler.isSet())
		world.getTimeLogger().enableKeepWholeHistory();

	// Load from XML:
	rapidxml::file<> fil_xml(sXMLfilename.c_str());
	world.load_from_XML(fil_xml.data(), sXMLfilename.c_str());

	// Attach world as a mvsim communications node:
	world.connectToServer();

	// Launch GUI thread:
	TThreadParams thread_params;
	thread_params.world = &world;
	std::thread thGUI =
		std::thread(&mvsim_server_thread_update_GUI, std::ref(thread_params));

	// Run simulation:
	const double tAbsInit = mrpt::Clock::nowDouble();
	// double REALTIME_FACTOR = 1.0;
	bool doExit = false;
	size_t teleopIdxVeh = 0;  // Index of the vehicle to teleop

	while (!doExit && !mrpt::system::os::kbhit())
	{
		// was the quit button hit in the GUI?
		if (world.gui_thread_must_close()) break;

		// Simulation
		// ============================================================
		// Compute how much time has passed to simulate in real-time:
		double tNew = mrpt::Clock::nowDouble();
		double incrTime = (tNew - tAbsInit) - world.get_simul_time();
		int incrTimeSteps =
			static_cast<int>(std::floor(incrTime / world.get_simul_timestep()));

		// Simulate:
		if (incrTimeSteps > 0)
		{
			world.run_simulation(incrTimeSteps * world.get_simul_timestep());
		}

		// I could use 10ms here but chono literals are since gcc 4.9.3
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		// GUI msgs, teleop, etc.
		// ====================================================

		std::string txt2gui_tmp;
		gui_key_events_mtx.lock();
		World::TGUIKeyEvent keyevent = gui_key_events;
		gui_key_events_mtx.unlock();

		// Global keys:
		switch (keyevent.keycode)
		{
			case GLFW_KEY_ESCAPE:
				doExit = true;
				break;
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
				teleopIdxVeh = keyevent.keycode - '1';
				break;
		};

		{
			const World::VehicleList& vehs = world.getListOfVehicles();
			txt2gui_tmp += mrpt::format(
				"Selected vehicle: %u/%u\n",
				static_cast<unsigned>(teleopIdxVeh + 1),
				static_cast<unsigned>(vehs.size()));
			if (vehs.size() > teleopIdxVeh)
			{
				// Get iterator to selected vehicle:
				World::VehicleList::const_iterator it_veh = vehs.begin();
				std::advance(it_veh, teleopIdxVeh);

				// Get speed: ground truth
				{
					const mrpt::math::TTwist2D& vel =
						it_veh->second->getVelocityLocal();
					txt2gui_tmp += mrpt::format(
						"gt. vel: lx=%7.03f, ly=%7.03f, w= %7.03fdeg/s\n",
						vel.vx, vel.vy, mrpt::RAD2DEG(vel.omega));
				}
				// Get speed: ground truth
				{
					const mrpt::math::TTwist2D& vel =
						it_veh->second->getVelocityLocalOdoEstimate();
					txt2gui_tmp += mrpt::format(
						"odo vel: lx=%7.03f, ly=%7.03f, w= %7.03fdeg/s\n",
						vel.vx, vel.vy, mrpt::RAD2DEG(vel.omega));
				}

				// Generic teleoperation interface for any controller that
				// supports it:
				{
					ControllerBaseInterface* controller =
						it_veh->second->getControllerInterface();
					ControllerBaseInterface::TeleopInput teleop_in;
					ControllerBaseInterface::TeleopOutput teleop_out;
					teleop_in.keycode = keyevent.keycode;
					controller->teleop_interface(teleop_in, teleop_out);
					txt2gui_tmp += teleop_out.append_gui_lines;
				}
			}
		}

		// Clear the keystroke buffer
		gui_key_events_mtx.lock();
		if (keyevent.keycode != 0) gui_key_events = World::TGUIKeyEvent();
		gui_key_events_mtx.unlock();

		msg2gui = txt2gui_tmp;	// send txt msgs to show in the GUI

	}  // end while()

	thread_params.closing(true);

	if (thGUI.joinable()) thGUI.join();

	// save full profiling, if enabled:
	if (world.getTimeLogger().isEnabledKeepWholeHistory())
	{
		const std::string sFil = "mvsim_profiler.m";
		std::cout << "\n***SAVING PROFILER DATA TO***: " << sFil << std::endl;
		world.getTimeLogger().saveToMFile(sFil);
	}

	return 0;
}

void mvsim_server_thread_update_GUI(TThreadParams& thread_params)
{
	while (!thread_params.isClosing())
	{
		mvsim::World::TUpdateGUIParams guiparams;
		guiparams.msg_lines = msg2gui;

		thread_params.world->update_GUI(&guiparams);

		// Send key-strokes to the main thread:
		if (guiparams.keyevent.keycode != 0)
		{
			gui_key_events_mtx.lock();
			gui_key_events = guiparams.keyevent;
			gui_key_events_mtx.unlock();
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	}
}