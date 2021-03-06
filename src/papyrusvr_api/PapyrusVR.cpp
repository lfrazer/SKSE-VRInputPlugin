#include "PapyrusVR.h"
#include "openvrhooks.h"

namespace PapyrusVR 
{
	//Custom Pose Event
	BSFixedString poseUpdateEventName("OnPosesUpdate");
	RegistrationSetHolder<TESForm*>				g_posesUpdateEventRegs;
	PoseUpdateListeners							g_poseUpdateListeners;

	//Custom Button Events
	BSFixedString vrButtonEventName("OnVRButtonEvent");
	RegistrationSetHolder<TESForm*>				g_vrButtonEventRegs;

	//Custom Button Events
	BSFixedString vrOverlapEventName("OnVROverlapEvent");
	RegistrationSetHolder<TESForm*>				g_vrOverlapEventRegs;

	//API
	std::mutex					listenersMutex; 
	SKSEMessagingInterface*		g_messagingInterface;
	PluginHandle*				g_pluginHandle;
	PapyrusVRAPI apiMessage;

	#pragma region Papyrus Events
		class EventQueueFunctor0 : public IFunctionArguments
		{
		public:
			EventQueueFunctor0(BSFixedString & a_eventName)
				: eventName(a_eventName.data) {}

			virtual bool Copy(Output * dst) { return true; }

			void operator() (const EventRegistration<TESForm*> & reg)
			{
				VMClassRegistry * registry = (*g_skyrimVM)->GetClassRegistry();
				registry->QueueEvent(reg.handle, &eventName, this);
			}

		private:
			BSFixedString	eventName;
		};

		template <typename T> void SetVMValue(VMValue * val, T arg)
		{
			VMClassRegistry * registry = (*g_skyrimVM)->GetClassRegistry();
			PackValue(val, &arg, registry);
		}
		template <> void SetVMValue<SInt32>(VMValue * val, SInt32 arg) { val->SetInt(arg); }
		class EventFunctor3 : public IFunctionArguments
		{
		public:
			EventFunctor3(BSFixedString & a_eventName, SInt32 aParam1, SInt32 aPram2, SInt32 aParam3)
				: eventName(a_eventName.data), param1(aParam1), param2(aPram2), param3(aParam3){}

			virtual bool Copy(Output * dst) 
			{ 
				dst->Resize(3);
				SetVMValue(dst->Get(0), param1);
				SetVMValue(dst->Get(1), param2);
				SetVMValue(dst->Get(2), param3);
				return true; 
			}

			void operator() (const EventRegistration<TESForm*> & reg)
			{
				VMClassRegistry * registry = (*g_skyrimVM)->GetClassRegistry();
				registry->QueueEvent(reg.handle, &eventName, this);
			}

		private:
			SInt32				param1;
			SInt32				param2;
			SInt32				param3;
			BSFixedString		eventName;
		};
	#pragma endregion

	#pragma region Papyrus Native Functions
		#pragma region SteamVR Coordinates
			void GetSteamVRDeviceRotation(StaticFunctionTag *base, SInt32 deviceEnum, VMArray<float> returnValues)
			{
				_MESSAGE("GetSteamVRDeviceRotation");
				CopyPoseToVMArray(deviceEnum, &returnValues, PoseParam::Rotation);
			}

			void GetSteamVRDeviceQRotation(StaticFunctionTag *base, SInt32 deviceEnum, VMArray<float> returnValues)
			{
				_MESSAGE("GetSteamVRDeviceQRotation");
				CopyPoseToVMArray(deviceEnum, &returnValues, PoseParam::QRotation);
			}

			void GetSteamVRDevicePosition(StaticFunctionTag *base, SInt32 deviceEnum, VMArray<float> returnValues)
			{
				_MESSAGE("GetSteamVRDevicePosition");
				CopyPoseToVMArray(deviceEnum, &returnValues, PoseParam::Position);

			}
		#pragma endregion

