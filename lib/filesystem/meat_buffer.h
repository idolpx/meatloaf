#ifndef MEATLIB_FILESYSTEM_MEAT_BUFFER
#define MEATLIB_FILESYSTEM_MEAT_BUFFER

#include <memory>
#include <fstream>

#include "meat_io.h"

namespace Meat {
    /********************************************************
     * C++ Input MFile buffer
     ********************************************************/

    class mfilebuf : public std::filebuf {
        std::unique_ptr<MStream> mstream;
        std::unique_ptr<MFile> mfile;

        static const size_t ibufsize = 256;
        static const size_t obufsize = 256;
        char* ibuffer;
        char* obuffer;

    public:
        mfilebuf() {
            ibuffer = new char[ibufsize];
            obuffer = new char[obufsize];
        };

        ~mfilebuf() {
            if(obuffer != nullptr)
                delete[] obuffer;

            if(ibuffer != nullptr)
                delete[] ibuffer;

            close();
        }

        std::filebuf* doOpen() {
            // Debug_println("In filebuf open pre reset mistream");
            mstream.reset(mfile->meatStream());
            // Debug_println("In filebuf open post reset mistream");
            if(mstream->isOpen()) {
                // Debug_println("In filebuf open success!");
                this->setp(obuffer, obuffer+obufsize);
                return this;
            }
            else
                return nullptr;
        }

        std::filebuf* open(std::string filename) {
            // Debug_println("In filebuf open");
            mfile.reset(MFSOwner::File(filename));
            return doOpen();
        };

        std::filebuf* open(MFile* file) {
            mfile.reset(MFSOwner::File(file->url));
            return doOpen();
        };

        virtual void close() {
            if(is_open()) {
                // Debug_printv("closing in filebuf\n");
                sync();
                mstream->close();
            }
        }

        bool is_open() const {
            if(mstream == nullptr)
                return false;
            else
                return mstream->isOpen();
        }

        /**
         *  @brief  Fetches more data from the controlled sequence.
         *  @return  The first character from the <em>pending sequence</em>.
         *
         *  Informally, this function is called when the input buffer is
         *  exhausted (or does not exist, as buffering need not actually be
         *  done).  If a buffer exists, it is @a refilled.  In either case, the
         *  next available character is returned, or @c traits::eof() to
         *  indicate a null pending sequence.
         *
         *  For a formal definition of the pending sequence, see a good text
         *  such as Langer & Kreft, or [27.5.2.4.3]/7-14.
         *
         *  A functioning input streambuf can be created by overriding only
         *  this function (no buffer area will be used).  For an example, see
         *  https://gcc.gnu.org/onlinedocs/libstdc++/manual/streambufs.html
         *
         *  @note  Base class version does nothing, returns eof().
         */

        // https://newbedev.com/how-to-write-custom-input-stream-in-c


        int underflow() override {
            if (!is_open()) {
                return std::char_traits<char>::eof();
            }
            else if (this->gptr() == this->egptr()) {
                // the next statement assumes "size" characters were produced (if
                // no more characters are available, size == 0.
                //auto buffer = reader->read();

                auto readCount = mstream->read((uint8_t*)ibuffer, ibufsize);

                //Debug_printv("--mfilebuf underflow, read bytes=%d--", readCount);

                this->setg(ibuffer, ibuffer, ibuffer + readCount);
            }
            // eback = beginning of get area
            // gptr = current character (get pointer)
            // egptr = one past end of get area

            return this->gptr() == this->egptr()
                ? std::char_traits<char>::eof()
                : std::char_traits<char>::to_int_type(*this->gptr());
        };



        /**
         *  @brief  Consumes data from the buffer; writes to the
         *          controlled sequence.
         *  @param  __c  An additional character to consume.
         *  @return  eof() to indicate failure, something else (usually
         *           @a __c, or not_eof())
         *
         *  Informally, this function is called when the output buffer
         *  is full (or does not exist, as buffering need not actually
         *  be done).  If a buffer exists, it is @a consumed, with
         *  <em>some effect</em> on the controlled sequence.
         *  (Typically, the buffer is written out to the sequence
         *  verbatim.)  In either case, the character @a c is also
         *  written out, if @a __c is not @c eof().
         *
         *  For a formal definition of this function, see a good text
         *  such as Langer & Kreft, or [27.5.2.4.5]/3-7.
         *
         *  A functioning output streambuf can be created by overriding only
         *  this function (no buffer area will be used).
         *
         *  @note  Base class version does nothing, returns eof().
         */


        // // https://newbedev.com/how-to-write-custom-input-stream-in-c

