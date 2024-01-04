[![Meatloaf (C64/C128/VIC20/+4)](images/meatloaf.logo.png)](https://meatloaf.cc)

This is the ESP32 version of [Meatloaf](https://github.com/idolpx/meatloaf) intended for the [FujiNet](https://github.com/FujiNetWIFI/) bring-up on the Commodore 64.

# What is Meatloaf?

## A cloud disk drive

While one can say Meatloaf is just another Commodore IEC Serial Floppy Drive similar to SD2IEC and its clones, Meatloaf is in fact much more than that, as it allows you to load not only local files stored on its internal flash file system or SD card, but also files from any URL you can imagine, straight into your Commodore computer without any additional software. For example you can load a game from some web server:

```BASIC
LOAD"HTTP://C64.ORG/GAMES_AZ/H/H.E.R.O.PRG",8
```

Or from a D64 image on your own Windows/Samba server (all known CBM image formats supported):

```BASIC
LOAD"SMB://STORAGE/C64/FAVORITES/PIRATES/PIRATES_A.D64/*",8
```

Or even straight from a D64 image residing inside ZIP archive that is located somewhere on the Internet:

```BASIC
LOAD"HTTP://C64WAREZ.NET/DROPZONE/FRESH_WAREZ.ZIP/GTA64.D64/START.PRG",8
```

## A NAS with WebDAV

If you rather keep your files locally, you can easily upload them to Meatloaf local file system via built-in WebDAV server. No need to shuffle SDCARD between your PC and Commodore!

## The best network adapter for Commodore ever built!

Of course - any URL could be opened using Kernal or BASIC I/O commands like `OPEN`, `CLOSE`, `PRINT#`, `GET#` or `INPUT#`, meaning you don't need a modem or a network card with dedicated application anymore! Just open a byte stream via a TCP socket:

```BASIC
10 OPEN 1,8,1,"TCP://64VINTAGEBBS.DYNDNS.ORG"
```

Or via HTTP REST API:

```BASIC
10 OPEN 1,8,1,"HTTPS://API.OPENAI.COM/V1/COMPLETIONS"
```

And read/write into it like it was a file on a diskette!

But that's not all. Since Meatloaf has built-in JSON parser you can easily interact with web services straight from your BASIC v2, by just using `OPEN`, `PRINT#`, `INPUT#` and `GET#` and CBM-style dos commands to set HTTP headers and query JSON fields!

```BASIC
610 OPEN 15,8,15
611 OPEN 1,8,2,"HTTPS://API.OPENAI.COM/V1/CHAT/COMPLETIONS"
630 REM *** ADD REQUEST HEADERS
632 PRINT#15, "H+:CONTENT-TYPE:APPLICATION/JSON"
633 PRINT#15, "H+:AUTHORIZATION:BEARER "+KE$
```

Current internet enabled programs include:

- ISS tracking application
- Chuck Norris jokes client
- Terminal client
- You can find more internet apps using `LOAD"ML:$",8`

## Many devices in one

Meatloaf is not limited to be just your standard drive 8, you can configure it to respond to any number of Commodore-DOS devices, from 4 to 30, at the same time.

# Instructions

## Build and installation

Visual studio code, and the platformio addon installed from the vscode store required.

- clone this repo
- Copy platformio.ini.sample to platformio.ini
- Edit platformio.ini to match your device and default wifi settings
- Upload filesystem image by clicking the alien head on left panel and selecting `lolin d32 pro` then `Platform` and then `Build Filesystem Image` and `Upload Filesystem Image` (this has to be done only once!)
- Press the PlatformIO Upload icon arrow at the bottom in the status bar. This procedure will take some time to complete, and you should start seeing some logging info of the project being built (See picture below)

![platformio_upload](/images/ml-build-1.png)

- meatloaf should now be running on the device

## Getting a listing of a HTTP server

HTTP doesn't support listing files, but since for Commodore a directory is just a file you can `LOAD`, it's enough to add this tiny PHP script to your HTTP server to get a file listing as if you were reading a diskette!
All you need is some web space on a server with PHP enabled.
Just drop the following script in a directory with all your files and name it 'index.php'

[Meatloaf PHP Server Script](https://gist.github.com/idolpx/ab8874f8396b6fa0d89cc9bab1e4dee2)

Once that is done just you can get a directory listing on your C64 with Meatloaf with a standard LOAD command, as the script just serves `$` file formatted as BASIC v2 code.

```
LOAD"HTTP://YOURDOMAIN.COM/PATH",8
```
