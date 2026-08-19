/**
 * fnFTP implementation
 */

#include "fnFTP.h"

#include <cstdio>
#include <string.h>

#ifdef ESP_PLATFORM
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <esp_tls.h>
#include <esp_crt_bundle.h>
#endif

#include "../../include/debug.h"

#include "fnSystem.h"

/*
ftpparse(&fp,buf,len) tries to parse one line of LIST output.

The line is an array of len characters stored in buf.
It should not include the terminating CR LF; so buf[len] is typically CR.

If ftpparse() can't find a filename, it returns 0.

If ftpparse() can find a filename, it fills in fp and returns 1.
fp is a struct ftpparse, defined below.
The name is an array of fp.namelen characters stored in fp.name;
fp.name points somewhere within buf.
*/

struct ftpparse
{
    char *name; /* not necessarily 0-terminated */
    int namelen;
    int flagtrycwd;  /* 0 if cwd is definitely pointless, 1 otherwise */
    int flagtryretr; /* 0 if retr is definitely pointless, 1 otherwise */
    int sizetype;
    long size; /* number of octets */
    int mtimetype;
    time_t mtime; /* modification time */
    int idtype;
    char *id; /* not necessarily 0-terminated */
    int idlen;
};

#define FTPPARSE_SIZE_UNKNOWN 0
#define FTPPARSE_SIZE_BINARY 1 /* size is the number of octets in TYPE I */
#define FTPPARSE_SIZE_ASCII 2  /* size is the number of octets in TYPE A */

#define FTPPARSE_MTIME_UNKNOWN 0
#define FTPPARSE_MTIME_LOCAL 1        /* time is correct */
#define FTPPARSE_MTIME_REMOTEMINUTE 2 /* time zone and secs are unknown */
#define FTPPARSE_MTIME_REMOTEDAY 3    /* time zone and time of day are unknown */
/*
When a time zone is unknown, it is assumed to be GMT. You may want
to use localtime() for LOCAL times, along with an indication that the
time is correct in the local time zone, and gmtime() for REMOTE* times.
*/

#define FTPPARSE_ID_UNKNOWN 0
#define FTPPARSE_ID_FULL 1 /* unique identifier for files on this FTP server */

/* ftpparse.c, ftpparse.h: library for parsing FTP LIST responses
20001223
D. J. Bernstein, djb@cr.yp.to
http://cr.yp.to/ftpparse.html

Commercial use is fine, if you let me know what programs you're using this in.

Currently covered formats:
EPLF.
UNIX ls, with or without gid.
Microsoft FTP Service.
Windows NT FTP Server.
VMS.
WFTPD.
NetPresenz (Mac).
NetWare.
MSDOS.

Definitely not covered:
Long VMS filenames, with information split across two lines.
NCSA Telnet FTP server. Has LIST = NLST (and bad NLST for directories).
*/

#include <time.h>

static long totai(long year, long month, long mday)
{
    long result;
    if (month >= 2)
        month -= 2;
    else
    {
        month += 10;
        --year;
    }
    result = (mday - 1) * 10 + 5 + 306 * month;
    result /= 10;
    if (result == 365)
    {
        year -= 3;
        result = 1460;
    }
    else
        result += 365 * (year % 4);
    year /= 4;
    result += 1461 * (year % 25);
    year /= 25;
    if (result == 36524)
    {
        year -= 3;
        result = 146096;
    }
    else
    {
        result += 36524 * (year % 4);
    }
    year /= 4;
    result += 146097 * (year - 5);
    result += 11017;
    return result * 86400;
}

static int flagneedbase = 1;
static time_t base; /* time() value on this OS at the beginning of 1970 TAI */
static long now;    /* current time */
static int flagneedcurrentyear = 1;
static long currentyear; /* approximation to current year */

static void initbase(void)
{
    struct tm *t;
    if (!flagneedbase)
        return;

    base = 0;
    t = gmtime(&base);
    base = -(totai(t->tm_year + 1900, t->tm_mon, t->tm_mday) + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec);
    /* assumes the right time_t, counting seconds. */
    /* base may be slightly off if time_t counts non-leap seconds. */
    flagneedbase = 0;
}

static void initnow(void)
{
    long day;
    long year;

    initbase();
    now = time((time_t *)0) - base;

    if (flagneedcurrentyear)
    {
        day = now / 86400;
        if ((now % 86400) < 0)
            --day;
        day -= 11017;
        year = 5 + day / 146097;
        day = day % 146097;
        if (day < 0)
        {
            day += 146097;
            --year;
        }
        year *= 4;
        if (day == 146096)
        {
            year += 3;
            day = 36524;
        }
        else
        {
            year += day / 36524;
            day %= 36524;
        }
        year *= 25;
        year += day / 1461;
        day %= 1461;
        year *= 4;
        if (day == 1460)
        {
            year += 3;
            day = 365;
        }
        else
        {
            year += day / 365;
            day %= 365;
        }
        day *= 10;
        if ((day + 5) / 306 >= 10)
            ++year;
        currentyear = year;
        flagneedcurrentyear = 0;
    }
}

/* UNIX ls does not show the year for dates in the last six months. */
/* So we have to guess the year. */
/* Apparently NetWare uses ``twelve months'' instead of ``six months''; ugh. */
/* Some versions of ls also fail to show the year for future dates. */
static long guesstai(long month, long mday)
{
    long year;
    long t;

    initnow();

    for (year = currentyear - 1; year < currentyear + 100; ++year)
    {
        t = totai(year, month, mday);
        if (now - t < 350 * 86400)
            return t;
    }
    return -1;
}

static int check(char *buf, const char *monthname)
{
    if ((buf[0] != monthname[0]) && (buf[0] != monthname[0] - 32))
        return 0;
    if ((buf[1] != monthname[1]) && (buf[1] != monthname[1] - 32))
        return 0;
    if ((buf[2] != monthname[2]) && (buf[2] != monthname[2] - 32))
        return 0;
    return 1;
}