		#pragma region Skyrim Coordinates
			void GetSkyrimDeviceRotation(StaticFunctionTag *base, SInt32 deviceEnum, VMArray<float> returnValues)
			{
				_MESSAGE("GetSkyrimDeviceRotation");
				CopyPoseToVMArray(deviceEnum, &returnValues, PoseParam::Rotation, true);
			}

			void GetSkyrimDeviceQRotation(StaticFunctionTag *base, SInt32 deviceEnum, VMArray<float> returnValues)
			{
				_MESSAGE("GetSkyrimDeviceQRotation");
				CopyPoseToVMArray(deviceEnum, &returnValues, PoseParam::QRotation, true);
			}

			void GetSkyrimDevicePosition(StaticFunctionTag *base, SInt32 deviceEnum, VMArray<float> returnValues)
			{
				_MESSAGE("GetSkyrimDevicePosition");
				CopyPoseToVMArray(deviceEnum, &returnValues, PoseParam::Position, true);
			}
		#pragma endregion

		#pragma region Events Management
			void FormRegisterForEvent(TESForm* object, RegistrationSetHolder<TESForm*>* regHolder)
			{
				GenericRegisterForEvent(object, regHolder);
				if (object && object->formID)
					_MESSAGE("%d registered", object->formID);
			}

			void FormUnregisterForEvent(TESForm* object, RegistrationSetHolder<TESForm*>* regHolder)
			{
				GenericUnregisterForEvent(object, regHolder);
				if (object && object->formID)
					_MESSAGE("%d unregistered", object->formID);
			}

			void RegisterForPoseUpdates(StaticFunctionTag *base,	TESForm* thisForm)
			{
				_MESSAGE("RegisterForPoseUpdates");
				FormRegisterForEvent(thisForm, &g_posesUpdateEventRegs);
			}

			void UnregisterForPoseUpdates(StaticFunctionTag *base,	TESForm* thisForm)
			{
				_MESSAGE("UnregisterForPoseUpdates");
				FormUnregisterForEvent(thisForm, &g_posesUpdateEventRegs);
			}

			void RegisterForVRButtonEvents(StaticFunctionTag *base, TESForm * thisForm)
			{
				_MESSAGE("RegisterForVRButtonEvents");
				FormRegisterForEvent(thisForm, &g_vrButtonEventRegs);
			}

			void UnregisterForVRButtonEvents(StaticFunctionTag *base, TESForm * thisForm)
			{
				_MESSAGE("UnregisterForVRButtonEvents");
				FormUnregisterForEvent(thisForm, &g_vrButtonEventRegs);
			}
			void RegisterForVROverlapEvents(StaticFunctionTag *base, TESForm * thisForm)
			{
				_MESSAGE("RegisterForVROverlapEvents");
				FormRegisterForEvent(thisForm, &g_vrOverlapEventRegs);
			}

			void UnregisterForVROverlapEvents(StaticFunctionTag *base, TESForm * thisForm)
			{
				_MESSAGE("UnregisterForVROverlapEvents");
				FormUnregisterForEvent(thisForm, &g_vrOverlapEventRegs);
			}
		#pragma endregion

		#pragma region Overlap Objects
			UInt32 RegisterLocalOverlapSphere(StaticFunctionTag *base, float radius, VMArray<float> vmPosition, VMArray<float> vmRotation, SInt32 deviceEnum)
			{
				_MESSAGE("RegisterLocalOverlapSphere");
				if (vmPosition.Length() != 3)
					return 0;
				if (vmRotation.Length() != 4)
					return 0;

				Vector3 position;
				Quaternion rotation;
				OpenVRUtils::CopyVMArrayToVector3(&vmPosition, &position);
				_MESSAGE("Got position X:%f Y:%f Z:%f", position.x, position.y, position.z);
				OpenVRUtils::CopyVMArrayToQuaternion(&vmRotation, &rotation);
				_MESSAGE("Got rotation X:%f Y:%f Z:%f W:%f", rotation.x, rotation.y, rotation.z, rotation.w);

				Matrix34* transform = new Matrix34;
				*transform = OpenVRUtils::CreateTransformMatrix(&position, &rotation);

				_MESSAGE("Created Transform Matrix");
				_MESSAGE("|\t%f\t%f\t%f\t%f\t|", transform->m[0][0], transform->m[0][1], transform->m[0][2], transform->m[0][3]);
				_MESSAGE("|\t%f\t%f\t%f\t%f\t|", transform->m[1][0], transform->m[1][1], transform->m[1][2], transform->m[1][3]);
				_MESSAGE("|\t%f\t%f\t%f\t%f\t|", transform->m[2][0], transform->m[2][1], transform->m[2][2], transform->m[2][3]);
				return GetVRManager()->CreateLocalOverlapSphere(radius, transform, (VRDevice)deviceEnum);
			}

