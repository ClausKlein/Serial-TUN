#include "tun-driver.h"

#ifndef NODEBUG
// #define SPDLOG_LEVEL_TRACE 0
// #define SPDLOG_LEVEL_DEBUG 1
// #define SPDLOG_LEVEL_INFO 2 // default log level
#    define SPDLOG_ACTIVE_LEVEL 0   // NOLINT
#endif

#include "spdlog/spdlog.h"
// NOTE: as second! CK
#include "spdlog/fmt/bin_to_hex.h"

#include <arpa/inet.h>
#include <array>
#include <chrono>
#include <errno.h>
#include <fcntl.h>
#include <iterator>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <thread>
#include <unistd.h>
using namespace std::literals;

constexpr uint16_t FRAME_LEN_MASK(0x7fff);
constexpr uint16_t FRAME_LENGTH(0x8000);

#ifndef NODEBUG
#    define wait100ms() std::this_thread::sleep_for(100ms)  // NOLINT
#else
#    define wait100ms() (void)0 // NOLINT
#endif

struct CommDevices
{
    int tapFileDescriptor;
    int serialFd;
    enum tun_mode_t mode;
};

char adapterName[IF_NAMESIZE] = {};
char serialDevice[_POSIX_PATH_MAX] = {};

/* IO cancelation */
extern volatile bool __io_canceled;

static inline bool io_is_enabled() { return !__io_canceled; }

static inline void io_cancel() { __io_canceled = true; }

volatile bool __io_canceled = false;

static void *serialToTap(void *ptr);
static void *tapToSerial(void *ptr);

static void signal_handler(int sig)
{
    SPDLOG_INFO("signal_handler() called with sig={}", sig);
    io_cancel();
}

/* Read exactly len bytes (Signal safe)*/
static inline int read_n(int fd, char *buf, size_t len)
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
        buf += rlen;
        res += rlen;
    }

    return res;
}

/* Write exactly len bytes (Signal safe)*/
static inline int write_n(int fd, char *buf, size_t len)
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
        buf += wlen;
        res += wlen;
    }

    return res;
}

/*
 * Create pipe. Return open fd.
 */
inline int pipe_open(int *fd)
{
    return socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
}

/* Write frames to pipe */
inline ssize_t pipe_write(int fd, char *buf, int len)
{
    return write_n(fd, buf, len);
}

/* Read frames from pipe */
inline ssize_t pipe_read(int fd, char *buf, int len)
{
    return read(fd, buf, len);
}

