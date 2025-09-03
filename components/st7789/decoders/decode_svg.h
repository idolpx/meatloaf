
// /***************************************************************************************
//  * * Function name:           SvgParser
//  ** Description:             Constructor 
//  ***************************************************************************************/

// #ifndef DECODE_SVG_H
// #define DECODE_SVG_H

// #include <stdio.h>
// #include <string.h>
// #include <stdlib.h>


// // ATTENTION: whenever you modify this line, change 'svgTypeNames' in 'processTag' accordingly!
// enum svgTypes_t {SVG, RECT, TEXT, TSPAN, GROUP, PATH, LINK, CIRCLE, NONE};
// enum svgPropertyState_t {UNDEFINED, UNSET, SET};

// struct svgStyle_t {
//     int16_t  x_offset;
//     int16_t  y_offset;
//     int16_t  x;
//     int16_t  y;
//     float    x_scale;
//     float    y_scale;
    
//     struct svgLinkList_t *linkRef;
    
//     uint32_t fill_color;
//     uint32_t stroke_color;
//     uint8_t  stroke_width;
//     uint8_t  font_size;
    
//     enum svgPropertyState_t  fill_color_set :2;
//     enum svgPropertyState_t  stroke_color_set :2;
//     enum svgPropertyState_t  stroke_width_set :2;
//     enum svgPropertyState_t  font_size_set :2;
    
// };

// // not yet dynamic
// #define SVG_PARSER_MAX_CALLBACK_ARGS 8

// struct svgCallbackList_t {
//     char * expression;
//     char * (*callback)(int argc, char* argv[]);
//     struct svgCallbackList_t *next;
// };

// struct svgLinkList_t {
//     char * link;
//     struct svgLinkList_t *next;
// };

// struct svgLinkRefList_t {
//     int16_t  x_min, y_min, x_max, y_max;
//     struct svgLinkList_t *linkRef;
//     struct svgLinkRefList_t *next;
// };


// class SvgOutput
// {
// public:  
//     SvgOutput() {};
    
//     virtual void circle(int16_t x, int16_t y, int16_t radius, struct svgStyle_t * style)
//     {   
//         DBG_OUT("CIRCLE: x %i y %i radius %i\n",x,y,radius);
//     } 
    
//     virtual void rect(int16_t x, int16_t y, int16_t width, int16_t height, struct svgStyle_t * style)
//     { 
//         DBG_OUT("RECT: x %i y %i width %i height %i\n",x,y,width, height);
        
//     }
//     virtual void text(int16_t x, int16_t y, char * text, struct svgStyle_t * style)
//     { 
//         DBG_OUT("TEXT: x %i y %i text \"%s\"\n",x,y,text);
        
//     }
//     virtual void path(uint16_t *data, uint16_t len, struct svgStyle_t * style)
//     { 
//         DBG_OUT("PATH: len: %i \n",len);
//     }    
    
// private:
    
// };

// // Class functions and variables
// class SvgParser 

// {
    
// public:
//     SvgParser(SvgOutput *newout);
//     uint8_t print(int16_t start_x=0, int16_t start_y=0);
//     uint8_t readFile(char * fileName);
//     uint8_t linkManagement(uint8_t cleanup=0);
//     uint8_t callbackManagement(uint8_t cleanup=0);
//     uint8_t addCallback(char * expression, char * (*callback)(int argc, char* argv[]));
    
//     uint8_t onClick(uint16_t x, uint16_t y, char **link);
//     uint8_t executeCallbacks();
    
// private:
    
//     SvgOutput *_output;
    
//     uint16_t _bufLen, _curPos;
//     char  * _data = NULL;
    
//     int8_t * _callbackStart = "&lt;@";
//     int8_t * _callbackEnd = "@&gt;";
//     int8_t * _linkSplit = "@";

//     bool _automaticLinkManagement = true;
    
//     struct svgLinkList_t * _linkList = NULL;
//     struct svgLinkRefList_t * _linkRefList = NULL;
//     struct svgCallbackList_t * _callbackList = NULL;
    
    
//     void cleanup(){
//         // clean up link lists
//         linkManagement(1);
//         //    callbackManagement(1);
//         if(_data != NULL)
//             free(_data);
//         _data = NULL;
//         _bufLen = 0;
//         _curPos = 0;
//     }
    
//     void trimStr();
    
//     uint8_t processElement(char * start, enum svgTypes_t type, struct svgStyle_t * style);
//     uint8_t processTag(char * start, char ** tagStart, uint16_t *processed, uint8_t parents, char *parentEnd, struct svgStyle_t * parentStyle);
    
//     uint8_t getProperty(char * start, const char * property, float * data);
//     uint8_t getProperty(char * start, const char * property, int16_t * data);
//     uint8_t getProperty(char * start, const char * property, char ** data);
//     char *  getPropertyStart(char * start, const char * property);
//     uint8_t parseStyle(char * start, struct svgStyle_t * style);
    
//     uint8_t addLinkReference(int16_t x_min, int16_t y_min, int16_t x_max, int16_t y_max, struct svgStyle_t * style);
//     char * executeCallbacks(char *programLine);
//     uint8_t parseInCallbacks();

    
    
// };




// #endif // DECODE_SVG_H