static const char *months[12] = {
    "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};

static int getmonth(char *buf, int len)
{
    int i;
    if (len == 3)
        for (i = 0; i < 12; ++i)
            if (check(buf, months[i]))
                return i;
    return -1;
}

static long getlong(char *buf, int len)
{
    long u = 0;
    while (len-- > 0)
        u = u * 10 + (*buf++ - '0');
    return u;
}

int ftpparse(struct ftpparse *fp, char *buf, int len)
{
    int i = 0;
    int j = 0;
    int state = 0;
    long size = 0;
    long year = 0;
    long month = 0;
    long mday = 0;
    long hour = 0;
    long minute = 0;

    fp->name = 0;
    fp->namelen = 0;
    fp->flagtrycwd = 0;
    fp->flagtryretr = 0;
    fp->sizetype = FTPPARSE_SIZE_UNKNOWN;
    fp->size = 0;
    fp->mtimetype = FTPPARSE_MTIME_UNKNOWN;
    fp->mtime = 0;
    fp->idtype = FTPPARSE_ID_UNKNOWN;
    fp->id = 0;
    fp->idlen = 0;

    if (len < 2) /* an empty name in EPLF, with no info, could be 2 chars */
        return 0;

    switch (*buf)
    {
    /* see http://pobox.com/~djb/proto/eplf.txt */
    /* "+i8388621.29609,m824255902,/,\tdev" */
    /* "+i8388621.44468,m839956783,r,s10376,\tRFCEPLF" */
    case '+':
        i = 1;
        for (j = 1; j < len; ++j)
        {
            if (buf[j] == 9)
            {
                fp->name = buf + j + 1;
                fp->namelen = len - j - 1;
                return 1;
            }
            if (buf[j] == ',')
            {
                switch (buf[i])
                {
                case '/':
                    fp->flagtrycwd = 1;
                    break;
                case 'r':
                    fp->flagtryretr = 1;
                    break;
                case 's':
                    fp->sizetype = FTPPARSE_SIZE_BINARY;
                    fp->size = getlong(buf + i + 1, j - i - 1);
                    break;
                case 'm':
                    fp->mtimetype = FTPPARSE_MTIME_LOCAL;
                    initbase();
                    fp->mtime = base + getlong(buf + i + 1, j - i - 1);
                    break;
                case 'i':
                    fp->idtype = FTPPARSE_ID_FULL;
                    fp->id = buf + i + 1;
                    fp->idlen = j - i - 1;
                }
                i = j + 1;
            }
        }
        return 0;

    /* UNIX-style listing, without inum and without blocks */
    /* "-rw-r--r--   1 root     other        531 Jan 29 03:26 README" */
    /* "dr-xr-xr-x   2 root     other        512 Apr  8  1994 etc" */
    /* "dr-xr-xr-x   2 root     512 Apr  8  1994 etc" */
    /* "lrwxrwxrwx   1 root     other          7 Jan 25 00:17 bin -> usr/bin" */
    /* Also produced by Microsoft's FTP servers for Windows: */
    /* "----------   1 owner    group         1803128 Jul 10 10:18 ls-lR.Z" */
    /* "d---------   1 owner    group               0 May  9 19:45 Softlib" */
    /* Also WFTPD for MSDOS: */
    /* "-rwxrwxrwx   1 noone    nogroup      322 Aug 19  1996 message.ftp" */
    /* Also NetWare: */
    /* "d [R----F--] supervisor            512       Jan 16 18:53    login" */
    /* "- [R----F--] rhesus             214059       Oct 20 15:27    cx.exe" */
    /* Also NetPresenz for the Mac: */
    /* "-------r--         326  1391972  1392298 Nov 22  1995 MegaPhone.sit" */
    /* "drwxrwxr-x               folder        2 May 10  1996 network" */
    case 'b':
    case 'c':
    case 'd':
    case 'l':
    case 'p':
    case 's':
    case '-':

        if (*buf == 'd')
            fp->flagtrycwd = 1;
        if (*buf == '-')
            fp->flagtryretr = 1;
        if (*buf == 'l')
            fp->flagtrycwd = fp->flagtryretr = 1;

        state = 1;
        i = 0;
        for (j = 1; j < len; ++j)
            if ((buf[j] == ' ') && (buf[j - 1] != ' '))
            {
                switch (state)
                {
                case 1: /* skipping perm */
                    state = 2;
                    break;
                case 2: /* skipping nlink */
                    state = 3;
                    if ((j - i == 6) && (buf[i] == 'f')) /* for NetPresenz */
                        state = 4;
                    break;
                case 3: /* skipping uid */
                    state = 4;
                    break;
                case 4: /* getting tentative size */
                    size = getlong(buf + i, j - i);
                    state = 5;
                    break;
                case 5: /* searching for month, otherwise getting tentative size */
                    month = getmonth(buf + i, j - i);
                    if (month >= 0)
                        state = 6;
                    else
                        size = getlong(buf + i, j - i);
                    break;
                case 6: /* have size and month */
                    mday = getlong(buf + i, j - i);
                    state = 7;
                    break;
                case 7: /* have size, month, mday */
                    if ((j - i == 4) && (buf[i + 1] == ':'))
                    {
                        hour = getlong(buf + i, 1);
                        minute = getlong(buf + i + 2, 2);
                        fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
                        initbase();
                        fp->mtime = base + guesstai(month, mday) + hour * 3600 + minute * 60;
                    }
                    else if ((j - i == 5) && (buf[i + 2] == ':'))
                    {
                        hour = getlong(buf + i, 2);
                        minute = getlong(buf + i + 3, 2);
                        fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
                        initbase();
                        fp->mtime = base + guesstai(month, mday) + hour * 3600 + minute * 60;
                    }
                    else if (j - i >= 4)
                    {
                        year = getlong(buf + i, j - i);
                        fp->mtimetype = FTPPARSE_MTIME_REMOTEDAY;
                        initbase();
                        fp->mtime = base + totai(year, month, mday);
                    }
                    else
                        return 0;
                    fp->name = buf + j + 1;
                    fp->namelen = len - j - 1;
                    state = 8;
                    break;
                case 8: /* twiddling thumbs */
                    break;
                }
                i = j + 1;
                while ((i < len) && (buf[i] == ' '))
                    ++i;
            }

        if (state != 8)
            return 0;

        fp->size = size;
        fp->sizetype = FTPPARSE_SIZE_BINARY;

        if (*buf == 'l')
            for (i = 0; i + 3 < fp->namelen; ++i)
                if (fp->name[i] == ' ')
                    if (fp->name[i + 1] == '-')
                        if (fp->name[i + 2] == '>')
                            if (fp->name[i + 3] == ' ')
                            {
                                fp->namelen = i;
                                break;
                            }

        /* eliminate extra NetWare spaces */
        if ((buf[1] == ' ') || (buf[1] == '['))
            if (fp->namelen > 3)
                if (fp->name[0] == ' ')
                    if (fp->name[1] == ' ')
                        if (fp->name[2] == ' ')
                        {
                            fp->name += 3;
                            fp->namelen -= 3;
                        }

        return 1;
    }

    /* MultiNet (some spaces removed from examples) */
    /* "00README.TXT;1      2 30-DEC-1996 17:44 [SYSTEM] (RWED,RWED,RE,RE)" */
    /* "CORE.DIR;1          1  8-SEP-1996 16:09 [SYSTEM] (RWE,RWE,RE,RE)" */
    /* and non-MutliNet VMS: */
    /* "CII-MANUAL.TEX;1  213/216  29-JAN-1996 03:33:12  [ANONYMOU,ANONYMOUS]   (RWED,RWED,,)" */
    for (i = 0; i < len; ++i)
        if (buf[i] == ';')
            break;
    if (i < len)
    {
        fp->name = buf;
        fp->namelen = i;
        if (i > 4)
            if (buf[i - 4] == '.')
                if (buf[i - 3] == 'D')
                    if (buf[i - 2] == 'I')
                        if (buf[i - 1] == 'R')
                        {
                            fp->namelen -= 4;
                            fp->flagtrycwd = 1;
                        }
        if (!fp->flagtrycwd)
            fp->flagtryretr = 1;
        while (buf[i] != ' ')
            if (++i == len)
                return 0;
        while (buf[i] == ' ')
            if (++i == len)
                return 0;
        while (buf[i] != ' ')
            if (++i == len)
                return 0;
        while (buf[i] == ' ')
            if (++i == len)
                return 0;
        j = i;
        while (buf[j] != '-')
            if (++j == len)
                return 0;
        mday = getlong(buf + i, j - i);
        while (buf[j] == '-')
            if (++j == len)
                return 0;
        i = j;
        while (buf[j] != '-')
            if (++j == len)
                return 0;
        month = getmonth(buf + i, j - i);
        if (month < 0)
            return 0;
        while (buf[j] == '-')
            if (++j == len)
                return 0;
        i = j;
        while (buf[j] != ' ')
            if (++j == len)
                return 0;
        year = getlong(buf + i, j - i);
        while (buf[j] == ' ')
            if (++j == len)
                return 0;
        i = j;
        while (buf[j] != ':')
            if (++j == len)
                return 0;
        hour = getlong(buf + i, j - i);
        while (buf[j] == ':')
            if (++j == len)
                return 0;
        i = j;
        while ((buf[j] != ':') && (buf[j] != ' '))
            if (++j == len)
                return 0;
        minute = getlong(buf + i, j - i);

        fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
        initbase();
        fp->mtime = base + totai(year, month, mday) + hour * 3600 + minute * 60;

        return 1;
    }

    /* MSDOS format */
    /* 04-27-00  09:09PM       <DIR>          licensed */
    /* 07-18-00  10:16AM       <DIR>          pub */
    /* 04-14-00  03:47PM                  589 readme.htm */
    if ((*buf >= '0') && (*buf <= '9'))
    {
        i = 0;
        j = 0;
        while (buf[j] != '-')
            if (++j == len)
                return 0;
        month = getlong(buf + i, j - i) - 1;
        while (buf[j] == '-')
            if (++j == len)
                return 0;
        i = j;
        while (buf[j] != '-')
            if (++j == len)
                return 0;
        mday = getlong(buf + i, j - i);
        while (buf[j] == '-')
            if (++j == len)
                return 0;
        i = j;
        while (buf[j] != ' ')
            if (++j == len)
                return 0;
        year = getlong(buf + i, j - i);
        if (year < 50)
            year += 2000;
        if (year < 1000)
            year += 1900;
        while (buf[j] == ' ')
            if (++j == len)
                return 0;
        i = j;
        while (buf[j] != ':')
            if (++j == len)
                return 0;
        hour = getlong(buf + i, j - i);
        while (buf[j] == ':')
            if (++j == len)
                return 0;
        i = j;
        while ((buf[j] != 'A') && (buf[j] != 'P'))
            if (++j == len)
                return 0;
        minute = getlong(buf + i, j - i);
        if (hour == 12)
            hour = 0;
        if (buf[j] == 'A')
            if (++j == len)
                return 0;
        if (buf[j] == 'P')
        {
            hour += 12;
            if (++j == len)
                return 0;
        }
        if (buf[j] == 'M')
            if (++j == len)
                return 0;

        while (buf[j] == ' ')
            if (++j == len)
                return 0;
        if (buf[j] == '<')
        {
            fp->flagtrycwd = 1;
            while (buf[j] != ' ')
                if (++j == len)
                    return 0;
        }
        else
        {
            i = j;
            while (buf[j] != ' ')
                if (++j == len)
                    return 0;
            fp->size = getlong(buf + i, j - i);
            fp->sizetype = FTPPARSE_SIZE_BINARY;
            fp->flagtryretr = 1;
        }
        while (buf[j] == ' ')
            if (++j == len)
                return 0;

        fp->name = buf + j;
        fp->namelen = len - j;

        fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
        initbase();
        fp->mtime = base + totai(year, month, mday) + hour * 3600 + minute * 60;

        return 1;
    }

    /* Some useless lines, safely ignored: */
    /* "Total of 11 Files, 10966 Blocks." (VMS) */
    /* "total 14786" (UNIX) */
    /* "DISK$ANONFTP:[ANONYMOUS]" (VMS) */
    /* "Directory DISK$PCSA:[ANONYM]" (VMS) */

    return 0;
}

/** FTP CONTROL CONNECTION *********************************************************************/

// Most decrypted bytes pulled out of the TLS session in one go. Control
// responses are short; this only has to be big enough that a whole one
// usually arrives in a single call.
#define FTP_TLS_RX_CHUNK 256

fnFtpControl::~fnFtpControl()
{
    stop();
}

int fnFtpControl::connect(const char *host, uint16_t port, int32_t timeout)
{
    stop();
    return _plain.connect(host, port, timeout);
}

bool fnFtpControl::start_tls(const char *host, uint16_t port)
{
#ifdef ESP_PLATFORM
    if (_tls != nullptr)
        return false; // already protected

    // Anything already waiting was sent before the handshake and so is
    // unauthenticated. A server has nothing to say between its 234 and the
    // handshake, so this is an injection attempt, not a race.
    if (_plain.available() != 0)
    {
        Debug_printf("fnFtpControl::start_tls() - unexpected data before handshake, refusing.\r\n");
        return true;
    }

    // Capture this while the socket still belongs to _plain - PORT (active
    // mode) needs it and esp-tls does not offer it.
    _local_ip = _plain.localIP();

    int sockfd = _plain.detach();
    if (sockfd < 0)
    {
        Debug_printf("fnFtpControl::start_tls() - no socket to upgrade.\r\n");
        return true;
    }

    esp_tls_t *tls = esp_tls_init();
    if (tls == nullptr)
    {
        Debug_printf("fnFtpControl::start_tls() - esp_tls_init() failed (out of memory).\r\n");
        closesocket(sockfd);
        return true;
    }

    // Telling esp-tls the connection is already in ESP_TLS_CONNECTING makes it
    // skip its own TCP connect and go straight to the handshake on our socket.
    if (esp_tls_set_conn_sockfd(tls, sockfd) != ESP_OK ||
        esp_tls_set_conn_state(tls, ESP_TLS_CONNECTING) != ESP_OK)
    {
        Debug_printf("fnFtpControl::start_tls() - could not adopt socket.\r\n");
        esp_tls_conn_destroy(tls); // closes sockfd
        return true;
    }

    esp_tls_cfg_t cfg = {};
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = FTP_TIMEOUT;

    // The certificate chain IS verified against the CA bundle; the name in it
    // is not. FTPS is routinely served under a certificate issued for the
    // machine rather than for the name the client dialled - meatloaf.cc
    // answers with one for vpsl.techknowpro.com - and refusing those would
    // make the feature unusable against most real servers. The cost is that a
    // holder of any CA-issued certificate could stand in the middle, so the
    // credentials sent over this channel are protected from eavesdroppers but
    // not from an active attacker.
    cfg.skip_common_name = true;

    int res = esp_tls_conn_new_sync(host, strlen(host), port, &cfg, tls);
    if (res != 1)
    {
        Debug_printf("fnFtpControl::start_tls() - handshake failed (%d).\r\n", res);
        esp_tls_conn_destroy(tls); // closes sockfd
        return true;
    }

    // From here reads are polled, so the descriptor must not block when a
    // record has only partly arrived.
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    _tls = tls;
    _fd = sockfd;
    _tls_connected = true;
    _rx.clear();

    Debug_printf("fnFtpControl::start_tls() - handshake complete (peer name not verified).\r\n");
    return false;
#else
    (void)host;
    (void)port;
    Debug_printf("fnFtpControl::start_tls() - TLS not available on this platform.\r\n");
    return true;
#endif
}

void fnFtpControl::stop()
{
#ifdef ESP_PLATFORM
    if (_tls != nullptr)
    {
        esp_tls_conn_destroy((esp_tls_t *)_tls); // closes _fd
        _tls = nullptr;
    }
#endif
    _fd = -1;
    _tls_connected = false;
    _rx.clear();
    _plain.stop();
}

uint8_t fnFtpControl::connected()
{
    if (_tls == nullptr)
        return _plain.connected();
    return (_tls_connected || !_rx.empty()) ? 1 : 0;
}

bool fnFtpControl::tls_fill()
{
#ifdef ESP_PLATFORM
    if (!_rx.empty())
        return true;
    if (_tls == nullptr || !_tls_connected)
        return false;

    esp_tls_t *tls = (esp_tls_t *)_tls;

    // mbedTLS may already hold a decrypted record even when the socket is
    // empty, so ask it before polling the descriptor.
    if (esp_tls_get_bytes_avail(tls) <= 0)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(_fd, &rfds);
        struct timeval tv = {0, 0};
        if (select(_fd + 1, &rfds, nullptr, nullptr, &tv) <= 0)
            return false;
    }

    uint8_t buf[FTP_TLS_RX_CHUNK];
    ssize_t num_read = esp_tls_conn_read(tls, buf, sizeof(buf));
    if (num_read > 0)
    {
        _rx.append((const char *)buf, num_read);
        return true;
    }
    if (num_read != ESP_TLS_ERR_SSL_WANT_READ && num_read != ESP_TLS_ERR_SSL_WANT_WRITE)
    {
        // 0 is a clean close, anything else is an error - either way the
        // session is finished.
        _tls_connected = false;
    }
    return false;
#else
    return false;
#endif
}

