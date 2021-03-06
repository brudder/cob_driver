/****************************************************************
 *
 * Copyright (c) 2010
 *
 * Fraunhofer Institute for Manufacturing Engineering	
 * and Automation (IPA)
 *
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *
 * Project name: care-o-bot
 * ROS stack name: cob_driver
 * ROS package name: cob_base_drive_chain
 * Description: This node provides control of the care-o-bot platform drives to the ROS-"network". For this purpose it offers several services and publishes data on different topics.
 *								
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *			
 * Author: Christian Connette, email:christian.connette@ipa.fhg.de
 * Supervised by: Christian Connette, email:christian.connette@ipa.fhg.de
 *
 * Date of creation: Feb 2010:
 * ToDo: Doesn't this node has to take care about the Watchdogs?
 *
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *	 * Redistributions of source code must retain the above copyright
 *	   notice, this list of conditions and the following disclaimer.
 *	 * Redistributions in binary form must reproduce the above copyright
 *	   notice, this list of conditions and the following disclaimer in the
 *	   documentation and/or other materials provided with the distribution.
 *	 * Neither the name of the Fraunhofer Institute for Manufacturing 
 *	   Engineering and Automation (IPA) nor the names of its
 *	   contributors may be used to endorse or promote products derived from
 *	   this software without specific prior written permission.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License LGPL as 
 * published by the Free Software Foundation, either version 3 of the 
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License LGPL for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public 
 * License LGPL along with this program. 
 * If not, see <http://www.gnu.org/licenses/>.
 *
 ****************************************************************/

//##################
//#### includes ####

// standard includes
//--

// ROS includes
#include <ros/ros.h>

// ROS message includes
#include <sensor_msgs/JointState.h>
#include <diagnostic_msgs/DiagnosticStatus.h>

// ROS service includes
#include <cob_srvs/Trigger.h>
#include <cob_srvs/GetJointState.h>
#include <cob_srvs/ElmoRecorderReadout.h>
#include <cob_srvs/ElmoRecorderConfig.h>


// external includes
#include <cob_base_drive_chain/CanCtrlPltfCOb3.h>
#include <cob_utilities/IniFile.h>
#include <cob_utilities/MathSup.h>

//####################
//#### node class ####
/**
* This node provides control of the care-o-bot platform drives to the ROS-"network". For this purpose it offers several services and publishes data on different topics.
*/
class NodeClass
{
	public:
		// create a handle for this node, initialize node
		ros::NodeHandle n;

		// topics to publish
		/**
		* On this topic "JointState" of type sensor_msgs::JointState the node publishes joint states when they are requested over the appropriate service srvServer_GetJointState.
		*/
		ros::Publisher topicPub_JointState;

		/**
		* On this topic "Diagnostic" of type diagnostic_msgs::DiagnosticStatus the node publishes states and error information about the platform.
		*/
		ros::Publisher topicPub_Diagnostic;


		// topics to subscribe, callback is called for new messages arriving
		/**
		* The node subscribes to the topic "JointStateCmd" and performs the requested motor commands
		*/
		ros::Subscriber topicSub_JointStateCmd;

		// service servers
		/**
		* Service requests cob_srvs::Trigger and initializes platform and motors
		*/
		ros::ServiceServer srvServer_Init;

		/**
		* Service requests cob_srvs::Trigger and resets platform and motors
		*/
		ros::ServiceServer srvServer_Recover;

		/**
		* Service requests cob_srvs::Trigger and shuts down platform and motors
		*/
		ros::ServiceServer srvServer_Shutdown;

		ros::ServiceServer srvServer_SetMotionType;

		/**
		* Service requests cob_srvs::GetJointState. It reads out the latest joint information from the CAN buffer and gives it back. It also publishes the informaion on the topic "JointState"
		*/
		ros::ServiceServer srvServer_GetJointState;

		/**
		* Service requests cob_srvs::ElmoRecorderSetup. It is used to configure the Elmo Recorder to record predefined sources. 
		* Parameters are:
		* int64 recordinggap #Specify every which time quantum (4*90usec) a new data point (of 1024 points in total) is recorded. the recording process starts immediately.
		*/
		ros::ServiceServer srvServer_ElmoRecorderConfig;

