#include "ml.h"

#include "../../../include/global_defines.h"
#include "../../../include/debug.h"

MLFile::~MLFile() {
    // just to be sure to close it if we don't read the directory until the very end
    m_http.close();
}


MFile* MLFile::getNextFileInDir() {

    if(!dirIsOpen) // might be first call, so let's try opening the dir
    {
        Serial.print("\ndirIsOpen = 0, rewinding");
        dirIsOpen = rewindDirectory();
    }

    if(!dirIsOpen)
        return nullptr; // we couldn't open it or whole dir was at this stage - return nullptr, as usual

    // calling this proc will read a single JSON line that will be processed into MFile and returned
//    m_lineBuffer = m_http.readStringUntil('\n');
//Serial.printf("Buffer read from ml server: %s\n", m_lineBuffer.c_str());
	if(m_lineBuffer.length() > 1)
	{
		// Parse JSON object
		DeserializationError error = deserializeJson(m_jsonHTTP, m_lineBuffer);
		if (error)
		{
			Serial.print("\r\ndeserializeJson() failed: ");
			Debug_printv(error.c_str());
            dirIsOpen = false;
            m_http.close();
            return nullptr;
		}
        else {
            /*
            Right now you're returning this:

            {"blocks":0,"line":"%12%22MEATLOAF+64+ARCHIVE%22+ID+99","type":"NFO"}
            {"blocks":0,"line":"%22%5BURL%5D++++++++++++++%22+NFO","type":"NFO"}

            So let's say now you return instead this:

            {"path":"ml://servername.com/full/file/path/demos","isDir":true,"size":0}

            */
            dirIsOpen = true;

            std::string fname ="ml://" + host + "/" + mstr::urlDecode(m_jsonHTTP["name"]).c_str();
            size_t size = m_jsonHTTP["size"];
            bool dir = m_jsonHTTP["dir"];

            return new MLFile(fname, size, dir); // note such path can't be used to do our "magic" stream-in-strea-in-stream, you can use it only to list dir
            //return new MLFile("ml://c64.meatloaf.cc/dummy/file.prg", 123, false);
        }

	}
    else {
        // no more entries, let's close the stream
        //Debug_printv("no more entries");

        dirIsOpen = false;
        return nullptr;
    }
};

bool MLFile::isDirectory() {
    return false;
    // //String url("http://c64.meatloaf.cc/api/");
    // //String ml_url = std::string("http://" + host + "/api/").c_str();
    // std::string ml_url = "http://" + host + "/api/";
    // std::string post_data = "a=checkp=" + mstr::urlEncode(path);

	// // Connect to HTTP server
	// Serial.printf("\r\nConnecting!\r\n--------------------\r\n%s\r\n%s\r\n", ml_url.c_str(), post_data.c_str());
	// if (!m_http.begin( ml_url ))
	// {
	// 	Serial.printf("\r\nConnection failed");
    //     return false;
	// }
	// m_http.set_header("Content-Type", "application/x-www-form-urlencoded");

    // // Setup response headers we want to collect
    // const char * headerKeys[] = {"ml_media_dir"} ;
    // const size_t numberOfHeaders = 1;
    // m_http.collect_headers(headerKeys, numberOfHeaders);

    // // Send the request
	// uint8_t httpCode = m_http.POST( post_data.c_str(), post_data.length() );

	// Serial.printf("HTTP Status: %d\r\n", httpCode); //Print HTTP return code

	// if (httpCode == 200) {
    //     if (m_http.get_header("ml_media_dir") == "1")
    //         return true;
    // }

    // return false;
};


