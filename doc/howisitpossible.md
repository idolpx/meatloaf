# How is it even possible?
In short: thanks to the genius way IEC protocol was implemented. Think how early MS-DOS files were limited to only 8 character file names and now realize that IEC allows arbitrary length of file names! So what really happens when you issue the command:
```
LOAD"SPYVSPSY",8
```
is that your Commodore just broadcasts a message over the IEC bus:
```
Hey! If there's anything on my IEC bus at address 8, I would like this thing to return me a stream of bytes of a file named "spyvsspy". 
```
If that thing happens to be a disk drive, like Commodore 1541, the drive firmware will process that message and if this very drive happens to be assigned address 8, it will spin the diskette and look if there's a file named "spyvspy" on it. If it is - it will start streaming the file data, if there isn't such file - it will just say "sorry, pal", and your Commodore will display `?FILE NOT FOUND  ERROR`.
So if you issue this command:
```
LOAD"HTTPS://SOMESERVER.COM/MR.ROBOT",8
```
Your computer will broadcast this over IEC:
```
Hey! If there's anything on my IEC bus at address 8, I would like this thing to return me a stream of bytes of a file named "https://someserver.com/mr.robot". 
```
And if your drive is 1541, exactly the same will happen: the drive will look for a file named "https;//someserver.com/mr.robot" on the diskette, and since it won't be there (as diskettes allow only 16 characters in the file name!), it will signal an error. But if 1541 was smart enough to know what "https" means and was able to connect to the Internet, it could just issue a HTTP GET command on "mr.robot" file on "someserver,com" and stream its contents back, exactly the same way it would stream a file from diskette! And that's exactly what Meatloaf does!
