// #include "decode_svg.h"

// #define COMPAT_TEST

// #if defined(ESP8266) || defined(COMPAT_TEST)
// unsigned int
// xtoi(const char *hexStr)
// {
//     unsigned int result = 0;

//     if (hexStr)
//     {
//         char c;
//         for (;;)
//         {
//             c = *hexStr++;
//             if ((c >= '0') && (c <= '9'))
//                 c -= '0';
//             else if ((c >= 'A') && (c <= 'F'))
//                 c -= 'A' - 10;
//             else if ((c >= 'a') && (c <= 'f'))
//                 c -= 'a' - 10;
//             else
//                 break;
//             result = (result * 16) + c;
//         }
//     }
//     return(result);
// }
// #endif


// SvgParser::SvgParser(SvgOutput *newout)
// {
//     _output = newout;
// }


// uint8_t SvgParser::getProperty(char * start, const char * property, float * data){
//     char *ptr = getPropertyStart(start, property);
//     //  DBG_OUT("get float %s\n",property);
//     if(ptr == NULL)
//         return false;
// //    return sscanf(ptr,"%f",data);
//     *data = atof(ptr);
//     return true;
// }

// uint8_t SvgParser::getProperty(char * start, const char * property, int16_t * data){
//     char *ptr = getPropertyStart(start, property);
//     //   DBG_OUT("get int %s\n",property);
    
//     if(ptr == NULL)
//         return false;
//     //return sscanf(ptr,"%i",data);
//     *data=atoi(ptr);
//     return true;
// }

// uint8_t SvgParser::getProperty(char * start, const char * property, char ** data){
//     char *ptr2;
//     char *ptr = getPropertyStart(start, property);
    
//     //  DBG_OUT("get char %s\n",property);
    
//     if(ptr == NULL)
//         return false;
    
//     ptr2 = strstr (ptr,"\"");
//     if(ptr2 == NULL) return false;
    
//     *data = (char *)malloc((ptr2-ptr)+1);
//     if(*data == NULL)
//         return false;
    
//     memcpy(*data,ptr,ptr2-ptr);
//     (*data)[ptr2-ptr]=0;   
    
//     return true;
// }

// char * SvgParser::getPropertyStart(char * start, const char * property){
//     char *ptr, *ptr2, *searchString;
//     uint16_t pLen = strlen(property);
//     uint16_t sLen;
//     uint8_t retVal;
    
//     searchString = (char *)malloc(pLen+4);
//     if(searchString == NULL)
//         return NULL;
//     memcpy(searchString+1,property,pLen);
//     searchString[0]=' ';
//     searchString[pLen+1]='=';
//     searchString[pLen+2]='"';
//     searchString[pLen+3]=0;
// #if defined(ESP8266) || defined(COMPAT_TEST)
//     ptr = strstr ((char *)start,searchString);
// #else
//     ptr = strcasestr ((char *)start,searchString);
// #endif
//     free(searchString);
    
//     if(ptr == NULL) return NULL;
//     return ptr+pLen+3;
// }

// uint8_t SvgParser::parseStyle(char * start, struct svgStyle_t * style){
//     char *ptr = start; 
//     char *ptr2;
//     enum svgPropertyState_t tmp_state;
//     uint32_t tmp_val;
//     float    tmp_val_f;
    
//     const char *propNames[] = {"fill:", "stroke:", "stroke-width:", "font-size:", NULL};
    
//     //  DBG_OUT("search style: #%s#\n",start);
    
//     for(;;){
//         if(*ptr == 0)
//             break;
//         ptr2 = strstr(ptr,";");
//         if(ptr2)
//             *ptr2 = 0;
        
//         //         DBG_OUT("style prop : %s\n",ptr);
        
//         for(int8_t i=0; ;i++){
//             if(propNames[i] == NULL) break;
// #if defined(ESP8266) || defined(COMPAT_TEST)
//             if(strncmp(ptr,propNames[i],strlen(propNames[i])) == 0){
// #else
//             if(strncasecmp(ptr,propNames[i],strlen(propNames[i])) == 0){
// #endif
//                 tmp_state = SET;
// #if defined(ESP8266) || defined(COMPAT_TEST)
//                                 if(strncmp(ptr+strlen(propNames[i]),"none",4) == 0){

// #else

