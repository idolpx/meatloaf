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

//#include "../../include/global_defines.h"
//#include "debug.h"

#include "iec_device.h"
#include "iec.h"

using namespace CBM;

namespace
{

	// Buffer for incoming and outgoing serial bytes and other stuff.
	char serCmdIOBuf[MAX_BYTES_PER_REQUEST];

} // unnamed namespace

Interface::Interface(IEC &iec, FS *fileSystem)
	: m_iec(iec)
	  // NOTE: Householding with RAM bytes: We use the middle of serial buffer for the ATNCmd buffer info.
	  // This is ok and won't be overwritten by actual serial data from the host, this is because when this ATNCmd data is in use
	  // only a few bytes of the actual serial data will be used in the buffer.
	  ,
	  m_atn_cmd(*reinterpret_cast<IEC::ATNCmd *>(&serCmdIOBuf[sizeof(serCmdIOBuf) / 2])), m_device(fileSystem)
//,  m_jsonHTTPBuffer(1024)
{
	m_fileSystem = fileSystem;
	DeviceDB *m_device = new DeviceDB(fileSystem);
	reset();
} // ctor

bool Interface::begin()
{
	m_device.init(String(DEVICE_DB));
	//m_device.check();
}

void Interface::reset(void)
{
	m_openState = O_NOTHING;
	m_queuedError = ErrIntro;
} // reset

void Interface::sendStatus(void)
{
	byte i, readResult;

	String status = String("00, OK, 00, 08");

	Debug_printf("\r\nsendStatus: ");
	// Length does not include the CR, write all but the last one should be with EOI.
	for (i = 0; i < readResult - 2; ++i)
		m_iec.send(status[i]);

	// ...and last byte in string as with EOI marker.
	m_iec.sendEOI(status[i]);
} // sendStatus

void Interface::sendDeviceInfo()
{
	Debug_printf("\r\nsendDeviceInfo:\r\n");

	// Reset basic memory pointer:
	uint16_t basicPtr = C64_BASIC_START;

	// #if defined(USE_LITTLEFS)
	FSInfo64 fs_info;
	m_fileSystem->info64(fs_info);
	// #endif
	char floatBuffer[10]; // buffer
	dtostrf(getFragmentation(), 3, 2, floatBuffer);

	// Send load address
	m_iec.send(C64_BASIC_START bitand 0xff);
	m_iec.send((C64_BASIC_START >> 8) bitand 0xff);
	Debug_println("");

	// Send List HEADER
	sendLine(basicPtr, 0, "\x14\x14\x12 %s V%s ", PRODUCT_ID, FW_VERSION);

	// CPU
	sendLine(basicPtr, 0, "\x14\x14" "SYSTEM ---");
	String sdk = String(ESP.getSdkVersion());
	sdk.toUpperCase();
	sendLine(basicPtr, 0, "\x14\x14" "SDK VER    : %s", sdk.c_str());
	//sendLine(basicPtr, 0, "BOOT VER   : %08X", ESP.getBootVersion());
	//sendLine(basicPtr, 0, "BOOT MODE  : %08X", ESP.getBootMode());
	//sendLine(basicPtr, 0, "CHIP ID    : %08X", ESP.getChipId());
	sendLine(basicPtr, 0, "\x14\x14" "CPU MHZ    : %d MHZ", ESP.getCpuFreqMHz());
	sendLine(basicPtr, 0, "\x14\x14" "CYCLES     : %u", ESP.getCycleCount());

	// POWER
	sendLine(basicPtr, 0, "\x14\x14" "POWER ---");
	//sendLine(basicPtr, 0, "VOLTAGE    : %d.%d V", ( ESP.getVcc() / 1000 ), ( ESP.getVcc() % 1000 ));

	// RAM
	sendLine(basicPtr, 0, "\x14\x14" "MEMORY ---");
	sendLine(basicPtr, 0, "\x14\x14" "RAM SIZE   : %5d B", getTotalMemory());
	sendLine(basicPtr, 0, "\x14\x14" "RAM FREE   : %5d B", getTotalAvailableMemory());
	sendLine(basicPtr, 0, "\x14\x14" "RAM >BLK   : %5d B", getLargestAvailableBlock());
	sendLine(basicPtr, 0, "\x14\x14" "RAM FRAG   : %s %%", floatBuffer);

	// ROM
	sendLine(basicPtr, 0, "\x14\x14" "ROM SIZE   : %5d B", ESP.getSketchSize() + ESP.getFreeSketchSpace());
	sendLine(basicPtr, 0, "\x14\x14" "ROM USED   : %5d B", ESP.getSketchSize());
	sendLine(basicPtr, 0, "\x14\x14" "ROM FREE   : %5d B", ESP.getFreeSketchSpace());

	// FLASH
	sendLine(basicPtr, 0, "\x14\x14" "STORAGE ---");
	//sendLine(basicPtr, 0, "FLASH SIZE : %5d B", ESP.getFlashChipRealSize());
	sendLine(basicPtr, 0, "\x14\x14" "FLASH SPEED: %d MHZ", (ESP.getFlashChipSpeed() / 1000000));

	// FILE SYSTEM
	sendLine(basicPtr, 0, "\x14\x14" "FILE SYSTEM ---");
	sendLine(basicPtr, 0, "\x14\x14" "TYPE       : %s", FS_TYPE);
	//  #if defined(USE_LITTLEFS)
	sendLine(basicPtr, 0, "\x14\x14" "SIZE       : %5d B", fs_info.totalBytes);
	sendLine(basicPtr, 0, "\x14\x14" "USED       : %5d B", fs_info.usedBytes);
	sendLine(basicPtr, 0, "\x14\x14" "FREE       : %5d B", fs_info.totalBytes - fs_info.usedBytes);
	//  #endif

	// NETWORK
	sendLine(basicPtr, 0, "\x14\x14" "NETWORK ---");
	char ip[16];
	sprintf(ip, "%s", ipToString(WiFi.softAPIP()).c_str());
	sendLine(basicPtr, 0, "\x14\x14" "AP MAC     : %s", WiFi.softAPmacAddress().c_str());
	sendLine(basicPtr, 0, "\x14\x14" "AP IP      : %s", ip);
	sprintf(ip, "%s", ipToString(WiFi.localIP()).c_str());
	sendLine(basicPtr, 0, "\x14\x14" "STA MAC    : %s", WiFi.macAddress().c_str());
	sendLine(basicPtr, 0, "\x14\x14" "STA IP     : %s", ip);

	// End program with two zeros after last line. Last zero goes out as EOI.
	m_iec.send(0);
	m_iec.sendEOI(0);

	// ledON();
} // sendDeviceInfo