		/**
		* Service requests cob_srvs::ElmoRecorderGet. It is used to start the read-out process of previously recorded data by the Elmo Recorder.
		* Parameters are:
		* int64 subindex 
		* #As Subindex, set the recorded source you want to read out:
		* #1: Main Speed
		* #2: Main Position
		* #10: ActiveCurrent
		* #16: Speed Command
		*
		* string fileprefix
		* #Enter the path+file-prefix for the logfile (of an existing directory!)
		* #The file-prefix is extended with _MotorNumber_RecordedSource.log
		*/
		ros::ServiceServer srvServer_ElmoRecorderReadout;

		// global variables
		// generate can-node handle
		CanCtrlPltfCOb3 *m_CanCtrlPltf;
		
		bool m_bisInitialized;
		int m_iNumMotors;
		int m_iNumDrives;

		struct ParamType
		{ 
			double dMaxDriveRateRadpS;
			double dMaxSteerRateRadpS;

			std::vector<double> vdWheelNtrlPosRad;
		};
		ParamType m_Param;
		
		std::string sIniDirectory;
		bool m_bPubEffort;
		bool m_bReadoutElmo;

		// Constructor
		NodeClass()
		{
			// initialization of variables
			m_bisInitialized = false;

			/// Parameters are set within the launch file
			// Read number of drives from iniFile and pass IniDirectory to CobPlatfCtrl.
			if (n.hasParam("IniDirectory"))
			{
				n.getParam("IniDirectory", sIniDirectory);
				ROS_INFO("IniDirectory loaded from Parameter-Server is: %s", sIniDirectory.c_str());
			}
			else
			{
				sIniDirectory = "Platform/IniFiles/";
				ROS_WARN("IniDirectory not found on Parameter-Server, using default value: %s", sIniDirectory.c_str());
			}

			n.param<bool>("PublishEffort", m_bPubEffort, false);
			if(m_bPubEffort) ROS_INFO("You have choosen to publish effort of motors, that charges capacity of CAN");
			
			
			IniFile iniFile;
			iniFile.SetFileName(sIniDirectory + "Platform.ini", "PltfHardwareCoB3.h");

			// get max Joint-Velocities (in rad/s) for Steer- and Drive-Joint
			iniFile.GetKeyInt("Config", "NumberOfMotors", &m_iNumMotors, true);
			iniFile.GetKeyInt("Config", "NumberOfWheels", &m_iNumDrives, true);
			if(m_iNumMotors < 2 || m_iNumMotors > 8) {
				m_iNumMotors = 8;
				m_iNumDrives = 4;
			}
			
			m_CanCtrlPltf = new CanCtrlPltfCOb3(sIniDirectory);
			
			// implementation of topics
			// published topics
			topicPub_JointState = n.advertise<sensor_msgs::JointState>("joint_states", 1); //EXP: not anymore /joint_states but local ns
			topicPub_Diagnostic = n.advertise<diagnostic_msgs::DiagnosticStatus>("diagnostic", 1);
			// subscribed topics
			topicSub_JointStateCmd = n.subscribe("joint_command", 1, &NodeClass::topicCallback_JointStateCmd, this);

			// implementation of service servers
			srvServer_Init = n.advertiseService("init", &NodeClass::srvCallback_Init, this);
			srvServer_ElmoRecorderConfig = n.advertiseService("ElmoRecorderConfig", &NodeClass::srvCallback_ElmoRecorderConfig, this);
			srvServer_ElmoRecorderReadout = n.advertiseService("ElmoRecorderReadout", &NodeClass::srvCallback_ElmoRecorderReadout, this);
			m_bReadoutElmo = false;

			srvServer_Recover = n.advertiseService("recover", &NodeClass::srvCallback_Recover, this);
			srvServer_Shutdown = n.advertiseService("shutdown", &NodeClass::srvCallback_Shutdown, this);
			//srvServer_isPltfError = n.advertiseService("isPltfError", &NodeClass::srvCallback_isPltfError, this); --> Publish this along with JointStates
			srvServer_GetJointState = n.advertiseService("GetJointState", &NodeClass::srvCallback_GetJointState, this);
		}