//                 if(strncasecmp(ptr+strlen(propNames[i]),"none",4) == 0){
//                     #endif
//                     tmp_state = UNSET;
//                     tmp_val = 0;
//                     tmp_val_f = 0;
//                 } else if(*(ptr+strlen(propNames[i])) == '#') {
// #if defined(ESP8266) || defined(COMPAT_TEST)
//                     tmp_val = xtoi(ptr+strlen(propNames[i])+1);
// #else

//                     if(!sscanf(ptr+strlen(propNames[i])+1,"%x",&tmp_val))
//                         return false;
//                     #endif

//                 } else if(*(ptr+strlen(propNames[i])) >= '0' && *(ptr+strlen(propNames[i])) <='9') {
// #if defined(ESP8266) || defined(COMPAT_TEST)
//                     tmp_val_f = atof(ptr+strlen(propNames[i]));
// #else
//                     if(!sscanf(ptr+strlen(propNames[i]),"%f",&tmp_val_f))
//                         return false;
// #endif
//                 }
                
//                 switch(i) {
//                     case 0: // fill
//                         style->fill_color_set = tmp_state;
//                         style->fill_color = tmp_val;
//                         break;
//                     case 1: // stroke
//                         style->stroke_color_set = tmp_state;
//                         style->stroke_color = tmp_val;
//                         break;
//                     case 2: // stroke-width
//                         style->stroke_width_set = tmp_state;
//                         if(tmp_val_f>0 && tmp_val_f<1) tmp_val_f = 1;
//                         style->stroke_width = tmp_val_f;
//                         break;
//                     case 3: // font-size
//                         style->font_size_set = tmp_state;
//                         if(tmp_val_f>0 && tmp_val_f<1) tmp_val_f = 1;
//                         style->font_size = tmp_val_f;
//                         break;
//                 }
//             }
//         }
        
//         if(ptr2)
//             ptr = ptr2 +1;
//         else
//             *ptr = 0;
//     }
// }

// uint8_t SvgParser::onClick(uint16_t x, uint16_t y, char **link){
    
//     struct svgLinkRefList_t ** tLinkRefList = &_linkRefList;
    
//     while(*tLinkRefList != NULL){
//         if(x >= (*tLinkRefList)->x_min && x <= (*tLinkRefList)->x_max && y >= (*tLinkRefList)->y_min && y <= (*tLinkRefList)->y_max) {
//             if(*link != NULL) free(link);
//             *link = (char *)malloc(strlen(((*tLinkRefList)->linkRef)->link));
//             if(*link == NULL) return false;
//             strcpy(*link,((*tLinkRefList)->linkRef)->link);
            
//             if(_automaticLinkManagement){
//                 if(((*tLinkRefList)->linkRef)->link[0] == '/'){
//                     char *ptr = strstr(((*tLinkRefList)->linkRef)->link, _linkSplit);
                    
//                     if(ptr != NULL){
//                         *ptr = 0;
//                         char * drop = executeCallbacks(ptr+strlen(_linkSplit));
//                     }
//                     if(readFile(((*tLinkRefList)->linkRef)->link)){
//                         print();
//                     }
//                 }
                
//             }
//             return true;
//         }
//         tLinkRefList = &(*tLinkRefList)->next; 
//     }
//     return false;
// }
 
// uint8_t SvgParser::addCallback(char * expression, char * (*callback)(int argc, char* argv[])){
//    struct svgCallbackList_t ** tCallbackList = &_callbackList;
                
//         while(*tCallbackList != NULL){
//             tCallbackList = &(*tCallbackList)->next;
//         }
//         *tCallbackList = (struct svgCallbackList_t *)malloc(sizeof(svgCallbackList_t));
//         if(*tCallbackList == NULL)
//             return false;
        
//         (*tCallbackList)->expression = (char *)malloc(strlen(expression)+1);
//         if((*tCallbackList)->expression == NULL){
//             free(*tCallbackList);
//             return false;
//         }
//         strcpy((*tCallbackList)->expression,expression);

//         (*tCallbackList)->callback = callback;
         
//         (*tCallbackList)->next = NULL;
//         return true;
// } 
    
// uint8_t SvgParser::callbackManagement(uint8_t cleanup){
//     if(!cleanup) DBG_OUT("callback management\n");
    
//     struct svgCallbackList_t ** tCallbackNext, ** tCallback = &_callbackList;
    