void Interface::sendDeviceStatus()
{
	Debug_printf("\r\nsendDeviceStatus:\r\n");

	// Reset basic memory pointer:
	uint16_t basicPtr = C64_BASIC_START;

	// Send load address
	m_iec.send(C64_BASIC_START bitand 0xff);
	m_iec.send((C64_BASIC_START >> 8) bitand 0xff);
	Debug_println("");

	// Send List HEADER
	sendLine(basicPtr, 0, "\x14\x14\x12 %s V%s ", PRODUCT_ID, FW_VERSION);

	// Current Config
	sendLine(basicPtr, 0, "\x14\x14" "DEVICE    : %d", m_device.device());
	sendLine(basicPtr, 0, "\x14\x14" "MEDIA     : %d", m_device.media());
	sendLine(basicPtr, 0, "\x14\x14" "PARTITION : %d", m_device.partition());
	sendLine(basicPtr, 0, "\x14\x14" "URL       : %s", m_device.url().c_str());
	sendLine(basicPtr, 0, "\x14\x14" "PATH      : %s", m_device.path().c_str());
	sendLine(basicPtr, 0, "\x14\x14" "ARCHIVE   : %s", m_device.archive().c_str());
	sendLine(basicPtr, 0, "\x14\x14" "IMAGE     : %s", m_device.image().c_str());
	sendLine(basicPtr, 0, "\x14\x14" "FILENAME  : %s", m_filename.c_str());

	// End program with two zeros after last line. Last zero goes out as EOI.
	m_iec.send(0);
	m_iec.sendEOI(0);

	ledON();
} // sendDeviceStatus

