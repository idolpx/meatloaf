#ifndef MEATLIB_FILESYSTEM_IEC_PIPE
#define MEATLIB_FILESYSTEM_IEC_PIPE

#include "meat_buffer.h"
#include "wrappers/iec_buffer.h"

/*
This is the main IEC named channel abstraction. Meatloaf keeps an array/map of these as long as they're alive
and removes them when channel is closed

- each named channel is connected to one file

- it allows pusing data to/from remote file

- when ATN is received the transfer stops and will be seamlesly resumed on next call to send/receive

- resuming transfer is possible as long as this channel is open

Example usage:

// c64 requests a remote file "http://address/file.prg"
auto newPipe = new iecPipe();
newPipe->establish("http://address/file.prg", std::ios_base::openmode::_S_in, iec);

// store the pipe in your table of channels
.....

// start transfer
newPipe->readFile();
// this method returns only when either happened:
// 1) the file was read wholly so you have to call newPipe->finish() and remove it from your table of channles
// 2) ATN was pulled, so if reading from this channel is later requested again, you fetch it from
//    your table of channels and call: 
//    storedPipe->readFile()
//    you can readFile() as many times as needed, and it will do what you expect (resume from where it was interrupted)
//    as long as it stays open and not EOF.


_S_out == C64 -> IEC -> file
_S_in  == file -> IEC -> C64

*/
class iecPipe {
    Meat::iostream* fileStream = nullptr;
    oiecstream iecStream;
    std::ios_base::openmode mode;

public:

    // you call this once, when C64 requested to open the stream
    bool establish(std::string filename, std::ios_base::openmode m, systemBus* i) {
        if(fileStream != nullptr)
            return false;

        mode = m;
        iecStream.open(i);

        fileStream = new Meat::iostream(filename.c_str(), mode);
    }

    // this stream is disposed of, nothing more will happen with it!
    void finish() {
        if(fileStream != nullptr)
            delete fileStream;

        fileStream = nullptr;
        iecStream.close(); // will push last byte and send EOI (if hasn't been closed yet)
    }

    bool seek(std::streampos p) {
        if(mode == std::ios_base::openmode::_S_in) {
            // we are LOADing, so we need to seek within the source file input stream (get buffer):
            fileStream->seekg(p);
            // our iec stream put buffer can potentially contain more bytes ready for read, but since we are
            // seeking, we need to flush them, so they don't get sent to C64 as if they were read from file
            iecStream.flushpbuff(); 
        }
        else if(mode == std::ios_base::openmode::_S_out) {
            // we are SAVE-ing, so, we need to seek within the destination file output stream (put buffer):
            fileStream->seekp(p);
            //iecStream.flushgbuff() - not sure if it will be required
        }

        return false;
    }

    // LOAD - push file bytes (fileStream.get) to C64 (iecStream.write)
    void readFile() {
        // this pipe wasn't itialized or wasn't meant for reading
        if(fileStream == nullptr || mode != std::ios_base::openmode::_S_in)
            return;
        
        // push bytes from file to C64 as long as they're available (eying ATN)
        while(!fileStream->eof() && !fileStream->bad() && !(IEC.flags bitand ATN_PULLED))
        {
            // If this is a socket stream, it might send NDA ("I have no data for you, but keep asking"), so lte's check it!
            (*fileStream).checkNda();
            if((*fileStream).nda()) {
                // JAIME: So you told me there's a way to signal "no data available on IEC", here's the place
                // to send it:

                // TODO
            }
            else {
                // let's try to get and send next byte to IEC
                char nextChar;
                //(*fileStream).get(nextChar);
                fileStream->get(nextChar);
                // EOF could be set after this get, so
                if(!fileStream->eof()) {
                    iecStream.write(&nextChar, 1);
                    Debug_printf("%.2X ", nextChar);
                }
            }
        }

        // the loop exited, it might be because:
        // - ATN was pulled?
        if(IEC.flags bitand ATN_PULLED) {
            // JAIME: anything needs to happen here?
        }
        // - read whole? Send EOI (automatically by the buffer)
        else if(fileStream->eof())
            iecStream.close();
        // - error occured?
        else if(fileStream->bad()) {
            // JAIME: can we somehow signal an error here?
        }
    }

    // SAVE - pull C64 bytes (iecStream.get) to remote file (fileStream.put)
    void writeFile() {
        // this pipe wasn't itialized or wasn't meant for writing
        if(fileStream == nullptr || mode != std::ios_base::openmode::_S_out)
            return;
        
        // push bytes from C64 to file as long as they're available (eying ATN)
        while(!fileStream->bad() && !(IEC.flags bitand ATN_PULLED))
        {
            char nextChar;
            //iecStream.get(nextChar); TODO
            if(!iecStream.eof()) {
                //(*fileStream).put('a');
                fileStream->put(nextChar);
            }
        }
        // so here either the C64 stopped sending bytes to write or the outpt stream died
        // nothing to see here, move along!
    }
};

#endif /* MEATLIB_FILESYSTEM_IEC_PIPE */