		// Destructor
		~NodeClass() 
		{
			m_CanCtrlPltf->shutdownPltf();
		}

		// topic callback functions 
		// function will be called when a new message arrives on a topic
		void topicCallback_JointStateCmd(const sensor_msgs::JointState::ConstPtr& msg)
		{
			ROS_DEBUG("Topic Callback joint_command");
			// only process cmds when system is initialized
			if(m_bisInitialized == true)
			{
				ROS_DEBUG("Topic Callback joint_command - Sending Commands to drives (initialized)");
		   		int iRet;
				sensor_msgs::JointState JointStateCmd = *msg;
				// check if velocities lie inside allowed boundaries
				for(int i = 0; i < m_iNumMotors; i++)
				{
					// for steering motors
					if( i == 1 || i == 3 || i == 5 || i == 7) // ToDo: specify this via the config-files
					{
						if (JointStateCmd.velocity[i] > m_Param.dMaxSteerRateRadpS)
						{
							JointStateCmd.velocity[i] = m_Param.dMaxSteerRateRadpS;
						}
						if (JointStateCmd.velocity[i] < -m_Param.dMaxSteerRateRadpS)
						{
							JointStateCmd.velocity[i] = -m_Param.dMaxSteerRateRadpS;
						}
					}
					// for driving motors
					else
					{
						if (JointStateCmd.velocity[i] > m_Param.dMaxDriveRateRadpS)
						{
							JointStateCmd.velocity[i] = m_Param.dMaxDriveRateRadpS;
						}
						if (JointStateCmd.velocity[i] < -m_Param.dMaxDriveRateRadpS)
						{
							JointStateCmd.velocity[i] = -m_Param.dMaxDriveRateRadpS;
						}
					}

					// and cmd velocities to Can-Nodes
					//m_CanCtrlPltf->setVelGearRadS(iCanIdent, dVelEncRadS);
					ROS_DEBUG("Send data to drives");
					iRet = m_CanCtrlPltf->setVelGearRadS(i, JointStateCmd.velocity[i]);
					ROS_DEBUG("Successfully sent data to drives");
					
					if(m_bPubEffort) 
						m_CanCtrlPltf->requestMotorTorque();
	  			}
			}
		}

		// service callback functions
		// function will be called when a service is querried

		// Init Can-Configuration
		bool srvCallback_Init(cob_srvs::Trigger::Request &req,
							  cob_srvs::Trigger::Response &res )
		{
			ROS_DEBUG("Service Callback init");
			if(m_bisInitialized == false)
			{
				m_bisInitialized = initDrives();
				//ROS_INFO("...initializing can-nodes...");
				//m_bisInitialized = m_CanCtrlPltf->initPltf();
				res.success.data = m_bisInitialized;
				if(m_bisInitialized)
				{
		   			ROS_INFO("Can-Node initialized");
				}
				else
				{
					res.error_message.data = "initialization of can-nodes failed";
				  	ROS_INFO("Initialization FAILED");
				}
			}
			else
			{
				ROS_ERROR("...platform already initialized...");
				res.success.data = false;
				res.error_message.data = "platform already initialized";
			}
			return true;
		}
		
		bool srvCallback_ElmoRecorderConfig(cob_srvs::ElmoRecorderConfig::Request &req,
							  cob_srvs::ElmoRecorderConfig::Response &res ){
			if(m_bisInitialized) {			
				m_CanCtrlPltf->evalCanBuffer();
				res.success = m_CanCtrlPltf->ElmoRecordings(0, req.recordinggap, "");
				res.message = "Successfully configured all motors for instant record";
			}

			return true;
		}
		