//     while(*tCallback != NULL){
//         tCallbackNext = &(*tCallback)->next;
//         if(cleanup){
//             free(((*tCallback)->expression));
//             free(*tCallback);
//             *tCallback = NULL;
//         }
//         else{
//             char * argv[2];
//             argv[0] = (*tCallback)->expression;
//             argv[1] = NULL;
//             char * ptr =(*tCallback)->callback(1,argv);

//             DBG_OUT("callback entry: #%s# returnValue: #%s#\n",(*tCallback)->expression,ptr);
//             free(ptr);
//         }
//         tCallback = tCallbackNext; 
//     }
// }

// char * SvgParser::executeCallbacks(char *programLine){
//     char * argv[SVG_PARSER_MAX_CALLBACK_ARGS+1];
//     uint8_t argc=0;
//     struct svgCallbackList_t ** tCallback;
    
//     argc = 1;
//     argv[0] = programLine;
//     //            DBG_OUT("cb tag: #%s#\n",argv[0]);
//     for(char * ptr = argv[0]; ptr < programLine+strlen(programLine); ptr++){
//         if(*ptr == ' '){
//             if(argc < SVG_PARSER_MAX_CALLBACK_ARGS){
//                 argv[argc] = ptr;
//                 *ptr = 0;
//                 argc++;
//             }
//         }
//     }
//     argv[argc+1] = NULL;
//     // argc and argv are prepared. search for callback function
//     tCallback = &_callbackList;
//     while(*tCallback != NULL){
//         if (strcmp((*tCallback)->expression,argv[0])==0){
//             return (*tCallback)->callback(1,argv);
//         }
//         tCallback = &(*tCallback)->next;
//     }
    
//     return NULL;    
// }

// uint8_t SvgParser::parseInCallbacks(){
    
//     char * foundStart=NULL, *foundStartNext=NULL, * foundEnd;
//     foundStart = strstr(_data,_callbackStart);
    
//     while(foundStart != NULL){
//         foundEnd = strstr(foundStart+strlen(_callbackStart),_callbackEnd);
//         if(foundEnd == NULL) {
//             //DBG_OUT("no end!\n%s\n",foundStart+strlen(_callbackStart));
//             return false;
//         }
//         foundStartNext = strstr(foundStart+strlen(_callbackStart),_callbackStart);
//         // check if there is an end tag before the next start tag.
//         // if not, this is not a proper callback tag!
//         if((foundStartNext == NULL) || (foundStartNext > foundEnd)){
//             *foundEnd = 0;
//             uint16_t lengthOfInsert = 0;
//             char * ptr = executeCallbacks(foundStart+strlen(_callbackStart));
//             if(ptr != NULL) lengthOfInsert = strlen(ptr);
            
//             // if(ptr != NULL) DBG_OUT("callback entry: #%s# returnValue: #%s#\n",foundStart,ptr);
//             uint16_t sizeOfTag = foundEnd-foundStart+strlen(_callbackEnd); 
            
//             // if _data gets smaller, copy before freeing unused memory
//             if (lengthOfInsert <= sizeOfTag){
//                 memcpy(foundStart+lengthOfInsert,foundStart+sizeOfTag,_bufLen-sizeOfTag-(foundStart-_data));
//                 if(ptr != NULL)
//                     memcpy(foundStart,ptr,lengthOfInsert);
//             }
            
//             if (lengthOfInsert!=foundEnd-foundStart+strlen(_callbackEnd)){
//                 _data = (char *)realloc((void *)_data,_bufLen + lengthOfInsert - sizeOfTag);
//                 _bufLen += lengthOfInsert - sizeOfTag;
//                 if(_data == NULL) {
//                     free(ptr);
//                     return false;
//                 }
//             }
//             if (lengthOfInsert>sizeOfTag){
//                 memmove(foundStart+lengthOfInsert,foundEnd+strlen(_callbackEnd),_bufLen-sizeOfTag);
//                 memcpy(foundStart,ptr,lengthOfInsert);
//             }
//             // next tag moved!
//             if(foundStartNext != NULL) foundStartNext += lengthOfInsert - sizeOfTag;
//             if(ptr != NULL) free(ptr);
//         }
//         foundStart = foundStartNext ;
//     }
// }