			void DestroyLocalOverlapObject(StaticFunctionTag *base, UInt32 objectHandle)
			{
				GetVRManager()->DestroyLocalOverlapObject(objectHandle);
			}
		#pragma endregion

		//Used for debugging
		std::clock_t start = clock();
		void TimeSinceLastCall(StaticFunctionTag* base)
		{
			clock_t end = clock();
			double elapsed_seconds = double(end - start) / CLOCKS_PER_SEC;
			_MESSAGE("90 events fired after %f seconds", elapsed_seconds);
			start = end;
		}

	#pragma endregion

	#pragma region OpenVR Hooks

		//SkyrimVR+0xC50C69
		clock_t lastFrame = clock();
		clock_t thisFrame;
		double deltaTime = 0.0f;
		void OnVRUpdate()
		{
			//Calculate deltaTime
			thisFrame = clock();
			deltaTime = double(thisFrame - lastFrame) / CLOCKS_PER_SEC;
			lastFrame = thisFrame;

			//Update Poses
			VRManager::GetInstance()->UpdatePoses();

			//Notify Listeners
            std::lock_guard<std::mutex> lock( listenersMutex );

			for (OnPoseUpdateCallback& callback : g_poseUpdateListeners)
				callback(deltaTime);


			//Notify Papyrus scripts
			//WARNING: Disabled cause this will currently freeze the game every 90 seconds
			//if (g_posesUpdateEventRegs.m_data.size() > 0)
			//	g_posesUpdateEventRegs.ForEach(
			//		EventQueueFunctor0(poseUpdateEventName)
			//	);
		}

	#pragma endregion

	#pragma region API
		void RegisterPoseUpdateListener(OnPoseUpdateCallback callback)
		{
            std::lock_guard<std::mutex> lock( listenersMutex );

			g_poseUpdateListeners.push_back(callback);
		}

		//Returns the VRManager singleton instance
		VRManagerAPI* GetVRManager()
		{
			return VRManager::GetInstance();
		}
	#pragma endregion

	#pragma region Messaging Interface

		// Listens for SKSE events
		void OnSKSEMessageRecived(SKSEMessagingInterface::Message* message)
		{
			if (message)
			{
				_MESSAGE("Recived SKSE message %d", message->type);
				if (message->type == SKSEMessagingInterface::kMessage_PostPostLoad)
				{
					if (g_messagingInterface && g_pluginHandle)
					{
						_MESSAGE("Game Loaded, Dispatching Init messages to all listeners");
						apiMessage.GetVRManager = GetVRManager;
						apiMessage.RegisterPoseUpdateListener = RegisterPoseUpdateListener;

						//Sends pointers to API functions/classes
						g_messagingInterface->Dispatch(*g_pluginHandle, kPapyrusVR_Message_Init, &apiMessage, sizeof(apiMessage), NULL);
					}
				}

				//Ready to Initialize VRManager
				if (message->type == SKSEMessagingInterface::kMessage_DataLoaded)
				{
					if (!VRManager::GetInstance()->IsInitialized())
					{
						// lfrazer: pass in VRSystem and VRCompositor to initialize from alternate OpenVR hook manager
						VRManager::GetInstance()->Init(OpenVRHookMgr::GetInstance()->GetVRSystem(), OpenVRHookMgr::GetInstance()->GetVRCompositor());
						// flag the OpenVR manager that we are ready to process input
						OpenVRHookMgr::GetInstance()->SetInputProcessingReady(true); 
					}
				}
			}
		}

