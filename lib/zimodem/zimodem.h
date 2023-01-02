/*
   Copyright 2016-2019 Bo Zimmerman

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License. 
*/
//#define TCP_SND_BUF                     4 * TCP_MSS
#define ZIMODEM_VERSION "3.7.1"
const char compile_date[] = __DATE__ " " __TIME__;
#define DEFAULT_NO_DELAY true
#define null 0
#define INCLUDE_IRCC true
//#define INCLUDE_SLIP true
//# define USE_DEVUPDATER true // only enable this if your name is Bo

#ifdef ARDUINO_ESP32_DEV
# define ZIMODEM_ESP32
#elif defined(ESP32)
# define ZIMODEM_ESP32
#elif defined(ARDUINO_ESP320)
# define ZIMODEM_ESP32
#elif defined(ARDUINO_NANO32)
# define ZIMODEM_ESP32
#elif defined(ARDUINO_LoLin32)
# define ZIMODEM_ESP32
#elif defined(ARDUINO_ESPea32)
# define ZIMODEM_ESP32
#elif defined(ARDUINO_QUANTUM)
# define ZIMODEM_ESP32
#else
# define ZIMODEM_ESP8266
#endif

#ifdef SUPPORT_LED_PINS
# ifdef GPIO_NUM_0
#   define DEFAULT_PIN_AA GPIO_NUM_16
#   define DEFAULT_PIN_HS GPIO_NUM_15
#   define DEFAULT_PIN_WIFI GPIO_NUM_0
# else
#   define DEFAULT_PIN_AA 16
#   define DEFAULT_PIN_HS 15
#   define DEFAULT_PIN_WIFI 0
# endif
# define DEFAULT_HS_BAUD 38400
# define DEFAULT_AA_ACTIVE LOW
# define DEFAULT_AA_INACTIVE HIGH
# define DEFAULT_HS_ACTIVE LOW
# define DEFAULT_HS_INACTIVE HIGH
# define DEFAULT_WIFI_ACTIVE LOW
# define DEFAULT_WIFI_INACTIVE HIGH
#endif

#ifdef ZIMODEM_ESP32
# define PIN_FACTORY_RESET GPIO_NUM_0
# define DEFAULT_PIN_DCD GPIO_NUM_14
# define DEFAULT_PIN_CTS GPIO_NUM_13
# define DEFAULT_PIN_RTS GPIO_NUM_15 // unused
# define DEFAULT_PIN_RI GPIO_NUM_32
# define DEFAULT_PIN_DSR GPIO_NUM_12
# define DEFAULT_PIN_DTR GPIO_NUM_27
# define debugPrintf Serial.printf
# define INCLUDE_SD_SHELL true
# define DEFAULT_FCT FCT_DISABLED
# define SerialConfig uint32_t
# define UART_CONFIG_MASK 0x8000000
# define UART_NB_BIT_MASK      0B00001100 | UART_CONFIG_MASK
# define UART_NB_BIT_5         0B00000000 | UART_CONFIG_MASK
# define UART_NB_BIT_6         0B00000100 | UART_CONFIG_MASK
# define UART_NB_BIT_7         0B00001000 | UART_CONFIG_MASK
# define UART_NB_BIT_8         0B00001100 | UART_CONFIG_MASK
# define UART_PARITY_MASK      0B00000011
# define UART_PARITY_NONE      0B00000000
# define UART_NB_STOP_BIT_MASK 0B00110000
# define UART_NB_STOP_BIT_0    0B00000000
# define UART_NB_STOP_BIT_1    0B00010000
# define UART_NB_STOP_BIT_15   0B00100000
# define UART_NB_STOP_BIT_2    0B00110000
# define preEOLN serial.prints
# define echoEOLN serial.write
//# define HARD_DCD_HIGH 1
//# define HARD_DCD_LOW 1
//# define INCLUDE_HOSTCM true // do this for special SP9000 modems only
#else  // ESP-8266, e.g. ESP-01, ESP-12E, inverted for C64Net WiFi Modem
# define DEFAULT_PIN_DSR 13
# define DEFAULT_PIN_DTR 12
# define DEFAULT_PIN_RI 14
# define DEFAULT_PIN_RTS 4
# define DEFAULT_PIN_CTS 5 // is 0 for ESP-01, see getDefaultCtsPin() below.
# define DEFAULT_PIN_DCD 2
# define DEFAULT_FCT FCT_DISABLED
# define RS232_INVERTED 1
# define debugPrintf doNothing
# define preEOLN(...)
# define echoEOLN(...) serial.prints(EOLN)
#endif

#ifdef RS232_INVERTED
# define DEFAULT_DCD_HIGH  HIGH
# define DEFAULT_DCD_LOW  LOW
# define DEFAULT_CTS_HIGH  HIGH
# define DEFAULT_CTS_LOW  LOW
# define DEFAULT_RTS_HIGH  HIGH
# define DEFAULT_RTS_LOW  LOW
# define DEFAULT_RI_HIGH  HIGH
# define DEFAULT_RI_LOW  LOW
# define DEFAULT_DSR_HIGH  HIGH
# define DEFAULT_DSR_LOW  LOW
# define DEFAULT_DTR_HIGH  HIGH
# define DEFAULT_DTR_LOW  LOW
#else
# define DEFAULT_DCD_HIGH  LOW
# define DEFAULT_DCD_LOW  HIGH
# define DEFAULT_CTS_HIGH  LOW
# define DEFAULT_CTS_LOW  HIGH
# define DEFAULT_RTS_HIGH  LOW
# define DEFAULT_RTS_LOW  HIGH
# define DEFAULT_RI_HIGH  LOW
# define DEFAULT_RI_LOW  HIGH
# define DEFAULT_DSR_HIGH  LOW
# define DEFAULT_DSR_LOW  HIGH
# define DEFAULT_DTR_HIGH  LOW
# define DEFAULT_DTR_LOW  HIGH
#endif