// uint8_t SvgParser::linkManagement(uint8_t cleanup){
//     if(!cleanup) DBG_OUT("\nlink management\n");
    
//     // clean or print link references
//     struct svgLinkRefList_t ** tLinkRefListNext, ** tLinkRefList = &_linkRefList;
    
//     while(*tLinkRefList != NULL){
//         tLinkRefListNext = &(*tLinkRefList)->next;
//         if(cleanup){
//             free(*tLinkRefList);
//             *tLinkRefList = NULL;
//         }
//         else
//             DBG_OUT("link reference entry: x_min %i x_max %i y_min %i y_max %i link %s\n",(*tLinkRefList)->x_min,(*tLinkRefList)->x_max,(*tLinkRefList)->y_min,(*tLinkRefList)->y_max,((*tLinkRefList)->linkRef)->link);
//         tLinkRefList = tLinkRefListNext; 
//     }
    
//     // clean or print link list
//     struct svgLinkList_t ** tLinkListNext, ** tLinkList = &_linkList;
    
//     while(*tLinkList != NULL){
//         tLinkListNext = &(*tLinkList)->next;
//         if(cleanup){
//             free(((*tLinkList)->link));
//             free(*tLinkList);
//             *tLinkList = NULL;
//         }
//         else
//             DBG_OUT("link entry: #%s#\n",(*tLinkList)->link);
//         tLinkList = tLinkListNext; 
//     }
//     if(cleanup){
//       _linkList = NULL;
//       _linkRefList = NULL;
//     }
// }

// uint8_t SvgParser::addLinkReference(int16_t x_min, int16_t y_min, int16_t x_max, int16_t y_max, struct svgStyle_t * style){
//         struct svgLinkRefList_t ** tLinkRefList = &_linkRefList;
        
//         if(style->linkRef == NULL) return false;
        
//         while(*tLinkRefList != NULL){
//             tLinkRefList = &(*tLinkRefList)->next;
//         }
//         *tLinkRefList = (struct svgLinkRefList_t *)malloc(sizeof(svgLinkRefList_t));
//         if(*tLinkRefList == NULL)
//             return false;
//         (*tLinkRefList)->x_min = x_min;
//         (*tLinkRefList)->y_min = y_min;
//         (*tLinkRefList)->x_max = x_max;
//         (*tLinkRefList)->y_max = y_max;
//         (*tLinkRefList)->linkRef = style->linkRef;
//         (*tLinkRefList)->next = NULL;
//         return true;
// }


// uint8_t SvgParser::processElement(char * start, enum svgTypes_t type, struct svgStyle_t * style){
//     float x,y,height,width;
//     char * styleString=NULL;
//     //  DBG_OUT("process elment type: %i \n",type);
    
   
//         char *transform;
//         if(getProperty(start, "transform", &transform)){
// #if defined(ESP8266) || defined(COMPAT_TEST)
//             if(strncmp(transform,"translate(",strlen("translate("))==0){
// #else
//                             if(strncasecmp(transform,"translate(",strlen("translate("))==0){
// #endif
//                 float x, y;
// #if defined(ESP8266) || defined(COMPAT_TEST)
//                 x = atof(transform+strlen("translate("));
//                 char * tmp = strstr(transform+strlen("translate("),",");
//                 if(tmp==NULL) {
//                     free(transform);
//                     return false;
//                 }
//                 tmp++;
//                 y = atof(tmp);

// #else
//                 if(!sscanf(transform+strlen("translate("),"%f,%f",&x,&y))
//                     return false;
// #endif
                
// //                DBG_OUT("translate: %i*%i \n",x,y);
//                 style->x_offset += x* style->x_scale;
//                 style->y_offset += y* style->y_scale;
//             }
//             free(transform);
//         }
//      if(type == GROUP){
//          // has nothing special
//     } 
//     else if(type == TEXT || type == TSPAN){
//         if(!getProperty(start, "x", &x))
//             return false;
//         if(!getProperty(start, "y", &y))
//             return false;
//         // check if there is a style string
//         if(getProperty(start, "style", &styleString))
//                 parseStyle(styleString, style);

//         style->x = x*style->x_scale+style->x_offset;
//         style->y = y*style->y_scale+style->y_offset;
        
//     } 
//     else if(type == LINK){
//         char * link;
//         uint16_t cnt = 0;
        
//         struct svgLinkList_t ** tLinkList = &_linkList;
        
