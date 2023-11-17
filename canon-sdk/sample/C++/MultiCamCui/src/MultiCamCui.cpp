#ifdef __APPLE__
#include <MacTypes.h>
#endif

#include <iostream>
#include <iterator>
#include <list>
#include <regex>
#include <string>
#include <vector>
#include <thread>
#include <memory>
#include "EDSDK.h"
#include "EDSDKTypes.h"
#include "Download.h"
#include "PreSetting.h"
#include "Property.h"
#include "CameraModel.h"
#include "utility.h"

static std::string control_number = "";
static bool keyflag;

using namespace std::chrono_literals;

void CheckKey() // After key is entered, _ endthread is automatically called.
{
	std::cin >> control_number;
	std::cin.ignore(); // ignore "\n"
	keyflag = true;
}

int main()
{
	EdsError err = EDS_ERR_OK;
	EdsCameraListRef cameraList = NULL;
	EdsCameraRef camera;
	std::vector<CameraModel *> _detectedCameraArray;
	std::vector<CameraModel *> _openedCamerasArray;
	EdsUInt32 min = 0, max = 0, i = 0;
	EdsUInt32 count = 0;
	EdsUInt32 data;
	bool isSDKLoaded = false;
	std::smatch match_results;
	std::shared_ptr<std::thread> th = std::shared_ptr<std::thread>();

	// Initialization of SDK
	err = EdsInitializeSDK();
	if (err == EDS_ERR_OK)
	{
		isSDKLoaded = true;
		std::this_thread::sleep_for(1000ms);
	}

	while (true)
	{
		bool loop = true;

		while (true)
		{
			if (loop)
			{
				// Acquisition of camera list
				if (err == EDS_ERR_OK)
				{
					err = EdsGetCameraList(&cameraList);
				}

				// Acquisition of number of Cameras
				if (err == EDS_ERR_OK)
				{
					err = EdsGetChildCount(cameraList, &count);
					if (count == 0)
					{
						std::cout << "Cannot detect any camera" << std::endl;
						pause_return();
						exit(EXIT_FAILURE);
					}
					else if (count > 30)
					{
						std::cout << "Too many cameras detected" << std::endl;
						pause_return();
						exit(EXIT_FAILURE);
					}
					std::cout << count << "cameras detected." << std::endl;
				}

				std::cout << "number\t"
						  << "Device Description" << std::endl;
				std::cout << "------+-------------------------" << std::endl;

				// Acquisition of camera at the head of the list
				for (i = 0; i < count; i++)
				{
					if (err == EDS_ERR_OK)
					{
						err = EdsGetChildAtIndex(cameraList, i, &camera);
						EdsDeviceInfo deviceInfo;
						err = EdsGetDeviceInfo(camera, &deviceInfo);
						if (err == EDS_ERR_OK && camera == NULL)
						{
							std::cout << "Camera is not found." << std::endl;
							pause_return();
							exit(EXIT_FAILURE);
						}
						std::cout << "[" << i + 1 << "]\t" << deviceInfo.szDeviceDescription << std::endl;
						_detectedCameraArray.push_back(new CameraModel(camera, i + 1, kEdsSaveTo_Host)); // select direction : kEdsSaveTo_Camera / kEdsSaveTo_Host / kEdsSaveTo_Both
						_detectedCameraArray[i]->setModelName(deviceInfo.szDeviceDescription);
					}
				}
				std::cout << "--------------------------------" << std::endl;
				std::cout << "Enter the number of the camera to connect [1-" << count << "]" << std::endl;
				std::cout << "\tor 'a' (=All)" << std::endl;
				std::cout << "\tor 'x' (=eXit)" << std::endl;
				std::cout << "> ";

				// Wait for input on another thread to send the command "getevent" periodically.
				th = std::make_shared<std::thread>(CheckKey);

				loop = false;
				keyflag = false; // initialize keyflag
			}
			if (keyflag == true) // Within this scope, the CheckKey thread is terminated
			{
				th->join();
				// min～max
				if (std::regex_match(control_number, match_results, std::regex(R"((\d+)-(\d+))")))
				{

					min = stoi(match_results[1].str());
					max = stoi(match_results[2].str());
					std::cout << "connecting to [" << min << "-" << max << "] ..." << std::endl;
					if (min > count || max > count)
					{
						std::cout << "invalid number" << std::endl;
						pause_return();
						exit(EXIT_FAILURE);
					}
					for (i = (min - 1); i < max; i++)
						_openedCamerasArray.push_back(_detectedCameraArray[i]);
					break;
				}

				// single camera
				else if (std::regex_search(control_number, match_results, std::regex("[0-9]")))
				{
					i = stoi(control_number) - 1;
					if (i > count - 1)
					{
						std::cout << "invalid number" << std::endl;
						pause_return();
						exit(EXIT_FAILURE);
					}
					std::cout << "connecting to [" << control_number << "] ..." << std::endl;
					_openedCamerasArray.push_back(_detectedCameraArray[i]);
					break;
				}

				// all
				else if (std::regex_search(control_number, match_results, std::regex("a", std::regex_constants::icase)))
				{
					std::cout << "connecting to all cameras..." << std::endl;
					for (i = 0; i < count; i++)
						_openedCamerasArray.push_back(_detectedCameraArray[i]);
					break;
				}

				// exit
				else if (std::regex_search(control_number, match_results, std::regex("x", std::regex_constants::icase)))
				{
					std::cout << "closing app..." << std::endl;
					pause_return();
					exit(EXIT_SUCCESS);
				}

				// retry
				else
				{
					keyflag = false;
					loop = true;
					pause_return();
					clr_screen();

					continue;
				}
			}
			// send getevent periodically
			EdsGetEvent();
			std::this_thread::sleep_for(200ms);
		}

		PreSetting(_detectedCameraArray);

		clr_screen();

		loop = true;

		for (i = 0; i < _openedCamerasArray.size(); i++)
			_openedCamerasArray[i]->OpenSessionCommand();

		while (true)
		{
			// control menu
			if (loop)
			{
				keyflag = false; // initialize keyflag
								 //				_beginthread(CheckKey, 0, NULL); // Start waiting for keystrokes
				th = std::make_shared<std::thread>(CheckKey);

				std::cout << "--------------------------------" << std::endl;
				std::cout
					<< "[ 1] Set Save To \n"
					<< "[ 2] Set Image Quality \n"
					<< "[ 3] Take Picture and download \n"
					<< "[ 4] Press Halfway \n"
					<< "[ 5] Press Completely \n"
					<< "[ 6] Press Off \n"
					<< "[ 7] TV \n"
					<< "[ 8] AV \n"
					<< "[ 9] ISO \n"
					<< "[10] White Balance \n"
					<< "[11] Drive Mode \n"
					<< "[12] Exposure Compensation \n"
					<< "[13] AE Mode (read only) \n"
					<< "[14] AF Mode (read only)  \n"
					<< "[15] Aspect setting (read only)  \n"
					<< "[16] Get Available shots (read only)  \n"
					<< "[17] Get Battery Level (read only)  \n"
					<< "[18] Edit Copyright" << std::endl;
				std::cout << "-------------------------------" << std::endl;
				std::cout
					<< "[20] Get Live View Image" << std::endl;
				std::cout << "-------------------------------" << std::endl;
				std::cout
					<< "[30] All File Download\n"
					<< "[31] Format Volume " << std::endl;
				std::cout << "-------------------------------" << std::endl;
				std::cout << "Enter the number of the control.\n"
						  << "\tor 'r' (=Return)" << std::endl;
				std::cout << "> ";
				loop = false;
			}

			if (keyflag == true) // Within this scope, the CheckKey thread is terminated
			{

				th->join();

				if (std::regex_search(control_number, match_results, std::regex("[0-9]")))
				{
					// set save to
					if (control_number == "1")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetProperty(kEdsPropID_SaveTo, SaveTo_table);

						ListAvailValue(SaveTo_table);

						std::cout << "input no." << std::endl;
						std::cout << ">";

						data = getvalue();
						if (data != -1 || data <= SaveTo_table.size())
						{
							for (i = 0; i < _openedCamerasArray.size(); i++)
							{
								if (data & kEdsSaveTo_Camera) // Check media
								{
									EdsUInt32 volume_count;
									volume_count = _openedCamerasArray[i]->GetVolume();
									if (volume_count == 0)
									{
										std::cout << " no card" << std::endl;
									}
								}
								_openedCamerasArray[i]->SetPropertyValue(kEdsPropID_SaveTo, &data);
								_openedCamerasArray[i]->setsaveTo(data);

								if (data & kEdsSaveTo_Both || data & kEdsSaveTo_Host) // When the Host or the Both is selected, it is recommended to execute the SetCapacity command.
								{
									EdsCapacity capacity = {0x7FFFFFFF, 0x1000, 1};
									_openedCamerasArray[i]->SetCapacity(capacity);
								}
							}
							pause_return();
						}
						clr_screen();
						loop = true;
						continue;
					}
					// set image quality
					if (control_number == "2")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetProperty(kEdsPropID_ImageQuality, ImageQuality_table);
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetPropertyDesc(kEdsPropID_ImageQuality, ImageQuality_table);

						std::cout << "input no." << std::endl;
						std::cout << ">";

						data = getvalue();
						if (data != -1 || data <= ImageQuality_table.size())
						{
							for (i = 0; i < _openedCamerasArray.size(); i++)
							{
								_openedCamerasArray[i]->SetProperty(kEdsPropID_ImageQuality, data, ImageQuality_table);
								_openedCamerasArray[i]->setImageQuality(data);
							}
							pause_return();
						}
						clr_screen();
						loop = true;
						continue;
					}
					// Take Picture
					else if (control_number == "3")
					{
						/* select with AF or without AF
						 *   kEdsCameraCommand_ShutterButton_Completely
						 *   kEdsCameraCommand_ShutterButton_Completely_NonAF
						 */
						EdsBool pause_flg = 0;
						for (i = 0; i < _openedCamerasArray.size(); i++)
						{
							err = _openedCamerasArray[i]->TakePicture(kEdsCameraCommand_ShutterButton_Completely_NonAF);
							pause_flg = pause_flg | (err != EDS_ERR_OK);
						}
						if (pause_flg)
							pause_return();
						clr_screen();
						loop = true;
						continue;
					}
					// Press halfway
					else if (control_number == "4")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->PressShutter(kEdsCameraCommand_ShutterButton_Halfway);
						clr_screen();
						loop = true;
						continue;
					}
					// Press Completely
					else if (control_number == "5")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->PressShutter(kEdsCameraCommand_ShutterButton_Completely);
						clr_screen();
						loop = true;
						continue;
					}
					// Press Off
					else if (control_number == "6")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->PressShutter(kEdsCameraCommand_ShutterButton_OFF);
						clr_screen();
						loop = true;
						continue;
					}
					// TV
					else if (control_number == "7")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetProperty(kEdsPropID_Tv, tv_table);
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetPropertyDesc(kEdsPropID_Tv, tv_table);

						std::cout << "input no. (ex. 54 = 1/250)" << std::endl;
						std::cout << ">";

						data = getvalue();
						if (data != -1 || data <= tv_table.size())
						{
							for (i = 0; i < _openedCamerasArray.size(); i++)
							{
								_openedCamerasArray[i]->SetProperty(kEdsPropID_Tv, data, tv_table);
								_openedCamerasArray[i]->setTv(data);
							}
							pause_return();
						}
						clr_screen();
						loop = true;
						continue;
					}
					// Av
					else if (control_number == "8")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetProperty(kEdsPropID_Av, av_table);
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetPropertyDesc(kEdsPropID_Av, av_table);

						std::cout << "input Av (ex. 21 = 5.6)" << std::endl;
						std::cout << ">";

						data = getvalue();
						if (data != -1 || data <= av_table.size())
						{
							for (i = 0; i < _openedCamerasArray.size(); i++)
							{
								_openedCamerasArray[i]->SetProperty(kEdsPropID_Av, data, av_table);
								_openedCamerasArray[i]->setAv(data);
							}
							pause_return();
						}
						clr_screen();
						loop = true;
						continue;
					}
					// ISO
					else if (control_number == "9")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetProperty(kEdsPropID_ISOSpeed, iso_table);
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetPropertyDesc(kEdsPropID_ISOSpeed, iso_table);

						std::cout << "input ISOSpeed (ex. 8 = ISO 200)" << std::endl;
						std::cout << ">";

						data = getvalue();
						if (data != -1 || data <= iso_table.size())
						{
							for (i = 0; i < _openedCamerasArray.size(); i++)
							{
								_openedCamerasArray[i]->SetProperty(kEdsPropID_ISOSpeed, data, iso_table);
								_openedCamerasArray[i]->setIso(data);
							}
							pause_return();
						}
						clr_screen();
						loop = true;
						continue;
					}
					// White Balanse
					else if (control_number == "10")

					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetProperty(kEdsPropID_WhiteBalance, whitebalance_table);
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetPropertyDesc(kEdsPropID_WhiteBalance, whitebalance_table);

						std::cout << "input WhiteBalance (ex. 0 = Auto)" << std::endl;
						std::cout << ">";

						data = getvalue();
						if (data != -1 || data <= whitebalance_table.size())
						{
							for (i = 0; i < _openedCamerasArray.size(); i++)
							{
								_openedCamerasArray[i]->SetProperty(kEdsPropID_WhiteBalance, data, whitebalance_table);
							}
							pause_return();
						}
						clr_screen();
						loop = true;
						continue;
					}
					// Drive Mode
					else if (control_number == "11")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetProperty(kEdsPropID_DriveMode, drivemode_table);
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetPropertyDesc(kEdsPropID_DriveMode, drivemode_table);

						std::cout << "input Drive Mode (ex. 0 = Single shooting)" << std::endl;
						std::cout << ">";
						data = getvalue();

						if (data != -1 || data <= drivemode_table.size())
						{
							for (i = 0; i < _openedCamerasArray.size(); i++)
								_openedCamerasArray[i]->SetProperty(kEdsPropID_DriveMode, data, drivemode_table);
							pause_return();
						}
						clr_screen();
						loop = true;
						continue;
					}
					// Exposure Compensation
					else if (control_number == "12")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetProperty(kEdsPropID_ExposureCompensation, ExposureComp_table);
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetPropertyDesc(kEdsPropID_ExposureCompensation, ExposureComp_table);

						std::cout << "!!! Exposure compensation is not available if the camera is in Manual exposure mode. !!!" << std::endl;
						std::cout << "input no. (ex. 0 = 0)" << std::endl;

						std::cout << ">";
						data = getvalue();

						if (data != -1 || data <= ExposureComp_table.size())
						{
							for (i = 0; i < _openedCamerasArray.size(); i++)
								_openedCamerasArray[i]->SetProperty(kEdsPropID_ExposureCompensation, data, ExposureComp_table);
							pause_return();
						}
						clr_screen();
						loop = true;
						continue;
					}
					// AE Mode (read only)
					else if (control_number == "13")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetProperty(kEdsPropID_AEMode, AEmode_table);
						// for (i = 0; i < _openedCamerasArray.size(); i++) _openedCamerasArray[i]->SendCommand(kEdsCameraCommand_SetModeDialDisable, 0); // Disables the mode dial.
						// for (i = 0; i < _openedCamerasArray.size(); i++) _openedCamerasArray[i]->GetPropertyDesc(kEdsPropID_AEModeSelect, AEmode_table);
						// for (i = 0; i < _openedCamerasArray.size(); i++) _openedCamerasArray[i]->SendCommand(kEdsCameraCommand_SetModeDialDisable, 1); // Cancels the disabled mode dial.

						pause_return();
						clr_screen();
						loop = true;
						continue;
					}
					// AF Mode (read only)
					else if (control_number == "14")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetProperty(kEdsPropID_AFMode, AFmode_table);

						pause_return();
						clr_screen();
						loop = true;
						continue;
					}
					// Aspect (read only)
					else if (control_number == "15")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
							_openedCamerasArray[i]->GetProperty(kEdsPropID_Aspect, Aspect_table);

						pause_return();
						clr_screen();
						loop = true;
						continue;
					}
					// AvailableShots (read only)
					else if (control_number == "16")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
						{
							_openedCamerasArray[i]->GetPropertyvalue(kEdsPropID_AvailableShots);
							std::cout << " shots" << std::endl;
						}
						pause_return();
						clr_screen();
						loop = true;
						continue;
					}
					// BatteryLevel (read only)
					else if (control_number == "17")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
						{
							_openedCamerasArray[i]->GetPropertyvalue(kEdsPropID_BatteryLevel);
							std::cout << " %" << std::endl;
						}

						pause_return();
						clr_screen();
						loop = true;
						continue;
					}
					// Edit Copyright
					else if (control_number == "18")
					{
						std::string str(64, 0);
						for (i = 0; i < _openedCamerasArray.size(); i++)
						{
							_openedCamerasArray[i]->GetPropertyvalue(kEdsPropID_Copyright);
							std::cout << " \n"
									  << std::endl;
						}
						std::cout << "What do you want?\n"
								  << std::endl;
						std::cout << "[E]dit? or [D]elete?\n"
								  << std::endl;
						std::cout << ">";
						err = getstrings(str, 64);
						if (std::regex_search(str, match_results, std::regex("e", std::regex_constants::icase)))
						{
							std::cout << "enter Copyright. (MAX 64 ASCII characters)\n"
									  << std::endl;
							std::cout << ">";
							err = getstrings(str, 64);
							if (err == 1)
							{
								for (i = 0; i < _openedCamerasArray.size(); i++)
								{
									_openedCamerasArray[i]->UILock();
									_openedCamerasArray[i]->SetPropertyValue(kEdsPropID_Copyright, str);
									_openedCamerasArray[i]->UIUnLock();
								}
							}
							pause_return();
							clr_screen();
							loop = true;
							continue;
						}
						else if (std::regex_search(str, match_results, std::regex("d", std::regex_constants::icase)))
						{
							str = "";
							for (i = 0; i < _openedCamerasArray.size(); i++)
							{
								_openedCamerasArray[i]->UILock();
								_openedCamerasArray[i]->SetPropertyValue(kEdsPropID_Copyright, str);
								_openedCamerasArray[i]->UIUnLock();
							}
							pause_return();
							clr_screen();
							loop = true;
							continue;
						}
						else
						{
							clr_screen();
							loop = true;
							continue;
						}
					}
					// ---------------------------------------------------------------------------------------------
					// Download EVF image
					else if (control_number == "20")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
						{
							_openedCamerasArray[i]->DownloadEvfCommand();
						}

						pause_return();
						clr_screen();
						loop = true;
						continue;
					}
					// ---------------------------------------------------------------------------------------------
					// Download All Image
					else if (control_number == "30")
					{
						DownloadImageAll(_openedCamerasArray);
						pause_return();
						clr_screen();
						loop = true;
						continue;
					}
					// Format Volume
					else if (control_number == "31")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
						{
							std::cout << "cam" << i + 1 << std::endl;
							EdsUInt32 volume_count;
							volume_count = _openedCamerasArray[i]->GetVolume();
							if (volume_count == 0)
							{
								std::cout << "no card" << std::endl;
							}
							else
							{
								std::cout << "enter the card number to Format" << std::endl;
								std::cout << ">";
								data = getvalue();
								_openedCamerasArray[i]->FormatVolume(data);
							}
						}
						pause_return();
						clr_screen();
						loop = true;
						continue;
					}
					// Rec Start
					else if (control_number == "40")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
						{
							_openedCamerasArray[i]->RecStart();
						}

						pause_return();
						clr_screen();
						loop = true;
						continue;
					}
					// Rec End
					else if (control_number == "41")
					{
						for (i = 0; i < _openedCamerasArray.size(); i++)
						{
							_openedCamerasArray[i]->RecEnd();
						}
						pause_return();
						clr_screen();
						loop = true;
						continue;
					}
					// other
					else
					{
						clr_screen();
						loop = true;
						continue;
					}
				}
				// Return
				else if (std::regex_search(control_number, match_results, std::regex("r", std::regex_constants::icase)))
				{
					clr_screen();
					keyflag = false;
					break;
				}
				else
				{
					clr_screen();
					loop = true;
					continue;
				}
			}
			// send getevent periodically
			EdsGetEvent();
			std::this_thread::sleep_for(200ms);
		}

		EdsUInt32 refcount = 0;
		bool berr = true;
		// Release camera list
		if (cameraList != NULL)
		{
			refcount = EdsRelease(cameraList);
		}

		// Release Camera

		for (i = 0; i < _openedCamerasArray.size(); i++)
		{
			if (_openedCamerasArray[i] != NULL)
			{
				berr = _openedCamerasArray[i]->CloseSessionCommand();
				refcount = EdsRelease(_openedCamerasArray[i]->getCameraObject());
				_openedCamerasArray[i] = NULL;
			}
		}
		// Remove elements before looping. Memory is automatically freed at the destructor of the vector when it leaves the scope.
		_openedCamerasArray.clear();
		clr_screen();
	}

	// Termination of SDK
	if (isSDKLoaded)
	{
		EdsTerminateSDK();
	}

	return FALSE;
}
