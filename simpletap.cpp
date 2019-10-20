#include "tun-driver.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

const uint16_t FRAME_LEN_MASK(0x7fff);
const uint16_t FRAME_LENGTH(0x8000);

struct CommDevices
{
    int tapFileDescriptor;
    int serialFd;
};

char adapterName[IFNAMSIZ] = {};
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
    fprintf(stderr, "signal_handler() called with sig(%d)\n", sig);
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
inline int pipe_write(int fd, char *buf, int len)
{
    return write_n(fd, buf, len);
}

/* Read frames from pipe */
inline int pipe_read(int fd, char *buf, int len) { return read(fd, buf, len); }

/* Functions to read/write frames. */
int frame_write(int fd, char *buf, size_t len)
{
    struct iovec iv[2];
    size_t flen = (len & FRAME_LEN_MASK) + sizeof(uint16_t);
    uint16_t hdr = htons(len);

    /* Write frame */
    iv[0].iov_len = sizeof(uint16_t);
    iv[0].iov_base = (char *)&hdr;
    iv[1].iov_len = len;
    iv[1].iov_base = buf;

    while (io_is_enabled()) {
        int wlen;
        if ((wlen = writev(fd, iv, 2)) < 0) {
            if (io_is_enabled() && (errno == EAGAIN || errno == EINTR)) {
                continue;
            }
            if (errno == ENOBUFS) {
                return 0;
            }
        }

        // NOTE: sizeof(uint16_t) == 2;
        if (wlen < 2 || (wlen - 2) != len) {
            fprintf(stderr, "writev() returned %d! flen=%d\n", wlen, flen);
            return -1;
        }

        /* Even if we wrote only part of the frame we can't use second write
         * since it will produce another frame */
        return wlen;
    }
    return 0;
}

