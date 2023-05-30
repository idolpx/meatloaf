#include <stdio.h>
#include <sstream>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <cctype>
#include <iomanip>

#include "file-utils.h"
#include "server.h"

using namespace WebDav;

Server::Server(std::string rootPath, std::string rootURI) : rootPath(rootPath), rootURI(rootURI) {}

static std::string urlDecode(std::string str)
{
    std::string ret;
    char ch;
    int i, ii, len = str.length();

    for (i = 0; i < len; i++)
    {
        if (str[i] != '%')
        {
            if (str[i] == '+')
                ret += ' ';
            else
                ret += str[i];
        }
        else
        {
            sscanf(str.substr(i + 1, 2).c_str(), "%x", &ii);
            ch = static_cast<char>(ii);
            ret += ch;
            i += 2;
        }
    }

    return ret;
}

static std::string urlEncode(const std::string &value)
{
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (std::string::const_iterator i = value.begin(), n = value.end(); i != n; ++i)
    {
        std::string::value_type c = (*i);

        // Keep alphanumeric and other accepted characters intact
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == ' ')
        {
            escaped << c;
            continue;
        }

        // Any other characters are percent-encoded
        escaped << std::uppercase;
        escaped << '%' << std::setw(2) << int((unsigned char)c);
        escaped << std::nouppercase;
    }

    return escaped.str();
}

std::string Server::uriToPath(std::string uri)
{
    if (uri.find(rootURI) != 0)
        return rootPath;

    std::string path = rootPath + uri.substr(rootURI.length());
    while (path.substr(path.length() - 1, 1) == "/")
        path = path.substr(0, path.length() - 1);

    return urlDecode(path);
}

std::string Server::pathToURI(std::string path)
{
    if (path.find(rootPath) != 0)
        return "";

    const char *sep = path[rootPath.length()] == '/' ? "" : "/";
    std::string uri = rootURI + sep + path.substr(rootPath.length());

    return urlEncode(uri);
}

std::string Server::formatTime(time_t t)
{
    char buf[32];
    struct tm *lt = localtime(&t);
    // strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", lt);
    strftime(buf, sizeof(buf), "%a, %d %b %H:%M:%S %Z", lt);

    return std::string(buf);
}

static void xmlElement(std::ostringstream &s, const char *name, const char *value)
{
    s << "<" << name << ">" << value << "</" << name << ">\n";
}

void Server::sendMultiStatusResponse(Response &resp, MultiStatusResponse &msr)
{
    std::ostringstream s;

    s << "<response>\n";
    xmlElement(s, "href", msr.href.c_str());
    s << "<propstat><prop>\n";

    for (const auto &p : msr.props)
        xmlElement(s, p.first.c_str(), p.second.c_str());

    xmlElement(s, "resourcetype", msr.isCollection ? "<collection/>" : "");

    s << "</prop>\n";
    xmlElement(s, "status", msr.status.c_str());

    s << "</propstat></response>\n";

    resp.sendChunk(s.str().c_str());
}

int Server::sendPropResponse(Response &resp, std::string path, int recurse)
{
    struct stat sb;
    int ret = stat(path.c_str(), &sb);
    if (ret < 0)
        return -errno;

    std::string uri = pathToURI(path);
    // printf("%s() path >%s< uri >%s<\n", __func__, path.c_str(), uri.c_str());

    MultiStatusResponse r;

    r.href = uri,
    r.status = "HTTP/1.1 200 OK",

    r.props["creationdate"] = formatTime(sb.st_ctime);
    r.props["getlastmodified"] = formatTime(sb.st_mtime);
    r.props["displayname"] = basename(path.c_str());

    r.isCollection = ((sb.st_mode & S_IFMT) == S_IFDIR);

    if (!r.isCollection)
    {
        r.props["getcontentlength"] = std::to_string(sb.st_size);
        r.props["getcontenttype"] = "application/binary";
        r.props["getetag"] = std::to_string(sb.st_ino);
    }

    sendMultiStatusResponse(resp, r);

    if (r.isCollection && recurse > 0)
    {
        DIR *dir = opendir(path.c_str());
        if (dir)
        {
            struct dirent *de;

            while ((de = readdir(dir)))
            {
                if (strcmp(de->d_name, ".") == 0 ||
                    strcmp(de->d_name, "..") == 0)
                    continue;

                std::string rpath = path + "/" + de->d_name;
                sendPropResponse(resp, rpath, recurse - 1);
            }

            closedir(dir);
        }
    }

    return 0;
}

// http entry points
int Server::doCopy(Request &req, Response &resp)
{
    if (req.getDestination().empty())
        return 400;

    if (req.getPath() == req.getDestination())
        return 403;

    int recurse =
        (req.getDepth() == Request::DEPTH_0) ? 0 : (req.getDepth() == Request::DEPTH_1) ? 1
                                                                                        : 32;

    std::string destination = uriToPath(req.getDestination());
    bool destinationExists = access(destination.c_str(), F_OK) == 0;

    int ret = copy_recursive(req.getPath(), destination, recurse, req.getOverwrite());

    switch (ret)
    {
    case 0:
        if (destinationExists)
            return 204;

        return 201;

    case -ENOENT:
        return 409;

    case -ENOSPC:
        return 507;

    case -ENOTDIR:
    case -EISDIR:
    case -EEXIST:
        return 412;

    default:
        return 500;
    }

    return 0;
}

