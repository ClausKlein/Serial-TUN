#include "tun-driver.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

#ifdef __linux__
#    include <linux/if_tun.h>
#endif

const uint16_t FRAME_LEN_MASK(0x7fff);
const uint16_t FRAME_LENGTH(0x8000);

struct CommDevices
{
    int tapFileDescriptor;
    int serialFd;
};

char adapterName[IFNAMSIZ] = {};
char serialDevice[_POSIX_PATH_MAX] = {};

static void *serialToTap(void *ptr);
static void *tapToSerial(void *ptr);

/* Functions to read/write frames. */
int frame_write(int fd, char *buf, size_t len)
{
    char *ptr;
    int wlen;

    // TODO: prevent this hack with writev()! CK
    ptr = buf - sizeof(uint16_t);

    *((uint16_t *)ptr) = htons(len);
    len = (len & FRAME_LEN_MASK) + sizeof(uint16_t);

    while (true) {
        if ((wlen = write(fd, ptr, len)) < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            if (errno == ENOBUFS) {
                return 0;
            }
        }

        /* Even if we wrote only part of the frame we can't use second write
         * since it will produce another frame */
        return wlen;
    }
}

int frame_read(int fd, char *buf, size_t len)
{
    uint16_t hdr;
    uint16_t flen;
    struct iovec iv[2];
    int rlen;

    /* Read frame */
    iv[0].iov_len = sizeof(uint16_t);
    iv[0].iov_base = (char *)&hdr;
    iv[1].iov_len = len;
    iv[1].iov_base = buf;

    while (true) {
        if ((rlen = readv(fd, iv, 2)) < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            return rlen;
        }
        hdr = ntohs(hdr);
        flen = hdr & FRAME_LEN_MASK;

        if (rlen < 2 || (rlen - 2) != flen) {
            return -1;
        }

        return hdr;
    }
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

    while (true) {
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

    return ptr;
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

    while (true) {
        // NOTE: we need space for fram length! CK
        count = read(tapFd, inBuffer + sizeof(uint16_t),
                     sizeof(inBuffer) - sizeof(uint16_t));
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

    return ptr;
}

int main(int argc, char *argv[])
{
    // Grab parameters
    int param;
    while ((param = getopt(argc, argv, "i:d:")) > 0) {
        switch (param) {
        case 'i':
            strncpy(adapterName, optarg, IFNAMSIZ - 1);
            break;
        case 'd':
            strncpy(serialDevice, optarg, sizeof(serialDevice) - 1);
            break;
        default:
            fprintf(stderr, "Unknown parameter %c\n", param);
            break;
        }
    }

    if (adapterName[0] == '\0') {
        fprintf(stderr, "Adapter name required (-i)\n");
        return EXIT_FAILURE;
    }
    if (serialDevice[0] == '\0') {
        fprintf(stderr, "Serial port required (-p)\n");
        return EXIT_FAILURE;
    }

#ifdef __linux__
    int tapFd = tun_alloc(adapterName, IFF_TAP | IFF_NO_PI);
#else
    int tapFd = tun_alloc(adapterName, 0);
#endif

    if (tapFd < 0) {
        fprintf(stderr, "Could not open /dev/net/tun\n");
        return EXIT_FAILURE;
    }

    int serialFd = open(serialDevice, O_RDWR);
    if (serialFd < 0) {
        perror("Could not open serial port!");
        close(tapFd);
        return EXIT_FAILURE;
    }

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

    pthread_join(tap2serial, NULL);
    printf("Thread tap-to-network returned %d\n", ret1);
    pthread_join(serial2tap, NULL);
    printf("Thread network-to-tap returned %d\n", ret2);

    close(tapFd);
    close(serialFd);
    return EXIT_SUCCESS;
}
