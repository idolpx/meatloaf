// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

// I2C:// - 2-Wire Hardware Bus (ESP-IDF driver)
//
// URL scheme: i2c://{ADDRESS}/{REGISTER}
//   i2c://           enumerate all devices found on the I2C bus
//   i2c://48         device at address 0x48 (hex, 0x prefix optional)
//   i2c://48/00      register 0x00 of device 0x48 (readable/writable stream)
//
// Addresses and registers are always interpreted as hexadecimal.
// Standard-mode 100 kHz, internal pull-ups enabled.
//
// https://www.nxp.com/docs/en/user-guide/UM10204.pdf
// https://ww1.microchip.com/downloads/aemDocuments/documents/MPD/ProductDocuments/DataSheets/24AA32A-24LC32A-32-Kbit-I2C-Serial-EEPROM-DS20001713.pdf
//

#ifndef MEATLOAF_DEVICE_I2C
#define MEATLOAF_DEVICE_I2C

#include <string>
#include <memory>

#include "driver/i2c.h"
#include "esp_err.h"

#include "meatloaf.h"
#include "meat_session.h"
#include "string_utils.h"
#include "../../include/pinmap.h"

// ---------------------------------------------------------------------------
// Default SDA/SCL pins — derived from board pin-map when available.
// Override via build flags (-D I2C_ML_SDA=GPIO_NUM_21) if needed.
// ---------------------------------------------------------------------------
#ifndef I2C_ML_SDA
#  if defined(PIN_GPIOX_SDA) && (PIN_GPIOX_SDA != GPIO_NUM_NC)
#    define I2C_ML_SDA  PIN_GPIOX_SDA
#  else
#    define I2C_ML_SDA  GPIO_NUM_21
#  endif
#endif

#ifndef I2C_ML_SCL
#  if defined(PIN_GPIOX_SCL) && (PIN_GPIOX_SCL != GPIO_NUM_NC)
#    define I2C_ML_SCL  PIN_GPIOX_SCL
#  else
#    define I2C_ML_SCL  GPIO_NUM_22
#  endif
#endif

#ifndef I2C_ML_PORT
#  define I2C_ML_PORT     I2C_NUM_0
#endif

#define I2C_ML_FREQ_HZ      100000   // 100 kHz standard mode
#define I2C_ML_TIMEOUT_MS   50

// Valid 7-bit I2C address range (reserved addresses excluded).
#define I2C_SCAN_ADDR_MIN   0x08
#define I2C_SCAN_ADDR_MAX   0x77


/********************************************************
 * I2CMSession — manages the I2C master driver lifecycle.
 * One session per I2C port, shared across all device streams.
 *
 * SessionBroker key: "i2c://:<port_num>"  e.g. "i2c://:0" for I2C_NUM_0.
 * host is unused; port carries the i2c_port_t value (0 or 1).
 ********************************************************/

class I2CMSession : public MSession {
public:
    I2CMSession(std::string host, uint16_t port = 0);
    ~I2CMSession() override;

    static std::string getScheme() { return "i2c"; }

    // MSession interface
    bool connect()     override;
    void disconnect()  override { connected = false; }
    bool keep_alive()  override { return connected; }

    // I2C bus operations
    bool      probe(uint8_t addr);
    esp_err_t read (uint8_t addr, uint8_t reg, bool has_reg, uint8_t*       buf, size_t len);
    esp_err_t write(uint8_t addr, uint8_t reg, bool has_reg, const uint8_t* buf, size_t len);

private:
    i2c_port_t _i2c_port;
};


/********************************************************
 * I2CMStream — byte stream to/from one I2C device/register.
 ********************************************************/

class I2CMStream : public MStream {
public:
    explicit I2CMStream(std::string path);
    ~I2CMStream() override;

    uint32_t size()      override { return 0; }
    uint32_t available() override { return _opened ? 1 : 0; }
    uint32_t position()  override { return _position; }
    size_t   error()     override { return _error; }
    bool     seek(uint32_t pos) override { _position = pos; return true; }

    bool     isOpen() override;
    bool     open(std::ios_base::openmode mode) override;
    void     close() override;
    uint32_t read (uint8_t* buf, uint32_t size) override;
    uint32_t write(const uint8_t* buf, uint32_t size) override;

private:
    uint8_t  _address     = 0;
    uint8_t  _reg         = 0;
    bool     _has_address = false;
    bool     _has_reg     = false;
    bool     _opened      = false;
    std::shared_ptr<I2CMSession> _session;

    void _parse_url(const std::string& path);
};


/********************************************************
 * I2CMFile — directory/file node for the I2C bus.
 ********************************************************/

class I2CMFile : public MFile {
public:
    explicit I2CMFile(std::string path);

    std::shared_ptr<MStream> getSourceStream(std::ios_base::openmode mode = std::ios_base::in) override;
    std::shared_ptr<MStream> getDecodedStream(std::shared_ptr<MStream> src) override { return src; }
    std::shared_ptr<MStream> createStream(std::ios_base::openmode mode) override;

    // URL without a register is treated as a directory:
    //   i2c://       → root (bus scan)
    //   i2c://48     → device directory (empty listing)
    //   i2c://48/00  → file (register stream)
    bool isDirectory() override { return !_has_reg; }

    bool     rewindDirectory()     override;
    MFile*   getNextFileInDir()    override;
    bool     exists()              override;

    time_t   getLastWrite()        override { return 0; }
    time_t   getCreationTime()     override { return 0; }

private:
    uint8_t  _address     = 0;
    uint8_t  _reg         = 0;
    bool     _has_address = false;
    bool     _has_reg     = false;
    int      _scan_addr   = I2C_SCAN_ADDR_MIN;
    std::shared_ptr<I2CMSession> _session;

    void _parse_fields();
};


/********************************************************
 * I2CMFileSystem — factory for the i2c:// scheme.
 ********************************************************/

class I2CMFileSystem : public MFileSystem {
public:
    I2CMFileSystem() : MFileSystem("i2c") { isRootFS = true; }

    bool   handles(std::string name) override { return mstr::startsWith(name, (char*)"i2c:", false); }
    MFile* getFile(std::string path) override;
};


#endif // MEATLOAF_DEVICE_I2C