		void RegisterMessagingInterface(SKSEMessagingInterface* messagingInterface)
		{
			if (messagingInterface && g_pluginHandle)
			{
				g_messagingInterface = messagingInterface;
				_MESSAGE("Registering for plugin loaded message!");
				g_messagingInterface->RegisterListener(*g_pluginHandle, "SKSE", OnSKSEMessageRecived);
			}
		}

		void RegisterHandle(PluginHandle* handle)
		{
			if (handle)
				g_pluginHandle = handle;
		}
	#pragma endregion

	#pragma region Utility Methods
		void CopyPoseToVMArray(UInt32 deviceType, VMArray<float>* resultArray, PoseParam parameter, bool skyrimWorldSpace)
		{
			TrackedDevicePose* requestedPose = VRManager::GetInstance()->GetPoseByDeviceEnum((VRDevice)deviceType);

			//Pose exists
			if (requestedPose)
			{
				Matrix34 matrix = requestedPose->mDeviceToAbsoluteTracking;

				if (parameter == PoseParam::Position)
				{
					//Position
					Vector3 devicePosition = OpenVRUtils::GetPosition(&(requestedPose->mDeviceToAbsoluteTracking));

					//Really dumb way to do it, just for testing
					if (skyrimWorldSpace)
					{
						NiAVObject* playerNode = (*g_thePlayer)->GetNiNode();
						devicePosition.x += playerNode->m_worldTransform.pos.x;
						devicePosition.y += playerNode->m_worldTransform.pos.y;
						devicePosition.z += playerNode->m_worldTransform.pos.z;
					}

					OpenVRUtils::CopyVector3ToVMArray(&devicePosition, resultArray);
				}
				else
				{
					Quaternion quatRotation = OpenVRUtils::GetRotation(&(requestedPose->mDeviceToAbsoluteTracking));

					if (parameter == PoseParam::Rotation)
					{
						//Euler
						Vector3 deviceRotation = OpenVRUtils::QuatToEuler(&quatRotation);
						OpenVRUtils::CopyVector3ToVMArray(&deviceRotation, resultArray);
					}
					else
					{
						//Quaternion
						OpenVRUtils::CopyQuaternionToVMArray(&quatRotation, resultArray);
					}
				}
			}
		}
	#pragma endregion

	void OnVRButtonEventRecived(VREventType eventType, EVRButtonId buttonId, VRDevice deviceId)
	{
		// lfrazer: Reduce log spam for recurring events
		//_MESSAGE("Dispatching eventType %d for button with ID: %d from device %d", eventType, buttonId, deviceId);
		//Notify Papyrus scripts
		if (g_vrButtonEventRegs.m_data.size() > 0)
			g_vrButtonEventRegs.ForEach(
				EventFunctor3(vrButtonEventName, eventType, buttonId, deviceId)
			);
	}

	void OnVROverlapEventRecived(VROverlapEvent eventType, UInt32 objectHandle, VRDevice deviceId)
	{
		// lfrazer: Reduce log spam for recurring events
		//_MESSAGE("Dispatching overlap %d for device with ID: %d from handle %d", eventType, deviceId, objectHandle);
		//Notify Papyrus scripts
		if (g_vrButtonEventRegs.m_data.size() > 0)
			g_vrButtonEventRegs.ForEach(
				EventFunctor3(vrOverlapEventName, eventType, objectHandle, deviceId)
			);
	}

