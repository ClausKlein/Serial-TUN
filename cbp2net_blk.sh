#!/bin/sh

TAP_NAME=tap0
IPV4_TESTNET=172.29.250.192/28
IPV4_NET=172.29.250.192/30
IPV4_RED=172.29.250.193/30
IPV4_BLK=172.29.250.194/30
IPV4_BRC=172.29.250.195/30

set -e
set -u
set -x

# Optional Schalte Loop im FPGA:
#FIXME /tmp/mmio /dev/spidip2.0  w 0x100400 1

# cleanup tap interface if exitst
ip addr delete ${IPV4_BLK} dev ${TAP_NAME} || echo OK
ip link delete ${TAP_NAME} || echo OK

# setup tap interface
ip tuntap add mode tap name ${TAP_NAME}
ip addr add ${IPV4_BLK} dev ${TAP_NAME}
ip link set dev ${TAP_NAME} up

# add default gateway for routing
#FIXME ip route add default via ${IPV4_RED} dev ${TAP_NAME}
ip route add ${IPV4_TESTNET} dev ${TAP_NAME}

ip link show ${TAP_NAME}
ip addr show dev ${TAP_NAME}
ip route list

#TODO Start in background
${1} -i ${TAP_NAME} -d /dev/spidip2.0 -v # verbose

