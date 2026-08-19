/**
 * FTP class for #FujiNet
 */

#ifndef FNFTP_H
#define FNFTP_H


#include "fnTcpClient.h"
#include "fnTcpServer.h"

using std::string;

#define FTP_TIMEOUT 15000 // This is how long we wait for a reply packet from the server

// The registered port for implicit FTPS, where the TLS handshake happens the
// moment the socket is up and the banner arrives already encrypted. Explicit
// FTPS (AUTH TLS after a plaintext banner) runs on the ordinary port 21, so
// the port is the only thing that distinguishes the two.
#define FTPS_IMPLICIT_PORT 990

/**
 * @brief The FTP control connection, which may be plaintext or TLS.
 *
 * Explicit FTPS (RFC 4217) upgrades an ALREADY CONNECTED control socket: the
 * banner and the AUTH TLS command are exchanged in the clear, and only then
 * does the handshake run over that same descriptor. So this owns an
 * fnTcpClient up to the upgrade, then takes the raw descriptor off it
 * (fnTcpClient::detach()) and hands it to esp-tls, which owns it from then on.
 *
 * The surface is exactly what fnFTP asks of a control connection, so call
 * sites read the same in both modes. Only the control channel is ever
 * wrapped - data connections stay plaintext (PROT C), which is what a server
 * configured with ProFTPD's "TLSRequired ctrl" wants and what keeps the cost
 * to a single TLS session.
 */
class fnFtpControl
{
public:
    ~fnFtpControl();

    int connect(const char *host, uint16_t port, int32_t timeout);

    /**
     * @brief Run the TLS handshake over the connected socket.
     * @param host server name, used for certificate validation.
     * @param port server port.
     * @return TRUE on error, FALSE on success.
     */
    bool start_tls(const char *host, uint16_t port);

    bool is_tls() const { return _tls != nullptr; }

    /**
     * @brief Take over an already-connected socket (active-mode data connection).
     */
    void adopt(const fnTcpClient &client);

    void stop();
    uint8_t connected();
    size_t available();
    int read();
    int read(uint8_t *buf, size_t len);
    int peek();
    void flush();
    size_t write(const std::string &str);
    size_t write(const uint8_t *buf, size_t len);
    in_addr_t localIP() const;

private:
    /**
     * @brief Try to pull ciphertext off the socket into _rx, without blocking.
     * @return true if _rx holds at least one byte afterwards.
     */
    bool tls_fill();

    fnTcpClient _plain;

    /* esp_tls_t*, kept as void* so esp_tls.h stays out of this header. */
    void *_tls = nullptr;

    /* Socket descriptor once TLS owns it, -1 otherwise. */
    int _fd = -1;

    /* Captured before the descriptor is handed over, for PORT (active mode). */
    in_addr_t _local_ip = 0;

    /* Plaintext bytes already decrypted but not yet consumed by the caller. */
    std::string _rx;

    bool _tls_connected = false;
};

class fnFTP
{
public:

    /**
     * ctor
     */
    fnFTP();

    /**
     * dtor
     */
    virtual ~fnFTP();

    /**
     *  Class 'fnFTP' does not have a copy constructor which is recommended since it has dynamic memory/resource allocation(s).
     * Unless these two functions are implemented, they are being deleted so they cannot be used
     */
    fnFTP (const fnFTP&) = delete;
    fnFTP& operator= (const fnFTP&) = delete;

    /**
     * Log into FTP server.
     * @param username username for login
     * @param password password for login
     * @param hostname host to login
     * @param port port to login (default 21)
     * @return TRUE on error, FALSE on success
     */
    bool login(const string &_username, const string &_password, const string &_hostname, unsigned short _port = 21);

    /**
     * @brief Negotiate AUTH TLS on the first login attempt rather than only
     * after the server refuses to talk in the clear. Set for an ftps:// URL.
     * Sticky - once the server has demanded TLS it is set anyway.
     */
    void require_tls(bool required) { _tls_required = _tls_required || required; }

    /**
     * Log out of FTP server, closes control connection.
     * @return TRUE on error, FALSE on success.
     */
    bool logout();

    /**
     * Open file on FTP server
     * @param path to file to open.
     * @param stor TRUE means STOR, otherwise RETR
     * @return TRUE if error, FALSE if successful.
     */
    /**
     * @brief Begin a RETR/STOR transfer, optionally resuming at an offset.
     * @param offset byte offset to restart at (REST), 0 for the whole file.
     */
    bool open_file(string path, bool stor, unsigned long offset = 0);