		bool srvCallback_ElmoRecorderReadout(cob_srvs::ElmoRecorderReadout::Request &req,
							  cob_srvs::ElmoRecorderReadout::Response &res ){
			if(m_bisInitialized) {
				m_CanCtrlPltf->evalCanBuffer();
				res.success = m_CanCtrlPltf->ElmoRecordings(1, req.subindex, req.fileprefix);
				if(res.success == 0) {
					res.message = "Successfully requested reading out of Recorded data";
					m_bReadoutElmo = true;
					ROS_WARN("CPU consuming evalCanBuffer used for ElmoReadout activated");
				} else if(res.success == 1) res.message = "Recorder hasn't been configured well yet";
				else if(res.success == 2) res.message = "A previous transmission is still in progress";
			}

			return true;
		}
		
		
		
		// reset Can-Configuration
		bool srvCallback_Recover(cob_srvs::Trigger::Request &req,
									 cob_srvs::Trigger::Response &res )
		{
			ROS_DEBUG("Service callback reset");
			res.success.data = m_CanCtrlPltf->resetPltf();
			if (res.success.data) {
	   			ROS_INFO("Can-Node resetted");
			} else {
				res.error_message.data = "reset of can-nodes failed";
				ROS_WARN("Reset of Can-Node FAILED");
			}

			return true;
		}
		
		// shutdown Drivers and Can-Node
		bool srvCallback_Shutdown(cob_srvs::Trigger::Request &req,
									 cob_srvs::Trigger::Response &res )
		{
			ROS_DEBUG("Service callback shutdown");
			res.success.data = m_CanCtrlPltf->shutdownPltf();
			if (res.success.data)
	   			ROS_INFO("Drives shut down");
			else
	   			ROS_INFO("Shutdown of Drives FAILED");

			return true;
		}

