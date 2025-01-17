#include "tun-driver.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifdef __linux__
#    include <linux/if_tun.h>

int tun_open_common(
    char *dev, enum tun_mode_t mode) // NOLINT(readability-non-const-parameter)
{
    if ((mode != VTUN_ETHER) && (mode != VTUN_P2P)) {
        return -1;
    }

    // Interface request structure
    struct ifreq ifr = {};

    // File descriptor
    int fileDescriptor;

    // Open the tun device, if it doesn't exists return the error
    const char *cloneDevice = "/dev/net/tun";
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    if ((fileDescriptor = open(cloneDevice, O_RDWR | O_CLOEXEC)) < 0) {
        perror("open /dev/net/tun");
        return fileDescriptor;
    }

    // Initialize the ifreq structure with 0s and the set flags
    memset(&ifr, 0, sizeof(ifr));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    ifr.ifr_flags = (mode ? IFF_TUN : IFF_TAP) | IFF_NO_PI;

    // If a device name is passed we should add it to the ifreq struct
    // Otherwise the kernel will try to allocate the next available
    // device of the given type
    if (*dev) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        strncpy(static_cast<char *>(ifr.ifr_name), dev, IF_NAMESIZE - 1);
    }

    // Ask the kernel to create the new device
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    int err = ioctl(fileDescriptor, TUNSETIFF, &ifr);
    if (err < 0) {
        // If something went wrong close the file and return
        perror("ioctl TUNSETIFF");
        close(fileDescriptor);
        return err;
    }

    // Write the device name back to the dev variable so the caller
    // can access it
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    strcpy(dev, static_cast<const char *>(ifr.ifr_name));

    // Return the file descriptor
    return fileDescriptor;
}

#else

int tun_open_common(
    char *dev, enum tun_mode_t mode) // NOLINT(readability-non-const-parameter)
{
    if ((mode != VTUN_ETHER) && (mode != VTUN_P2P)) {
        return -1;
    }

    char tunname[_POSIX_NAME_MAX];
    if (*dev) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        snprintf(static_cast<char *>(tunname), sizeof(tunname), "/dev/%s", dev);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        return open(static_cast<char *>(tunname), O_RDWR | O_CLOEXEC);
    }

    int err = 0;
    for (int i = 0; i < INT8_MAX; i++) {
        int fd;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        snprintf(static_cast<char *>(tunname), sizeof(tunname), "/dev/%s%d",
                 (mode ? "tun" : "tap"), i);
        /* Open device */
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        if ((fd = open(static_cast<char *>(tunname), O_RDWR | O_CLOEXEC)) > 0) {
            strcpy(dev, tunname + 5); // NOLINT: offset 5 is OK! CK
            return fd;
        }

        if (errno != ENOENT) {
            err = errno;
        } else if (i > 0) {
            break; /* don't try all devices */
        }
    }
    if (err) {
        errno = err;
    }

    return -1;
}

#endif