int frame_read(int fd, char *buf, size_t len)
{
    uint16_t hdr;
    struct iovec iv[2];
    int rlen;

    /* Read frame */
    iv[0].iov_len = sizeof(uint16_t);
    iv[0].iov_base = (char *)&hdr;
    iv[1].iov_len = len;
    iv[1].iov_base = buf;

    while (io_is_enabled()) {
        if ((rlen = readv(fd, iv, 2)) < 0) {
            if (io_is_enabled() && (errno == EAGAIN || errno == EINTR)) {
                continue;
            }
            // NO! return rlen;
        }
        hdr = ntohs(hdr);
        uint16_t flen = hdr & FRAME_LEN_MASK;

        // NOTE: sizeof(uint16_t) == 2;
        if (rlen < 2 || (rlen - 2) != flen) {
            fprintf(stderr, "readv() returned %d! flen=%d\n", rlen, flen);
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

    char inBuffer[FRAME_LENGTH];
    size_t outSize = 0;
    int inIndex = 0;

    // Incoming byte count
    size_t count;
    ssize_t serialResult;

    while (io_is_enabled()) {
        // Read bytes from serial
        serialResult = frame_read(serialFd, &inBuffer[0], sizeof(inBuffer));

        if (serialResult < 0) {
            perror("Serial read error!");
        } else {
            // Write the packet to the virtual interface
            count = write(tapFd, inBuffer, serialResult);
            if (count != serialResult) {
                perror("TAP write error!");
            }
        }
    }

    return NULL;
}

static void *tapToSerial(void *ptr)
{
    // Grab thread parameters
    struct CommDevices *args = (struct CommDevices *)ptr;

    int tapFd = args->tapFileDescriptor;
    int serialFd = args->serialFd;

    // Create TAP buffer
    char inBuffer[FRAME_LENGTH];

    // Incoming byte count
    ssize_t count;
    int serialResult;

    while (io_is_enabled()) {
        count = read(tapFd, inBuffer, sizeof(inBuffer));
        if (count < 0) {
            perror("Could not read from TAP!");
            continue;
        }

        // Write to serial port
        serialResult = frame_write(serialFd, inBuffer, count);
        if (serialResult < 0) {
            perror("Could not write to serial!");
        }
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    enum tun_mode_t mode = VTUN_ETHER;

    // Grab parameters
    int param;
    while ((param = getopt(argc, argv, "i:d:p")) > 0) {
        switch (param) {
        case 'i':
            strncpy(adapterName, optarg, IFNAMSIZ - 1);
            break;
        case 'd':
            strncpy(serialDevice, optarg, sizeof(serialDevice) - 1);
            break;
        case 'p':
            mode = VTUN_PIPE;
            break;
        default:
            fprintf(stderr, "Usage: %s -i tun0 -d /dev/spidip2.0 [-p]\n",
                    *argv);
            return EXIT_FAILURE;
        }
    }

#ifdef __linux__
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

    int tapFd = tun_open_common(adapterName, VTUN_ETHER);
    if (tapFd < 0) {
        perror(adapterName);
        fprintf(stderr, "Could not open tuntap interface!\n");
        if (mode != VTUN_PIPE) {
            return EXIT_FAILURE;
        }

        fprintf(stderr, "Test mode, use VTUN_PIPE first end!\n");
        tapFd = fd[0];
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
        char pingMsg[] = "Ping";
        frame_write(serialFd, pingMsg, sizeof(pingMsg));
    }

    // register signal handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGHUP, &sa, NULL);   // terminal line hangup (1)
    sigaction(SIGQUIT, &sa, NULL);  // quit program (3)
    // TODO sigaction(SIGPIPE, SIG_IGN, NULL); // timer expired (14)
    sigaction(SIGTERM, &sa, NULL);  // software termination signal (15)
    // NO! XXX sigaction(SIGALRM, &sa, NULL);

    // Create threads
    pthread_t serial2tap;
    pthread_t tap2serial;
    int ret1;
    int ret2;

    struct CommDevices threadParams;
    threadParams.tapFileDescriptor = tapFd;
    threadParams.serialFd = serialFd;

    printf("Starting threads\n");
    ret1 =
        pthread_create(&tap2serial, NULL, tapToSerial, (void *)&threadParams);
    ret2 =
        pthread_create(&serial2tap, NULL, serialToTap, (void *)&threadParams);

    if (mode == VTUN_PIPE) {
        char msg[] = "Ping\n";
        pipe_write(1, msg, sizeof(msg)); // stdout
        if (readn_t(0, msg, sizeof(msg), 3) < 0) {
            fprintf(stderr, "Timeout while read from stdin\n");
        }
        signal_handler(3);
        alarm(1); // NOTE: XXX send unhandled SIGALRM after 1 sec! CK
    }

    pthread_join(tap2serial, NULL);
    fprintf(stderr, "Thread tap-to-network returned %d\n", ret1);
    pthread_join(serial2tap, NULL);
    fprintf(stderr, "Thread network-to-tap returned %d\n", ret2);

    close(tapFd);
    close(serialFd);
    return EXIT_SUCCESS;
}

/***
Claus-MBP:Serial-TUN clausklein$ timeout -s 1 5 ../.build-Serial-TUN-Debug/simpletap -d /dev/XXX -p
No such file or directory
Could not open tuntap interface!
Test mode, use VTUN_PIPE first end!
/dev/XXX: No such file or directory
Could not open serial port!
Test mode, use VTUN_PIPE second end!
Starting threads
Ping
Timeout while read from stdin
signal_handler() called with sig(3)
Alarm clock: 14
Claus-MBP:Serial-TUN clausklein$ timeout -s 1 5 ../.build-Serial-TUN-Debug/simpletap -d /dev/XXX -p
No such file or directory
Could not open tuntap interface!
Test mode, use VTUN_PIPE first end!
/dev/XXX: No such file or directory
Could not open serial port!
Test mode, use VTUN_PIPE second end!
Starting threads
Ping
Pong
signal_handler() called with sig(1)
signal_handler() called with sig(3)
Claus-MBP:Serial-TUN clausklein$
***/