		bool srvCallback_GetJointState(cob_srvs::GetJointState::Request &req,
									 cob_srvs::GetJointState::Response &res )
		{
			ROS_DEBUG("Service Callback GetJointState");
			// init local variables
			int j, k, ret_sprintf;
			bool bIsError;
			std::vector<double> vdAngGearRad, vdVelGearRad, vdEffortGearNM;
			std::string str_steer, str_drive, str_num, str_cat;
			// ToDo: search for a more elegant way to compose JointNames
			char c_num [1];

			// init strings
			str_steer = "Steer";
			str_drive = "Drive";

			// set default values
			vdAngGearRad.resize(m_iNumMotors, 0);
			vdVelGearRad.resize(m_iNumMotors, 0);
			vdEffortGearNM.resize(m_iNumMotors, 0);

			// create temporary (local) JointState/Diagnostics Data-Container
			sensor_msgs::JointState jointstate;
			diagnostic_msgs::DiagnosticStatus diagnostics;
			

			//Do you have to set frame_id manually??

			// get time stamp for header
			jointstate.header.stamp = ros::Time::now();
			// set frame_id for header
			// jointstate.header.frame_id = frame_id; //Where to get this id from?

			// assign right size to JointState
			jointstate.name.resize(m_iNumMotors);
			jointstate.position.resize(m_iNumMotors);
			jointstate.velocity.resize(m_iNumMotors);
			jointstate.effort.resize(m_iNumMotors);

			if(m_bisInitialized == false)
			{
				// as long as system is not initialized
				bIsError = false;

				j = 0;
				k = 0;

				// set data to jointstate			
				for(int i = 0; i<m_iNumMotors; i++)
				{
					jointstate.position[i] = 0.0;
					jointstate.velocity[i] = 0.0;
					jointstate.effort[i] = 0.0;

					// set joint names
   					if( i == 1 || i == 3 || i == 5 || i == 7) // ToDo: specify this via the config-files
					{
						// create name for identification in JointState msg
						j = j+1;
						ret_sprintf = sprintf(c_num, "%i", j);
						str_num.assign(1, c_num[0]);
						str_cat = str_steer + str_num;
					}
					else
					{
						// create name for identification in JointState msg
						k = k+1;
						ret_sprintf = sprintf(c_num, "%i", k);
						str_num.assign(1, c_num[0]);
						str_cat = str_drive + str_num;
					}
					// set joint names
					jointstate.name[i] = str_cat;
				}
			}
			else
			{
				// as soon as drive chain is initialized
				// read Can-Buffer
				m_CanCtrlPltf->evalCanBuffer();
				
				j = 0;
				k = 0;
				for(int i = 0; i<m_iNumMotors; i++)
				{
					m_CanCtrlPltf->getGearPosVelRadS(i,  &vdAngGearRad[i], &vdVelGearRad[i]);
					
					//Get motor torque
					if(m_bPubEffort) {
						for(int i=0; i<m_iNumMotors; i++) {
							m_CanCtrlPltf->getMotorTorque(i, &vdEffortGearNM[i]); //(int iCanIdent, double* pdTorqueNm)
						}
					}
					
   					// if a steering motor was read -> correct for offset
   					if( i == 1 || i == 3 || i == 5 || i == 7) // ToDo: specify this via the config-files
					{
						// correct for initial offset of steering angle (arbitrary homing position)
						vdAngGearRad[i] += m_Param.vdWheelNtrlPosRad[j];
						MathSup::normalizePi(vdAngGearRad[i]);
						j = j+1;
						// create name for identification in JointState msg
						ret_sprintf = sprintf(c_num, "%i", j);
						str_num.assign(1, c_num[0]);
						str_cat = str_steer + str_num;
					}
					else
					{
						// create name for identification in JointState msg
						k = k+1;
						ret_sprintf = sprintf(c_num, "%i", k);
						str_num.assign(1, c_num[0]);
						str_cat = str_drive + str_num;
					}
					// set joint names
					jointstate.name[i] = str_cat;
				}

				// set data to jointstate
				for(int i = 0; i<m_iNumMotors; i++)
				{
					jointstate.position[i] = vdAngGearRad[i];
					jointstate.velocity[i] = vdVelGearRad[i];
					jointstate.effort[i] = vdEffortGearNM[i];
				}
			}

			// set answer to srv request
			res.jointstate = jointstate;

			// publish jointstate message
			topicPub_JointState.publish(jointstate);
			ROS_DEBUG("published new drive-chain configuration (JointState message)");
			

			if(m_bisInitialized)
			{
				// read Can only after initialization
				bIsError = m_CanCtrlPltf->isPltfError();
			}

			// set data to diagnostics
			if(bIsError)
			{
				diagnostics.level = 2;
				diagnostics.name = "drive-chain can node";
				diagnostics.message = "one or more drives are in Error mode";
			}
			else
			{
				if (m_bisInitialized)
				{
					diagnostics.level = 0;
					diagnostics.name = "drive-chain can node";
					diagnostics.message = "drives operating normal";
				}
				else
				{
					diagnostics.level = 1;
					diagnostics.name = "drive-chain can node";
					diagnostics.message = "drives are initializing";
				}
			}

			// publish diagnostic message
			topicPub_Diagnostic.publish(diagnostics);
			ROS_DEBUG("published new drive-chain configuration (JointState message)");

			return true;
		}