    /**
     * @brief End a transfer that is not being read to completion: drop the
     * data connection and consume the closing response, so the control
     * channel stays in step.
     */
    void end_transfer();

    /**
     * Open directory on FTP server, grab it, and return back.
     * @param path directory to retrieve.
     * @param pattern pattern to retrieve.
     * @return TRUE if error, FALSE if successful.
     */
    bool open_directory(string path, string pattern);

    /**
     * Read and return one parsed line of directory
     * @param name pointer to output name
     * @param filesize pointer to output filesize
     * @return TRUE if error, FALSE if successful
     */
    bool read_directory(string& name, long& filesize, bool &is_dir);

    /**
     * @brief Ask the server to change to path, as a directory test.
     * @param path absolute path to test.
     * @return true if the server accepted it as a directory.
     */
    bool change_directory(string path);

    /**
     * @brief End a listing early: drop the data connection and consume the
     * closing response, so the control channel stays in step.
     */
    void close_directory();

    /**
     * Read file from data socket into buffer.
     * @param buf target buffer
     * @param len length of target buffer
     * @param range_begin optional start byte position for partial file read (0 = no range)
     * @param range_end optional end byte position for partial file read (0 = no range)
     * @return TRUE if error, FALSE if successful.
     */
    bool read_file(uint8_t* buf, unsigned short len, unsigned long range_begin = 0, unsigned long range_end = 0);

    /**
     * Write file from buffer into data socket.
     * @param buf source buffer
     * @param len length of source buffer
     * @return TRUE if error, FALSE if successful.
     */
    bool write_file(uint8_t* buf, unsigned short len);

    /**
     * @brief close data and/or control sockets.
     */
    bool close();

    /**
     * @brief parsed out response code from controlResponse
     * @return int containing parsed out response code.
     */
    int status();

    /**
     * @brief return # of bytes waiting in data socket
     * @return # of bytes waiting in data socket
     */
    int data_available();

    /**
     * @brief return if data connected
     * @return TRUE if connected, FALSE if disconnected
     */
    bool data_connected();

    /**
     * @brief return if control connection is active
     * @return TRUE if connected, FALSE if disconnected
     */
    bool control_connected();

    /**
     * Recovery FTP connection.
     * @return TRUE on error, FALSE on success
     */
    bool reconnect();

    /**
     * @brief get size of file at path
     * @param path path to file
     * @return size of file in bytes, or -1 on error.
     */
    int32_t get_file_size(string path);

protected:
private:
    /**
     * The hostname
     */
    string hostname;

    /* do STOR - file opened for write */
    bool _stor = false;

    /* if to check control channel too while dealing with data channel */
    bool _expect_control_response = false;

    /* FTP status code, taken from FTP server response */
    int _statusCode = 0;

    /**
     * The port number. (21 by default)
     */
    unsigned short control_port = 21;

    /**
     * The control connection, plaintext or TLS (see fnFtpControl).
     */
    fnFtpControl *control = nullptr;

    /**
     * TRUE once the server has told us it will not talk in the clear, so the
     * next login attempt negotiates AUTH TLS. Servers differ on which command
     * they refuse (some the banner, most USER), so this is set wherever a
     * rejection names TLS or SSL rather than at one fixed point.
     */
    bool _tls_required = false;

    /**
     * The data connection. Plaintext unless PROT P was accepted, in which case
     * every data transfer gets its own TLS session over it.
     */
    fnFtpControl *data = nullptr;

    /**
     * TRUE once the server has accepted PROT P, so data connections must be
     * TLS. A server that keeps data in the clear (the default, and what PROT C
     * asks for) leaves this false.
     */
    bool _data_protected = false;

    /**
     * last response from control connection.
     */
    string controlResponse;

    /**
     * Username
     */
    string username;

    /**
     * Password
     */
    string password;

    /**
     * Directory buffer stream
     */
    /**
     * @brief Bytes pulled from the data connection that are not yet a whole
     * line. A listing is parsed one line at a time straight off the socket,
     * so only this remainder is ever held - buffering the whole listing
     * exhausted the internal heap on a board without PSRAM.
     */
    std::string dirRemainder;