byte Interface::loop(void)
{
	//#ifdef HAS_RESET_LINE
	//	if(m_iec.checkRESET()) {
	//		// IEC reset line is in reset state, so we should set all states in reset.
	//		reset();
	//
	//
	//		return IEC::ATN_RESET;
	//	}
	//#endif
	// Wait for it to get out of reset.
	//while (m_iec.checkRESET())
	//{
	//	Debug_println("ATN_RESET");
	//}

	//	noInterrupts();
	IEC::ATNCheck retATN = m_iec.checkATN(m_atn_cmd);
	//	interrupts();

	if (retATN == IEC::ATN_ERROR)
	{
		//Debug_printf("\r\n[ERROR]");
		reset();
		retATN == IEC::ATN_IDLE;
	}
	// Did anything happen from the host side?
	else if (retATN not_eq IEC::ATN_IDLE)
	{

		switch (m_atn_cmd.command)
		{
		case IEC::ATN_CODE_OPEN:
			if (m_atn_cmd.channel == 0)
				Debug_printf("\r\n[OPEN] LOAD \"%s\",%d ", m_atn_cmd.str, m_atn_cmd.device);
			if (m_atn_cmd.channel == 1)
				Debug_printf("\r\n[OPEN] SAVE \"%s\",%d ", m_atn_cmd.str, m_atn_cmd.device);

			// Open either file or prg for reading, writing or single line command on the command channel.
			// In any case we just issue an 'OPEN' to the host and let it process.
			// Note: Some of the host response handling is done LATER, since we will get a TALK or LISTEN after this.
			// Also, simply issuing the request to the host and not waiting for any response here makes us more
			// responsive to the CBM here, when the DATA with TALK or LISTEN comes in the next sequence.
			handleATNCmdCodeOpen(m_atn_cmd);
			break;

		case IEC::ATN_CODE_DATA: // data channel opened
			Debug_printf("\r\n[DATA] ");
			if (retATN == IEC::ATN_CMD_TALK)
			{
				// when the CMD channel is read (status), we first need to issue the host request. The data channel is opened directly.
				if (m_atn_cmd.channel == CMD_CHANNEL)
					handleATNCmdCodeOpen(m_atn_cmd);		 // This is typically an empty command,
				handleATNCmdCodeDataTalk(m_atn_cmd.channel); // ...but we do expect a response from PC that we can send back to CBM.
			}
			else if (retATN == IEC::ATN_CMD_LISTEN)
				handleATNCmdCodeDataListen();
			else if (retATN == IEC::ATN_CMD)	 // Here we are sending a command to PC and executing it, but not sending response
				handleATNCmdCodeOpen(m_atn_cmd); // back to CBM, the result code of the command is however buffered on the PC side.
			break;

		case IEC::ATN_CODE_CLOSE:
			Debug_printf("\r\n[CLOSE] ");
			// handle close with host.
			handleATNCmdClose();
			break;

		case IEC::ATN_CODE_LISTEN:
			Debug_printf("\r\n[LISTEN] ");
			break;
		case IEC::ATN_CODE_TALK:
			Debug_printf("\r\n[TALK] ");
			break;
		case IEC::ATN_CODE_UNLISTEN:
			Debug_printf("\r\n[UNLISTEN] ");
			break;
		case IEC::ATN_CODE_UNTALK:
			Debug_printf("\r\n[UNTALK] ");
			break;
		} // switch
	}	  // IEC not idle

	return retATN;
} // handler

