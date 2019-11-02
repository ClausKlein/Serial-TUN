#!/bin/sh

TAP_NAME=tap0
IPV4_TESTNET=172.29.250.192/28
IPV4_SUBNET=172.29.250.200/29
IPV4_VGW=172.29.250.201/28
IPV4_ETH=172.29.250.202/28
IPV4_RED=172.29.250.203/29
IPV4_BLK=172.29.250.204/29

set -e
set -u
set -x

# cleanup tap interface if exitst
ip addr delete ${IPV4_BLK} dev ${TAP_NAME} || echo OK
ip link delete ${TAP_NAME} || echo OK

# setup tap interface
ip tuntap add mode tap name ${TAP_NAME}
ip addr add ${IPV4_BLK} dev ${TAP_NAME}
ip link set dev ${TAP_NAME} up

# add default gateway for routing to RIO
ip route flush default
ip route add default via ${IPV4_RED} dev ${TAP_NAME}
#TBD ip route add ${IPV4_SUBNET} dev ${TAP_NAME}

ip link show ${TAP_NAME}
ip addr show dev ${TAP_NAME}
ip route list

#TODO Start in background
${1} -i ${TAP_NAME} -d /dev/spidip2.0 -v # verbose

