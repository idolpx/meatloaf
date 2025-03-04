// #include "graphql.h"

// void GraphQL::connect(const char * host, uint16_t port, const char * url) {
//     auto cb = [&](WSEvent type, uint8_t * payload) {
//         switch (type)
//         {
//         case WSEvent::error:
//             callCallback(GQEvent::error, nullptr);
//             break;
//         case WSEvent::disconnected:
//             isConnected = false;
//             callCallback(GQEvent::disconnected, nullptr);
//             break;
//         case WSEvent::connected:
//             isConnected = true;
//             printf_P(PSTR("GraphQL: Websocket connected. Sending connection init\n"));
//             _ws.sendtxt("{\"type\":\"connection_init\",\"payload\":{}}");
//             /* If I don't send GQEvent::connected, but instead later after receiving connection_ack, 
//                 CRASH! Why? */
//             callCallback(GQEvent::connected, nullptr);
//             break;
//         case WSEvent::text:
//             {
//                 char* type = parseText((char *)payload, "type");
//                 if (type) {
//                     //printf_P(PSTR("GraphQL: type:%s\n"), type);
//                     if (!strncmp_P(type, PSTR("connection_ack"), 14)) { // Connection successfull
//                         printf_P(PSTR("GraphQL: Got connection_ack\n"));
//                         /* See comment above about the CRASH! */
//                         //callCallback(GQEvent::connected, nullptr);
//                         return;
//                     }
//                     if (!strncmp_P(type, PSTR("ka"), 2)) { // Keep-alive from server
//                         printf_P(PSTR("."));
//                         return;
//                     }
//                     if (!strncmp_P(type, PSTR("error"), 5)) { // Error
//                         callCallback(GQEvent::error, (char *)payload);
//                         return;
//                     }
//                 }
//                 //printf_P(PSTR("GraphQL: ws text:%s\n"),(char*)payload);
//                 callCallback(GQEvent::data, (char *)payload);
//             }
//             break;
//         default:
//             printf_P(PSTR("WS Got unimplemented event\n"));
//             break;
//         }
//     };
//     _ws.setCallback(cb);
//     _ws.connect(host, port, url);
// }

// static int id=1;
// // Returns id (for matching responses)
// int GraphQL::gqOperation(const char *oper, const char *data) {
//     char buf[1024];
//     sprintf_P(buf, PSTR("{\"id\":\"%d\",\"type\":\"start\",\"payload\":{\"query\":"
//                         "\"%s{"
//                         "%s"
//                         "}\"}}"), id, oper, data);
//     printf_P(PSTR("GraphQL: Sending %s: %s\n"), oper, buf);
//     _ws.sendtxt(buf);
//     return id++;
// }

// int GraphQL::mutation(const char *data) {
//     return gqOperation("mutation", data);
// }

// int GraphQL::query(const char *data) {
//     return gqOperation("query", data);
// }


// int GraphQL::subscription(const char *data) {
//     return gqOperation("subscription", data);
// }

// void GraphQL::disconnect() {
//     _ws.disconnect();
// }
// void GraphQL::loop(void) {
//     _ws.loop();
// }

// void GraphQL::callCallback(GQEvent type, char* payload) {
//     if (_callback) {
//         _callback(type, payload);
//     }
// }

// /************* "JSON parsing" (i.e. return first matching string) routines ******************/
// static const char* findKey(const char* json, const char *key) {
//     int keyLen = strlen(key);
//     const char *p = json;
//     do {
//         p = strstr(p, key);
//         if (p && (*(p-1) != '"' || *(p+keyLen) != '"')) { 
//             p++;
//         } else {
//             break;
//         }
//     } while (p);
//     return p;
// }

// bool parseBool(const char* json, const char *key) {
//     int keyLen = strlen(key);
//     const char *p = findKey(json, key);

//     if (p) {
//         return (*(p+keyLen+2) == 't');
//     }
//     return false;
// }

// char* parseText(const char* json, const char *key) {
//     static char buf[81]={0};
//     int keyLen = strlen(key);
//     const char *p = findKey(json, key);
//     if (!p) return NULL;

//     const char *end = strchr(p+keyLen+3, '"');
//     if (!end) return NULL;
//     strncpy(buf, p+keyLen+3, end-(p+keyLen+3));
//     buf[end-(p+keyLen+3)] = '\0';
//     buf[80]='\0';

//     return buf;
// }

// int parseInt(const char* json, const char *key) {
//     static char buf[11]={0};
//     int keyLen = strlen(key);
//     const char *p = findKey(json, key);
//     if (!p) return -1;
//     char *b = buf;
//     p += keyLen + 2;
//     while(isdigit(*p)) {
//         *b = *p;
//         p++;
//         b++;
//     }
//     *b = '\0';
//     return atoi(buf);
// }