size_t fnFtpControl::available()
{
    if (_tls == nullptr)
        return _plain.available();
    tls_fill();
    return _rx.size();
}

int fnFtpControl::read()
{
    if (_tls == nullptr)
        return _plain.read();
    if (_rx.empty() && !tls_fill())
        return -1;
    int c = (uint8_t)_rx[0];
    _rx.erase(0, 1);
    return c;
}

int fnFtpControl::read(uint8_t *buf, size_t len)
{
    if (_tls == nullptr)
        return _plain.read(buf, len);

    // Every caller today clamps len to available(), so the wait below never
    // actually runs. It is here because tls_fill() polls with a zero timeout:
    // an unbounded caller would otherwise get a short read at any TCP segment
    // or TLS record boundary, and fnFTP::read_file() reports a short read as a
    // transfer failure.
    size_t done = 0;
    int tmout_counter = 1 + FTP_TIMEOUT / 10;
    while (done < len)
    {
        if (_rx.empty() && !tls_fill())
        {
            if (!connected() || --tmout_counter == 0)
                break;
            fnSystem.delay(10);
            continue;
        }
        size_t take = _rx.size();
        if (take > len - done)
            take = len - done;
        memcpy(buf + done, _rx.data(), take);
        _rx.erase(0, take);
        done += take;
    }
    return (int)done;
}