/* Functions to read/write frames. */
ssize_t frame_write(int fd, char *buf, size_t len)
{
    struct iovec iv[2];
    ssize_t flen = (len & FRAME_LEN_MASK) + sizeof(uint16_t);
    uint16_t hdr = htons(len);

    /* Write frame */
    iv[0].iov_len = sizeof(uint16_t);
    iv[0].iov_base = (char *)&hdr;
    iv[1].iov_len = len;
    iv[1].iov_base = buf;

    while (io_is_enabled()) {
        ssize_t wlen;
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
    iv[0].iov_base = (char *)&hdr;
    iv[1].iov_len = len;
    iv[1].iov_base = buf;

    while (io_is_enabled()) {
        ssize_t rlen;
        if ((rlen = readv(fd, iv, 2)) < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                SPDLOG_DEBUG("EAGAIN|EINTR = readv()");
                continue;
            }
            return rlen;
        }

        hdr = ntohs(hdr);
        ssize_t flen = hdr & FRAME_LEN_MASK;

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
    struct timeval tv;

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

/**
 * Handles getting packets from the serial port and writing them to the TAP
 * interface
 * @param ptr       - Pointer to the CommDevices struct
 */
static void *serialToTap(void *ptr)
{
    // Grab thread parameters
    struct CommDevices *args = (struct CommDevices *)ptr;
    int tapFd = args->tapFileDescriptor;
    int serialFd = args->serialFd;

    // Create TAP buffer
    std::array<char, FRAME_LENGTH> inBuffer;

    while (io_is_enabled()) {
        // Read bytes from serial
        // Incoming byte count
        ssize_t serialResult =
            frame_read(serialFd, inBuffer.data(), inBuffer.size());
        if (serialResult <= 0) {
            SPDLOG_ERROR("Serial read error {}({})", strerror(errno), errno);
            wait100ms();
            if (args->mode == VTUN_PIPE) {
                char msg[] = "Hallo again";
                (void)frame_write(tapFd, msg, sizeof(msg));
            }
            continue;
        }

        // Write the packet to the virtual interface
        ssize_t count = write(tapFd, inBuffer.data(), serialResult);
        if (count != serialResult) {
            SPDLOG_ERROR("TAP write error {}({})", strerror(errno), errno);
            wait100ms();
            continue;
        }

        SPDLOG_TRACE(
            "serialToTap {}:{:n}", count,
            spdlog::to_hex(std::begin(inBuffer), std::begin(inBuffer) + count));
        wait100ms();
    }

    SPDLOG_INFO("serialToTap thread stopped");
    return NULL;
}

static void *tapToSerial(void *ptr)
{
    // Grab thread parameters
    struct CommDevices *args = (struct CommDevices *)ptr;
    int tapFd = args->tapFileDescriptor;
    int serialFd = args->serialFd;

    // Create TAP buffer
    std::array<char, FRAME_LENGTH> inBuffer;

    while (io_is_enabled()) {
        // Incoming byte count
        ssize_t count = read(tapFd, inBuffer.data(), inBuffer.size());
        if (count <= 0) {
            SPDLOG_ERROR("TAP read error {}({})", strerror(errno), errno);
            wait100ms();
            continue;
        }

        // Write to serial port
        ssize_t serialResult;
        if (args->mode == VTUN_PIPE) {
            serialResult = pipe_write(serialFd, inBuffer.data(), count);
        } else {
            serialResult = frame_write(serialFd, inBuffer.data(), count);
        }
        if (serialResult < 0) {
            SPDLOG_ERROR("Serial write error {}({})", strerror(errno), errno);
            wait100ms();
            continue;
        }

        SPDLOG_TRACE(
            "tapToSerial {}:{:n}", count,
            spdlog::to_hex(std::begin(inBuffer), std::begin(inBuffer) + count));
        wait100ms();
    }

    SPDLOG_INFO("tapToSerial thread stopped");
    return NULL;
}

int main(int argc, char *argv[])
{
    enum tun_mode_t mode = VTUN_ETHER;

    // Grab parameters
    int param;
    while ((param = getopt(argc, argv, "i:d:pv")) > 0) {
        switch (param) {
        case 'i':
            strncpy(adapterName, optarg, IF_NAMESIZE - 1);
            break;
        case 'd':
            strncpy(serialDevice, optarg, sizeof(serialDevice) - 1);
            break;
        case 'p':
            mode = VTUN_PIPE;
            // fallthrough
        case 'v':
            spdlog::set_level(spdlog::level::trace); // Set global log level
            break;
        default:
            fprintf(stderr, "Usage: %s -i tun0 -d /dev/spidip2.0 [-p] [-v]\n",
                    *argv);
            return EXIT_FAILURE;
        }
    }

#if defined(__linux__) && defined(NOT_YET)
    if (adapterName[0] == '\0') {
        fprintf(stderr, "Tuntap interface name required (-i)\n");
        return EXIT_FAILURE;
    }
#endif

    if (serialDevice[0] == '\0') {
        fprintf(stderr, "Serial port required (-d)\n");
        return EXIT_FAILURE;
    }

    int fd[2] = {-1, -1};
    if (mode == VTUN_PIPE) {
        if (pipe_open(fd) < 0) {
            fprintf(stderr, "Can't create pipe. %s(%d)", strerror(errno),
                    errno);
            return EXIT_FAILURE;
        }
    }

    int tapFd = tun_open_common(adapterName, mode);
    if (tapFd < 0) {
        perror(adapterName);
        fprintf(stderr, "Could not open tuntap interface!\n");
        if (mode != VTUN_PIPE) {
            return EXIT_FAILURE;
        }

        fprintf(stderr, "Test mode, use VTUN_PIPE first end!\n");
        tapFd = fd[0];
        char pingMsg[] = "\x05\0Ping";
        ssize_t len = write_n(tapFd, pingMsg, sizeof(pingMsg));
        if (len <= 0) {
            perror("write_n Ping");
        }
    }

    int serialFd = open(serialDevice, O_RDWR | O_CLOEXEC);
    if (serialFd < 0) {
        perror(serialDevice);
        fprintf(stderr, "Could not open serial port!\n");
        if (mode != VTUN_PIPE) {
            close(tapFd);
            return EXIT_FAILURE;
        }

        fprintf(stderr, "Test mode, use VTUN_PIPE second end!\n");
        serialFd = fd[1];
        char pingMsg[] = "Pong";
        ssize_t len = frame_write(serialFd, pingMsg, sizeof(pingMsg));
        if (len <= 0) {
            perror("frame_write Pong");
        }
    }

    // register signal handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGHUP, &sa, NULL);  // terminal line hangup (1)
    sigaction(SIGQUIT, &sa, NULL); // quit program (3)
    // TODO sigaction(SIGPIPE, SIG_IGN, NULL); // timer expired (14)
    // FIXME sigaction(SIGTERM, &sa, NULL);  // software termination signal (15)
    // NO! XXX sigaction(SIGALRM, &sa, NULL);

    // Create threads
    pthread_t serial2tap;
    pthread_t tap2serial;

    struct CommDevices threadParams;
    threadParams.tapFileDescriptor = tapFd;
    threadParams.serialFd = serialFd;
    threadParams.mode = mode;

    SPDLOG_INFO("Starting threads");
    do {
        int err = pthread_create(&tap2serial, NULL, tapToSerial,
                                 (void *)&threadParams);
        if (err != 0) {
            fprintf(stderr, "Can't create tapToSerial thread. %s(%d)",
                    strerror(err), err);
            break;
        }
        err = pthread_create(&serial2tap, NULL, serialToTap,
                             (void *)&threadParams);
        if (err != 0) {
            fprintf(stderr, "Can't create serialToTap thread. %s(%d)",
                    strerror(err), err);
            break;
        }

        if (mode == VTUN_PIPE) {
            char msg[] = "Hallo\n";
            pipe_write(1, msg, sizeof(msg)); // stdout
            if (readn_t(0, msg, sizeof(msg), 3) < 0) {
                fprintf(stderr, "Timeout while read from stdin\n");
            } else {
                sleep(1);
            }
            signal_handler(3);

            alarm(4); // NOTE: XXX send unhandled SIGALRM after 4 sec! CK
        }

        err = pthread_join(tap2serial, NULL);
        SPDLOG_INFO("Thread tapToSerial joined {}", err);
        close(tapFd);

        err = pthread_join(serial2tap, NULL);
        SPDLOG_INFO("Thread serialToTap joined {}", err);
        close(serialFd);
        return EXIT_SUCCESS;
    } while (false);

    close(tapFd);
    close(serialFd);
    return EXIT_FAILURE;
}
