#include "tun-driver.h"

#include <arpa/inet.h>
#include <cerrno>
#include <sys/uio.h>

/* Read exactly len bytes (Signal safe) */
int read_n(int fd, char *buf, size_t len)
{
    int res = 0;
    int rlen;

    while (!__io_canceled && len > 0) {
        if ((rlen = read(fd, buf, len)) < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                SPDLOG_DEBUG("EAGAIN|EINTR = read()");
                continue;
            }
            return -1;
        }
        if (!rlen) {
            return 0;
        }

        len -= rlen;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        buf += rlen;
        res += rlen;
    }

    return res;
}

/* Write exactly len bytes (Signal safe) */
int write_n(int fd, char *buf, size_t len)
{
    int res = 0;
    int wlen;

    while (!__io_canceled && len > 0) {
        if ((wlen = write(fd, buf, len)) < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                SPDLOG_DEBUG("EAGAIN|EINTR = write()");
                continue;
            }
            return -1;
        }
        if (!wlen) {
            return 0;
        }

        len -= wlen;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        buf += wlen;
        res += wlen;
    }

    return res;
}

/* Functions to read/write frames. */
ssize_t frame_write(int fd, char *buf, size_t len)
{
    struct iovec iv[2];
    ssize_t flen = (len & ETHER_FRAME_LEN_MASK) + sizeof(uint16_t);
    uint16_t hdr = htons(len);

    /* Write frame */
    iv[0].iov_len = sizeof(uint16_t);
    iv[0].iov_base = &hdr;
    iv[1].iov_len = len;
    iv[1].iov_base = buf;

    while (io_is_enabled()) {
        ssize_t wlen;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        if ((wlen = writev(fd, iv, 2)) < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                SPDLOG_DEBUG("EAGAIN|EINTR = writev()");
                continue;
            }
            if (errno == ENOBUFS) {
                SPDLOG_DEBUG("ENOBUFS = writev()");
                return 0;
            }
            return wlen;
        }

        // NOTE: sizeof(uint16_t) == 2;
        if (wlen < 2 || (wlen - 2) != (ssize_t)len) {
            SPDLOG_ERROR("writev() returned len={} flen={}", wlen, flen);
            errno = EBADMSG;
            return -1;
        }

        /* Even if we wrote only part of the frame we can't use second write
         * since it will produce another frame */
        return wlen;
    }
    return 0;
}

ssize_t frame_read(int fd, char *buf, size_t len)
{
    uint16_t hdr;
    struct iovec iv[2];

    /* Read frame */
    iv[0].iov_len = sizeof(uint16_t);
    iv[0].iov_base = &hdr;
    iv[1].iov_len = len;
    iv[1].iov_base = buf;

    while (io_is_enabled()) {
        ssize_t rlen;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        if ((rlen = readv(fd, iv, 2)) < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                SPDLOG_DEBUG("EAGAIN|EINTR = readv()");
                continue;
            }
            return rlen;
        }

        hdr = ntohs(hdr);
        ssize_t flen = hdr & ETHER_FRAME_LEN_MASK;

        // NOTE: sizeof(uint16_t) == 2;
        if (rlen < 2 || (rlen - 2) != flen) {
            SPDLOG_ERROR("readv() returned len={} flen={}", rlen, flen);
            errno = EBADMSG;
            return -1;
        }

        return hdr;
    }
    return 0;
}

/* Read N bytes with timeout */
int readn_t(int fd, char *buf, size_t count, time_t timeout)
{
    fd_set fdset;
    struct timeval tv = {};

    tv.tv_usec = 0;
    tv.tv_sec = timeout;

    FD_ZERO(&fdset);
    FD_SET(fd, &fdset);
    if (select(fd + 1, &fdset, NULL, NULL, &tv) <= 0) {
        SPDLOG_DEBUG("select() Timeout");
        return -1;
    }

    return read_n(fd, buf, count);
}