int fnFtpControl::peek()
{
    if (_tls == nullptr)
        return _plain.peek();
    if (_rx.empty() && !tls_fill())
        return -1;
    return (uint8_t)_rx[0];
}

void fnFtpControl::flush()
{
    if (_tls == nullptr)
    {
        _plain.flush();
        return;
    }
    _rx.clear();
    while (tls_fill())
        _rx.clear();
}

size_t fnFtpControl::write(const std::string &str)
{
    return write((const uint8_t *)str.data(), str.size());
}

size_t fnFtpControl::write(const uint8_t *buf, size_t len)
{
    if (_tls == nullptr)
        return _plain.write(buf, len);

#ifdef ESP_PLATFORM
    const uint8_t *pos = buf;
    size_t remaining = len;
    int tmout_counter = 1 + FTP_TIMEOUT / 10;

    while (remaining > 0 && tmout_counter-- > 0)
    {
        ssize_t written = esp_tls_conn_write((esp_tls_t *)_tls, pos, remaining);
        if (written > 0)
        {
            pos += written;
            remaining -= written;
            continue;
        }
        if (written == ESP_TLS_ERR_SSL_WANT_READ || written == ESP_TLS_ERR_SSL_WANT_WRITE)
        {
            fnSystem.delay(10);
            continue;
        }
        Debug_printf("fnFtpControl::write() - failed (%d).\r\n", (int)written);
        _tls_connected = false;
        break;
    }
    return len - remaining;
#else
    return 0;
#endif
}

void fnFtpControl::adopt(const fnTcpClient &client)
{
    stop();
    _plain = client;
}

in_addr_t fnFtpControl::localIP() const
{
    if (_tls != nullptr)
        return _local_ip;
    return _plain.localIP();
}

/** FTP CLIENT *********************************************************************************/

fnFTP::fnFTP()
{
    _stor = false;
    _expect_control_response = false;
    control = new fnFtpControl();
    data = new fnFtpControl();
}

fnFTP::~fnFTP()
{
    if (control != nullptr)
        delete control;
    if (data != nullptr)
        delete data;

    control = nullptr;
    data = nullptr;
}

bool fnFTP::login(const string &_username, const string &_password, const string &_hostname, unsigned short _port)
{
    username = _username;
    password = _password;
    hostname = _hostname;
    control_port = _port;

    Debug_printf("fnFTP::login(%s,%u)\r\n", hostname.c_str(), control_port);

    // First attempt is in the clear unless this is implicit FTPS, or the
    // caller asked for ftps://, or a previous attempt already established that
    // this server refuses that.
    bool used_tls = _tls_required || implicit_tls();

    if (!do_login(used_tls))
        return false;

    // A server that will not talk in the clear says so by refusing a command,
    // not in its banner, so the discovery only happens mid-attempt. Retry once
    // over TLS - but only if this attempt was not already the TLS one.
    if (used_tls || !_tls_required)
        return true;

    Debug_printf("Server requires TLS. Retrying login with AUTH TLS.\r\n");
    control->stop();
    return do_login(true);
}

bool fnFTP::response_demands_tls()
{
    if (!is_negative_permanent_reply() && !is_negative_transient_reply())
        return false;
    return controlResponse.find("TLS") != string::npos ||
           controlResponse.find("SSL") != string::npos;
}

bool fnFTP::start_data_tls()
{
    if (!_data_protected)
        return false;
    Debug_printf("fnFTP::start_data_tls() - securing data connection.\r\n");
    return data->start_tls(hostname.c_str(), control_port);
}

