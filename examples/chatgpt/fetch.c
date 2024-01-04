#include <c64.h>
#include <cbm.h>
#include <string.h>
#include <stdlib.h>

#define LFN 2     // The logical file number to use for I/O
#define DEV 12    // The network device #
#define SAN 2     // The secondary address (SA) to use on DEV.
#define CMD 15    // The secondary address for the Command channel

const char url[]="https://api.openai.com/v1/chat/completions";

const char cmd_parse[]="jsonparse,2";
const char response_query[] = "jq,2,/choices[0]/message/content";

static char tmp[80];

/**
 * @brief fetch chatgpt response to prompt
 * @param prompt_s pointer to prompt string
 * @param response_s pointer to response string
 */
void fetch(char *prompt_s, char *response_s)
{
  memset(tmp,0,sizeof(tmp));
  memset(prompt_s,0,255);
  memset(response_s,0,1024);

  cbm_open(CMD,DEV,CMD,"");
  cbm_open(LFN,DEV,SAN,url);

  cbm_write(CMD,cmd_parse,sizeof(cmd_parse));

  cbm_write(CMD,ts_query,sizeof(ts_query));
  memset(tmp,0,sizeof(tmp));
  cbm_read(LFN,tmp,sizeof(tmp));
  *ts = atol(tmp);

  cbm_write(CMD,lon_query,sizeof(lon_query));
  memset(tmp,0,sizeof(tmp));
  cbm_read(LFN,tmp,sizeof(tmp));
  strcpy(lon_s,tmp);
  *lon = atoi(tmp);

  cbm_write(CMD,lat_query,sizeof(lat_query));
  memset(tmp,0,sizeof(tmp));
  cbm_read(LFN,tmp,sizeof(tmp));
  strcpy(lat_s,tmp);
  *lat = atoi(tmp);

  cbm_close(LFN);
  cbm_close(CMD);
}