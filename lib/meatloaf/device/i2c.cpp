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

#include "i2c.h"

#include <cstdlib>
#include <cstdio>

#include "meat_session.h"
#include "peoples_url_parser.h"
#include "../../include/debug.h"

// Parse a hex string ("48" or "0x48") as a uint8_t.
static uint8_t i2c_parse_hex8(const std::string& s) {
    if (s.empty()) return 0;
    return static_cast<uint8_t>(strtoul(s.c_str(), nullptr, 16));
}


/********************************************************
 * I2CMSession
 ********************************************************/

I2CMSession::I2CMSession(std::string /*host*/, uint16_t port)
    : MSession("i2c://:" + std::to_string(port), "", port)
    , _i2c_port(static_cast<i2c_port_t>(port))
{
    // I2C bus persists indefinitely — disable SessionBroker expiry.
    setKeepAliveInterval(0);
}

I2CMSession::~I2CMSession() {
    disconnect();
}

bool I2CMSession::connect() {
    if (connected) return true;

    i2c_config_t conf = {};
    conf.mode             = I2C_MODE_MASTER;
    conf.sda_io_num       = I2C_ML_SDA;
    conf.scl_io_num       = I2C_ML_SCL;
    conf.sda_pullup_en    = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en    = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_ML_FREQ_HZ;

    esp_err_t err = i2c_param_config(_i2c_port, &conf);
    if (err != ESP_OK) {
        Debug_printv("I2C session: param_config failed (0x%x)", err);
        return false;
    }

    err = i2c_driver_install(_i2c_port, I2C_MODE_MASTER, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        // Another component already owns this port (e.g. PCF8575) — reuse it.
        Debug_printv("I2C session: port %d already initialised, reusing", _i2c_port);
    } else if (err != ESP_OK) {
        Debug_printv("I2C session: driver_install failed (0x%x)", err);
        return false;
    }

    connected = true;
    updateActivity();
    Debug_printv("I2C session: port %d ready (SDA=%d SCL=%d)", _i2c_port, I2C_ML_SDA, I2C_ML_SCL);
    return true;
}