bool fnFTP::do_login(bool use_tls)
{
    _data_protected = false;

    // Attempt to open control socket.
    if (!control->connect(hostname.c_str(), control_port, FTP_TIMEOUT))
    {
        Debug_printf("Could not log in, errno = %u\r\n", errno);
        _statusCode = 421; // service not available
        return true;
    }

    // Implicit FTPS: the server expects the handshake the moment the socket is
    // up, and its banner arrives already encrypted. There is no AUTH TLS and
    // no plaintext phase to fall back from, so a failure here is fatal.
    if (implicit_tls())
    {
        Debug_printf("Implicit FTPS - handshaking before the banner.\r\n");
        if (control->start_tls(hostname.c_str(), control_port))
        {
            _statusCode = 421; // service not available
            return true;
        }
    }

    Debug_printf("Connected, waiting for 220.\r\n");

    // Wait for banner.
    if (parse_response())
    {
        Debug_printf("Timed out waiting for 220 banner.\r\n");
        _tls_required = _tls_required || response_demands_tls();
        return true;
    }

    if (!is_positive_completion_reply() || !is_connection())
    {
        Debug_printf("Bad banner. Response was: %s\r\n", controlResponse.c_str());
        _tls_required = _tls_required || response_demands_tls();
        return true;
    }

    // Explicit FTPS: upgrade the connection we have just read the banner on.
    // Skipped when implicit already protected it before the banner.
    if (use_tls && !control->is_tls())
    {
        Debug_printf("Sending AUTH TLS.\r\n");
        AUTH_TLS();

        if (parse_response() || !is_positive_completion_reply())
        {
            Debug_printf("Server refused AUTH TLS. Response was: %s\r\n", controlResponse.c_str());
            return true;
        }

        if (control->start_tls(hostname.c_str(), control_port))
            return true;
    }

    if (control->is_tls())
    {
        // RFC 4217: PBSZ must precede PROT, and is always 0 for stream mode.
        // Ask for protected data connections. A server that keeps data in the
        // clear refuses this, and clear is the default when no PROT is in
        // force, so a refusal needs no follow-up - it just leaves
        // _data_protected false. Protecting the data channel costs a SECOND
        // TLS session for the duration of each transfer, which a board without
        // PSRAM will likely not have the internal heap for; there the
        // handshake fails and the transfer reports an error.
        PBSZ();
        parse_response(); // advisory
        PROT('P');
        _data_protected = (!parse_response() && is_positive_completion_reply());
        Debug_printf("Data connections will be %s.\r\n",
                     _data_protected ? "TLS-protected" : "plaintext");
    }

    Debug_printf("Sending USER.\r\n");

    USER();

    if (parse_response())
    {
        Debug_printf("Timed out waiting for 331 or 230.\r\n");
        _tls_required = _tls_required || response_demands_tls();
        return true;
    }

    if (is_positive_intermediate_reply() && is_authentication())
    {
        Debug_printf("Sending PASS.\r\n");
        // Send password
        PASS();

        if (parse_response())
        {
            Debug_printf("Timed out waiting for 230.\r\n");
            _tls_required = _tls_required || response_demands_tls();
            return true;
        }
    }
    else
    {
        Debug_printf("Will not send password. Response was: %s\r\n", controlResponse.c_str());
    }

    if (is_positive_completion_reply() && is_authentication())
    {
        Debug_printf("Logged in successfully. Setting type.\r\n");
        TYPE();
    }
    else
    {
        Debug_printf("Could not finish log in. Response was: %s\r\n", controlResponse.c_str());
        return true;
    }

    if (parse_response())
    {
        Debug_printf("Timed out waiting for 200.\r\n");
        return true;
    }

    if (is_positive_completion_reply() && is_syntax())
    {
        Debug_printf("Logged in\r\n");
    }
    else
    {
        Debug_printf("Could not set image type. Ignoring.\r\n");
    }

    return false;
}

bool fnFTP::logout()
{
    Debug_printf("fnFTP::logout()\r\n");
    if (!control->connected())
    {
        Debug_printf("Logout called when not connected.\r\n");
        return false;
    }

    if (data->connected())
    {
        ABOR();
        parse_response(); // Ignored.
        data->stop();
    }

    QUIT();

    if (parse_response())
    {
        Debug_printf("Timed out waiting for 221.\r\n");
    }

    control->stop();

    return false;
}

bool fnFTP::reconnect()
{
    Debug_println("Trying to re-login");
    if (control->connected()) logout();
    return login(username, password, hostname, control_port);
}

int32_t fnFTP::get_file_size(string path)
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::get_file_size(%s) attempted while not logged in. Aborting.\r\n", path.c_str());
        return (size_t)-1;
    }

    // Send SIZE command
    SIZE(path);

    if (parse_response())
    {
        Debug_printf("Timed out waiting for 213 response.\r\n");
        return -1;
    }

    if (status() == 213)
    {
        // Parse size from response
        int32_t size = strtoull(controlResponse.substr(4).c_str(), nullptr, 10);
        Debug_printf("File size of %s is %lu bytes.\r\n", path.c_str(), size);
        return size;
    }
    else
    {
        Debug_printf("Could not get file size. Response was: %s\r\n", controlResponse.c_str());
        return -1;
    }
}

bool fnFTP::open_file(string path, bool stor, unsigned long offset)
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::open_file(%s) attempted while not logged in. Aborting.\r\n", path.c_str());
        return true;
    }

    int retries = 2;
    while (get_data_port())
    {
        if ((is_negative_permanent_reply() || is_negative_transient_reply()) && retries--)
        {
            // recovery attempt
            fnSystem.delay(2000);
            if (!reconnect())
                continue; // successfully reconnected
        }
        Debug_printf("fnFTP::open_file(%s, %s) could not get data port. Aborting.\n", path.c_str(), stor ? "STOR" : "RETR");
        return true;
    }

    // Restart marker, if resuming. REST must come after the data connection is
    // set up and immediately before the transfer command; the server answers
    // 350 and applies it to the next RETR/STOR only.
    if (offset > 0)
    {
        REST(offset);
        if (parse_response() || _statusCode != 350)
        {
            Debug_printf("fnFTP::open_file(%s) - REST %lu refused: %s\r\n",
                         path.c_str(), offset, controlResponse.c_str());
            if (_active_mode)
                _active_server.stop();
            return true;
        }
    }

    // Do command
    if (stor == true)
    {
        STOR(path);
    }
    else
    {
        RETR(path);
    }

    if (parse_response())
    {
        Debug_printf("Timed out waiting for 150 response.\r\n");
        if (_active_mode)
            _active_server.stop();
        return true;
    }

    if (is_positive_preliminary_reply() && is_filesystem_related())
    {
        if (accept_active_connection())
        {
            Debug_printf("fnFTP::open_file(%s, %s) - active mode connection failed.\r\n", path.c_str(), stor ? "STOR" : "RETR");
            return true;
        }
        if (start_data_tls())
        {
            Debug_printf("fnFTP::open_file(%s, %s) - data channel TLS failed.\r\n", path.c_str(), stor ? "STOR" : "RETR");
            data->stop();
            return true;
        }
        _stor = stor;
        _expect_control_response = !stor;
        Debug_printf("Server began transfer.\r\n");
        return false;
    }
    else
    {
        Debug_printf("Server could not begin transfer. Response was: %s\r\n", controlResponse.c_str());
        if (_active_mode)
            _active_server.stop();
        return true;
    }
}

