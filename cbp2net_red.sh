#!/bin/sh

TAP_NAME=tap1
IPV4_TESTNET=172.29.250.192/28
IPV4_SUBNET=172.29.250.200/29
IPV4_VGW=172.29.250.201/28
IPV4_ETH=172.29.250.202/28
IPV4_RED=172.29.250.203/29
IPV4_BLK=172.29.250.204/29

set -e
set -u
set -x

# Optional Schalte Loops im FPGA:
#TODO /tmp/mmio /dev/qspi_bypass w 0x100400 1

# cleanup tap interface if exitst
ip addr delete ${IPV4_RED} dev ${TAP_NAME} || echo OK
ip link delete ${TAP_NAME} || echo OK

# setup eth0 ip addr
ip addr delete ${IPV4_ETH} dev eth0
ip addr add ${IPV4_ETH} dev eth0

# setup tap interface
ip tuntap add mode tap name ${TAP_NAME}
ip addr add ${IPV4_RED} dev ${TAP_NAME}
ip link set dev ${TAP_NAME} up

# add default gateway for routing to PC
ip route flush default
ip route add default via ${IPV4_VGW} dev eth0
#TBD ip route add ${IPV4_SUBNET} dev eth0

# enable routing
echo 1 > /proc/sys/net/ipv4/ip_forward

ip link show ${TAP_NAME}
ip addr show dev ${TAP_NAME}
ip route list

#TODO Start in background
${1} -i ${TAP_NAME} -d /dev/qspi_bypass -v # verbose

