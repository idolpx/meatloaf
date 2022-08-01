# meatloaf-specialty
Meatloaf for FujiNet

![meatloaf](/images/meatloaf.png)

This is the ESP32 version of [Meatloaf](https://github.com/idolpx/meatloaf) intended for the [FujiNet](https://github.com/FujiNetWIFI/) bring-up on the Commodore 64.

## Quick Instructions

Visual studio code, and the platformio addon installed from the vscode store required.

'''
Step 1: Copy platform.ini.sample to platform.ini
'''

'''
Step 2: Edit platform.ini to match your system
'''

'''
Step 3: Copy include/ssid-example.h to include/ssid.h
'''

'''
Step 4: Edit ssid.h to match your wifi
'''

'''
Step 5: Press the PlatformIO Upload icon arrow at the bottom in the status bar
'''
 ![platformio_upload](/images/ml-build-1.png)


Be sure to execute the following command in the project folder to pull the needed submodules.
```
git submodule update --init --recursive
```

Copy "platformio.ini.sample" to "platformio.ini" and edit the settings for your system.