bool fnFTP::open_directory(string path, string pattern)
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::open_directory(%s%s) attempted while not logged in. Aborting.\r\n", path.c_str(), pattern.c_str());
        return true;
    }

    int retries = 2;
    while (get_data_port())
    {
        if ((is_negative_permanent_reply() || is_negative_transient_reply()) && retries--)
        {
            // recovery attempt
            fnSystem.delay(2000);
            if (!reconnect())
                continue; // successfully reconnected
        }
        Debug_printf("fnFTP::open_directory(%s%s) could not get data port, aborting.\n", path.c_str(), pattern.c_str());
        return true;
    }

    // perform LIST
    LIST(path, pattern);

    if (parse_response())
    {
        Debug_printf("fnFTP::open_directory(%s%s) Timed out waiting for 150 response.\r\n", path.c_str(), pattern.c_str());
        if (_active_mode)
            _active_server.stop();
        return true;
    }

    Debug_printf("fnFTP::open_directory(%s%s) - %s\r\n", path.c_str(), pattern.c_str(), controlResponse.c_str());

    if (is_positive_preliminary_reply() && is_filesystem_related())
    {
        // Do nothing.
        Debug_printf("Got our 150\r\n");
    }
    else
    {
        Debug_printf("Didn't get our 150\r\n");
        if (_active_mode)
            _active_server.stop();
        return true;
    }

    if (accept_active_connection())
    {
        Debug_printf("fnFTP::open_directory(%s%s) - active mode connection failed.\r\n", path.c_str(), pattern.c_str());
        return true;
    }

    if (start_data_tls())
    {
        Debug_printf("fnFTP::open_directory(%s%s) - data channel TLS failed.\r\n", path.c_str(), pattern.c_str());
        data->stop();
        return true;
    }

    // The listing is served line by line straight off the data socket by
    // read_directory(). It used to be read here in full into a stringstream,
    // which on a board without PSRAM exhausted the internal heap while the
    // buffer doubled, and operator new aborts (ESP-IDF builds -fno-exceptions).
    dirRemainder.clear();
    _dir_streaming = true;
    _dir_got_response = false;

    return false; // all good.
}

void fnFTP::close_directory()
{
    if (!_dir_streaming)
        return;

    _dir_streaming = false;
    dirRemainder.clear();

    // Unconditionally, NOT guarded by connected(): by the time a listing ends,
    // connected() has already returned false (it peeked and saw the server's
    // FIN), so a guard here leaves our half of the socket open. The server
    // waits for our FIN before sending its closing response and only gives up
    // after its own ~10 s timeout - which is exactly what a guard here cost,
    // measured at 10396 ms on every listing.
    data->stop();

    if (!_dir_got_response)
    {
        _dir_got_response = true;
        if (parse_response())
            Debug_printf("fnFTP::close_directory() - timed out waiting for 226.\r\n");
    }
}

bool fnFTP::next_directory_line(string &line)
{
    uint8_t buf[256];
    int tmout_counter = 1 + FTP_TIMEOUT / 50;

    while (_dir_streaming)
    {
        size_t nl = dirRemainder.find('\n');
        if (nl != string::npos)
        {
            line = dirRemainder.substr(0, nl);
            dirRemainder.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return true;
        }

        if (data->available() > 0)
        {
            int len = data->available();
            int num_read = data->read(buf, len > (int)sizeof(buf) ? sizeof(buf) : len);
            if (num_read < 0)
            {
                Debug_printf("fnFTP::next_directory_line() - read error\r\n");
                break;
            }
            if (num_read > 0)
            {
                dirRemainder.append((const char *)buf, num_read);
                tmout_counter = 1 + FTP_TIMEOUT / 50; // reset timeout counter
                continue;
            }
        }

        // Consume the closing response as soon as it turns up, so the control
        // channel is clear whether or not the caller reads to the end.
        if (!_dir_got_response && control->available())
        {
            _dir_got_response = true;
            parse_response();
        }

        if (!data->connected() && data->available() == 0)
            break; // server closed the data connection - listing complete

        if (--tmout_counter == 0)
        {
            Debug_printf("fnFTP::next_directory_line() - Timeout\r\n");
            break;
        }
        fnSystem.delay(50); // wait for more data
    }

    // Whatever is left without a terminator is still an entry.
    if (!dirRemainder.empty())
    {
        line = dirRemainder;
        dirRemainder.clear();
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        close_directory();
        return !line.empty();
    }

    close_directory();
    return false;
}

bool fnFTP::change_directory(string path)
{
    CWD(path);

    if (parse_response())  // returns true on error
        return false;

    // 250 is the usual answer, but some servers reply 200.
    return _statusCode >= 200 && _statusCode < 300;
}

bool fnFTP::read_directory(string &name, long &filesize, bool &is_dir)
{
    string line;
    struct ftpparse parse;

    if (!next_directory_line(line) || line.empty())
        return true; // no more entries

    //Debug_printf("fnFTP::read_directory - %s\r\n",line.c_str());
    ftpparse(&parse, (char *)line.c_str(), line.length());
    name = string(parse.name ? parse.name : "???");

    // Strip symlink target from name (e.g., "transfer -> crossplatform/transfer/" becomes "transfer")
    size_t arrow_pos = name.find(" -> ");
    if (arrow_pos != string::npos)
    {
        name = name.substr(0, arrow_pos);
    }

    filesize = parse.size;
    is_dir = (parse.flagtrycwd == 1);
    //Debug_printf("Name: \"%s\" size: %lu is_dir: %d\r\n", name.c_str(), filesize, is_dir);
    return false;
}

bool fnFTP::read_file(uint8_t *buf, unsigned short len, unsigned long range_begin, unsigned long range_end)
{
    // Debug_printv("fnFTP::read_file(%p, %u, %lu, %lu)", buf, len, range_begin, range_end);

    // If range parameters are provided and different from current, send RANG command
    if ((range_begin > 0 || range_end > 0) && (range_begin != _range_begin || range_end != _range_end))
    {
        RANG(range_begin, range_end);
        _range_begin = range_begin;
        _range_end = range_end;
    }

    if (!data->connected() && data->available() == 0)
    {
        Debug_printf("fnFTP::read_file(%p,%u) - data socket not connected, aborting.\r\n", buf, len);
        return true;
    }
    return len != data->read(buf, len);
}

bool fnFTP::write_file(uint8_t *buf, unsigned short len)
{
    //Debug_printf("fnFTP::write_file(%p,%u)\r\n", buf, len);
    if (!data->connected())
    {
        Debug_printf("fnFTP::write_file(%p,%u) - data socket not connected, aborting.\r\n", buf, len);
        return true;
    }

    return len != data->write(buf, len);
}

bool fnFTP::close()
{
    bool res = false;
    Debug_printf("fnFTP::close()\r\n");
    if (_stor)
    {
        if (data->connected())
        {
            data->stop();
        }
        if (parse_response())
        {
            Debug_printf("Timed out waiting for 226.\r\n");
            res = true;
        }
    }
    _stor = false;
    _expect_control_response = false;
    control->flush();
    return res;
}

int fnFTP::status()
{
    return _statusCode;
}

int fnFTP::data_available()
{
    return data->available();
}

void fnFTP::end_transfer()
{
    data->stop();

    if (_expect_control_response)
    {
        _expect_control_response = false;
        if (parse_response())
            Debug_printf("fnFTP::end_transfer() - timed out waiting for 226.\r\n");
    }

    _stor = false;
    control->flush();
}

bool fnFTP::data_connected()
{
    if (_expect_control_response && control->available())
        _expect_control_response = parse_response();
    return _expect_control_response || data->connected();
}

bool fnFTP::control_connected()
{
    return control != nullptr && control->connected();
}

/** FTP UTILITY FUNCTIONS **********************************************************************/