bool I2CMSession::probe(uint8_t addr) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(_i2c_port, cmd, pdMS_TO_TICKS(I2C_ML_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return (err == ESP_OK);
}

esp_err_t I2CMSession::read(uint8_t addr, uint8_t reg, bool has_reg,
                             uint8_t* buf, size_t len) {
    if (len == 0) return ESP_OK;

    if (has_reg) {
        // Write register address, repeated-start, then read.
        return i2c_master_write_read_device(
            _i2c_port, addr,
            &reg, 1,
            buf, len,
            pdMS_TO_TICKS(I2C_ML_TIMEOUT_MS));
    }

    // Raw read — no register prefix.
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (len > 1)
        i2c_master_read(cmd, buf, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, buf + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(_i2c_port, cmd, pdMS_TO_TICKS(I2C_ML_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return err;
}

esp_err_t I2CMSession::write(uint8_t addr, uint8_t reg, bool has_reg,
                              const uint8_t* buf, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    if (has_reg)
        i2c_master_write_byte(cmd, reg, true);
    if (len > 0)
        i2c_master_write(cmd, const_cast<uint8_t*>(buf), len, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(_i2c_port, cmd, pdMS_TO_TICKS(I2C_ML_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return err;
}


/********************************************************
 * I2CMStream
 ********************************************************/

I2CMStream::I2CMStream(std::string path) : MStream(path) {
    _parse_url(path);
}

I2CMStream::~I2CMStream() {
    close();
}

bool I2CMStream::isOpen() {
    return _opened && _session && _session->isConnected();
}

bool I2CMStream::open(std::ios_base::openmode /*mode*/) {
    if (isOpen()) return true;
    if (!_has_address) {
        Debug_printv("I2C stream: no device address in URL [%s]", url.c_str());
        return false;
    }

    _session = SessionBroker::obtain<I2CMSession>("", I2C_ML_PORT);
    if (!_session) {
        Debug_printv("I2C stream: failed to obtain session");
        return false;
    }

    if (!_session->probe(_address)) {
        Debug_printv("I2C stream: no ACK from device 0x%02X", _address);
        _session.reset();
        return false;
    }

    _session->acquireIO();
    _opened = true;
    return true;
}

void I2CMStream::close() {
    if (_session) {
        _session->releaseIO();
        _session.reset();
    }
    _opened = false;
}

uint32_t I2CMStream::read(uint8_t* buf, uint32_t size) {
    if (!isOpen() && !open(std::ios_base::in)) return 0;
    if (!buf || size == 0) return 0;

    esp_err_t err = _session->read(_address, _reg, _has_reg, buf, size);
    if (err != ESP_OK) {
        Debug_printv("I2C read 0x%02X/0x%02X failed (0x%x)", _address, _reg, err);
        _error = 1;
        return 0;
    }
    _position += size;
    return size;
}

uint32_t I2CMStream::write(const uint8_t* buf, uint32_t size) {
    if (!isOpen() && !open(std::ios_base::out)) return 0;
    if (!buf || size == 0) return 0;

    esp_err_t err = _session->write(_address, _reg, _has_reg, buf, size);
    if (err != ESP_OK) {
        Debug_printv("I2C write 0x%02X/0x%02X failed (0x%x)", _address, _reg, err);
        _error = 1;
        return 0;
    }
    _position += size;
    return size;
}

void I2CMStream::_parse_url(const std::string& path) {
    auto p = PeoplesUrlParser::parseURL(path);
    if (!p) return;

    if (!p->host.empty()) {
        _address     = i2c_parse_hex8(p->host);
        _has_address = true;
    }
    // Strip leading slashes to isolate the register string.
    std::string cmd = p->path;
    while (!cmd.empty() && cmd.front() == '/') cmd.erase(cmd.begin());
    if (!cmd.empty()) {
        _reg     = i2c_parse_hex8(cmd);
        _has_reg = true;
    }
}


/********************************************************
 * I2CMFile
 ********************************************************/

I2CMFile::I2CMFile(std::string path) : MFile(path) {
    _parse_fields();
    isWritable = true;

    if (!_has_address) {
        // Root node — represents the entire I2C bus.
        media_header = "I2C BUS";
        media_id     = "00 2A";
    }
}

std::shared_ptr<MStream> I2CMFile::getSourceStream(std::ios_base::openmode mode) {
    return createStream(mode);
}

std::shared_ptr<MStream> I2CMFile::createStream(std::ios_base::openmode mode) {
    auto stream = std::make_shared<I2CMStream>(url);
    stream->open(mode);
    return stream;
}

bool I2CMFile::rewindDirectory() {
    _scan_addr = I2C_SCAN_ADDR_MIN;
    _session   = SessionBroker::obtain<I2CMSession>("", I2C_ML_PORT);
    return (_session != nullptr);
}

MFile* I2CMFile::getNextFileInDir() {
    // Device-level directory is intentionally empty — the user navigates to
    // specific registers directly (e.g. cd i2c://48/00).
    if (_has_address || !_session) return nullptr;

    // Bus-root scan: probe each address in the valid range.
    while (_scan_addr <= I2C_SCAN_ADDR_MAX) {
        uint8_t addr = static_cast<uint8_t>(_scan_addr++);
        if (_session->probe(addr)) {
            char entry[8];
            snprintf(entry, sizeof(entry), "%02X", addr);
            auto f = new I2CMFile("i2c://" + std::string(entry));
            f->name       = entry;
            f->size       = 0;
            f->isWritable = true;
            return f;
        }
    }
    return nullptr;
}

bool I2CMFile::exists() {
    if (!_has_address) return true;   // Root always exists.
    auto session = SessionBroker::obtain<I2CMSession>("", I2C_ML_PORT);
    if (!session) return false;
    return session->probe(_address);
}

void I2CMFile::_parse_fields() {
    auto p = PeoplesUrlParser::parseURL(url);
    if (!p) return;

    if (!p->host.empty()) {
        _address     = i2c_parse_hex8(p->host);
        _has_address = true;
    }
    std::string cmd = p->path;
    while (!cmd.empty() && cmd.front() == '/') cmd.erase(cmd.begin());
    if (!cmd.empty()) {
        _reg     = i2c_parse_hex8(cmd);
        _has_reg = true;
    }
}


/********************************************************
 * I2CMFileSystem
 ********************************************************/

MFile* I2CMFileSystem::getFile(std::string path) {
    return new I2CMFile(path);
}
