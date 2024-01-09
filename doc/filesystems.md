# Introduction
One of the main Meatloaf functionalities is converting any stream of data to IEC protocol, this is done using a "filesystem". Filesystem in context of Meatloaf is an abstraction that can provide directories (that you can `LOAD"$",8` and `LIST`) or file streams (that you can `LOAD` or `OPEN` to interact with them).
If you used other devices like SD2IEC it might be obvious for you that it should be possible to load a file from a D64 image stored on your SD card. But what about loading a program from a D64 stored on HTTP server? Or inside a ZIP? Or maybe straight from a D64, stored in a RAR archive which someone put on a SMB server? Well, Meatloaf makes it really easy. While archive, SMB, D64 and other filesystems are already implemented for you, there might be some services out there that could be turned into a file system, that you could list and load straight from BASIC v2 and it is very easy to implement them using Meatloaf API.
## What are data streams?
Think about these examples:
- A HTTP server allows loading (GET) and saving (PUT) of files, but has no way to list a directory
- A Web Socket is a stream that provides input and output data
- A telnet server is also just a file you can write to and read from, but there is not a directory you can list
## What are directories?
Think about things you can enumerate or list:
- A ZIP or RAR file can be both listed and used to obtain data of files stored within it
- A D64 file contains a list of files, which can be read or written
- A FTP server can list files and allows loading and saving them
- A WebDAV server does all that HTTP can do, plus list directories
- A SD card can contains files and directories, which you can list
# Implementing a new filesystem - streams
Implementing a new filesystem consists of two parts:
- implementing directory listing (if applicable)
- implementing input and output streams (possibly seekable by different means)
## Source, Decoder and Bottom streams
Imagine you are implementing a filesystem for accessing contents of a ZIP archive (you don't have to, as it already exists). This ZIP might lie anywhere - on HTTP, FTP, SMB, DropBox, Google Drive - you name it, but you shouldn't really care where this archive is located, all you care about is the contents of the ZIP, its _Source Data Stream_. By reading this stream, you can find individual file headers and provide a _Decoder Stream_ that allows accessing each file and its uncompressed byte stream. There are very rare cases when you want to implement a file system that cannot be hosted inside another file system, for example: can you have a HTTP filesystem on an SD card? Nope, only Chuck Norris knows how to do that. Can you have an FTP within a ZIP? Nope, that doesn't make any sense. Such "root" file system is called _Bottom Stream_ in the below documentation.
Examples of Meatloaf _Bottom Streams_ are:
- local flash file system (SD card is mounted into this filesystem)
- HTTP
- SMB
- FTP
- TCP
- UDP
- TELNET
- WS (websocket)
- Implementation of some API with its own protocol URL

## getSourceStream
This method provides _Source Stream_ for your filesystem. The `getSourceStream` method default implementation does exactly that - provides you with a source stream of bytes on which you will be building your _Decoder Stream_. The default implementation of this method does all the magic of giving you the data, regardless of where your source file (ZIP archive in our example) sits, on FTP, HTTP, inside D64 on HTTP, inside a D64 on a RAR in SMB...
So why do we mention this method? Well, there's only one case when you have to override it - when implementing a _Bottom Stream_! So, if you're developing such "root" filesystem, all this method has to return is a stream that allows reading from this very file system. 

It's also important to understand how Meatloaf resolves the URL path when trying to prepare such _Source Stream_. Let's assume, an user tries to obtain a stream of such file:

```http://some-server.com/storage/new_files/copy_party_2024.zip/g/gta64/disk1.d64/start```

Meatloaf will scann the path __from right to left__ to match topmost filesystem that will serve the stream. Going from right:

- `start` will not match anything
- `disk1.d64` will match a D64 filesystem, so this will be file start in D64 filesystem, but since this filesystem requires a _Source Stream_, the scan will continue left:
- `gta64` will match nothing
- `g` will match nothing
- `copy_party_2024.zip` will match archive filesystem, so the _Source Stream_ for the D64 will be a file named `g/gta64/disk1.d64` inside the ZIP filesystem, now we need _Source Stream_ for the archive, continuing left:
- `new_files` will match nothing
- `storage` will match nothing
- `some-server.com` will match nothing
- `http` will match HTTP filesystem, which will be the _Source Stream_ for the archive filesystem

This will create a hierarchy of Russian-doll like streams, looking more or less like this: `FileStream(D64Stream(ArchiveStream(HttpStream)))`

## getDecodedStream
This is the method you have to implement, when creating your own container streams that might be placed inside other streams. Example of such container streams are:
- Archive files: ZIP, RAR, 7Z etc.
- D64, D81 image files
- TAR files
- Any stream that that contains data about smaller parts inside and is either browsable, seekable or might be re-read from start

This method accepts _Source Data Stream_ as input and outputs a _Decoder Data Stream_ that allows (for example) random access to the data this file system represents (uncompressed ZIP files in our example)
# Implementing new filesystem - directories
TODO