//         if(!getProperty(start, "xlink:href", &link))
//             return false;
// //        DBG_OUT("link to: #%s#\n",link);
        
//         while(*tLinkList != NULL){
//             tLinkList = &(*tLinkList)->next;
//             cnt ++;
//         }
//         *tLinkList = (struct svgLinkList_t *)malloc(sizeof(svgLinkList_t));
//         (*tLinkList)->link = link;
//         (*tLinkList)->next = NULL;
        
//         // all childs will get this attribute
//         style->linkRef = *tLinkList;
//     } 
//     else if(type == RECT){
//         if(!getProperty(start, "x", &x))
//             return false;
//         if(!getProperty(start, "y", &y))
//             return false;
//         if(!getProperty(start, "width", &width))
//             return false;
//         if(!getProperty(start, "height", &height))
//             return false;
//         if(!getProperty(start, "style", &styleString))
//             return false;
//         x = x*style->x_scale+style->x_offset;
//         y = y*style->y_scale+style->y_offset;
//         height = height*style->x_scale;
//         width = width*style->y_scale;
// //        DBG_OUT("rect: x: %f y: %f height: %f width: %f \n",x,y,height,width);
        
//         parseStyle(styleString, style);
// //        DBG_OUT("fill: %X stroke color %X stroke width %i \n",style->fill_color, style->stroke_color,style->stroke_width);
        
//         addLinkReference(x, y, x+width, y+height, style);

//         _output->rect(x, y, width, height, style);
//     } 
//     else if(type == CIRCLE){
//         float radius;
//         if(!getProperty(start, "cx", &x))
//             return false;
//         if(!getProperty(start, "cy", &y))
//             return false;
//         if(!getProperty(start, "r", &radius))
//             return false;
//         if(!getProperty(start, "style", &styleString))
//             return false;
//         x = x*style->x_scale+style->x_offset;
//         y = y*style->y_scale+style->y_offset;
//         //TODO: radius scale?!
//         radius = radius * style->x_scale; 
// //        DBG_OUT("circle: x: %f y: %f radius: %f \n",x,y,radius);
        
//         parseStyle(styleString, style);
// //        DBG_OUT("fill: %X stroke color %X stroke width %i \n",style->fill_color, style->stroke_color,style->stroke_width);
        
//         addLinkReference(x-radius, y-radius, x+radius, y+radius, style);
//         _output->circle(x, y, radius, style);

//     } 
//     else if(type == PATH){
//         char * data, *ptr, *ptr2;
//         uint8_t absolutePos = true;
//         uint8_t first = true;
//         float last_x, last_y;
//         uint16_t convertedLen=0;
//         uint16_t *converted;
        
//         if(!getProperty(start, "d", &data))
//             return false;
//         if(!getProperty(start, "style", &styleString))
//             return false;
        
//         parseStyle(styleString, style);
//  //       DBG_OUT("stroke color %X stroke width %i \n",style->stroke_color,style->stroke_width);        
        
//         //      DBG_OUT("data %s \n",data);        
//         converted = (uint16_t *)data;
//         // the plain text is overwritten by the coordinate array
//         ptr = data;
//         for(;;){
//             if(*ptr == 0)
//                 break;
//             ptr2 = strstr(ptr," ");
//             if(ptr2)
//                 *ptr2 = 0;
            
//             // DBG_OUT("style prop : %s\n",ptr);
//             if(*ptr == 'M') absolutePos = true;
//             else if (*ptr == 'm') absolutePos = false;
//             else if (*ptr >= '0' && *ptr <= '9' || *ptr == '-'){
// #if defined(ESP8266) || defined(COMPAT_TEST)
//                 x = atof(ptr);
//                 char * tmp = strstr(ptr,",");
//                 if(tmp==NULL) {
//                     free(data);
//                     return false;
//                 }
//                 tmp++;
//                 y = atof(tmp);
// #else
//                 if(!sscanf(ptr,"%f,%f",&x,&y)){
//                     free(data);
//                     return false;
//                 }
// #endif
//                 //x *= style->x_scale;
//                 //y *= style->y_scale;
                
//                 if(first || absolutePos){
//                     first = false;
                    