void Interface::handleATNCmdCodeOpen(IEC::ATNCmd &atn_cmd)
{
	m_device.select(atn_cmd.device);
	m_filename = String((char *)atn_cmd.str);
	m_filename.trim();
	m_filetype = m_filename.substring(m_filename.lastIndexOf(".") + 1);
	m_filetype.toUpperCase();
	if (m_filetype.length() > 4 || m_filetype.length() == m_filename.length())
		m_filetype = "";

	Dir local_file = m_fileSystem->openDir(String(m_device.path() + m_filename));

	//Serial.printf("\r\n$IEC: DEVICE[%d] DRIVE[%d] PARTITION[%d] URL[%s] PATH[%s] IMAGE[%s] FILENAME[%s] FILETYPE[%s] COMMAND[%s]\r\n", m_device.device(), m_device.drive(), m_device.partition(), m_device.url().c_str(), m_device.path().c_str(), m_device.image().c_str(), m_filename.c_str(), m_filetype.c_str(), atn_cmd.str);
	if (m_filename.startsWith(F("$")))
	{
		m_openState = O_DIR;
	}
	else if (local_file.isDirectory())
	{
		// Enter directory
		Debug_printf("\r\nchangeDir: [%s] >", m_filename.c_str());
		m_device.path(m_device.path() + m_filename.substring(3) + F("/"));
		m_openState = O_DIR;
	}
	else if (m_filename.startsWith(F("CD")))
	{
		uint8_t pos = 3;
		if (m_filename.endsWith(F("^"))) // Switch to local root
		{
			m_device.url("");
			m_device.path("");
			m_device.archive("");
			m_device.image("");
		}
		else if (m_filename.endsWith(F("_")))
		{
			if (m_device.archive().length())
			{
				// Unmount archive file
				m_device.archive("");
			}
			else if (m_device.image().length())
			{
				// Unmount image file
				m_device.image("");
			}
			else if (m_device.url().length() && m_device.path() == "/")
			{
				// Unmount url
				m_device.url("");
			}
			else
			{
				// Go back a directory
				pos = m_device.path().lastIndexOf("/", m_device.path().length() - 2) + 1;
				m_device.path(m_device.path().substring(0, pos));
			}
		}
		else if (m_filename.length() > 3)
		{
			if (m_filename.startsWith(F("CD//"))) // Switch to local/server root
			{
				pos = 4;
				m_device.path("");
				m_device.archive("");
				m_device.image("");
			}
			else if (m_filename.startsWith(F("CD:")))
			{
				pos = 3;
			}

			// Enter directory
			m_device.path(m_device.path() + m_filename.substring(pos) + F("/"));
		}

		if (atn_cmd.channel == 0x00)
		{
			m_openState = O_DIR;
		}
	}
	else if (m_filetype.startsWith(F("URL")) && m_filetype.length() > 0)
	{
		// Load URL file
		m_openState = O_URL;
	}
	else if (m_filename.startsWith(F("HTTP://")))
	{
		EdUrlParser* url = EdUrlParser::parseUrl(m_filename.c_str());

#ifdef DEBUG
		Serial.printf("\r\nURL: [%s]\r\n", url->url.c_str());
		Serial.printf("Root: [%s]\r\n", url->root.c_str());
		Serial.printf("Base: [%s]\r\n", url->base.c_str());
		Serial.printf("Scheme: [%s]\r\n", url->scheme.c_str());
		Serial.printf("Username: [%s]\r\n", url->username.c_str());
		Serial.printf("Password: [%s]\r\n", url->password.c_str());
		Serial.printf("Host: [%s]\r\n", url->hostname.c_str());
		Serial.printf("Port: [%s]\r\n", url->port.c_str());
		Serial.printf("Path: [%s]\r\n", url->path.c_str());
		Serial.printf("File: [%s]\r\n", url->filename.c_str());
		Serial.printf("Extension: [%s]\r\n", url->extension.c_str());
		Serial.printf("Query: [%s]\r\n", url->query.c_str());
		Serial.printf("Fragment: [%s]\r\n", url->fragment.c_str());
#endif

		// Mount url
		Debug_printf("\r\nmount: [%s] >", m_filename.c_str());
		m_device.partition(0);
		m_device.url(url->root.c_str());		
		m_device.path(url->path.c_str());
		m_filename = url->filename.c_str();
		m_filetype = url->extension.c_str();
		m_device.archive("");
		m_device.image("");

		if (url != NULL)
			delete url;

		m_openState = O_DIR;
		if (m_filename.length())
		{
			m_openState = O_FILE;
		}
	}
	else if (String(F(ARCHIVE_TYPES)).indexOf(m_filetype) >= 0 && m_filetype.length() > 0)
	{
		// Mount archive file
		Debug_printf("\r\nmount: [%s] >", m_filename.c_str());
		m_device.archive(m_filename);
		m_device.image("");

		m_openState = O_DIR;
	}
	else if (String(F(IMAGE_TYPES)).indexOf(m_filetype) >= 0 && m_filetype.length() > 0)
	{
		// Mount image file
		Debug_printf("\r\nmount: [%s] >", m_filename.c_str());
		m_device.image(m_filename);

		m_openState = O_DIR;
	}
	else if (m_filename.startsWith(F("@INFO")))
	{
		m_filename = "";
		m_openState = O_DEVICE_INFO;
	}
	else if (m_filename.startsWith(F("@STAT")))
	{
		m_filename = "";
		m_openState = O_DEVICE_STATUS;
	}
	else
	{
		m_openState = O_FILE;
	}

	if (m_openState == O_DIR)
	{
		m_filename = "$";
		m_filetype = "";
		m_atn_cmd.str[0] = '\0';
		m_atn_cmd.strLen = 0;
	}

	//Debug_printf("\r\nhandleATNCmdCodeOpen: %d (M_OPENSTATE) [%s]", m_openState, m_atn_cmd.str);
	Serial.printf("\r\nDEVICE[%d]\nMEDIA[%d]\nPARTITION[%d]\nURL[%s]\nPATH[%s]\nARCHIVE[%s]\nIMAGE[%s]\nFILENAME[%s]\nFILETYPE[%s]\nCOMMAND[%s]\r\n", 
					m_device.device(), 
					m_device.media(), 
					m_device.partition(), 
					m_device.url().c_str(), 
					m_device.path().c_str(),
					m_device.archive().c_str(), 
					m_device.image().c_str(), 
					m_filename.c_str(), 
					m_filetype.c_str(), 
					atn_cmd.str
	);

} // handleATNCmdCodeOpen