    /**
     * @brief true while a listing's data connection is open.
     */
    bool _dir_streaming = false;

    /**
     * @brief true once the listing's closing response has been consumed.
     */
    bool _dir_got_response = false;

    /**
     * @brief Pull the next complete line of the listing off the data socket.
     * @param line receives the line, without its terminator.
     * @return true if a line was produced, false at end of listing.
     */
    bool next_directory_line(string &line);

    /**
     * The data port returned by EPSV/PASV
     */
    unsigned short data_port = 0;

    /**
     * TRUE if the data connection for the current transfer is active mode (PORT),
     * meaning the server connects to us rather than the other way around.
     */
    bool _active_mode = false;

    /**
     * Listening socket used for active mode (PORT) data transfers.
     */
    fnTcpServer _active_server;

    /**
     * read and parse control response
     * @return true on error, false on success.
     */
    bool parse_response();

    /**
     * read single line of control response
     * @return bytes read
     */
    int read_response_line(char *buf, int buflen);

    /**
     * Ask server to prepare a data port for us, trying extended passive mode
     * (EPSV) first, falling back to passive mode (PASV), and finally to
     * active mode (PORT) if both passive attempts fail.
     * @return TRUE if error, FALSE if successful.
     */
    bool get_data_port();

    /**
     * @brief Try to get a data connection via EPSV (RFC 2428).
     * @return TRUE if error, FALSE if successful.
     */
    bool get_data_port_epsv();

    /**
     * @brief Try to get a data connection via PASV (RFC 959).
     * @return TRUE if error, FALSE if successful.
     */
    bool get_data_port_pasv();

    /**
     * @brief Try to get a data connection via PORT/active mode (RFC 959).
     * We open a local listening socket, tell the server about it via PORT,
     * and the actual connection is accepted later in accept_active_connection().
     * @return TRUE if error, FALSE if successful.
     */
    bool get_data_port_port();

    /**
     * @brief If the current transfer is active mode (PORT), wait for and
     * accept the server's incoming data connection. No-op otherwise.
     * @return TRUE if error (e.g. timed out waiting), FALSE if successful.
     */
    bool accept_active_connection();

    /**
     * @brief Is response a positive preliminary reply?
     * @return true or false.
     */
    bool is_positive_preliminary_reply() { return controlResponse[0] == '1'; }

    /**
     * @brief Is response a positive completion reply?
     * @return true or false.
     */
    bool is_positive_completion_reply() { return controlResponse[0] == '2'; }

    /**
     * @brief Is response a positive intermediate reply?
     * @return true or false.
     */
    bool is_positive_intermediate_reply() { return controlResponse[0] == '3'; }

    /**
     * @brief Is response a negative transient reply?
     * @return true or false.
     */
    bool is_negative_transient_reply() { return controlResponse[0] == '4'; }

    /**
     * @brief Is response a positive intermediate reply?
     * @return true or false.
     */
    bool is_negative_permanent_reply() { return controlResponse[0] == '5'; }

    /**
     * @brief Is response a protected reply?
     * @return true or false.
     */
    bool is_protected_reply() { return controlResponse[0] == '6'; }

    /**
     * @brief Is response a syntax error?
     * @return true or false.
     */
    bool is_syntax() { return controlResponse[1] == '0'; }

    /**
     * @brief Is response informational?
     * @return true or false.
     */
    bool is_informational() { return controlResponse[1] == '1'; }

    /**
     * @brief Is response referring to a change in connection state?
     * @return true or false.
     */
    bool is_connection() { return controlResponse[1] == '2'; }

    /**
     * @brief Is response referring to an authoeization/authentication issue?
     * @return true or false.
     */
    bool is_authentication() { return controlResponse[1] == '3'; }

    /**
     * @brief IS response filesystem related?
     * @return true or false.
     */
    bool is_filesystem_related() { return controlResponse[1] == '5'; }

    /**
     * @brief One login attempt, over a freshly connected control socket.
     * @param use_tls negotiate AUTH TLS immediately after the banner.
     * @return TRUE on error, FALSE on success.
     */
    bool do_login(bool use_tls);

    /**
     * @brief Does the current response say the server wants TLS?
     */
    bool response_demands_tls();

