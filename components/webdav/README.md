# WebDav server for POSIX-compatible systems

This package implements a simple WebDav server for systems with a POSIX-compatible file system storage. The server has limited functionality and primarily targets embedded platforms with limited resources.

# Compatibility

## Web server

The code has all parts that are specific to the http request and response factored out through an abstact base class. Examples are provided for the ESPIDF http server as well as libsoup.

## File system

A POSIX compatible file system must be provided. In particular, the code uses the following functions:

        * `fopen()`, `fclose()`
        * `fread()`, `fwrite()`
        * `stat()`
        * `access()`
        * `opendir()`, `closedir()`
        * `unlink()`
        * `rename()`

# Current limitations

* No support for locking/unlocking
* No support for property writing

# Installation

## As ESP-IDF component

In your project repository, add this component as sub-module:

```
git submodule init
git submodule add https://github.com/zonque/WebDavServer.git components/webdav
```

# License

LGPL-3, see LICENSE file.

