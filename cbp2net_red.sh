#!/bin/sh

TAP_NAME=tap1
IPV4_TESTNET=172.29.250.192/28
IPV4_NET=172.29.250.192/30
IPV4_RED=172.29.250.193/30
IPV4_BLK=172.29.250.194/30
IPV4_BRC=172.29.250.195/30

set -e
set -u
set -x

# Optional Schalte Loop im FPGA:
#TODO /tmp/mmio /dev/qspi_bypass w 0x100400 1

# cleanup tap interface if exitst
ip addr delete ${IPV4_RED} dev ${TAP_NAME} || echo OK
ip link delete ${TAP_NAME} || echo OK

# setup tap interface
ip tuntap add mode tap name ${TAP_NAME}
ip addr add ${IPV4_RED} dev ${TAP_NAME}
ip link set dev ${TAP_NAME} up

# enable routing
echo 1 > /proc/sys/net/ipv4/ip_forward

ip link show ${TAP_NAME}
ip addr show dev ${TAP_NAME}
ip route list

#TODO Start in background
${1} -i ${TAP_NAME} -d /dev/qspi_bypass -v # verbose

