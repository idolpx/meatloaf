/*
   ESP32 / FreeRTOS / lwIP platform configuration for libiscsi.
   This file is included whenever HAVE_CONFIG_H is defined.
*/
#ifndef __ISCSI_ESP32_CONFIG_H__
#define __ISCSI_ESP32_CONFIG_H__

#define HAVE_POLL_H          1
#define HAVE_UNISTD_H        1
#define HAVE_SYS_TYPES_H     1
#define HAVE_ARPA_INET_H     1
#define HAVE_SYS_SOCKET_H    1
#define HAVE_STDBOOL_H       1
#define HAVE_STDLIB_H        1
#define HAVE_STRING_H        1
#define HAVE_STDINT_H        1

/* iSER (iSCSI over RDMA) requires Linux kernel infrastructure — not available on ESP32 */
#undef HAVE_LINUX_ISER

/* lwIP provides IPPROTO_TCP but not SOL_TCP; define it so socket.c skips
   the getprotobyname() path (which is also unavailable on ESP32). */
#ifndef SOL_TCP
#define SOL_TCP IPPROTO_TCP
#endif

/* No multi-threading support via libiscsi's own semaphore wrappers;
   we use the synchronous (polling) event loop in sync.c instead. */
#undef HAVE_MULTITHREADING

#endif /* __ISCSI_ESP32_CONFIG_H__ */