//                     last_x = style->x_offset + x * (float)style->x_scale;
//                     last_y = style->y_offset + y * (float)style->x_scale;
                    
                    
//                 } else {
//                     last_x += x/2 * (float)style->x_scale;
//                     last_y += y/2 * (float)style->x_scale;
//                 }
                
//                 converted[convertedLen*2  ] = last_x;
//                 converted[convertedLen*2+1] = last_y;
//                 convertedLen++;
//             }
//             // ignore curves, arcs etc.
            
//             if(ptr2)
//                 ptr = ptr2 +1;
//             else
//                 *ptr = 0;
//         }
//         _output->path(converted, convertedLen, style);

//         free(data);
//     } 
//     else if(type == SVG){
//         char * viewbox;
//         float viewboxData[4];

//         if(!getProperty(start, "width", &width))
//             return false;

//         if(!getProperty(start, "height", &height))
//             return false;

//         if(getProperty(start, "viewBox", &viewbox)){
// #if defined(ESP8266) || defined(COMPAT_TEST)
// //            DBG_OUT("viewbox %s\n",viewbox);
//             viewboxData[0] = atof(viewbox);
            
//             char * tmp = strstr(viewbox," ");
//             if(tmp==NULL) return false;
//             tmp++;
//             viewboxData[1] = atof(tmp);
            
//             tmp = strstr(tmp," ");
//             if(tmp==NULL) return false;
//             tmp++;
//             viewboxData[2] = atof(tmp);
            
//             tmp = strstr(tmp," ");
//             if(tmp==NULL) return false;
//             tmp++;
//             viewboxData[3] = atof(tmp);
            
// //            DBG_OUT("viewbox: %i %i %i %i\n",viewboxData[0],viewboxData[1],viewboxData[2],viewboxData[3]);
// #else
            
//             if(!sscanf(viewbox,"%f %f %f %f",&viewboxData[0],&viewboxData[1],&viewboxData[2],&viewboxData[3]))
//                 return false;
// #endif
//             style->x_offset = viewboxData[0];
//             style->y_offset = viewboxData[1];
//             style->x_scale = width / viewboxData[2];
//             style->y_scale = height / viewboxData[3];
//             free(viewbox);
//         }
        
// //             DBG_OUT("svg: height: %f width: %f \n",height,width);
//              struct svgStyle_t clearStyle;
//              memcpy(&clearStyle,style,sizeof(svgStyle_t));
//              clearStyle.fill_color = 0xFFFFFF; // white
//              clearStyle.fill_color_set = SET;
//              clearStyle.stroke_color_set = UNSET;
//              _output->rect(style->x_offset, style->x_offset, width, height, &clearStyle);

//     }
    
//     // DBG_OUT("success: %d\n",sscanf(ptr + strlen("x=\""),"%f",&x));
//     if(styleString!=NULL) free(styleString);

//     return true;
// }

// // returns stop of tag
// // 0: error
// // 1: file end
// // 2: parent end
// // 3: success
// uint8_t SvgParser::processTag(char * start, char ** tagStart, uint16_t *processed, uint8_t parents, char * parentEnd, struct svgStyle_t * parentStyle){
//     const char *svgTypeNames[] = {"svg", "rect", "text", "tspan", "g", "path", "a", "circle", NULL};
    
//     char *curPos, *thisTagEnd, *childTagStart, *searchResult, *endTag;
//     bool endAfterTag = false; // tag is completely finished
//     bool halfAfterTag = false; // first part is finished and > is removed
//     bool hasChilds = false;
//     uint16_t childProcessed;
//     enum svgTypes_t type = NONE;  
//     struct svgStyle_t style;
    
//     *processed = 0;
//     // search for new tag
//     for(curPos = start; curPos < _data+_bufLen-1; curPos++){
//         // start of new tag?
//         if(*curPos == '<' && !(*(curPos+1)=='!' || *(curPos+1)=='?'))
//             break;
//     }
    
//     *tagStart = curPos;
    
//     // EOF?
//     if(curPos == _data+_bufLen){
//         *processed = (uint16_t)(_data + _bufLen - start);
//         return 1;
//     }
    
//     curPos++;
    
//     // search for end of tag name
//     for(searchResult = curPos; searchResult < _data+_bufLen; searchResult++){
//         if(*searchResult == ' ' || *searchResult == '>' || *searchResult == 0){
//             if(*searchResult == '>'){
//                 halfAfterTag = true;
//                 thisTagEnd = searchResult+1;
//             }
//             break;
//         }
//     } 
        
