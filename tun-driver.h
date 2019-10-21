#pragma once
#include <net/if.h>

enum tun_mode_t
{
    VTUN_ETHER = 0, // TAP mode
    VTUN_P2P = 1,   // TUN mode
    VTUN_PIPE = 2   // test loop only! CK
};

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Create a new TUN adapter
     * @param dev       The new adapter's path
     * @param mode      The new adapter's mode
     * @return The file descriptor to communicate with the device
     */
    int tun_open_common(char dev[IF_NAMESIZE], enum tun_mode_t mode);

#ifdef __cplusplus
}
#endif
