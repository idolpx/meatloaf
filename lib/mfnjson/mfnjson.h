/**
 * JSON Parser wrapper for #FujiNet
 * 
 * Thom Cherryhomes
 *   <thom.cherryhomes@gmail.com>
 */

#ifndef MJSON_H
#define MJSON_H

#include <cJSON.h>
#include <cJSON_Utils.h>

#include "../network-protocol/Protocol.h"
#include "meat_stream.h"

class MFNJSON
{
public:
    MFNJSON();
    virtual ~MFNJSON();

    void setLineEnding(const std::string &_lineEnding);
    void setStream(std::shared_ptr<MStream> stream);
    void setReadQuery(const std::string &queryString);
    cJSON *resolveQuery();
    bool status(NetworkStatus *status);
    
    bool parse();
    int readValueLen();
    bool readValue(uint8_t *buf, unsigned short len);
    std::string processString(std::string in);
    int json_bytes_remaining = 0;
    void setQueryParam(uint8_t qp);
    
private:
    cJSON *_json = nullptr;
    cJSON *_item = nullptr;
    std::shared_ptr<MStream> stream = nullptr;
    std::string _queryString;
    std::string lineEnding;
    std::string getValue(cJSON *item);
    std::string _parseBuffer;
};

#endif /* MJSON_H */