//     // no end of tag name was found!
//     if(searchResult == _data+_bufLen){
//         *processed = _data + _bufLen - start;
//         return 0;
//     }
    
//     if(*(searchResult-1) == '/' || *(searchResult-1) == '?'){
//         endAfterTag = true;
//         searchResult--;
//     }
    
//     *searchResult = 0;
//     endTag = curPos-1;
//     *endTag = '/';
    
    
//     if(parentEnd != NULL){
// #if defined(ESP8266) || defined(COMPAT_TEST)
//         if(strcmp(curPos,parentEnd) == 0){
// #else
//         if(strcasecmp(curPos,parentEnd) == 0){
// #endif
//       //      for(int8_t i=0;i<parents-1;i++) { DBG_OUT(" "); } DBG_OUT("end of parent tag #%s#\n",parentEnd);
            
//             *processed =  strlen(parentEnd)+2;
//             return 2;
//         }
//     }
    
//     // ok. here we have an independent tag. copy stlye of parent
//     if(parentStyle != 0){
//         memcpy(&style,parentStyle,sizeof(style));
//     } else {
//         style.x_scale = 1;
//         style.y_scale = 1;
//         style.fill_color = 0xFFFFFF;
//         style.stroke_color = 0;
//         style.linkRef = NULL;
        
//         style.fill_color_set = UNDEFINED;
//         style.stroke_color_set = UNDEFINED;
//         style.stroke_width_set = UNDEFINED;
//         style.font_size_set = UNDEFINED;
//     }
//     // DBG_OUT("x start %i y start %i x scale %f y scale %f\n",style.x_offset, style.y_offset, style.x_scale, style.y_scale);
//    // for(int8_t i=0;i<parents;i++) { DBG_OUT(" "); } DBG_OUT("tag: #%s#\n",curPos);
    
//     // convert tag name to svgType
//     type = NONE; // if unknown
//     for(int8_t i=0; ;i++){
//         if(svgTypeNames[i] == NULL) break;
//         if(strcmp(curPos,svgTypeNames[i]) == 0){
//             type = (svgTypes_t)i;
// //            DBG_OUT("found %s\n",svgTypeNames[i]);
//         }
//     }
    
//     curPos = searchResult;
    
//     for( ; curPos < _data+_bufLen; curPos++){
//         if(*curPos == '>' || endAfterTag || halfAfterTag) {
//             if(!halfAfterTag)
//                 thisTagEnd = curPos+1;
//             halfAfterTag = true;
            
//             *searchResult = ' ';
//             *curPos = 0;
            
//             processElement(searchResult, type, &style);
//             *searchResult = 0;  
            
//             // completely ready with tag?
//             if (*(curPos-1)=='/'  || *(curPos-1)=='?' || endAfterTag){ // end of tag? could be removed previously
//                 *processed = curPos-start;
//                 return 3;
//             }
//             // search for sub tags
            
//             else {
//                 //                  DBG_OUT("sub tag search\n");
                
//                 for(;;){
//                     uint16_t retVal = processTag(curPos,&childTagStart,&childProcessed, parents+1,endTag,&style);
//                     if(retVal == 0)
//                         return 0;
//                     else if(retVal == 1)
//                         return 1;
//                     else if(retVal == 2){
//                         // parent end!
//                         *processed = curPos +childProcessed- start;
//                         // check for tag value widthin > <
                   
//                         if(!hasChilds) {
//                             *childTagStart=0;
//                             *tagStart = childTagStart;
//                             if(strlen(thisTagEnd)){
//                                if(type == TEXT || type == TSPAN){
//                               // DBG_OUT("TEXT: x %i y %i stroke color %X stroke width %i value #%s#\n",style.x,style.y,style.stroke_color,style.stroke_width,thisTagEnd);
//                               _output->text(style.x,style.y,thisTagEnd,&style);
//                                }
//                             }
//                         }
//                         return 3;
//                     }
//                     else if(retVal == 3){
//                         hasChilds = true;
//                         // child end. search next child
//                         curPos += childProcessed;
//                     }
//                     //*tagStart = curPos;
//                     thisTagEnd = curPos;
//                 }
//             }
//         }
//     }
    
//     return 0;
// }

