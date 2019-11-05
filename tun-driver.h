#pragma once

#ifndef NDEBUG
// #define SPDLOG_LEVEL_TRACE 0
// #define SPDLOG_LEVEL_DEBUG 1
// #define SPDLOG_LEVEL_INFO 2 // default log level
#    define SPDLOG_ACTIVE_LEVEL 0 // NOLINT
#endif

#define SPDLOG_FUNCTION static_cast<const char *>(__func__)

#include "spdlog/spdlog.h"
// NOTE: as second! CK
#include "spdlog/fmt/bin_to_hex.h"

#include <fcntl.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

constexpr uint16_t ETHER_FRAME_LEN_MASK(0x7fff);
constexpr uint16_t ETHER_FRAME_LENGTH(0x8000);

enum tun_mode_t
{
    VTUN_ETHER = 0, // TAP mode
    VTUN_P2P = 1,   // TUN mode
    VTUN_PIPE = 2   // test loop only! CK
};

/**
 * Create a new TUN adapter
 * @param dev       The new adapter's path
 * @param mode      The new adapter's mode
 * @return The file descriptor to communicate with the device
 */
int tun_open_common(char *dev, enum tun_mode_t mode);

/* IO cancelation */
extern volatile bool __io_canceled;
static inline bool io_is_enabled() { return !__io_canceled; }
static inline void io_cancel() { __io_canceled = true; }

/* Read exactly len bytes (Signal safe) */
int read_n(int fd, char *buf, size_t len);

/* Write exactly len bytes (Signal safe) */
int write_n(int fd, char *buf, size_t len);

/*
 * Create pipe. Return open fd.
 */
static inline int pipe_open(int *fd)
{
    return socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
}

/* Write frames to pipe */
static inline ssize_t pipe_write(int fd, char *buf, int len)
{
    return write_n(fd, buf, len);
}

/* Read frames from pipe */
static inline ssize_t pipe_read(int fd, char *buf, int len)
{
    return read(fd, buf, len);
}

/* Functions to read/write frames. */
ssize_t frame_write(int fd, char *buf, size_t len);
ssize_t frame_read(int fd, char *buf, size_t len);

/* Read N bytes with timeout */
int readn_t(int fd, char *buf, size_t count, time_t timeout);
