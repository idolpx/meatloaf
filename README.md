[![Meatloaf (C64/C128/VIC20/+4)](images/meatloaf.logo.png)](https://meatloaf.cc)

This is the ESP32 version of [Meatloaf](https://github.com/idolpx/meatloaf) intended for the [FujiNet](https://github.com/FujiNetWIFI/) bring-up on the Commodore 64.

## Quick Instructions

Visual studio code, and the platformio addon installed from the vscode store required.

```
clone this repo
```

```
Copy platformio.ini.sample to platformio.ini
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

# Want to host your own files on a webserver?

All you need is some web space on a server with PHP enabled.
Just drop the following script in a directory with all your files and name it 'index.php'

[Meatloaf PHP Server Script](https://gist.github.com/idolpx/ab8874f8396b6fa0d89cc9bab1e4dee2)

Once that is done just you can get a directory listing on your C64 with Meatloaf with a standard LOAD command.

```
LOAD"HTTP://YOURDOMAIN.COM/PATH",8
```





