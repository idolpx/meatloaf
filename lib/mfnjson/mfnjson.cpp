/**
 * JSON Wrapper for #FujiNet
 *
 * Thomas Cherryhomes
 *   <thom.cherryhomes@gmail.com>
 */

#include "mfnjson.h"

#include <string.h>
#include <sstream>
#include <math.h>
#include <iomanip>
#include <ostream>
#include "string_utils.h"
#include "../../include/debug.h"
#include "../utils/utils.h"

/**
 * ctor
 */
MFNJSON::MFNJSON()
{
    Debug_printf("MFNJSON::ctor()\r\n");
    _json = nullptr;
}

/**
 * dtor
 */
MFNJSON::~MFNJSON()
{
    Debug_printf("MFNJSON::dtor()\r\n");
    if (_json != nullptr)
        cJSON_Delete(_json);
    _json = nullptr;
}

/**
 * Specify line ending
 */
void MFNJSON::setLineEnding(const std::string &_lineEnding)
{
    lineEnding = _lineEnding;
}

/**
 * Attach protocol handler
 */
void MFNJSON::setStream(std::shared_ptr<MStream> s)
{
    Debug_printf("MFNJSON::setStream()\r\n");
    stream = s;
}

/**
 * Set read query string
 */
void MFNJSON::setReadQuery(const std::string &queryString)
{
    Debug_printf("MFNJSON::setReadQuery queryString: %s\r\n", queryString.c_str());
    _queryString = queryString;
    _item = resolveQuery();
    json_bytes_remaining = readValueLen();
}

/**
 * Resolve query string
 */
cJSON *MFNJSON::resolveQuery()
{
    if (_queryString.empty())
        return _json;

    return cJSONUtils_GetPointer(_json, _queryString.c_str());
}

/**
 * Process string, strip out HTML tags if needed
 */
std::string MFNJSON::processString(std::string in)
{
    while (in.find("<") != std::string::npos)
    {
        auto startpos = in.find("<");
        auto endpos = in.find(">") + 1;

        if (endpos != std::string::npos)
        {
            in.erase(startpos, endpos - startpos);
        }
    }

#ifdef BUILD_IEC
    in = mstr::toPETSCII2(in);
#endif

    return in;
}

/**
 * Return normalized string of JSON item
 */
std::string MFNJSON::getValue(cJSON *item)
{
    if (item == NULL)
    {
        Debug_printf("\r\nFNJSON::getValue called with null item, returning empty string.\r\n");
        return std::string("");
    }
    // Fix where the print cursor is.
    Debug_printf("\r\n");

    std::stringstream ss;

    if (cJSON_IsString(item))
    {
        char *strValue = cJSON_GetStringValue(item);
        Debug_printf("S: [cJSON_IsString] %s\r\n", strValue);
        ss << processString(strValue + lineEnding);
    }
    else if (cJSON_IsBool(item))
    {
        bool isTrue = cJSON_IsTrue(item);
        Debug_printf("S: [cJSON_IsBool] %s\r\n", isTrue ? "true" : "false");
        ss << (isTrue ? "TRUE" : "FALSE") + lineEnding;
    }
    else if (cJSON_IsNull(item))
    {
        Debug_printf("S: [cJSON_IsNull]\r\n");
        ss << "NULL" + lineEnding;
    }
    else if (cJSON_IsNumber(item))
    {
        double num = cJSON_GetNumberValue(item);
        bool isInt = isApproximatelyInteger(num);
        // Is the number an integer?
        if (isInt)
        {
            // yes, return as 64 bit integer
            Debug_printf("S: [cJSON_IsNumber INT] %d\r\n", (int64_t)num);
            ss << (int64_t)num;
        }
        else
        {
            // no, return as double with max. 10 digits
            Debug_printf("S: [cJSON_IsNumber] %f\r\n", num);
            ss << std::setprecision(10) << num;
        }

        ss << lineEnding;
    }
    else if (cJSON_IsObject(item))
    {
        #ifdef BUILD_IEC
            // Set line ending when returning multiple values
            setLineEnding("\x0a");
        #endif

        if (item->child == NULL)
        {
            Debug_printf("MFNJSON::getValue OBJECT has no CHILD, adding empty string\r\n");
            ss << lineEnding;
        }
        else
        {
            item = item->child;
            do
            {
                // #ifdef BUILD_IEC
                //     // Convert key to PETSCII
                //     string tempStr = string((const char *)item->string);                    
                //     ss << mstr::toPETSCII2(tempStr);
                // #else
                    ss << item->string;
                //#endif

                ss << lineEnding + getValue(item);
            } while ((item = item->next) != NULL);
        }

    }
    else if (cJSON_IsArray(item))
    {
        cJSON *child = item->child;
        do
        {
            ss << getValue(child);
        } while ((child = child->next) != NULL);
    }
    else
        ss << "UNKNOWN" + lineEnding;
  
    return ss.str();
}

/**
 * Return requested value
 */
bool MFNJSON::readValue(uint8_t *rx_buf, unsigned short len)
{    
    if (_item == nullptr)
        return true; // error

    memcpy(rx_buf, getValue(_item).data(), len);

    return false; // no error.
}

/**
 * Return requested value length
 */
int MFNJSON::readValueLen()
{
    if (_item == nullptr)
        return 0;

    return getValue(_item).size();
}

/**
 * Parse data from protocol
 */
bool MFNJSON::parse()
{
    if (_json != nullptr)
    {
        // delete and set to null. we only set a new _json value if the parsebuffer is not empty
        cJSON_Delete(_json);
        _json = nullptr;
    }

    if (stream == nullptr)
    {
        Debug_printf("MFNJSON::parse() - NULL protocol.\r\n");
        return false;
    }
    _parseBuffer.clear();

    uint8_t buffer[256];

    while (auto bytes_read = stream->read(buffer, 256))
    {
        _parseBuffer += std::string((char *)buffer, bytes_read);
        vTaskDelay(10);
    }

    Debug_printf("S: %s\r\n", _parseBuffer.c_str());
    // only try and parse the buffer if it has data. Empty response doesn't need parsing.
    if (!_parseBuffer.empty())
    {
        _json = cJSON_Parse(_parseBuffer.c_str());
    }

    if (_json == nullptr)
    {
        Debug_printf("MFNJSON::parse() - Could not parse JSON, parseBuffer length: %d\r\n", _parseBuffer.size());
        return false;
    }

    return true;
}

bool MFNJSON::status(NetworkStatus *s)
{
    Debug_printf("MFNJSON::status(%u) %s\r\n", json_bytes_remaining, getValue(_item).c_str());
    s->connected = true;
    s->rxBytesWaiting = json_bytes_remaining;
    s->error = json_bytes_remaining == 0 ? 136 : 0;
    return false;
}