// uint8_t SvgParser::print(int16_t start_x, int16_t start_y){
//     uint16_t processed;
//     uint16_t done = 0;
//     uint8_t retVal=0;
//     char *childTagEnd;
//     //uint8_t processTag(char * start,  uint16_t *processed, uint8_t parents, char *parentEnd);
//     while(done!=_bufLen){
// //        DBG_OUT("print run last val %i start %i\n",retVal,done);
//         retVal = processTag(_data+done,&childTagEnd,&processed,0,NULL,NULL);
//         if(retVal != 3)
//             return false;
        
//         done += processed;
//     }  
    
// #ifdef ESP8266
//             DBG_OUT("free memory: %i\n",system_get_free_heap_size());
// #endif
//     return true;
// }


// void SvgParser::trimStr() {
//     int lastCopy = 0;
//     int startCut = 0;
//     int pos;
//     char *tmp;
    
//     enum {NORMAL, LAST_SPACE, WITHIN_SOME_SPACES} lastWasSpace;
    
//     // remove all newline, carriage return and tabs
//     for (int i = 0; i < _bufLen; i++) {
//         if ((_data)[i] == '\n' || (_data)[i] == '\r' || (_data)[i] == '\t')
//             (_data)[i] = ' ';
//     }
    
//     lastWasSpace = LAST_SPACE;
    
//     for (pos = 0; pos < _bufLen; pos++) {
//         switch (lastWasSpace) {
            
//             case NORMAL:
//                 if ((_data)[pos] == ' ') {
//                     lastWasSpace = LAST_SPACE;
//                 }
//                 break;
                
//             case LAST_SPACE:
//                 if ((_data)[pos] == ' ') {
//                     lastWasSpace = WITHIN_SOME_SPACES;
//                     startCut = pos;
//                 } else
//                     lastWasSpace = NORMAL;
//                 break;
                
//             case WITHIN_SOME_SPACES:
//                 // start cutting with first non space character
//                 if ((_data)[pos] != ' ') {
//                     memcpy(&((_data)[startCut]), &((_data)[pos]), _bufLen - pos + 1);
//                     pos = startCut;
//                     lastWasSpace = NORMAL;
//                 }
//                 break;
//         }
//     }
    
//     // some spaces at the end?
//     if (lastWasSpace == WITHIN_SOME_SPACES) {
//         (_data)[startCut] = 0;
//     }
    
//     _bufLen = strlen((const char *)_data);
//     if ((_data)[_bufLen - 1] == ' ') {
//         (_data)[_bufLen - 1] = 0;
//         _bufLen++;
//     }
    
//     _data = (char *)realloc((void *)_data,_bufLen);
// }

// uint8_t SvgParser::readFile(char * fileNameIn){
//     // as the fileName could be origin from the link system, it has to be saved before cleaning up!!!
//     char * fileName = (char *)malloc(strlen(fileNameIn)+1);
//     if(fileName == NULL) return false;
//     strcpy(fileName, fileNameIn);
//     // clean up first
//     cleanup();

// #ifdef ESP8266
//      if (!SPIFFS.begin())
//          return false;

//   // check if calibration file exists
//     File f = SPIFFS.open(fileName, "r");
//     DBG_OUT("filename: #%s#\n",fileName);

//     if (f) {
//         _bufLen = f.size();

//         _data = (int8_t *)malloc(_bufLen + 1); 
//         if(_data == NULL) {
//             f.close();
//             return false;
//         }
//       if (f.readBytes(_data, _bufLen) != _bufLen){
//           free(_data);
//           f.close();
//           return false;
//       }
//       f.close();
//     } else return false;
    
// #else
//     FILE *f = fopen(fileName, "rb");
//     fseek(f, 0, SEEK_END);
//     long fsize = ftell(f);
//     fseek(f, 0, SEEK_SET);  //same as rewind(f);

//     _data = (char *)malloc(fsize + 1); 
//     fread(_data, fsize, 1, f);
//     _data[fsize] = 0; // terminate 
//     fclose(f);
    
//     _bufLen = fsize;
    
// #endif
//     _curPos = 0;
//     //DBG_OUT("length read: %i\n",_bufLen);
//     trimStr();

//     // if there are registered callbacks, parse for them in the data
//     if(_callbackList != NULL) parseInCallbacks();
//     //DBG_OUT("length after trim: %i\n",_bufLen);
//     return true;
// }