int Server::doDelete(Request &req, Response &resp)
{
    if (req.getDepth() != Request::DEPTH_INFINITY)
        return 400;

    int ret = rm_rf(req.getPath().c_str());
    if (ret < 0)
        return 404;

    return 200;
}

int Server::doGet(Request &req, Response &resp)
{
    struct stat sb;
    int ret = stat(req.getPath().c_str(), &sb);
    if (ret < 0)
        return 404;

    FILE *f = fopen(req.getPath().c_str(), "r");
    if (!f)
        return 500;

    resp.setHeader("Content-Length", sb.st_size);
    resp.setHeader("ETag", sb.st_ino);
    resp.setHeader("Last-Modified", formatTime(sb.st_mtime));
    // resp.flush();

    ret = 0;

    const int chunkSize = 8192;
    char *chunk = (char *)malloc(chunkSize);

    for (;;)
    {
        size_t r = fread(chunk, 1, chunkSize, f);
        if (r <= 0)
            break;

        if (!resp.sendChunk(chunk, r))
        {
            ret = -1;
            break;
        }
    }

    free(chunk);
    fclose(f);
    resp.closeChunk();

    if (ret == 0)
        return 200;

    return 500;
}

int Server::doHead(Request &req, Response &resp)
{
    struct stat sb;
    int ret = stat(req.getPath().c_str(), &sb);
    if (ret < 0)
        return 404;

    resp.setHeader("Content-Length", sb.st_size);
    resp.setHeader("ETag", sb.st_ino);
    resp.setHeader("Last-Modified", formatTime(sb.st_mtime));

    return 200;
}

int Server::doLock(Request &req, Response &resp)
{
    return 501;
}

int Server::doMkcol(Request &req, Response &resp)
{
    if (req.getContentLength() != 0)
        return 415;

    int ret = mkdir(req.getPath().c_str(), 0755);
    if (ret == 0)
        return 201;

    switch (errno)
    {
    case EEXIST:
        return 405;

    case ENOENT:
        return 409;

    default:
        return 500;
    }
}

int Server::doMove(Request &req, Response &resp)
{
    if (req.getDestination().empty())
        return 400;

    struct stat sourceStat;
    int ret = stat(req.getPath().c_str(), &sourceStat);
    if (ret < 0)
        return 404;

    std::string destination = uriToPath(req.getDestination());
    bool destinationExists = access(destination.c_str(), F_OK) == 0;

    if (destinationExists)
    {
        if (!req.getOverwrite())
            return 412;

        rm_rf(destination.c_str());
    }

    ret = rename(req.getPath().c_str(), destination.c_str());

    switch (ret)
    {
    case 0:
        if (destinationExists)
            return 204;

        return 201;

    case -ENOENT:
        return 409;

    case -ENOSPC:
        return 507;

    case -ENOTDIR:
    case -EISDIR:
    case -EEXIST:
        return 412;

    default:
        return 500;
    }
}

int Server::doOptions(Request &req, Response &resp)
{
    return 200;
}

int Server::doPropfind(Request &req, Response &resp)
{
    bool exists = (req.getPath() == rootPath) ||
                  (access(req.getPath().c_str(), R_OK) == 0);

    Debug_printv("root[%s] req[%s]", rootPath.c_str(), req.getPath().c_str());
    
    if (!exists)
        return 404;

    int recurse =
        (req.getDepth() == Request::DEPTH_0) ? 0 : (req.getDepth() == Request::DEPTH_1) ? 1
                                                                                        : 32;

    resp.setStatus(207);
    resp.setHeader("Transfer-Encoding", "chunked");
    resp.setContentType("text/xml; charset=\"utf-8\"");
    resp.flushHeaders();

    resp.sendChunk("<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n");
    resp.sendChunk("<multistatus xmlns=\"DAV:\">\n");

    sendPropResponse(resp, req.getPath(), recurse);
    resp.sendChunk("</multistatus>\n");
    resp.closeChunk();

    return 207;
}

int Server::doProppatch(Request &req, Response &resp)
{
    bool exists = access(req.getPath().c_str(), R_OK) == 0;

    if (!exists)
        return 404;

    return 501;
}

int Server::doPut(Request &req, Response &resp)
{
    bool exists = access(req.getPath().c_str(), R_OK) == 0;
    FILE *f = fopen(req.getPath().c_str(), "w");
    if (!f)
        return 404;

    int remaining = req.getContentLength();

    const int chunkSize = 8192;
    char *chunk = (char *)malloc(chunkSize);

    int ret = 0;

    while (remaining > 0)
    {
        int r, w;
        r = req.readBody(chunk, std::min(remaining, chunkSize));
        if (r <= 0)
            break;

        w = fwrite(chunk, 1, r, f);
        if (w != r)
        {
            ret = -errno;
            break;
        }

        remaining -= w;
    }

    free(chunk);
    fclose(f);
    resp.closeChunk();

    if (ret < 0)
        return 500;

    if (exists)
        return 200;

    return 201;
}

int Server::doUnlock(Request &req, Response &resp)
{
    return 501;
}
