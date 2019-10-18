#include "tun-driver.h"

#include <fcntl.h>
#include <limits.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifdef __linux__
#    include <linux/if_tun.h>

int tun_alloc(char dev[IFNAMSIZ], short flags)
{
    // Interface request structure
    struct ifreq ifr;

    // File descriptor
    int fileDescriptor;

    // Open the tun device, if it doesn't exists return the error
    const char *cloneDevice = "/dev/net/tun";
    if ((fileDescriptor = open(cloneDevice, O_RDWR)) < 0) {
        perror("open /dev/net/tun");
        return fileDescriptor;
    }

    // Initialize the ifreq structure with 0s and the set flags
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = flags;

    // If a device name is passed we should add it to the ifreq struct
    // Otherwise the kernel will try to allocate the next available
    // device of the given type
    if (*dev) {
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    }

    // Ask the kernel to create the new device
    int err = ioctl(fileDescriptor, TUNSETIFF, (void *)&ifr);
    if (err < 0) {
        // If something went wrong close the file and return
        perror("ioctl TUNSETIFF");
        close(fileDescriptor);
        return err;
    }

    // Write the device name back to the dev variable so the caller
    // can access it
    strcpy(dev, ifr.ifr_name);

    // Return the file descriptor
    return fileDescriptor;
}

#else

int tun_alloc(char dev[IFNAMSIZ], short flags)
{
    char tunname[_POSIX_NAME_MAX];

    if (*dev) {
        snprintf(tunname, sizeof(tunname), "/dev/%s", dev);
        return open(tunname, O_RDWR);
    }

    for (int i = 0; i < INT8_MAX; i++) {
        int fd;
        snprintf(tunname, sizeof(tunname), "/dev/tun%d", i);
        /* Open device */
        if ((fd = open(tunname, O_RDWR)) > 0) {
            sprintf(dev, "tun%d", i);
            return fd;
        }
    }
    return -1;
}
#endif
