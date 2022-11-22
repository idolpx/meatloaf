[![Meatloaf (C64/C128/VIC20/+4)](images/meatloaf.logo.png)](https://meatloaf.cc)

This is the ESP32 version of [Meatloaf](https://github.com/idolpx/meatloaf) intended for the [FujiNet](https://github.com/FujiNetWIFI/) bring-up on the Commodore 64.

## Quick Instructions

Visual studio code, and the platformio addon installed from the vscode store required.

```
clone this repo
```

Be sure to execute the following command in the project folder to pull the needed submodules.
```
git submodule update --init --recursive
```

```
Copy platformio.ini.sample to platform.ini
```

```
Edit platformio.ini to match your system
```

```
Copy include/ssid.h.sample to include/ssid.h
```

```
Edit ssid.h to match your wifi
```

```
Press the PlatformIO Upload icon arrow at the bottom in the status bar.
This procedure will take some time to complete, and you should start seeing
some logging info of the project being built.
(See picture below)
```
![platformio_upload](/images/ml-build-1.png)

```
meatloaf should now be running on the device
```