        int overflow(int ch  = traits_type::eof()) override
        {

            //Debug_printv("in overflow");
                // pptr =  Returns the pointer to the current character (put pointer) in the put area.
                // pbase = Returns the pointer to the beginning ("base") of the put area.
                // epptr = Returns the pointer one past the end of the put area.

            if (!is_open()) {
                return EOF;
            }

            char* end = this->pptr();
            if ( ch != EOF ) {
                *end ++ = ch;
            }

            //Debug_printv("%d bytes in buffer will be written", end-pbase());

            uint8_t* pBase = (uint8_t*)this->pbase();

            if ( mstream->write( pBase, end - this->pbase() ) == 0 ) {
                ch = EOF;
            } else if ( ch == EOF ) {
                ch = 0;
            }
            this->setp(obuffer, obuffer+obufsize);
            
            return ch;
        };

        std::streampos seekpos(std::streampos __pos, std::ios_base::openmode __mode = std::ios_base::in | std::ios_base::out) override {
            std::streampos __ret = std::streampos(off_type(-1));

            if(mstream->seek(__pos)) {
                //__ret.state(_M_state_cur);
                __ret = std::streampos(off_type(__pos));
                // probably requires resetting setg!
                this->setg(ibuffer, ibuffer, ibuffer);
                this->setp(obuffer, obuffer+obufsize);
            }

            return __ret;
        }

        /**
         *  @brief  Synchronizes the buffer arrays with the controlled sequences.
         *  @return  -1 on failure.
         *
         *  Each derived class provides its own appropriate behavior,
         *  including the definition of @a failure.
         *  @note  Base class version does nothing, returns zero.
         * 
         * sync: Called on flush, should output any characters in the buffer to the sink. 
         * If you never call setp (so there's no buffer), you're always in sync, and this 
         * can be a no-op. overflow or uflow can call this one, or both can call some 
         * separate function. (About the only difference between sync and uflow is that 
         * uflow will only be called if there is a buffer, and it will never be called 
         * if the buffer is empty. sync will be called if the client code flushes the stream.)
         */
        int sync() override { 
            //Debug_printv("in wrapper sync");
            
            if(this->pptr() == this->pbase()) {
                return 0;
            }
            else {
                uint8_t* buffer = (uint8_t*)this->pbase();

                // pptr =  Returns the pointer to the current character (put pointer) in the put area.
                // pbase = Returns the pointer to the beginning ("base") of the put area.
                // epptr = Returns the pointer one past the end of the put area.
                

                //Debug_printv("before write call, mostream!=null[%d]", mostream!=nullptr);
                auto result = mstream->write(buffer, this->pptr()-this->pbase()); 
                //Debug_printv("%d bytes left in buffer written to sink, rc=%d", pptr()-pbase(), result);

                this->setp(obuffer, obuffer+obufsize);

                return (result != 0) ? 0 : -1;  
            }  
        };
    };


/********************************************************
 * C++ Input MFile stream
 ********************************************************/
// https://stdcxx.apache.org/doc/stdlibug/39-3.html

    class ifstream : public std::istream {
        mfilebuf buff;
        std::string url; 

    public:

        ifstream(const ifstream &copied) : std::ios(0),  std::istream( &buff ) {
            
        }

        ifstream(std::string u) : std::ios(0),  std::istream( &buff ), url(u) {
            auto f = MFSOwner::File(u);
            delete f;
        };
        ifstream(MFile* file) : std::ios(0),  std::istream( &buff ), url(file->url) {
        };

        ~ifstream() {
            //Debug_printv("ifstream destructor");
            close();
        }

        virtual void open() {
            //Debug_printv("ifstream open %s", url.c_str());
            buff.open(url);
        }

        virtual void close() {
            //Debug_printv("ifstream close for %s bufOpen=%d\n", url.c_str(), buff.is_open());
            buff.close();
            //Debug_printv("ifstream AFTER close for %s\n", url.c_str());
        }

        virtual bool is_open() {
            return buff.is_open();
        }

        U8Char getUtf8() {
            U8Char codePoint(this);
            return codePoint;
        }

        char getPetscii() {
            return getUtf8().toPetscii();
        }
    };

    // ifstream& operator>>(ifstream& is, U8Char& c) {
    //     //U8Char codePoint(this);
    //     c = U8Char(&is);
    //     return is;
    // }


/********************************************************
 * C++ Output MFile stream
 ********************************************************/
// https://stdcxx.apache.org/doc/stdlibug/39-3.html

    class ofstream : public std::ostream {
        mfilebuf buff;
        std::string url;

    public:
        ofstream(const ofstream &copied) : std::ios(0), std::ostream( &buff )
        { 
            
        }
        ofstream(std::string u) : std::ios(0), std::ostream( &buff ), url(u) {};
        ofstream(MFile* file) : std::ios(0), std::ostream( &buff ), url(file->url) {};

        ~ofstream() {
            close();
        };

        virtual void open() {
            buff.open(url);
        }

        virtual void close() {
            buff.close();
        }

        virtual bool is_open() {
            return buff.is_open();
        }

        void putPetscii(char c) {
            U8Char wide = U8Char(c);
            (*this) << wide.toUtf8();
        }
    };
}

#endif /* MEATLIB_FILESYSTEM_MEAT_BUFFER */
