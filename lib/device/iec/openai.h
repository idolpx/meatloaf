#ifndef IECOPENAI_H
#define IECOPENAI_H

#include "../../bus/bus.h"

#define TC_SIZE 256 // size of returned time string.

class iecOpenAI : public virtualDevice
{
    private:
    
    std::string api_key = "";
    std::string response_format = "json";

    public:

    iecOpenAI();
    ~iecOpenAI();

#if 0
    device_state_t process() override;
#else
    virtual device_state_t openChannel(/*int chan, IECPayload &payload*/) override;
    virtual device_state_t closeChannel(/*int chan*/) override;
    virtual device_state_t readChannel(/*int chan*/) override;
    virtual device_state_t writeChannel(/*int chan, IECPayload &payload*/) override;
#endif

    void iec_open();
    void iec_close();
    void iec_reopen();
    void iec_reopen_talk();
    void iec_reopen_listen();

    void set_apikey(std::string s);
    void set_response_format(std::string s);

};

#endif /* IECOPENAI_H */