bool MLFile::rewindDirectory() {
    if (!isDirectory()) {
        dirIsOpen = false;
        return false;
    }

    Debug_printv("Requesting JSON dir from PHP: ");

	//String url("http://c64.meatloaf.cc/api/");
    //String ml_url = std::string("http://" + host + "/api/").c_str();
    std::string ml_url = "http://" + host + "/api/";
	//String post_data("p=" + urlencode(m_device.path()) + "&i=" + urlencode(m_device.image()) + "&f=" + urlencode(m_filename));
    //String post_data = std::string("p=" + mstr::urlEncode(path)).c_str(); // pathInStream will return here /c64.meatloaf.cc/some/directory
    std::string post_data = "p=" + mstr::urlEncode(path);

	// Connect to HTTP server
	Serial.printf("\r\nConnecting!\r\n--------------------\r\n%s\r\n%s\r\n", ml_url.c_str(), post_data.c_str());
	if (!m_http.begin( ml_url ))
	{
		Serial.printf("\r\nConnection failed");
		dirIsOpen = false;
        return false;
	}
	m_http.set_header("Content-Type", "application/x-www-form-urlencoded");

    // Setup response headers we want to collect
    const char * headerKeys[] = {"accept-ranges", "content-type", "content-length", "ml_media_header", "ml_media_id", "ml_media_blocks_free", "ml_media_block_size"} ;
    const size_t numberOfHeaders = 7;
    m_http.collect_headers(headerKeys, numberOfHeaders);

    // Send the request
	uint8_t httpCode = m_http.POST(post_data.c_str(), post_data.length());

	Serial.printf("HTTP Status: %d\r\n", httpCode); //Print HTTP return code

	if (httpCode != 200) {
//        Debug_printv(m_http.errorToString(httpCode));
		dirIsOpen = false;

        // // Show HTTP Headers
        // Debug_printv("HEADERS--------------");
        // int i = 0;
        // for (i=0; i < m_http.headers(); i++)
        // {
        //     Debug_printv(m_http.header(i));
        // }
        // Debug_printv("DATA-----------------");
        // Debug_printv(m_http.getString());
        // Debug_printv("---------------------");

    }
    else
    {
        dirIsOpen = true;
        media_header = m_http.get_header("ml_media_header");
        media_id = m_http.get_header("ml_media_id");
        media_block_size = stoi(m_http.get_header("ml_media_block_size"));
        media_blocks_free = stoi(m_http.get_header("ml_media_blocks_free"));
    }

    return dirIsOpen;
};

MIStream* MLFile::inputStream() {
    // has to return OPENED stream
    //Debug_printv("[%s]", url.c_str());
    MIStream* istream = new MLIStream(url);
    istream->open();   
    return istream;
}; 


bool MLIStream::open() {
    PeoplesUrlParser urlParser;
    urlParser.parseUrl(url);

    m_isOpen = false;

    //String ml_url = std::string("http://" + urlParser.host + "/api/").c_str();
	//String post_data("p=" + urlencode(m_device.path()) + "&i=" + urlencode(m_device.image()) + "&f=" + urlencode(m_filename));
    //String post_data = std::string("p=" + mstr::urlEncode(urlParser.path)).c_str(); // pathInStream will return here /c64.meatloaf.cc/some/directory
    std::string ml_url = "https://api.meatloaf.cc/?" + urlParser.name;
    //std::string post_data = urlParser.path;
    url = ml_url;
    
    int httpCode = tryOpen();

    while(httpCode == HttpStatus_MovedPermanently || httpCode == HttpStatus_Found || httpCode == 303)
    {
        Debug_printv("--- Page moved, doing redirect");
        httpCode = tryOpen();
        // esp_err_t redirRc = esp_http_client_set_redirection(m_http);
        // if(redirRc == ESP_FAIL)
        //     return false;

        // Debug_printv("--- Page moved, pre open");

        // esp_http_client_set_method(m_http, HTTP_METHOD_GET);
        // esp_err_t openRc = esp_http_client_open(m_http, 0); // or open? It's not entirely clear...
        // if(openRc == ESP_FAIL)
        //     return false;

        // Debug_printv("--- Page moved, pre fetch header");

        // m_length = esp_http_client_fetch_headers(m_http); // without this it doesn't work properly

        // Debug_printv("--- post status");
    }
    
    if(httpCode != HttpStatus_Ok && httpCode != 301) {
        Debug_printv("opening stream failed, httpCode=%d", httpCode);
        close();
        return false;
    }

    m_isOpen = true;
    m_position = 0;

    Debug_printv("length=%d isFriendlySkipper=[%d] isText=[%d], httpCode=[%d]", m_length, isFriendlySkipper, isText, httpCode);

    return true;
};