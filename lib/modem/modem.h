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

// The modem engine.
//
// One global instance. Either console can drive it and command-mode responses
// reach both, so both see the modem's state; stream data goes only to the
// attached port. A task owns the engine and never touches a console file
// descriptor -- see modem_port.h for why.

#ifndef MEATLOAF_MODEM
#define MEATLOAF_MODEM

#ifdef ENABLE_MODEM

#include <memory>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "at_parser.h"
#include "at_result.h"
#include "at_settings.h"
#include "escape.h"
#include "modem_port.h"
#include "phonebook.h"

class MStream;

enum class ModemState
{
    COMMAND,         // no connection
    DIALING,         // connect in progress
    ONLINE,          // connected, bytes flowing
    ONLINE_COMMAND,  // connected, +++ taken, AT commands accepted
};

class Modem
{
public:
    // Stack for the modem task. Phase 1 reaches dial (MFSOwner path
    // resolution) plus libtelnet; the AGENTS.md reference points are an HTTPS
    // GET with TLS handshake at 4360 bytes and AFP at 9332. 8 KB leaves
    // headroom without claiming the deep tier, which phase 4 adds separately
    // for SSH. Confirm against the `ps` high-water mark in Task 12.
    static constexpr uint32_t TASK_STACK = 8192;
    static constexpr uint32_t TASK_PRIORITY = 5;   // well below IEC's 17
    static constexpr BaseType_t TASK_CORE = 0;     // IEC owns core 1

    // Creates the task. Called once at boot: task stacks are internal-DRAM
    // only with no PSRAM fallback and can fail from fragmentation, so the
    // stack is claimed while contiguous internal RAM is still available. The
    // task idles on its ports until a console enters modem mode.
    bool start();
    void stop();
    bool isRunning() const { return task_ != nullptr; }

    // Registers a console's port. False when the port table is full.
    bool attach(ModemPort *port);
    void detach(ModemPort *port);

    // True while a connection is up, in either ONLINE or ONLINE_COMMAND.
    bool sessionActive() const;

    // Loads settings and the phonebook from mlConfig. Safe before start().
    void loadConfig();
    // Writes settings and the phonebook to mlConfig and saves. AT&W.
    void saveConfig();

private:
    static void taskEntry(void *arg);
    void run();

    void serviceCommandMode();
    void serviceStreamMode();

    // Applies one parsed line. Commands run left to right and stop at the
    // first failure, which is real-modem behaviour and stops a line like
    // AT&S62=1DT"bad" from half-applying silently.
    void executeLine(const std::string &line);

    // Returns false to make the caller emit ERROR. A command that has already
    // emitted its own result (a failed dial reports BUSY or NO ANSWER) sets
    // reported to true so the caller does not add a second code.
    bool executeCommand(const AtCommand &cmd, bool &reported);

    bool doDial(const AtCommand &cmd, bool &reported);
    void doHangup();
    bool doReturnOnline(bool &reported);
    void doInfo(long which);
    bool doPhonebook(const AtCommand &cmd);

    // Writes to every open port. Command-mode output.
    void broadcast(const std::string &s);
    // Writes to the attached port only. Stream-mode data.
    void toAttached(const uint8_t *buf, size_t n);
    void sendResult(AtResult r);

    // Guards ports_ against the shell tasks that call attach()/detach() while
    // the modem task is running. Everything else is touched only by the modem
    // task itself.
    class Lock;

    static constexpr size_t MAX_PORTS = 2;  // one serial, one TCP

    TaskHandle_t             task_ = nullptr;
    SemaphoreHandle_t        mutex_ = nullptr;
    volatile bool            stop_requested_ = false;

    ModemPort               *ports_[MAX_PORTS] = { nullptr, nullptr };

    ModemState               state_ = ModemState::COMMAND;
    AtSettings               settings_;
    Phonebook                phonebook_;
    EscapeDetector           escape_;
    std::shared_ptr<MStream> conn_;
    std::string              last_line_;   // for A/
    std::string              cmd_buf_;     // partial command-mode line

    // Bytes pushTx() could not take because a console's TX buffer stayed full
    // for the whole timeout. Counted rather than logged: both writers run on
    // the modem task while it is doing socket I/O, and Debug_printv expands to
    // console.printf on an ENABLE_CONSOLE board -- a TCP write from an I/O hot
    // path is the NFS 0x6400 heap-corruption condition AGENTS.md bans. Reported
    // once per session from doHangup(), which is not a hot path.
    uint32_t                 tx_dropped_ = 0;
};

extern Modem modem;

#endif // ENABLE_MODEM
#endif // MEATLOAF_MODEM
