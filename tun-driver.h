#pragma once
#include <net/if.h>

enum tun_mode_t
{
    MODE_TAP = 0,
    MODE_TUN = 1,
};

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Create a new TUN adapter
     * @param dev       The new adapter's path
     * @param istun     The new adapter's mode
     * @return The file descriptor to communicate with the device
     */
    int tun_open_common(char dev[IFNAMSIZ], enum tun_mode_t istun);

#ifdef __cplusplus
}
#endif
