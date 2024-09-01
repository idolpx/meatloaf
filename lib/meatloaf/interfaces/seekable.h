
#include <cstddef>
#include <cstdint>

#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

class Seekable
{
protected:
    uint32_t _size = 0;
    uint32_t _position = 0;
    uint8_t _error = 0;

public:
    size_t block_size = 256;

    virtual uint32_t size() {
        return _size;
    };

    virtual uint32_t available() {
        return _size - _position;
    };

    virtual uint32_t position() {
        return _position;
    }
    virtual void position( uint32_t p) {
        _position = p;
    }

    virtual size_t error() {
        return _error;
    }

    virtual bool eos()  {
//        Debug_printv("_size[%d] m_bytesAvailable[%d] _position[%d]", _size, available(), _position);
        if ( available() == 0 )
            return true;
        
        return false;
    }
    virtual void reset() 
    {
        _size = block_size;
        _position = 0;
    };

    virtual bool seek(uint32_t pos, int mode) {
        if(mode == SEEK_SET) {
            return seek(pos);
        }
        else if(mode == SEEK_CUR) {
            if(pos == 0) return true;
            return seek(position()+pos);
        }
        else {
            return seek(size() - pos);
        }
    }
    virtual bool seek(uint32_t pos) = 0;
};