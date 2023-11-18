#include <vector>
#include <iostream>
#include "EDSDK.h"
#include "EDSDKTypes.h"
#include "CameraEvent.h"
#include "CameraModel.h"

EdsError PreSetting(std::vector<CameraModel *> _model)
{
	EdsError err = EDS_ERR_OK;
	EdsCameraRef cameraref;
	EdsUInt32 bodyID;

	for (EdsUInt32 i = 0; i < _model.size(); i++)
	{
		cameraref = _model[i]->getCameraObject();
		bodyID = _model[i]->getbodyID();
		if (err == EDS_ERR_OK)
		{
			err = EdsSetPropertyEventHandler(cameraref, kEdsPropertyEvent_All, handlePropertyEvent, (EdsVoid *)bodyID); // bodyID
		}

		// Set Object Event Handler
		if (err == EDS_ERR_OK)
		{
			err = EdsSetObjectEventHandler(cameraref, kEdsObjectEvent_All, handleObjectEvent, (EdsVoid *)bodyID);
		}

		// Set State Event Handler
		if (err == EDS_ERR_OK)
		{
			err = EdsSetCameraStateEventHandler(cameraref, kEdsStateEvent_All, handleSateEvent, (EdsVoid *)bodyID);
		}
	}
	return err;
}
