*****

# for Windows
## Operating environment
 We recommend using this sample with "windows terminal" because it uses ESC character (\033) for screen control.

## Install build tools
 Visual Studio 2019 version 16.5 or later
 Build it as a cmake project.

## Build Method
 1.Unzip MultiCamCui.zip.

 2.Start Visual Studio.

 3.Click "Open a local folder,"
   Select the folder created by unzipping MultiCamCui.zip.

 4.Choose between x64-Debug or x64-Release
   If you want to run the sample app on another PC rather than a build machine, you must build it with x 64 -Release.

 5.Build -> Build All


*****

# for Linux
## Operating environment
 We recommend using this sample with
  "Raspberry Pi 4/Raspberry Pi OS 32bit" and "Jetson nano/ubuntu18.04 64bit."

## Install build tools

 1.Install toolchain
  sudo apt install cmake build-essential

  ### for jetson nano:
    sudo apt install software-properties-common
    sudo add-apt-repository ppa:ubuntu-toolchain-r/test
    sudo apt-get update
    sudo apt install gcc-9 g++-9
    sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-9 10
    sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-9 10


## Build Method
 1.Unzip MultiCamCui.zip.

 2.Change the folder created by unzipping MultiCamCui.zip.
   cd MultiCamCui

 3.Edit CMakeLists.txt
  If you are building on the arm64 architecture, change line 61 of "CMakeLists.txt" to:
    ${EDSDK_LDIR}/Library/ARM64/libEDSDK.so
  If you are building with arm32 architecture, you do not need to change it.

 4.Configure
   cmake CMakeLists.txt

 5.Build
   make


*****


# How to use the sample app
 1.Connect the camera to your PC with a USB cable.

 2.Run MultiCamCui.exe.
   The top menu lists the detected cameras.

 3.Select the camera you want to connect.
   ex.
   - Select camera No.2 to No.5
     Enter "2-5"

   - Select camera No.3
     Enter "3"

   - Select all listed cameras
     Enter "a"

   - Quit the app
     Enter "x"

   * The camera number changes in the order you connect the camera to your PC.

 4.Control menu
   The control menu is the following:
		[ 1] Set Save To
		[ 2] Set Image Quality
		[ 3] Take Picture and download
		[ 4] Press Halfway
		[ 5] Press Completely
		[ 6] Press Off
		[ 7] TV
		[ 8] AV
		[ 9] ISO
		[10] White Balance
		[11] Drive Mode
		[12] Exposure Compensation
		[13] AE Mode (read only)
		[14] AF Mode (read only)
		[15] Aspect setting (read only)
		[16] Get Available shots (read only)
		[17] Get Battery Level (read only)
		[18] Edit Copyright
		[20] Get Live View Image
		[30] All File Download
		[31] Format Volume

   Select the item number you want to control.
   The following is a description of the operation for each input number.
   *Enter "r" key to move to "Top Menu"

		[ 1] Set Save To
    Set the destination for saving images.

		[ 2] Set Image Quality
    Set the image Quality.

		[ 3] Take Picture and download
    Press and release the shutter button without AF action,
    create a "cam + number" folder in the folder where MultiCamCui.exe is located
    and save the pictures taken with each camera.

    * If you can't shoot, change the mode dial to "M" and then try again.
    * The camera number changes in the order you connect the camera to your PC.

		[ 4] Press Halfway
    Press the shutter button halfway.

		[ 5] Press Completely
    Press the shutter button completely.
    When Drive mode is set to continuous shooting,
    Continuous shooting is performed.

		[ 6] Press Off
    Release the shutter button.

		[ 7] TV
    Set the Tv settings.

		[ 8] AV
    Set the Av settings.

		[ 9] ISO
    Set the ISO settings.

		[10] White Balance
    Set the White Balance settings.

		[11] Drive Mode
    Set the Drive mode settings.

		[12] Exposure Compensation
    Set the exposure compensation settings.

		[13] AE Mode (read only)
    Indicates the AE mode settings. (not configurable)

		[14] AF Mode (read only)
    Indicates the AF mode settings. (not configurable)

	  [15] Aspect setting (read only)
    Indicates the aspect settings. (not configurable)

	  [16] Get Available shots (read only)
    Indicates the number of shots available on a camera. (not configurable)

	  [17] Get Battery Level (read only)
    Indicates the camera battery level. (not configurable)

	  [18] Edit Copyright
    Indicates/Set a string identifying the copyright information on the camera.

		[20] Get Live View Image
    Get one live view image.
    In the folder where MultiCamCui.exe is located
    Automatically create a "cam number" folder and save each camera's image.

		[30] All File Download
    Download all picture File in the camera's card to PC.
    In the folder where MultiCamCui.exe is located
    automatically create a "cam number" folder and save each camera's image.

		[31] Format Volume
    Formats volumes of memory cards in a camera.

   * Some settings may not be available depending on the mode dial of the camera.
     If you can't set, change the mode dial to "M" and then try again.