bool fnFTP::parse_response()
{
    char respBuf[384];  // room for control message incl. file path and file size
    int num_read = 0;
    bool multi_line = false;

    controlResponse.clear();

    while(true)
    {
        num_read = read_response_line(respBuf, sizeof(respBuf));
        if (num_read < 0)
        {
            // Timeout
            _statusCode = 421;  // service not available
            return true;        // error
        }
        if (num_read >= 4)
        {
            if (isdigit(respBuf[0]) && isdigit(respBuf[1]) && isdigit(respBuf[2]))
            {
                if (respBuf[3] == ' ')  // done, got NNN<space>
                    break;
                if (respBuf[3] == '-')
                {
                    // head of multi-line response
                    multi_line = true;
                    continue;
                }
            }
        }
        if (multi_line) // ignore body of multi-line response
            continue;
        // error - nothing above
        Debug_printf("fnFTP::parse_response() - failed\r\n");
        _statusCode = 501;  //syntax error
        return true;        // error
    }

    // update control response and status code
    controlResponse = string((char *)respBuf, num_read);
    _statusCode = atoi(controlResponse.substr(0, 3).c_str());
    Debug_printf("fnFTP::parse_response() - %d, \"%s\"\r\n", _statusCode, controlResponse.c_str());

    if (_statusCode >= 400)
        return true;

    return false; // ok
}

int fnFTP::read_response_line(char *buf, int buflen)
{
    int num_read = 0;
    int c;
    int tmout_counter = 1 + FTP_TIMEOUT / 50;

    while(true)
    {
        if (control->available() == 0)
        {
            if (--tmout_counter == 0)
            {
                Debug_printf("fnFTP::read_response_line() - Timeout waiting response\r\n");
                return -1;
            }
            fnSystem.delay(50);
            continue;
        }

        c = control->read(); // singe byte
        if (c < 0)
            break;  // read error

        if(c == '\n' || c == '\r') // almost done, got line
        {
            // eat all line terminators
            if (control->available())
            {
                // test next byte
                c = control->peek();
                if (c == '\n' || c== '\r')
                    continue; // read it
            }
            break; // done
        }
        // store char, ignore rest of too long response
        if (num_read < buflen)
            buf[num_read++] = (char) c;
        tmout_counter = 1 + FTP_TIMEOUT / 50; // reset timeout counter
    }
    return num_read;
}

bool fnFTP::get_data_port()
{
    Debug_printf("fnFTP::get_data_port()\r\n");

    _active_mode = false;
    control->flush();

    if (!get_data_port_epsv())
        return false; // success

    Debug_printf("EPSV failed (%s), falling back to PASV.\r\n", controlResponse.c_str());

    if (!get_data_port_pasv())
        return false; // success

    Debug_printf("PASV failed (%s), falling back to PORT (active mode).\r\n", controlResponse.c_str());

    return get_data_port_port();
}

bool fnFTP::get_data_port_epsv()
{
    size_t port_pos_beg, port_pos_end;

    Debug_printf("fnFTP::get_data_port()\r\n");

    control->flush();
    EPSV();

    Debug_printf("Did EPSV, getting response.\r\n");

    if (parse_response())
    {
        Debug_printf("Timed out waiting for response.\r\n");
        return true;
    }

    // accept only 229 response: Entering Extended Passive Mode (|||nnnn|)
    if (_statusCode != 229)
    {
        Debug_printf("Cannot get data port. Response was: %s\n", controlResponse.c_str());
        return true;
    }

    // At this point, we have a port mapping trapped in (|||1234|), peel it out of there.
    port_pos_beg = controlResponse.find_first_of("|") + 3;
    port_pos_end = controlResponse.find_last_of("|");
    data_port = atoi(controlResponse.substr(port_pos_beg, port_pos_end).c_str());

    Debug_printf("Server gave us data port: %u\r\n", data_port);

    // Go ahead and connect to data port, so that control port is unblocked, if it's blocked.
    if (!data->connect(hostname.c_str(), data_port, FTP_TIMEOUT))
    {
        Debug_printf("Could not open data port %u, errno = %u\r\n", data_port, errno);
        return true;
    }
    else
    {
        Debug_printf("Data port %u opened (EPSV).\r\n", data_port);
    }

    return false;
}

bool fnFTP::get_data_port_pasv()
{
    PASV();

    Debug_printf("Did PASV, getting response.\r\n");

    if (parse_response())
    {
        Debug_printf("Timed out waiting for response.\r\n");
        return true;
    }

    // accept only 227 response: Entering Passive Mode (h1,h2,h3,h4,p1,p2)
    if (_statusCode != 227)
    {
        Debug_printf("Cannot get data port. Response was: %s\n", controlResponse.c_str());
        return true;
    }

    size_t paren_beg = controlResponse.find('(');
    size_t paren_end = controlResponse.find(')', paren_beg == string::npos ? 0 : paren_beg);
    if (paren_beg == string::npos || paren_end == string::npos)
    {
        Debug_printf("Could not parse PASV response: %s\r\n", controlResponse.c_str());
        return true;
    }

    string nums = controlResponse.substr(paren_beg + 1, paren_end - paren_beg - 1);
    unsigned int h1, h2, h3, h4, p1, p2;
    if (sscanf(nums.c_str(), "%u,%u,%u,%u,%u,%u", &h1, &h2, &h3, &h4, &p1, &p2) != 6)
    {
        Debug_printf("Could not parse PASV address: %s\r\n", nums.c_str());
        return true;
    }

    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u", h1, h2, h3, h4);
    data_port = (uint16_t)((p1 << 8) | p2);

    Debug_printf("Server gave us data address %s:%u\r\n", ip_str, data_port);

    // Note: some servers behind NAT report an internal/unreachable IP here;
    // we use it as given, per RFC 959.
    if (!data->connect(ip_str, data_port, FTP_TIMEOUT))
    {
        Debug_printf("Could not open data port %s:%u, errno = %u\r\n", ip_str, data_port, errno);
        return true;
    }
    else
    {
        Debug_printf("Data port %s:%u opened (PASV).\r\n", ip_str, data_port);
    }

    return false;
}

bool fnFTP::get_data_port_port()
{
    if (!_active_server.begin(0))
    {
        Debug_printf("Could not start listening socket for active mode.\r\n");
        return true;
    }

    in_addr_t local_ip = control->localIP();
    uint16_t local_port = _active_server.port();
    const uint8_t *ip_bytes = (const uint8_t *)&local_ip;

    PORT(ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3], local_port);

    if (parse_response())
    {
        Debug_printf("Timed out waiting for response.\r\n");
        _active_server.stop();
        return true;
    }

    if (!is_positive_completion_reply())
    {
        Debug_printf("Server rejected PORT. Response was: %s\r\n", controlResponse.c_str());
        _active_server.stop();
        return true;
    }

    _active_mode = true;
    Debug_printf("Listening on port %u for server to connect (PORT).\r\n", local_port);

    return false;
}

bool fnFTP::accept_active_connection()
{
    if (!_active_mode)
        return false; // nothing to do, EPSV/PASV already connected

    int tmout_counter = 1 + FTP_TIMEOUT / 50;
    while (!_active_server.hasClient())
    {
        if (--tmout_counter == 0)
        {
            Debug_printf("fnFTP::accept_active_connection() - timed out waiting for server to connect.\r\n");
            _active_server.stop();
            return true;
        }
        fnSystem.delay(50);
    }

    data->adopt(_active_server.client());
    _active_server.stop(); // done listening, we only needed the one connection

    Debug_printf("fnFTP::accept_active_connection() - server connected.\r\n");
    return false;
}

/** FTP VERBS **********************************************************************************/

void fnFTP::AUTH_TLS()
{
    control->write("AUTH TLS\r\n");
}

void fnFTP::PBSZ()
{
    control->write("PBSZ 0\r\n");
}

void fnFTP::PROT(char level)
{
    control->write(string("PROT ") + level + "\r\n");
}

