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

#include "modem.h"

#ifdef ENABLE_MODEM

#include <map>

#include <esp_heap_caps.h>
#include "esp_timer.h"

#include "meatloaf.h"
#include "mlConfig.h"
#include "fnWiFi.h"
#include "fnSystem.h"
#include "../../include/version.h"
#include "../../include/debug.h"

Modem modem;

namespace
{

uint32_t now_ms()
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

} // namespace

class Modem::Lock
{
public:
    explicit Lock(SemaphoreHandle_t m) : m_(m)
    {
        if (m_ != nullptr)
            xSemaphoreTake(m_, portMAX_DELAY);
    }
    ~Lock()
    {
        if (m_ != nullptr)
            xSemaphoreGive(m_);
    }

private:
    SemaphoreHandle_t m_;
};

bool Modem::start()
{
    if (task_ != nullptr)
        return true;

    if (mutex_ == nullptr)
    {
        mutex_ = xSemaphoreCreateMutex();
        if (mutex_ == nullptr)
        {
            Debug_printv("modem: mutex allocation failed");
            return false;
        }
    }

    settings_.factory();
    loadConfig();

    stop_requested_ = false;
    if (xTaskCreatePinnedToCore(&Modem::taskEntry, "modem", TASK_STACK, this,
                                TASK_PRIORITY, &task_, TASK_CORE) != pdPASS)
    {
        // Report the numbers that actually explain the failure. Free heap and
        // largest contiguous block are different questions, and only the
        // second one explains an allocation failure.
        Debug_printv("modem: task create failed free_internal=%u largest=%u",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        task_ = nullptr;
        return false;
    }
    return true;
}

void Modem::stop()
{
    stop_requested_ = true;
}

bool Modem::attach(ModemPort *port)
{
    if (port == nullptr)
        return false;

    Lock lock(mutex_);
    for (size_t i = 0; i < MAX_PORTS; ++i)
    {
        if (ports_[i] == port)
            return true;
    }
    for (size_t i = 0; i < MAX_PORTS; ++i)
    {
        if (ports_[i] == nullptr)
        {
            ports_[i] = port;
            return true;
        }
    }
    return false;
}

void Modem::detach(ModemPort *port)
{
    Lock lock(mutex_);
    for (size_t i = 0; i < MAX_PORTS; ++i)
    {
        if (ports_[i] == port)
            ports_[i] = nullptr;
    }
}

bool Modem::sessionActive() const
{
    return state_ == ModemState::ONLINE || state_ == ModemState::ONLINE_COMMAND;
}

void Modem::broadcast(const std::string &s)
{
    if (s.empty())
        return;

    Lock lock(mutex_);
    for (size_t i = 0; i < MAX_PORTS; ++i)
    {
        if (ports_[i] != nullptr && ports_[i]->isOpen())
        {
            // Command-mode output is short -- a result code, an echoed
            // character, a phonebook line -- against a 1024-byte TX buffer, so
            // a short count here is not backpressure but a console that has
            // stopped draining entirely. Truncating a result code is worse than
            // useless (the terminal waits forever for a terminator it will
            // never see), so the shortfall is recorded for doHangup() to
            // report. It is not retried: this call holds mutex_, and blocking
            // longer delays the detach() a shell task needs to exit modem mode.
            size_t sent = ports_[i]->pushTx((const uint8_t *)s.data(), s.size(), 100);
            if (sent < s.size())
                tx_dropped_ += (uint32_t)(s.size() - sent);
        }
    }
}

void Modem::toAttached(const uint8_t *buf, size_t n)
{
    if (buf == nullptr || n == 0)
        return;

    Lock lock(mutex_);
    for (size_t i = 0; i < MAX_PORTS; ++i)
    {
        if (ports_[i] != nullptr && ports_[i]->isOpen() && ports_[i]->attached())
        {
            // Stream data is a BBS at full speed against a 1024-byte buffer, so
            // a terminal that cannot keep up WILL come up short here. Dropping
            // is the deliberate policy: the alternative is holding mutex_ for
            // longer than 500 ms, which stalls the modem task -- and with it
            // every other port and the detach() path -- to protect one slow
            // console. What the user sees is missing characters on screen; what
            // makes that diagnosable rather than mysterious is the count, which
            // doHangup() reports at the end of the session.
            size_t sent = ports_[i]->pushTx(buf, n, 500);
            if (sent < n)
                tx_dropped_ += (uint32_t)(n - sent);
        }
    }
}

void Modem::sendResult(AtResult r)
{
    broadcast(at_format_result(r, settings_));
}

void Modem::taskEntry(void *arg)
{
    static_cast<Modem *>(arg)->run();
}

void Modem::run()
{
    Debug_printv("modem: task started");

    while (!stop_requested_)
    {
        if (state_ == ModemState::ONLINE)
            serviceStreamMode();
        else
            serviceCommandMode();
    }

    doHangup();
    Debug_printv("modem: task exiting");
    task_ = nullptr;
    vTaskDelete(nullptr);
}

// pdMS_TO_TICKS() TRUNCATES, and CONFIG_FREERTOS_HZ is 100 on every board here:
// a 5 ms delay is ZERO ticks, and vTaskDelay(0) yields only to tasks of equal or
// higher priority -- it never lets a lower-priority one run. The modem task is
// priority 5 and the console shells are priority 4 on the same core, so an idle
// delay that rounds to nothing starves the very task that feeds this one. The
// session then looks hung with the connection still up and the remote still
// live: output keeps working (a burst fills the port's TX buffer, which blocks
// this task in pushTx and lets the shell run) while nothing typed ever arrives.
// Never let an idle delay round to zero.
static inline TickType_t modem_idle_ticks(uint32_t ms)
{
    TickType_t t = pdMS_TO_TICKS(ms);
    return t > 0 ? t : 1;
}

void Modem::serviceCommandMode()
{
    uint8_t buf[64];
    size_t got = 0;

    {
        Lock lock(mutex_);
        for (size_t i = 0; i < MAX_PORTS && got == 0; ++i)
        {
            if (ports_[i] != nullptr && ports_[i]->isOpen())
                got = ports_[i]->popRx(buf, sizeof(buf), 0);
        }
    }

    if (got == 0)
    {
        // A suspended connection must still report NO CARRIER when the remote
        // hangs up -- that is the difference between ONLINE_COMMAND and being
        // offline, and it is what makes ATO meaningful.
        if (state_ == ModemState::ONLINE_COMMAND && conn_ != nullptr &&
            !conn_->isOpen())
        {
            conn_.reset();
            state_ = ModemState::COMMAND;
            sendResult(AtResult::NO_CARRIER);
        }
        vTaskDelay(modem_idle_ticks(20));
        return;
    }

    long cr = settings_.getRegister(AT_S_CR);
    long bs = settings_.getRegister(AT_S_BS);

    for (size_t i = 0; i < got; ++i)
    {
        uint8_t c = buf[i];

        if (c == '\n')
            continue;  // a CRLF terminal sends both; CR is the terminator

        if (c == (uint8_t)cr || c == '\r')
        {
            if (settings_.echo)
                broadcast("\r\n");
            std::string line = cmd_buf_;
            cmd_buf_.clear();
            executeLine(line);
            continue;
        }

        if (c == (uint8_t)bs || c == 0x7F)
        {
            if (!cmd_buf_.empty())
            {
                cmd_buf_.pop_back();
                if (settings_.echo)
                    broadcast("\b \b");
            }
            continue;
        }

        // A command line has no business being unbounded; a runaway sender
        // would otherwise grow this string until the heap gave out.
        if (cmd_buf_.size() < 256)
        {
            cmd_buf_.push_back((char)c);
            if (settings_.echo)
                broadcast(std::string(1, (char)c));
        }
    }
}

void Modem::serviceStreamMode()
{
    if (conn_ == nullptr || !conn_->isOpen())
    {
        conn_.reset();
        state_ = ModemState::COMMAND;
        escape_.reset();
        sendResult(AtResult::NO_CARRIER);
        return;
    }

    // Terminal -> remote, through the escape detector.
    uint8_t in[128];
    size_t got = 0;
    {
        Lock lock(mutex_);
        for (size_t i = 0; i < MAX_PORTS && got == 0; ++i)
        {
            if (ports_[i] != nullptr && ports_[i]->isOpen() &&
                ports_[i]->attached())
                got = ports_[i]->popRx(in, sizeof(in), 0);
        }
    }

    std::string forward;
    EscapeDetector::Verdict verdict = EscapeDetector::Verdict::NONE;

    for (size_t i = 0; i < got; ++i)
        verdict = escape_.feed(in[i], now_ms(), forward);

    // tick() is what completes the trailing guard and what releases held
    // bytes that turned out to be data, so it must run on every idle pass.
    if (got == 0)
        verdict = escape_.tick(now_ms(), forward);

    if (!forward.empty())
        conn_->write((const uint8_t *)forward.data(), (uint32_t)forward.size());

    if (verdict == EscapeDetector::Verdict::ESCAPED)
    {
        // The connection stays up. That distinction is what makes ATO
        // meaningful, and it is the one Zimodem does not draw.
        state_ = ModemState::ONLINE_COMMAND;
        cmd_buf_.clear();
        sendResult(AtResult::OK);
        return;
    }

    // Remote -> terminal.
    uint8_t out[512];
    uint32_t n = conn_->read(out, sizeof(out));
    if (n > 0)
        toAttached(out, n);
    else if (got == 0)
        vTaskDelay(modem_idle_ticks(5));

}

void Modem::executeLine(const std::string &raw)
{
    std::string line = raw;

    if (at_is_repeat(line))
        line = last_line_;
    else if (!line.empty())
        last_line_ = line;

    if (line.empty())
        return;

    AtLine cmds;
    size_t err_pos = 0;
    if (!at_parse(line, cmds, &err_pos))
    {
        Debug_printv("modem: parse error at %u in [%s]", (unsigned)err_pos,
                     line.c_str());
        sendResult(AtResult::ERROR);
        return;
    }

    for (const auto &cmd : cmds)
    {
        bool reported = false;
        if (!executeCommand(cmd, reported))
        {
            if (!reported)
                sendResult(AtResult::ERROR);
            return;
        }
        // A successful dial has already emitted CONNECT and switched state;
        // anything after it on the line would be typed into the session.
        if (state_ == ModemState::ONLINE)
            return;
    }

    sendResult(AtResult::OK);
}

bool Modem::executeCommand(const AtCommand &cmd, bool &reported)
{
    if (cmd.prefix == '+')
    {
        if (cmd.name == "SHELL")
        {
            // Leaves modem mode. The modem, its settings and any connection
            // stay alive, which is what makes the shared model meaningful.
            // Detaching is the signal the shell pumps watch for.
            Lock lock(mutex_);
            for (size_t i = 0; i < MAX_PORTS; ++i)
            {
                if (ports_[i] != nullptr)
                    ports_[i]->setAttached(false);
            }
            return true;
        }
        return false;
    }

    if (cmd.prefix == '&')
    {
        switch (cmd.verb)
        {
        case 'S':
            if (cmd.query)
            {
                long v = settings_.getRegister(cmd.number);
                if (v < 0)
                    return false;
                broadcast("\r\n" + std::to_string(v) + "\r\n");
                return true;
            }
            return settings_.setRegister(cmd.number, cmd.value);
        case 'W':
            saveConfig();
            return true;
        case 'F':
            settings_.factory();
            return true;
        default:
            return false;
        }
    }

    switch (cmd.verb)
    {
    case 'D':
        return doDial(cmd, reported);

    case 'H':
        doHangup();
        reported = true;  // doHangup() emits NO CARRIER when it had a call
        return true;

    case 'O':
        return doReturnOnline(reported);

    case 'Z':
        doHangup();
        reported = false;  // ATZ answers OK even though it may have hung up
        settings_.factory();
        loadConfig();
        return true;

    case 'E':
        settings_.echo = (cmd.number != 0);
        return true;

    case 'Q':
        settings_.quiet = (cmd.number != 0);
        return true;

    case 'V':
        settings_.verbose = (cmd.number != 0);
        return true;

    case 'X':
        if (cmd.number < 0 || cmd.number > 4)
            return false;
        settings_.xlevel = (uint8_t)cmd.number;
        return true;

    case 'S':
        if (cmd.query)
        {
            long v = settings_.getRegister(cmd.number);
            if (v < 0)
                return false;
            broadcast("\r\n" + std::to_string(v) + "\r\n");
            return true;
        }
        return settings_.setRegister(cmd.number, cmd.value);

    case 'I':
        doInfo(cmd.number);
        return true;

    case 'P':
        return doPhonebook(cmd);

    // Phase 3 owns ATA. Until then it is a recognised command that cannot
    // succeed, which is a clearer answer than "unknown command".
    case 'A':
        return false;

    default:
        return false;
    }
}

bool Modem::doDial(const AtCommand &cmd, bool &reported)
{
    if (sessionActive())
    {
        sendResult(AtResult::BUSY);
        reported = true;
        return false;
    }

    if (!fnWiFi.connected())
    {
        sendResult(AtResult::NO_DIALTONE);
        reported = true;
        return false;
    }

    std::string target = cmd.arg;
    std::string mods = cmd.mods;

    // Bare digits are a phonebook lookup.
    if (!target.empty() &&
        target.find_first_not_of("0123456789") == std::string::npos)
    {
        const PhonebookEntry *e = phonebook_.find(target);
        if (e == nullptr)
            return false;
        target = e->host + ":" + std::to_string(e->port);
        if (mods.empty())
            mods = e->mods;
    }

    std::string host;
    uint16_t port = 23;
    if (!phonebook_split_hostport(target, host, port))
        return false;

    // ATDT dials telnet; so does a plain ATD when S62 is set, which is the
    // Zimodem convention terminal programs already send.
    bool use_telnet = (mods.find('T') != std::string::npos) ||
                      (settings_.getRegister(AT_S_TELNET) != 0);

    std::string url = (use_telnet ? "telnet://" : "tcp://") + host + ":" +
                      std::to_string(port);

    state_ = ModemState::DIALING;
    Debug_printv("modem: dialing %s", url.c_str());

    auto file = std::shared_ptr<MFile>(MFSOwner::File(url));
    std::shared_ptr<MStream> stream;
    if (file != nullptr)
        stream = file->getSourceStream(std::ios_base::in | std::ios_base::out);

    // Open it, do not merely ask whether it is open. TelnetMStream is
    // constructed CLOSED and opens lazily on its first read()/write(), so a
    // freshly created one always answers isOpen() == false however well the
    // transport underneath it connected -- testing isOpen() here reported
    // NO ANSWER for every telnet dial, with the socket already established.
    // A dial has to settle CONNECT versus NO ANSWER now rather than on some
    // later first byte, so this is the one place that must open eagerly.
    if (stream == nullptr ||
        (!stream->isOpen() &&
         !stream->open(std::ios_base::in | std::ios_base::out)))
    {
        state_ = ModemState::COMMAND;
        sendResult(AtResult::NO_ANSWER);
        reported = true;
        return false;
    }

    conn_ = stream;
    escape_.configure((uint8_t)settings_.getRegister(AT_S_ESCCHAR),
                      (uint16_t)settings_.getRegister(AT_S_GUARD));
    escape_.reset();

    state_ = ModemState::ONLINE;
    sendResult(AtResult::CONNECT);
    reported = true;
    return true;
}

void Modem::doHangup()
{
    if (conn_ != nullptr)
    {
        conn_->close();
        conn_.reset();
    }
    escape_.reset();

    if (state_ != ModemState::COMMAND)
    {
        state_ = ModemState::COMMAND;
        sendResult(AtResult::NO_CARRIER);
    }

    // Session end is the one place it is safe to say this: no socket I/O is in
    // flight, so a console write here cannot race one. Without it a terminal
    // that fell behind shows missing characters and nothing anywhere says why.
    if (tx_dropped_ > 0)
    {
        broadcast("\r\nmodem: dropped " + std::to_string(tx_dropped_) +
                  " byte(s) to a console that stopped draining\r\n");
        tx_dropped_ = 0;
    }
}

bool Modem::doReturnOnline(bool &reported)
{
    if (state_ != ModemState::ONLINE_COMMAND || conn_ == nullptr ||
        !conn_->isOpen())
        return false;

    escape_.reset();
    state_ = ModemState::ONLINE;
    sendResult(AtResult::CONNECT);
    reported = true;
    return true;
}

void Modem::doInfo(long which)
{
    std::string out = "\r\n";

    switch (which)
    {
    case 1:
        out += "E" + std::string(settings_.echo ? "1" : "0");
        out += " Q" + std::string(settings_.quiet ? "1" : "0");
        out += " V" + std::string(settings_.verbose ? "1" : "0");
        out += " X" + std::to_string((int)settings_.xlevel);
        out += "\r\nS0=" + std::to_string(settings_.getRegister(AT_S_AUTOANSWER));
        out += " S2=" + std::to_string(settings_.getRegister(AT_S_ESCCHAR));
        out += " S7=" + std::to_string(settings_.getRegister(AT_S_CONNTIMEOUT));
        out += " S12=" + std::to_string(settings_.getRegister(AT_S_GUARD));
        out += " S62=" + std::to_string(settings_.getRegister(AT_S_TELNET));
        out += "\r\n";
        break;

    case 2:
        out += fnSystem.Net.get_ip4_address_str() + "\r\n";
        break;

    case 3:
        out += std::string(fnWiFi.connected() ? "connected" : "not connected") + "\r\n";
        break;

    case 4:
        out += std::string(FN_VERSION_FULL) + "\r\n";
        break;

    case 5:
        for (long n = 0; n < AtSettings::REGISTER_COUNT; ++n)
        {
            long v = settings_.getRegister(n);
            if (v != 0)
                out += "S" + std::to_string(n) + "=" + std::to_string(v) + "\r\n";
        }
        break;

    default:
        out += "Meatloaf modem " + std::string(FN_VERSION_FULL) + "\r\n";
        break;
    }

    broadcast(out);
}

bool Modem::doPhonebook(const AtCommand &cmd)
{
    // Bare ATP lists; the parser sets assign only when an '=' was present.
    if (!cmd.assign)
    {
        std::string out = "\r\n";
        for (const auto &e : phonebook_.all())
        {
            out += e.number + ": " + e.host + ":" + std::to_string(e.port);
            if (!e.mods.empty())
                out += " " + e.mods;
            out += "\r\n";
        }
        broadcast(out);
        return true;
    }

    std::string number = std::to_string(cmd.number);

    // ATPn= with no argument deletes.
    if (cmd.arg.empty())
        return phonebook_.erase(number);

    return phonebook_.store(number, cmd.arg, cmd.mods);
}

void Modem::loadConfig()
{
    auto &root = mlConfig.data();
    if (!root.contains("modem"))
        return;

    auto &m = root["modem"];

    if (m.contains("settings") && m["settings"].is_object())
    {
        std::map<std::string, long> kv;
        for (auto it = m["settings"].begin(); it != m["settings"].end(); ++it)
        {
            if (it.value().is_number_integer())
                kv[it.key()] = it.value().template get<long>();
        }
        settings_.fromKeyValues(kv);
    }

    if (m.contains("phonebook") && m["phonebook"].is_object())
    {
        phonebook_.clear();
        for (auto it = m["phonebook"].begin(); it != m["phonebook"].end(); ++it)
        {
            auto &e = it.value();
            if (!e.contains("host"))
                continue;
            std::string hostport = e["host"].template get<std::string>();
            if (e.contains("port"))
                hostport += ":" + std::to_string(e["port"].template get<int>());
            std::string mods = e.contains("mods")
                                   ? e["mods"].template get<std::string>()
                                   : std::string();
            phonebook_.store(it.key(), hostport, mods);
        }
    }
}

void Modem::saveConfig()
{
    auto &root = mlConfig.data();

    // Rebuild both subtrees rather than merging, so a deleted phonebook entry
    // or a register returned to its default actually disappears.
    root["modem"]["settings"] = psram_json::object();
    for (const auto &pair : settings_.toKeyValues())
        root["modem"]["settings"][pair.first] = pair.second;

    root["modem"]["phonebook"] = psram_json::object();
    for (const auto &e : phonebook_.all())
    {
        root["modem"]["phonebook"][e.number]["host"] = e.host;
        root["modem"]["phonebook"][e.number]["port"] = (int)e.port;
        if (!e.mods.empty())
            root["modem"]["phonebook"][e.number]["mods"] = e.mods;
    }

    // save() hashes each section and writes only what changed; there are no
    // dirty flags to set.
    mlConfig.save();
}

#endif // ENABLE_MODEM
