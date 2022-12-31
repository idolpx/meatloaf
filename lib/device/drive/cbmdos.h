// based on doscmd.h/c from SD2IEC by Ingo Korb
//
// https://c64os.com/post/sd2iecdocumentation
//

#ifndef CBMDOS_H
#define CBMDOS_H

#include <stdint.h>
#include <ctime>

#define CONFIG_COMMAND_BUFFER_SIZE 254

typedef struct date
{
    uint8_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} __attribute__((packed)) date_t;

typedef struct
{
    uint32_t fat;
    struct
    {
        uint8_t track;
        uint8_t sector;
    } dxx;
} dir_t;

typedef struct
{
    uint8_t part;
    dir_t dir;
} path_t;

typedef enum
{
    FL_NONE = 0,
    FL_DREAMLOAD,
    FL_DREAMLOAD_OLD,
    FL_TURBODISK,
    FL_FC3_LOAD,
    FL_FC3_SAVE,
    FL_FC3_FREEZED,
    FL_ULOAD3,
    FL_GI_JOE,
    FL_EPYXCART,
    FL_GEOS_S1_64,
    FL_GEOS_S1_128,
    FL_GEOS_S23_1541,
    FL_GEOS_S23_1571,
    FL_GEOS_S23_1581,
    FL_WHEELS_S1_64,
    FL_WHEELS_S1_128,
    FL_WHEELS_S2,
    FL_WHEELS44_S2,
    FL_WHEELS44_S2_1581,
    FL_NIPPON,
    FL_AR6_1581_LOAD,
    FL_AR6_1581_SAVE,
    FL_ELOAD1,
    FL_FC3_OLDFREEZED,
    FL_MMZAK,
    FL_N0SDOS_FILEREAD
} fastloaderid_t;

extern uint8_t current_error;
// extern uint8_t error_buffer[CONFIG_ERROR_BUFFER_SIZE];

void set_error_ts(uint8_t errornum, uint8_t track, uint8_t sector);
void set_error(uint8_t errornum);
uint8_t set_ok_message(buffer_t *buf);

// Commodore DOS error codes
#define ERROR_OK 0
#define ERROR_SCRATCHED 1
#define ERROR_PARTITION_SELECTED 2
#define ERROR_STATUS 3
#define ERROR_LONGVERSION 9
#define ERROR_READ_NOHEADER 20
#define ERROR_READ_NOSYNC 21
#define ERROR_READ_NODATA 22
#define ERROR_READ_CHECKSUM 23
#define ERROR_WRITE_VERIFY 25
#define ERROR_WRITE_PROTECT 26
#define ERROR_READ_HDRCHECKSUM 27
#define ERROR_DISK_ID_MISMATCH 29
#define ERROR_SYNTAX_UNKNOWN 30
#define ERROR_SYNTAX_UNABLE 31
#define ERROR_SYNTAX_TOOLONG 32
#define ERROR_SYNTAX_JOKER 33
#define ERROR_SYNTAX_NONAME 34
#define ERROR_FILE_NOT_FOUND_39 39
#define ERROR_RECORD_MISSING 50
#define ERROR_RECORD_OVERFLOW 51
#define ERROR_FILE_TOO_LARGE 52
#define ERROR_WRITE_FILE_OPEN 60
#define ERROR_FILE_NOT_OPEN 61
#define ERROR_FILE_NOT_FOUND 62
#define ERROR_FILE_EXISTS 63
#define ERROR_FILE_TYPE_MISMATCH 64
#define ERROR_NO_BLOCK 65
#define ERROR_ILLEGAL_TS_COMMAND 66
#define ERROR_ILLEGAL_TS_LINK 67
#define ERROR_NO_CHANNEL 70
#define ERROR_DIR_ERROR 71
#define ERROR_DISK_FULL 72
#define ERROR_DOSVERSION 73
#define ERROR_DRIVE_NOT_READY 74
#define ERROR_PARTITION_ILLEGAL 77
#define ERROR_BUFFER_TOO_SMALL 78
#define ERROR_IMAGE_INVALID 79
#define ERROR_UNKNOWN_DRIVECODE 98
#define ERROR_CLOCK_UNSTABLE 99

class cbmDOS
{
    uint8_t command_buffer[CONFIG_COMMAND_BUFFER_SIZE + 2];
    uint8_t command_length;
    date_t date_match_start;
    date_t date_match_end;

    uint16_t datacrc;

    void run_loader(uint16_t address);
    void clean_cmdbuffer(void);
    int8_t parse_blockparam(uint8_t values[]);
    uint8_t parse_bool(void);

    // Command Handlers
    void parse_mkdir(void);
    void do_chdir(uint8_t *parsestr);
    void parse_chdir(void);
    void parse_rmdir(void);
    void parse_dircommand(void);

    void parse_block(void);
    void parse_copy(void);
    void parse_changepart(void);
    void parse_direct(void);
    void parse_getpartition(void);
    void parse_initialize(void);

    void handle_memexec(void);
    void handle_memread(void);
    void capture_fl_data(uint16_t address, uint8_t length);
    void handle_memwrite(void);
    void parse_memory(void);

    void parse_new(void);
    void parse_position(void);
    void parse_rename(void);
    void parse_scratch(void);

    void parse_timeread(void);
    void parse_timewrite(void);
    void parse_time(void);

    void parse_user(void);
    void parse_xcommand(void);

    void parse_doscommand(void);
    void do_chdir(uint8_t *parsestr);
};

#endif // CBMDOS_H