		//EXPERIMENTAL: publish JointStates cyclical instead of service callback
		bool publish_JointStates()
		{
			ROS_DEBUG("Service Callback GetJointState");
			// init local variables
			int j, k, ret_sprintf;
			bool bIsError;
			std::vector<double> vdAngGearRad, vdVelGearRad, vdEffortGearNM;
			std::string str_steer, str_drive, str_num, str_cat;
			// ToDo: search for a more elegant way to compose JointNames
			char c_num [1];

			// init strings
			str_steer = "Steer";
			str_drive = "Drive";

			// set default values
			vdAngGearRad.resize(m_iNumMotors, 0);
			vdVelGearRad.resize(m_iNumMotors, 0);
			vdEffortGearNM.resize(m_iNumMotors, 0);

			// create temporary (local) JointState/Diagnostics Data-Container
			sensor_msgs::JointState jointstate;
			diagnostic_msgs::DiagnosticStatus diagnostics;
			

			//Do you have to set frame_id manually??

			// get time stamp for header
			jointstate.header.stamp = ros::Time::now();
			// set frame_id for header
			// jointstate.header.frame_id = frame_id; //Where to get this id from?

			// assign right size to JointState
			jointstate.name.resize(m_iNumMotors);
			jointstate.position.resize(m_iNumMotors);
			jointstate.velocity.resize(m_iNumMotors);
			jointstate.effort.resize(m_iNumMotors);

			if(m_bisInitialized == false)
			{
				// as long as system is not initialized
				bIsError = false;

				j = 0;
				k = 0;

				// set data to jointstate			
				for(int i = 0; i<m_iNumMotors; i++)
				{
					jointstate.position[i] = 0.0;
					jointstate.velocity[i] = 0.0;
					jointstate.effort[i] = 0.0;

/*
					// set joint names
   					if( i == 1 || i == 3 || i == 5 || i == 7) // ToDo: specify this via the config-files
					{
						// create name for identification in JointState msg
						j = j+1;
						ret_sprintf = sprintf(c_num, "%i", j);
						str_num.assign(1, c_num[0]);
						str_cat = str_steer + str_num;
					}
					else
					{
						// create name for identification in JointState msg
						k = k+1;
						ret_sprintf = sprintf(c_num, "%i", k);
						str_num.assign(1, c_num[0]);
						str_cat = str_drive + str_num;
					}
					// set joint names
					jointstate.name[i] = str_cat;
*/
				}
			}
			else
			{
				// as soon as drive chain is initialized
				// read Can-Buffer
				ROS_DEBUG("Read CAN-Buffer");
				m_CanCtrlPltf->evalCanBuffer();
				ROS_DEBUG("Successfully read CAN-Buffer");
				
				j = 0;
				k = 0;
				for(int i = 0; i<m_iNumMotors; i++)
				{
					m_CanCtrlPltf->getGearPosVelRadS(i,  &vdAngGearRad[i], &vdVelGearRad[i]);
					
					//Get motor torque
					if(m_bPubEffort) {
						for(int i=0; i<m_iNumMotors; i++) {
							m_CanCtrlPltf->getMotorTorque(i, &vdEffortGearNM[i]); //(int iCanIdent, double* pdTorqueNm)
						}
					}
					
   					// if a steering motor was read -> correct for offset
   					if( i == 1 || i == 3 || i == 5 || i == 7) // ToDo: specify this via the config-files
					{
						// correct for initial offset of steering angle (arbitrary homing position)
						vdAngGearRad[i] += m_Param.vdWheelNtrlPosRad[j];
						MathSup::normalizePi(vdAngGearRad[i]);
						j = j+1;
						// create name for identification in JointState msg
/*
						ret_sprintf = sprintf(c_num, "%i", j);
						str_num.assign(1, c_num[0]);
						str_cat = str_steer + str_num;

					}
					else
					{
						// create name for identification in JointState msg
						k = k+1;

						ret_sprintf = sprintf(c_num, "%i", k);
						str_num.assign(1, c_num[0]);
						str_cat = str_drive + str_num;
*/
					}
					// set joint names
//					jointstate.name[i] = str_cat;

				}

				// set data to jointstate
				for(int i = 0; i<m_iNumMotors; i++)
				{
					jointstate.position[i] = vdAngGearRad[i];
					jointstate.velocity[i] = vdVelGearRad[i];
					jointstate.effort[i] = vdEffortGearNM[i];
				}
			}

			// set answer to srv request
			// res.jointstate = jointstate;

			// publish jointstate message
			topicPub_JointState.publish(jointstate);
			ROS_DEBUG("published new drive-chain configuration (JointState message)");
			

			if(m_bisInitialized)
			{
				// read Can only after initialization
				bIsError = m_CanCtrlPltf->isPltfError();
			}

			// set data to diagnostics
			if(bIsError)
			{
				diagnostics.level = 2;
				diagnostics.name = "drive-chain can node";
				diagnostics.message = "one or more drives are in Error mode";
			}
			else
			{
				if (m_bisInitialized)
				{
					diagnostics.level = 0;
					diagnostics.name = "drive-chain can node";
					diagnostics.message = "drives operating normal";
				}
				else
				{
					diagnostics.level = 1;
					diagnostics.name = "drive-chain can node";
					diagnostics.message = "drives are initializing";
				}
			}

			// publish diagnostic message
			topicPub_Diagnostic.publish(diagnostics);
			ROS_DEBUG("published new drive-chain configuration (JointState message)");

			return true;
		}
		