void Interface::handleATNCmdCodeDataTalk(byte chan)
{
	// process response into m_queuedError.
	// Response: ><code in binary><CR>
	String urlFile;
	EdUrlParser* url;

	Debug_printf("\r\nhandleATNCmdCodeDataTalk: %d (CHANNEL) %d (M_OPENSTATE)", chan, m_openState);

	if (chan == CMD_CHANNEL)
	{
		// Send status message
		sendStatus();
		// go back to OK state, we have dispatched the error to IEC host now.
		m_queuedError = ErrOK;
	}
	else
	{

		//Debug_printf("\r\nm_openState: %d", m_openState);

		switch (m_openState)
		{
		case O_NOTHING:
			// Say file not found
			m_iec.sendFNF();
			break;

		case O_INFO:
			// Reset and send SD card info
			reset();
			sendListing();
			break;

		case O_FILE:
			// Send program file
			if (m_device.url().length())
			{
				sendFileHTTP();
			}
			else
			{
				sendFile();
			}
			break;

		case O_URL:
			// urlFile = String(m_device.path() + m_filename);
			// Debug_printf("\r\nOpening URL: [%s]", urlFile.c_str());
			// m_filename = readLine(m_fileSystem, urlFile);
			// url = EdUrlParser::parseUrl(m_filename.c_str());

			// // Mount url
			// Debug_printf("\r\nmount: [%s] >", m_filename.c_str());
			// m_device.partition(0);
			// m_device.url(url->root.c_str());		
			// m_device.path(url->path.c_str());
			// m_filename = url->filename.c_str();
			// m_filetype = url->extension.c_str();
			// m_device.archive("");
			// m_device.image("");
			// sendListingHTTP();

			// if (url != NULL)
			// 	delete url;
		 	break;

		case O_DIR:
			// Send listing
			if (m_device.url().length())
			{
				sendListingHTTP();
			}
			else
			{
				sendListing();
			}
			break;

		case O_FILE_ERR:
			// FIXME: interface with Host for error info.
			//sendListing(/*&send_file_err*/);
			m_iec.sendFNF();
			break;

		case O_DEVICE_INFO:
			// Send device info
			sendDeviceInfo();
			break;

		case O_DEVICE_STATUS:
			// Send device info
			sendDeviceStatus();
			break;
		}
	}

} // handleATNCmdCodeDataTalk

void Interface::handleATNCmdCodeDataListen()
{
	byte lengthOrResult;
	boolean wasSuccess = false;

	// process response into m_queuedError.
	// Response: ><code in binary><CR>

	serCmdIOBuf[0] = 0;

	Debug_printf("\r\nhandleATNCmdCodeDataListen: %s", serCmdIOBuf);

	if (not lengthOrResult or '>' not_eq serCmdIOBuf[0])
	{
		// FIXME: Check what the drive does here when things go wrong. FNF is probably not right.
		m_iec.sendFNF();
		strcpy_P(serCmdIOBuf, "response not sync.");
	}
	else
	{
		if (lengthOrResult = Serial.readBytes(serCmdIOBuf, 2))
		{
			if (2 == lengthOrResult)
			{
				lengthOrResult = serCmdIOBuf[0];
				wasSuccess = true;
			}
			else
			{
				//Log(Error, FAC_IFACE, serCmdIOBuf);
			}
		}
		m_queuedError = wasSuccess ? lengthOrResult : ErrSerialComm;

		if (ErrOK == m_queuedError)
			saveFile();
		//		else // FIXME: Check what the drive does here when saving goes wrong. FNF is probably not right. Dummyread entire buffer from CBM?
		//			m_iec.sendFNF();
	}
} // handleATNCmdCodeDataListen

void Interface::handleATNCmdClose()
{
	Debug_printf("\r\nhandleATNCmdClose: Success!");

	//Serial.printf("\r\nIEC: DEVICE[%d] DRIVE[%d] PARTITION[%d] URL[%s] PATH[%s] IMAGE[%s] FILENAME[%s] FILETYPE[%s]\r\n", m_device.device(), m_device.drive(), m_device.partition(), m_device.url().c_str(), m_device.path().c_str(), m_device.image().c_str(), m_filename.c_str(), m_filetype.c_str());
	Debug_printf("\r\n=================================\r\n\r\n");

	m_filename = "";
} // handleATNCmdClose

// send single basic line, including heading basic pointer and terminating zero.
uint16_t Interface::sendLine(uint16_t &basicPtr, uint16_t blocks, const char *format, ...)
{
	// Format our string
	va_list args;
	va_start(args, format);
	char text[vsnprintf(NULL, 0, format, args) + 1];
	vsnprintf(text, sizeof text, format, args);
	va_end(args);

	return sendLine(basicPtr, blocks, text);
}

uint16_t Interface::sendLine(uint16_t &basicPtr, uint16_t blocks, char *text)
{
	byte i;
	uint16_t b_cnt = 0;

	Debug_printf("%d %s ", blocks, text);

	// Get text length
	uint8_t len = strlen(text);

	// Increment next line pointer
	basicPtr += len + 5;

	// Send that pointer
	m_iec.send(basicPtr bitand 0xFF);
	m_iec.send(basicPtr >> 8);

	// Send blocks
	m_iec.send(blocks bitand 0xFF);
	m_iec.send(blocks >> 8);

	// Send line contents
	for (i = 0; i < len; i++)
		m_iec.send(text[i]);

	// Finish line
	m_iec.send(0);

	Debug_println("");

	b_cnt += (len + 5);

	return b_cnt;
} // sendLine

