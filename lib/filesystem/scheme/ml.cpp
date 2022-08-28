#include "ml.h"

// #include "../../../include/global_defines.h"
// #include "../../../include/debug.h"


// MStream* MLFile::inputStream() {
//     // has to return OPENED stream
//     //Debug_printv("[%s]", url.c_str());
//     MStream* istream = new MLIStream(url);
//     istream->open();   
//     return istream;
// }; 


// bool MLIStream::open() {
//     PeoplesUrlParser urlParser;
//     urlParser.parseUrl(url);

//     std::string ml_url = "https://api.meatloaf.cc/?" + urlParser.name;
//     url = ml_url;
    
//     Debug_printv("url[%s]", url.c_str());
//     return m_http.GET(url);
// };