		// other function declarations
		bool initDrives();
};

//#######################
//#### main programm ####
int main(int argc, char** argv)
{
	// initialize ROS, spezify name of node
	ros::init(argc, argv, "base_drive_chain");

	NodeClass nodeClass;
	
	// specify looprate of control-cycle
 	ros::Rate loop_rate(100); // Hz 

	while(nodeClass.n.ok())
	{
		//Read-out of CAN buffer is only necessary during read-out of Elmo Recorder		
		if( nodeClass.m_bReadoutElmo ) {
			if(nodeClass.m_bisInitialized) nodeClass.m_CanCtrlPltf->evalCanBuffer();
			
			if(nodeClass.m_CanCtrlPltf->ElmoRecordings(100, 0, "") == 0) {
				nodeClass.m_bReadoutElmo = false;
				ROS_INFO("CPU consuming evalCanBuffer used for ElmoReadout deactivated");
			}
		}

		nodeClass.publish_JointStates();
		
		loop_rate.sleep();
		ros::spinOnce();
	}

	return 0;
}

//##################################
//#### function implementations ####
bool NodeClass::initDrives()
{
	ROS_INFO("Initializing Base Drive Chain");

	// init member vectors
	m_Param.vdWheelNtrlPosRad.assign((m_iNumDrives),0);
//	m_Param.vdWheelNtrlPosRad.assign(4,0);
	// ToDo: replace the following steps by ROS configuration files
	// create Inifile class and set target inifile (from which data shall be read)
	IniFile iniFile;

	//n.param<std::string>("PltfIniLoc", sIniFileName, "Platform/IniFiles/Platform.ini");
	iniFile.SetFileName(sIniDirectory + "Platform.ini", "PltfHardwareCoB3.h");

	// get max Joint-Velocities (in rad/s) for Steer- and Drive-Joint
	iniFile.GetKeyDouble("DrivePrms", "MaxDriveRate", &m_Param.dMaxDriveRateRadpS, true);
	iniFile.GetKeyDouble("DrivePrms", "MaxSteerRate", &m_Param.dMaxSteerRateRadpS, true);
	
	// get Offset from Zero-Position of Steering
	if(m_iNumDrives >=1)
		iniFile.GetKeyDouble("DrivePrms", "Wheel1NeutralPosition", &m_Param.vdWheelNtrlPosRad[0], true);
	if(m_iNumDrives >=2)
		iniFile.GetKeyDouble("DrivePrms", "Wheel2NeutralPosition", &m_Param.vdWheelNtrlPosRad[1], true);
	if(m_iNumDrives >=3)
		iniFile.GetKeyDouble("DrivePrms", "Wheel3NeutralPosition", &m_Param.vdWheelNtrlPosRad[2], true);
	if(m_iNumDrives >=4)
		iniFile.GetKeyDouble("DrivePrms", "Wheel4NeutralPosition", &m_Param.vdWheelNtrlPosRad[3], true);

	//Convert Degree-Value from ini-File into Radian:
	for(int i=0; i<m_iNumDrives; i++)
	{
		m_Param.vdWheelNtrlPosRad[i] = MathSup::convDegToRad(m_Param.vdWheelNtrlPosRad[i]);
	}
//	m_Param.vdWheelNtrlPosRad[1] = MathSup::convDegToRad(m_Param.vdWheelNtrlPosRad[1]);
//	m_Param.vdWheelNtrlPosRad[2] = MathSup::convDegToRad(m_Param.vdWheelNtrlPosRad[2]);
//	m_Param.vdWheelNtrlPosRad[3] = MathSup::convDegToRad(m_Param.vdWheelNtrlPosRad[3]);

	// debug log
	ROS_INFO("Initializing CanCtrlItf");
	bool bTemp1;
	bTemp1 =  m_CanCtrlPltf->initPltf();
	// debug log
	ROS_INFO("Initializing done");


	return bTemp1;
}