uint16_t Interface::sendHeader(uint16_t &basicPtr)
{
	uint16_t byte_count;

	// Send List HEADER
	byte space_cnt = (16 - strlen(PRODUCT_ID)) / 2;
	byte_count += sendLine(basicPtr, 0, "\x12\"%*s%s%*s\" %.02d 2A", space_cnt, "", PRODUCT_ID, space_cnt, "", m_device.device());

	// Send Extra INFO
	if (m_device.url().length())
	{
		byte_count += sendLine(basicPtr, 0, "%*s\"%-*s\" NFO", 3, "", 16, "[URL]");
		byte_count += sendLine(basicPtr, 0, "%*s\"%-*s\" NFO", 3, "", 16, m_device.url().c_str());
	}
	if (m_device.path().length() > 1)
	{
		byte_count += sendLine(basicPtr, 0, "%*s\"%-*s\" NFO", 3, "", 16, "[PATH]");
		byte_count += sendLine(basicPtr, 0, "%*s\"%-*s\" NFO", 3, "", 16, m_device.path().c_str());
	}
	if (m_device.url().length() + m_device.path().length() > 1)
	{
		byte_count += sendLine(basicPtr, 0, "%*s\"----------------\" NFO", 3, "");
	}

	return byte_count;
}

void Interface::sendListing()
{
	Debug_printf("\r\nsendListing:\r\n");

	uint16_t byte_count = 0;
	String extension = "DIR";

	// Reset basic memory pointer:
	uint16_t basicPtr = C64_BASIC_START;

	// Send load address
	m_iec.send(C64_BASIC_START bitand 0xff);
	m_iec.send((C64_BASIC_START >> 8) bitand 0xff);
	byte_count += 2;
	Debug_println("");

	byte_count += sendHeader(basicPtr);

	// Send List ITEMS
	Dir dir = m_fileSystem->openDir(m_device.path());
	while (dir.next())
	{
		uint16_t block_cnt = dir.fileSize() / 256;
		byte block_spc = 3;
		if (block_cnt > 9)
			block_spc--;
		if (block_cnt > 99)
			block_spc--;
		if (block_cnt > 999)
			block_spc--;

		byte space_cnt = 21 - (dir.fileName().length() + 5);
		if (space_cnt > 21)
			space_cnt = 0;

		if (dir.fileSize())
		{
			block_cnt = dir.fileSize() / 256;
			if ( block_cnt < 1)
				block_cnt = 1;

			uint8_t ext_pos = dir.fileName().lastIndexOf(".") + 1;
			if (ext_pos && ext_pos != dir.fileName().length())
			{
				extension = dir.fileName().substring(ext_pos);
				extension.toUpperCase();
			}
			else
			{
				extension = "PRG";
			}
		}
		else
		{
			extension = "DIR";
		}

		// Don't show hidden folders or files
		if (!dir.fileName().startsWith("."))
		{
			byte_count += sendLine(basicPtr, block_cnt, "%*s\"%s\"%*s %3s", block_spc, "", dir.fileName().c_str(), space_cnt, "", extension.c_str());
		}

		//Debug_printf(" (%d, %d)\r\n", space_cnt, byte_count);
		ledToggle(true);
	}

	byte_count += sendFooter(basicPtr);

	// End program with two zeros after last line. Last zero goes out as EOI.
	m_iec.send(0);
	m_iec.sendEOI(0);

	Debug_printf("\r\nsendListing: %d Bytes Sent\r\n", byte_count);

	ledON();
} // sendListing

uint16_t Interface::sendFooter(uint16_t &basicPtr)
{
	// Send List FOOTER
	// #if defined(USE_LITTLEFS)
	FSInfo64 fs_info;
	m_fileSystem->info64(fs_info);
	return sendLine(basicPtr, (fs_info.totalBytes - fs_info.usedBytes) / 256, "BLOCKS FREE.");
	// #elif defined(USE_SPIFFS)
	// 	return sendLine(basicPtr, 00, "UNKNOWN BLOCKS FREE.");
	// #endif
	//Debug_println("");
}

