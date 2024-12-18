// #ifdef BUILD_IEC

// #include "openai.h"

// #include <OpenAI.h>

// #include "string_utils.h"

// iecOpenAI::iecOpenAI()
// {

// }

// iecOpenAI::~iecOpenAI()
// {
// }

// void iecOpenAI::set_apikey(std::string s)
// {
//     Debug_printf("set_apikey(%s)\r\n",s.c_str());
//     api_key = mstr::toUTF8(s);
// }

// void iecOpenAI::set_response_format(std::string s)
// {
//     Debug_printf("set_response_format(%s)\r\n",s.c_str());
//     response_format = mstr::toUTF8(s);
// }

// #if 1
// device_state_t iecOpenAI::openChannel(/*int chan, IECPayload &payload*/)
// {
//   return state;
// }

// device_state_t iecOpenAI::closeChannel(/*int chan*/)
// {
//   return state;
// }

// device_state_t iecOpenAI::readChannel(/*int chan*/)
// {
//   return state;
// }

// device_state_t iecOpenAI::writeChannel(/*int chan, IECPayload &payload*/)
// {
//   //write(commanddata.channel);
//   return state;
// }
// #else
// device_state_t iecOpenAI::process()
// {
//     virtualDevice::process();

//     switch (commanddata.secondary)
//     {
//     case IEC_OPEN:
//         iec_open();
//         break;
//     case IEC_CLOSE:
//         iec_close();
//         break;
//     case IEC_REOPEN:
//         iec_reopen();
//         break;
//     default:
//         break;
//     }

//     return state;
// }
// #endif

// void iecOpenAI::iec_open()
// {
//     // if (mstr::isNumeric(payload))
//     //     set_timestamp(payload);
//     // else
//     //     set_timestamp_format(payload);
// }

// void iecOpenAI::iec_close()
// {

// }

// void iecOpenAI::iec_reopen()
// {
//     switch (commanddata.primary)
//     {
//     case IEC_TALK:
//         iec_reopen_talk();
//         break;
//     case IEC_LISTEN:
//         iec_reopen_listen();
//         break;
//     }
// }

// void iecOpenAI::iec_reopen_listen()
// {
//     Debug_printf("IEC REOPEN LISTEN\r\n");

//     //mstr::toASCII(payload);

//     Debug_printf("Sending over %s\r\n",payload.c_str());

//     // if (mstr::isNumeric(payload))
//     //     set_timestamp(payload);
//     // else
//     //     set_timestamp_format(payload);
// }

// void iecOpenAI::iec_reopen_talk()
// {
//     struct tm *info;
//     char output[128];
//     std::string s;

//     // if (!ts) // ts == 0, get current time
//     //     time(&ts);
    
//     // info = localtime(&ts);

//     // if (tf.empty())
//     // {
//     //     Debug_printf("sending default time string.\r\n");
//     //     s = std::string(asctime(info));
//     //     mstr::replaceAll(s,":",".");
//     // }
//     // else
//     // {
//     //     Debug_printf("Sending strftime of format %s\r\n",tf.c_str());
//     //     strftime(output,sizeof(output),tf.c_str(),info);
//     //     s = std::string(output);
//     // }
    
//     mstr::toUpper(s);
    
//     IEC.sendBytes(s, true);
// }

// #endif /* BUILD_IEC */