#define DEFAULT_BAUD_RATE 1200
#define DEFAULT_SERIAL_CONFIG SERIAL_8N1
#define MAX_PIN_NO 50
#define INTERNAL_FLOW_CONTROL_DIV 380
#define DEFAULT_RECONNECT_DELAY 5000
#define MAX_RECONNECT_DELAY 1800000

class ZMode
{
  public:
    virtual void serialIncoming();
    virtual void loop();
};

#include "pet2asc.h"
#include "rt_clock.h"
#include "filelog.h"
#include "serout.h"
#include "connSettings.h"
#include "wificlientnode.h"
#include "stringstream.h"
#include "phonebook.h"
#include "wifiservernode.h"
#include "zstream.h"
#include "proto_http.h"
#include "proto_ftp.h"
#include "zconfigmode.h"
#include "zcommand.h"
#include "zprint.h"

#ifdef INCLUDE_SD_SHELL
#  ifdef INCLUDE_HOSTCM
#    include "zhostcmmode.h"
#  endif
#  include "proto_xmodem.h"
#  include "proto_zmodem.h"
#  include "proto_kermit.h"
#  include "zbrowser.h"
#endif
#ifdef INCLUDE_SLIP
#  include "zslipmode.h"
#endif
#ifdef INCLUDE_IRCC
#  include "zircmode.h"
#endif

static WiFiClientNode *conns = null;
static WiFiServerNode *servs = null;
static PhoneBookEntry *phonebook = null;
static bool pinSupport[MAX_PIN_NO];
static bool browseEnabled = false;
static std::string termType = DEFAULT_TERMTYPE;
static std::string busyMsg = DEFAULT_BUSYMSG;

static ZMode *currMode = null;
static ZStream streamMode;
//static ZSlip slipMode; // not yet implemented
static ZCommand commandMode;
static ZPrint printMode;
static ZConfig configMode;
static RealTimeClock zclock(0);
#ifdef INCLUDE_SD_SHELL
#  ifdef INCLUDE_HOSTCM
     static ZHostCMMode hostcmMode;
#  endif
   static ZBrowser browseMode;
#endif
#ifdef INCLUDE_SLIP
   static ZSLIPMode slipMode;
#endif
#ifdef INCLUDE_IRCC
   static ZIRCMode ircMode;
#endif

enum BaudState
{
  BS_NORMAL,
  BS_SWITCH_TEMP_NEXT,
  BS_SWITCHED_TEMP,
  BS_SWITCH_NORMAL_NEXT
};

static std::string wifiSSI;
static std::string wifiPW;
static std::string hostname;
static IPAddress *staticIP = null;
static IPAddress *staticDNS = null;
static IPAddress *staticGW = null;
static IPAddress *staticSN = null;
static unsigned long lastConnectAttempt = 0;
static unsigned long nextReconnectDelay = 0; // zero means don't attempt reconnects
static SerialConfig serialConfig = DEFAULT_SERIAL_CONFIG;
static int baudRate=DEFAULT_BAUD_RATE;
static int dequeSize=1+(DEFAULT_BAUD_RATE/INTERNAL_FLOW_CONTROL_DIV);
static BaudState baudState = BS_NORMAL; 
static unsigned long resetPushTimer=0;
static int tempBaud = -1; // -1 do nothing
static int dcdStatus = LOW;
static int pinDCD = DEFAULT_PIN_DCD;
static int pinCTS = DEFAULT_PIN_CTS;
static int pinRTS = DEFAULT_PIN_RTS;
static int pinDSR = DEFAULT_PIN_DSR;
static int pinDTR = DEFAULT_PIN_DTR;
static int pinRI = DEFAULT_PIN_RI;
static int dcdActive = DEFAULT_DCD_HIGH;
static int dcdInactive = DEFAULT_DCD_LOW;
static int ctsActive = DEFAULT_CTS_HIGH;
static int ctsInactive = DEFAULT_CTS_LOW;
static int rtsActive = DEFAULT_RTS_HIGH;
static int rtsInactive = DEFAULT_RTS_LOW;
static int riActive = DEFAULT_RI_HIGH;
static int riInactive = DEFAULT_RI_LOW;
static int dtrActive = DEFAULT_DTR_HIGH;
static int dtrInactive = DEFAULT_DTR_LOW;
static int dsrActive = DEFAULT_DSR_HIGH;
static int dsrInactive = DEFAULT_DSR_LOW;

static int getDefaultCtsPin();
static void doNothing(const char* format, ...);
static void s_pinWrite(uint8_t pinNo, uint8_t value);
static void setHostName(const char *hname);
static void setNewStaticIPs(IPAddress *ip, IPAddress *dns, IPAddress *gateWay, IPAddress *subNet);
static bool connectWifi(const char* ssid, const char* password, IPAddress *ip, IPAddress *dns, IPAddress *gateWay, IPAddress *subNet);
static void checkBaudChange();
static void changeBaudRate(int baudRate);
static void changeSerialConfig(SerialConfig conf);
static int checkOpenConnections();
void checkReconnect();
void checkFactoryReset();

void setup();
void service();