void Interface::sendFile()
{
	uint16_t i = 0;
	bool success = true;

	uint16_t bi = 0;
	uint16_t load_address = 0;
	char b[1];
	char ba[9];

	ba[8] = '\0';

	// Update device database
	m_device.save();

	// Find first program
	if (m_filename.endsWith("*"))
	{
		m_filename = "";

		if (m_device.path() == "/" && m_device.archive().length() == 0 && m_device.image().length() == 0)
		{
			m_filename = ".sys/fb64";
		}
		else
		{
			Dir dir = m_fileSystem->openDir(m_device.path());
			while (dir.next() && dir.isDirectory())
			{
				Debug_printf("\r\nsendFile: %s", dir.fileName().c_str());
			}
			if (dir.isFile())
				m_filename = dir.fileName();
		}
	}
	String inFile = String(m_device.path() + m_filename);

	File file = m_fileSystem->open(inFile, "r");

	if (!file.available())
	{
		Debug_printf("\r\nsendFile: %s (File Not Found)\r\n", inFile.c_str());
		m_iec.sendFNF();
	}
	else
	{
		size_t len = file.size();

		// Get file load address
		file.readBytes(b, 1);
		success = m_iec.send(b[0]);
		load_address = *b & 0x00FF; // low byte
		file.readBytes(b, 1);
		success = m_iec.send(b[0]);
		load_address = load_address | *b << 8;  // high byte
		// fseek(file, 0, SEEK_SET);

		Debug_printf("\r\nsendFile: [%s] [$%.4X] (%d bytes)\r\n=================================\r\n", inFile.c_str(), load_address, len);
		for (i = 2; success and i < len; ++i) 
		{
			success = file.readBytes(b, 1);
			if (success)
			{
#ifdef DATA_STREAM
				if (bi == 0)
				{
					Debug_printf(":%.4X ", load_address);
					load_address += 8;
				}
#endif
				if (i == len - 1)
				{
					success = m_iec.sendEOI(b[0]); // indicate end of file.
				}
				else
				{
					success = m_iec.send(b[0]);
				}

#ifdef DATA_STREAM
			// Show ASCII Data
				if (b[0] < 32 || b[0] >= 127) 
				b[0] = 46;

				ba[bi++] = b[0];

				if(bi == 8)
				{
				size_t t = (i * 100) / len;
					Debug_printf(" %s (%d %d%%)\r\n", ba, i, t);
				bi = 0;
				}
#endif
				// Toggle LED
				if (i % 50 == 0)
				{
					ledToggle(true);
				}
			}

			if ( m_iec.status(IEC_PIN_ATN) == IEC::IECline::pulled )
			{
				success = true;
				break;
			}
		}
		file.close();
		Debug_println("");
		Debug_printf("%d of %d bytes sent\r\n", i, len);

		ledON();

		if (!success || i != len)
		{
			Debug_println("sendFile: Transfer aborted!");
		}
	}
} // sendFile

void Interface::saveFile()
{
	String outFile = String(m_device.path() + m_filename);
	byte b;

	Debug_printf("\r\nsaveFile: %s", outFile.c_str());

	File file = m_fileSystem->open(outFile, "w");
	//	noInterrupts();
	if (!file.available())
	{
		Debug_printf("\r\nsaveFile: %s (Error)\r\n", outFile.c_str());
	}
	else
	{
		boolean done = false;
		// Recieve bytes until a EOI is detected
		do
		{
			b = m_iec.receive();
			done = (m_iec.state() bitand IEC::eoiFlag) or (m_iec.state() bitand IEC::errorFlag);

			file.write(b);
		} while (not done);
		file.close();
	}
	//	interrupts();
} // saveFile

void Interface::sendListingHTTP()
{
	Debug_printf("\r\nsendListingHTTP: ");

	uint16_t byte_count = 0;

	String user_agent(String(PRODUCT_ID) + " [" + String(FW_VERSION) + "]");
	String url(m_device.url() + "/api/");
	String post_data("p=" + urlencode(m_device.path()) + "&i=" + urlencode(m_device.image()) + "&f=" + urlencode(m_filename));

	// Connect to HTTP server
	WiFiClient client;
	HTTPClient http;
	http.setUserAgent(user_agent);
	// http.setFollowRedirects(true);
	http.setTimeout(10000);
	url.toLowerCase();
	if (!http.begin(client, url))
	{
		Debug_println(F("\r\nConnection failed"));
		m_iec.sendFNF();
		return;
	}
	http.addHeader("Content-Type", "application/x-www-form-urlencoded");

	Debug_printf("\r\nConnected!\r\n--------------------\r\n%s\r\n%s\r\n%s\r\n", user_agent.c_str(), url.c_str(), post_data.c_str());

	uint8_t httpCode = http.POST(post_data);	 //Send the request
	WiFiClient payload = http.getStream(); //Get the response payload as Stream
	//String payload = http.getString();    //Get the response payload as String

	Debug_printf("HTTP Status: %d\r\n", httpCode); //Print HTTP return code
	if (httpCode != 200)
	{
		Debug_println(F("Error"));
		m_iec.sendFNF();
		return;
	}

	//Serial.println(payload);    //Print request response payload
	m_lineBuffer = payload.readStringUntil('\n');

	// Reset basic memory pointer:
	uint16_t basicPtr = C64_BASIC_START;

	// Send load address
	m_iec.send(C64_BASIC_START bitand 0xff);
	m_iec.send((C64_BASIC_START >> 8) bitand 0xff);
	byte_count += 2;
	Debug_println("");

	do
	{
		// Parse JSON object
		DeserializationError error = deserializeJson(m_jsonHTTP, m_lineBuffer);
		if (error)
		{
			Serial.print(F("\r\ndeserializeJson() failed: "));
			Serial.println(error.c_str());
			break;
		}

		byte_count += sendLine(basicPtr, m_jsonHTTP["blocks"], "%s", urldecode(m_jsonHTTP["line"].as<String>()).c_str());
		ledToggle(true);

		m_lineBuffer = payload.readStringUntil('\n');
		//Serial.printf("\r\nlinebuffer: %d %s", m_lineBuffer.length(), m_lineBuffer.c_str());
	} while (m_lineBuffer.length() > 1);

	//End program with two zeros after last line. Last zero goes out as EOI.
	m_iec.send(0);
	m_iec.sendEOI(0);

	Debug_printf("\r\nBytes Sent: %d\r\n", byte_count);

	http.end(); //Close connection

	ledON();
} // sendListingHTTP

