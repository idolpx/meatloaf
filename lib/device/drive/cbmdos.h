// based on doscmd.h/c from SD2IEC by Ingo Korb

#ifndef CBMDOS_H
#define CBMDOS_H

#define CONFIG_COMMAND_BUFFER_SIZE 254

class cbmDOS {
    uint8_t command_buffer[CONFIG_COMMAND_BUFFER_SIZE+2];
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
}


#endif // CBMDOS_H
