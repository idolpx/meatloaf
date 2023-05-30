
#ifndef HTTP_TEMPLATES_H
#define HTTP_TEMPLATES_H

#include <string>

std::string format_uptime();
long uptime_seconds();

const std::string substitute_tag(const std::string &tag);

std::string parse_contents(const std::string &contents);
bool is_parsable(const char *extension);

#endif // HTTP_TEMPLATES_H