void Interface::sendFileHTTP()
{
	uint16_t i = 0;
	bool success = true;

	uint16_t bi = 0;
	uint16_t load_address = 0;
	char b[1];
	char ba[9];

	ba[8] = '\0';

	// Update device database
	m_device.save();

	Debug_printf("\r\nsendFileHTTP: ");

	String user_agent(String(PRODUCT_ID) + " [" + String(FW_VERSION) + "]");
	String url;
	String post_data;
	//if ( m_device.image().length() )
	{
		url = m_device.url() + "/api/";
		post_data = "p=" + urlencode(m_device.path()) + "&i=" + urlencode(m_device.image()) + "&f=" + urlencode(m_filename);
	}
	// else
	// {
	// 	url = m_device.url() + m_device.path() + m_filename;
	// }

	// Connect to HTTP server
	uint8_t httpCode = 0;
	WiFiClient client;
	HTTPClient http;
	http.setUserAgent(user_agent);
	// http.setFollowRedirects(true);
	http.setTimeout(10000);
	url.toLowerCase();
	if (!http.begin(client, url))
	{
		Debug_println(F("\r\nConnection failed"));
		m_iec.sendFNF();
		return;
	}

	Debug_printf("\r\nConnected!\r\n--------------------\r\n%s\r\n%s\r\n%s\r\n", user_agent.c_str(), url.c_str(), post_data.c_str());

	if ( post_data.length() )
	{
		http.addHeader("Content-Type", "application/x-www-form-urlencoded");
		httpCode = http.POST(post_data); //Send the request
	}
	else
	{
		httpCode = http.GET(); //Send the request
	}
	WiFiClient file = http.getStream();  //Get the response payload as Stream

	if (!file.available())
	{
		Debug_printf("\r\nsendFileHTTP: %s (File Not Found)\r\n", url.c_str());
		m_iec.sendFNF();
	}
	else
	{
		size_t len = http.getSize();

		// Get file load address
		file.readBytes(b, 1);
		success = m_iec.send(b[0]);
		load_address = *b & 0x00FF; // low byte
		file.readBytes(b, 1);
		success = m_iec.send(b[0]);
		load_address = load_address | *b << 8;  // high byte
		// fseek(file, 0, SEEK_SET);

		Debug_printf("\r\nsendFileHTTP: [%s] [$%.4X] (%d bytes)\r\n=================================\r\n", m_filename.c_str(), load_address, len);
		for (i = 2; success and i < len; ++i)
		{ 
			// End if sending to CBM fails.
			success = file.readBytes(b, 1);
			if (success)
			{
#ifdef DATA_STREAM
				if (bi == 0)
        		{
					Debug_printf(":%.4X ", load_address);
					load_address += 8;
				}
#endif
				if (i == len - 1)
				{
					success = m_iec.sendEOI(b[0]); // indicate end of file.
				}
				else
				{
					success = m_iec.send(b[0]);
				}

#ifdef DATA_STREAM
            // Show ASCII Data
				if (b[0] < 32 || b[0] >= 127) 
                b[0] = 46;

				ba[bi++] = b[0];

				if(bi == 8)
            	{
					size_t t = (i * 100) / len;
					Debug_printf(" %s (%d %d%%)\r\n", ba, i, t);
                	bi = 0;
            	}
#endif
				// Toggle LED
				if (i % 50 == 0)
				{
					ledToggle(true);
				}
            }

			// if ( m_iec.status(IEC_PIN_ATN) == IEC::IECline::pulled )
			// {
			// 	success = true;
			// 	break;
			// }
        }
        http.end();
        Debug_println("");
		Debug_printf("%d of %d bytes sent\r\n", i, len);

        ledON();

		if (!success || i != len)
        {
			Debug_println("Transfer aborted!");
        }
    }
}