    /**
     * @brief Is this an implicit-FTPS connection (handshake before banner)?
     * Decided by port alone, since that is the only thing separating implicit
     * FTPS from explicit - and it holds for ftp:// as well, because 990 means
     * implicit whatever the URL called itself.
     */
    bool implicit_tls() const { return control_port == FTPS_IMPLICIT_PORT; }

    /**
     * @brief Run the TLS handshake on a just-established data connection.
     * No-op unless PROT P was accepted. Called after the 150, which is when
     * the server is listening for it.
     * @return TRUE on error, FALSE on success.
     */
    bool start_data_tls();

    /**
     * @brief Ask for a TLS-protected control connection (RFC 4217).
     */
    void AUTH_TLS();

    /**
     * @brief Set the protection buffer size. Always 0 for stream mode.
     */
    void PBSZ();

    /**
     * @brief Set the data channel protection level.
     * @param level 'C' for clear, 'P' for private.
     */
    void PROT(char level);

    /**
     * @brief Perform USER command on open control connection
     */
    void USER();

    /**
     * @brief Perform PASS command on open control connection
     */
    void PASS();

    /**
     * @brief Perform TYPE I command on open control connection
     */
    void TYPE();

    /**
     * @brief Log out.
     */
    void QUIT();

    /**
     * @brief Enter extended passive mode (RFC 2428)
     */
    void EPSV();

    /**
     * @brief Enter passive mode (RFC 959)
     */
    void PASV();

    /**
     * @brief Enter active mode (RFC 959), telling the server our IP/port to connect to.
     * @param h1,h2,h3,h4 our IP address octets
     * @param port our listening port
     */
    void PORT(uint8_t h1, uint8_t h2, uint8_t h3, uint8_t h4, uint16_t port);

    /**
     * @brief Ask server to retrieve path
     * @param path path to retrieve.
     */
    void RETR(string path);

    /**
     * @brief set the restart marker for the next transfer.
     * @param offset byte offset to restart at.
     */
    void REST(unsigned long offset);

    /**
     * @brief change current directory to path.
     * @param path path to change directory to.
     */
    void CWD(string path);

    /**
     * @brief ask server for directory listing.
     * @param path path of directory listing
     * @param pattern requested pattern
     */
    void LIST(string path, string pattern);

    /**
     * @brief ask server to abort current transfer
     */
    void ABOR();

    /**
     * @brief ask server to store path
     * @param path path to store
     */
    void STOR(string path);

    /**
     * @brief send RANG command to server for partial file transfer (RFC 3659)
     * @param start start byte position
     * @param end end byte position
     */
    void RANG(unsigned long start, unsigned long end);

    /**
     * @brief ask server to get size of file at path
     * @param path path to file
     */
    void SIZE(string path);

    /**
     * @brief send NOOP command to server
     */
    void NOOP();

public:
    /**
     * Delete file on FTP server
     * @param path path to file to delete.
     * @return TRUE if error, FALSE if successful.
     */
    bool delete_file(string path);

    /**
     * Rename file on FTP server
     * @param pathFrom original file path
     * @param pathTo new file path
     * @return TRUE if error, FALSE if successful.
     */
    bool rename_file(string pathFrom, string pathTo);

    /**
     * Create directory on FTP server
     * @param path path of directory to create.
     * @return TRUE if error, FALSE if successful.
     */
    bool make_directory(string path);

    /**
     * Remove directory on FTP server
     * @param path path of directory to remove.
     * @return TRUE if error, FALSE if successful.
     */
    bool remove_directory(string path);

    /**
     * Send NOOP command as lightweight keep-alive
     * @return TRUE on success, FALSE on error.
     */
    bool keep_alive();

protected:
    /**
     * @brief send DEL (or DELE) command to server to delete file
     * @param path path of file to delete.
     */
    void DELE(string path);

    /**
     * @brief send RNFR/RNTO commands to server to rename file
     * @param pathFrom original path
     * @param pathTo new path
     */
    void RNFR(string pathFrom);
    void RNTO(string pathTo);

    /**
     * @brief send MKD command to server to make directory
     * @param path path of directory to create.
     */
    void MKD(string path);

    /**
     * @brief send RMD command to server to remove directory
     * @param path path of directory to remove.
     */
    void RMD(string path);

private:
    /**
     * Range start position for partial file transfer
     */
    unsigned long _range_begin = 0;

    /**
     * Range end position for partial file transfer
     */
    unsigned long _range_end = 0;
};

#endif /* FNFTP_H */