void fnFTP::USER()
{
    control->write("USER " + username + "\r\n");
}

void fnFTP::PASS()
{
    control->write("PASS " + password + "\r\n");
}

void fnFTP::TYPE()
{
    Debug_printf("fnFTP::TYPE()\r\n");
    control->write("TYPE I\r\n");
}

void fnFTP::QUIT()
{
    Debug_printf("fnFTP::QUIT()\r\n");
    control->write("QUIT\r\n");
}

void fnFTP::EPSV()
{
    Debug_printf("fnFTP::EPSV()\r\n");
    control->write("EPSV\r\n");
}

void fnFTP::PASV()
{
    Debug_printf("fnFTP::PASV()\r\n");
    control->write("PASV\r\n");
}

void fnFTP::PORT(uint8_t h1, uint8_t h2, uint8_t h3, uint8_t h4, uint16_t port)
{
    Debug_printf("fnFTP::PORT(%u.%u.%u.%u:%u)\r\n", h1, h2, h3, h4, port);
    control->write("PORT " + std::to_string(h1) + "," + std::to_string(h2) + "," +
                   std::to_string(h3) + "," + std::to_string(h4) + "," +
                   std::to_string(port >> 8) + "," + std::to_string(port & 0xff) + "\r\n");
}

void fnFTP::RETR(string path)
{
    Debug_printf("fnFTP::RETR(%s)\r\n",path.c_str());
    control->write("RETR " + path + "\r\n");
}

void fnFTP::CWD(string path)
{
    Debug_printf("fnFTP::CWD(%s)\r\n",path.c_str());
    control->write("CWD " + path + "\r\n");
}

void fnFTP::REST(unsigned long offset)
{
    Debug_printf("fnFTP::REST(%lu)\r\n", offset);
    control->write("REST " + std::to_string(offset) + "\r\n");
}

void fnFTP::LIST(string path, string pattern)
{
    Debug_printf("fnFTP::LIST(%s,%s)\r\n",path.c_str(),pattern.c_str());
    control->write("LIST " + path + pattern + "\r\n");
}

void fnFTP::ABOR()
{
    Debug_printf("fnFTP::ABOR()\r\n");
    control->write("ABOR\r\n");
}

void fnFTP::STOR(string path)
{
    Debug_printf("fnFTP::STOR(%s)\r\n",path.c_str());
    control->write("STOR " + path + "\r\n");
}

void fnFTP::RANG(unsigned long start, unsigned long end)
{
    Debug_printf("fnFTP::RANG(%lu,%lu)\r\n", start, end);
    control->write("RANG " + std::to_string(start) + "-" + std::to_string(end) + "\r\n");
    if (parse_response())
    {
        Debug_printf("fnFTP::RANG - error response from server\r\n");
    }
}

void fnFTP::SIZE(string path)
{
    Debug_printf("fnFTP::SIZE(%s)\r\n",path.c_str());
    control->write("SIZE " + path + "\r\n");
}

void fnFTP::NOOP()
{
    Debug_printf("fnFTP::NOOP\r\n");
    control->write("NOOP\r\n");
}

bool fnFTP::keep_alive()
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::keep_alive() attempted while not logged in. Aborting.\r\n");
        return false;
    }

    NOOP();
    if (parse_response())
    {
        Debug_printf("fnFTP::keep_alive - timeout\r\n");
        return false;
    }

    if (is_positive_completion_reply())
    {
        Debug_printf("fnFTP::keep_alive - successful\r\n");
        return true;
    }
    else
    {
        Debug_printf("fnFTP::keep_alive - error: %s\r\n", controlResponse.c_str());
        return false;
    }
}


bool fnFTP::delete_file(string path)
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::delete_file(%s) attempted while not logged in. Aborting.\r\n", path.c_str());
        return true;
    }

    DELE(path);
    if (parse_response())
    {
        Debug_printf("fnFTP::delete_file - timeout\r\n");
        return true;
    }

    if (is_positive_completion_reply())
    {
        Debug_printf("fnFTP::delete_file - file deleted\r\n");
        return false;
    }
    else
    {
        Debug_printf("fnFTP::delete_file - error: %s\r\n", controlResponse.c_str());
        return true;
    }
}

bool fnFTP::rename_file(string pathFrom, string pathTo)
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::rename_file(%s -> %s) attempted while not logged in. Aborting.\r\n", pathFrom.c_str(), pathTo.c_str());
        return true;
    }

    RNFR(pathFrom);
    if (parse_response())
    {
        Debug_printf("fnFTP::rename_file - timeout on RNFR\r\n");
        return true;
    }

    if (!is_positive_intermediate_reply())
    {
        Debug_printf("fnFTP::rename_file - RNFR error: %s\r\n", controlResponse.c_str());
        return true;
    }

    RNTO(pathTo);
    if (parse_response())
    {
        Debug_printf("fnFTP::rename_file - timeout on RNTO\r\n");
        return true;
    }

    if (is_positive_completion_reply())
    {
        Debug_printf("fnFTP::rename_file - file renamed\r\n");
        return false;
    }
    else
    {
        Debug_printf("fnFTP::rename_file - RNTO error: %s\r\n", controlResponse.c_str());
        return true;
    }
}

bool fnFTP::make_directory(string path)
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::make_directory(%s) attempted while not logged in. Aborting.\r\n", path.c_str());
        return true;
    }

    MKD(path);
    if (parse_response())
    {
        Debug_printf("fnFTP::make_directory - timeout\r\n");
        return true;
    }

    if (is_positive_completion_reply())
    {
        Debug_printf("fnFTP::make_directory - directory created\r\n");
        return false;
    }
    else
    {
        Debug_printf("fnFTP::make_directory - error: %s\r\n", controlResponse.c_str());
        return true;
    }
}

bool fnFTP::remove_directory(string path)
{
    if (!control->connected())
    {
        Debug_printf("fnFTP::remove_directory(%s) attempted while not logged in. Aborting.\r\n", path.c_str());
        return true;
    }

    RMD(path);
    if (parse_response())
    {
        Debug_printf("fnFTP::remove_directory - timeout\r\n");
        return true;
    }

    if (is_positive_completion_reply())
    {
        Debug_printf("fnFTP::remove_directory - directory removed\r\n");
        return false;
    }
    else
    {
        Debug_printf("fnFTP::remove_directory - error: %s\r\n", controlResponse.c_str());
        return true;
    }
}

void fnFTP::DELE(string path)
{
    Debug_printf("fnFTP::DELE(%s)\r\n", path.c_str());
    control->write("DELE " + path + "\r\n");
}

void fnFTP::RNFR(string pathFrom)
{
    Debug_printf("fnFTP::RNFR(%s)\r\n", pathFrom.c_str());
    control->write("RNFR " + pathFrom + "\r\n");
}

void fnFTP::RNTO(string pathTo)
{
    Debug_printf("fnFTP::RNTO(%s)\r\n", pathTo.c_str());
    control->write("RNTO " + pathTo + "\r\n");
}

void fnFTP::MKD(string path)
{
    Debug_printf("fnFTP::MKD(%s)\r\n", path.c_str());
    control->write("MKD " + path + "\r\n");
}

void fnFTP::RMD(string path)
{
    Debug_printf("fnFTP::RMD(%s)\r\n", path.c_str());
    control->write("RMD " + path + "\r\n");
}