	//Entry Point
	bool RegisterFuncs(VMClassRegistry* registry) 
	{
		_MESSAGE("Registering native functions...");
		registry->RegisterFunction(new NativeFunction2 <StaticFunctionTag, void, SInt32, VMArray<float>>("GetSteamVRDeviceRotation_Native", "PapyrusVR", PapyrusVR::GetSteamVRDeviceRotation, registry));
		registry->RegisterFunction(new NativeFunction2 <StaticFunctionTag, void, SInt32, VMArray<float>>("GetSteamVRDeviceQRotation_Native", "PapyrusVR", PapyrusVR::GetSteamVRDeviceQRotation, registry));
		registry->RegisterFunction(new NativeFunction2 <StaticFunctionTag, void, SInt32, VMArray<float>>("GetSteamVRDevicePosition_Native", "PapyrusVR", PapyrusVR::GetSteamVRDevicePosition, registry));
		registry->RegisterFunction(new NativeFunction2 <StaticFunctionTag, void, SInt32, VMArray<float>>("GetSkyrimDeviceRotation_Native", "PapyrusVR", PapyrusVR::GetSkyrimDeviceRotation, registry));
		registry->RegisterFunction(new NativeFunction2 <StaticFunctionTag, void, SInt32, VMArray<float>>("GetSkyrimDeviceQRotation_Native", "PapyrusVR", PapyrusVR::GetSkyrimDeviceQRotation, registry));
		registry->RegisterFunction(new NativeFunction2 <StaticFunctionTag, void, SInt32, VMArray<float>>("GetSkyrimDevicePosition_Native", "PapyrusVR", PapyrusVR::GetSkyrimDevicePosition, registry));

		registry->RegisterFunction(new NativeFunction4 <StaticFunctionTag, UInt32, float, VMArray<float>, VMArray<float>, SInt32>("RegisterLocalOverlapSphere", "PapyrusVR", PapyrusVR::RegisterLocalOverlapSphere, registry));
		registry->RegisterFunction(new NativeFunction1 <StaticFunctionTag, void, UInt32>("DestroyLocalOverlapObject", "PapyrusVR", PapyrusVR::DestroyLocalOverlapObject, registry));

		registry->RegisterFunction(new NativeFunction1 <StaticFunctionTag, void, TESForm*>("RegisterForPoseUpdates", "PapyrusVR", PapyrusVR::RegisterForPoseUpdates, registry));
		registry->RegisterFunction(new NativeFunction1 <StaticFunctionTag, void, TESForm*>("UnregisterForPoseUpdates", "PapyrusVR", PapyrusVR::UnregisterForPoseUpdates, registry));
		registry->RegisterFunction(new NativeFunction1 <StaticFunctionTag, void, TESForm*>("RegisterForVRButtonEvents", "PapyrusVR", PapyrusVR::RegisterForVRButtonEvents, registry));
		registry->RegisterFunction(new NativeFunction1 <StaticFunctionTag, void, TESForm*>("UnregisterForVRButtonEvents", "PapyrusVR", PapyrusVR::UnregisterForVRButtonEvents, registry));
		registry->RegisterFunction(new NativeFunction1 <StaticFunctionTag, void, TESForm*>("RegisterForVROverlapEvents", "PapyrusVR", PapyrusVR::RegisterForVROverlapEvents, registry));
		registry->RegisterFunction(new NativeFunction1 <StaticFunctionTag, void, TESForm*>("UnregisterForVROverlapEvents", "PapyrusVR", PapyrusVR::UnregisterForVROverlapEvents, registry));

		registry->RegisterFunction(new NativeFunction0 <StaticFunctionTag, void>("TimeSinceLastCall", "PapyrusVR", PapyrusVR::TimeSinceLastCall, registry)); //Debug function
		
		// Still need to register some handlers

		_MESSAGE("Registering for VR Button Events");
		VRManager::GetInstance()->RegisterVRButtonListener(PapyrusVR::OnVRButtonEventRecived);


		_MESSAGE("Registering for VR Overlap Events");
		VRManager::GetInstance()->RegisterVROverlapListener(PapyrusVR::OnVROverlapEventRecived);

		// lfrazer: Removing PapyrusVR trampoline hooks since we are doing it with a different method of DLL function hooking - result will be VR updates may not be called from render thread anymore

		return true;
	}
}
