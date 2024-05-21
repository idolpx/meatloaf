#ifndef MEATLIB_FILESYSTEM_IEC_PIPE
#define MEATLIB_FILESYSTEM_IEC_PIPE

#include "meat_buffer.h"
#include "wrappers/iec_buffer.h"

/*
This is the main IEC named channel abstraction. Meatloaf keeps an array/map of these as long as they're alive
and removes them when channel is closed

- each named channel is connected to one file

- it allows pushing data to/from remote file

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
typedef enum {
    STATUS_OK = 0,
    STATUS_EOF,
    STATUS_NDA,
    STATUS_BAD,
} pipe_status_t;

class iecPipe {
    Meat::iostream* fileStream = nullptr;
    oiecstream iecStream;
    std::ios_base::openmode mode;

public:
    pipe_status_t status = STATUS_OK;

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
                // JAIME: This signals No Data Available!
                // It is also used for File Not Found
                // STATUS on the C64 side will be set appropriately
                //
                // PRZEM: What about Thom's SRQ? Would it be useble here?
                //
                // JAIME: I don't think so.  We are using SRQ to indicate that data is available to keep from polling on the C64 side
                // and that is only for our custom program. It would not work with a standard LOAD/SAVE.
                // We can set the status code and the virtual device using this class can read it and set the status on the command channel
                status = STATUS_NDA;
                IEC.senderTimeout();
            }
            else {
                // let's try to get and send next byte to IEC
                // we will peek next char in case ATN is pulled during attempt to write
                int next_char = fileStream->peek();
             
                if (next_char == EOF) {
                    if (fileStream->eof()) {
                        status = STATUS_EOF;
                    } else if (fileStream->fail()) {
                        status = STATUS_BAD;
                    } else {
                        status = STATUS_BAD;
                    }
                } else {
                    char ch = static_cast<char>(next_char);
                    iecStream.write(&ch, 1);
                    Debug_printf("%.2X ", ch);
                }

                // if ATN was pulled, we need to break the loop
                if(IEC.flags bitand ATN_PULLED) {
                    // PRZEM: set some status here?
                    break;
                }
                else {
                    // ATN wasn't pulled, so we can safely flush that byte we peeked from the buffer
                    char nextChar;
                    fileStream->get(nextChar);
                    status = STATUS_OK;
                }
            }
        }

        // the loop exited, it might be because:
        // - ATN was pulled?
        if(IEC.flags bitand ATN_PULLED) {
            // PRZEM: anything needs to happen here?
            
            // JAIME: We just need to make sure the pointer is set so the byte it was trying to send 
            // does not advance so a byte isn't skipped on resume
            //
            // PRZEM: nate that we are checking ATN_PULLED at the top of the loop. If it was pulled
            // we 1. won't read by fileStream->get(nextChar) and 2. won't send by iecStream.write(&nextChar, 1)
            // isn't that enough?

            // JAIME: That should be enough unless it pulls ATN while sending the byte.
        }
        // - read whole? Send EOI!
        else if(fileStream->eof()) {
            iecStream.close();
            status = STATUS_EOF;
        }
        // - error occured?
        else if(fileStream->bad()) {
            // PRZEM: can we somehow signal an error here?

            // JAIME: I think we used senderTimout() here too
            
            // PRZEM: So how do we know if this is a recoverable timeout (like a socket waiting for more data)
            // or a fatal error (like broken connection, removed diskette etc.)?

            // JAIME: We check the status on the command channel to determine the error code.
            // We need to have a status on this class that can be checked by our virtual device class and 
            // then it will set the status on the command channel based on the cause of the error
            IEC.senderTimeout();
            status = STATUS_BAD;
        }
    }

    // SAVE - pull C64 bytes (iecStream.get) to remote file (fileStream.put)
    void writeFile() {
        // this pipe wasn't initialized or wasn't meant for writing
        if(fileStream == nullptr || mode != std::ios_base::openmode::_S_out)
            return;
        
        // push bytes from C64 to file as long as they're available (eying ATN)
        while(!fileStream->bad() && !(IEC.flags bitand ATN_PULLED))
        {
            char nextChar;
            //iecStream.get(nextChar); TODO
            if(!iecStream.eof()) {
                fileStream->put(nextChar);
                status = STATUS_OK;
            }
        }
        // so here either the C64 stopped sending bytes to write or the output stream died
        // nothing to see here, move along!
    }
};

#endif /* MEATLIB_FILESYSTEM_IEC_PIPE */
