#!/bin/sh

set -e
set -u
set -x

if [ "${USER}x" = "rootx" ] ; then
  # Schalte Loop im FPGA:
  #TODO /tmp/mmio /dev/qspi_bypass w 0x100400 1

  # cleanup tap0 interface if exitst
  ip addr delete 192.168.111.1/24 dev tap0 || echo OK
  ip link delete tap0 || echo OK

  # setup tap0 interface
  ip tuntap add mode tap name tap0 user klein_cl ### $USER

  ip addr add 192.168.111.1/24 dev tap0

  #TODO ip link set dev tap0 up
  echo 1 > /proc/sys/net/ipv4/ip_forward
fi

ip link show tap0
ip addr show dev tap0

#TODO Start in background
${1} -i tap0 -h || echo "OK"
${1} -i XXX -p || echo "ERROR IGNORED"
${1} -d /dev/tty1 || echo "ERROR IGNORED"
echo "PIPE_TEST" | ${1} -d /dev/XXX -i XXX -p || echo "ERROR IGNORED"
${1} -i tap0 -d /dev/qspi_bypass -v || echo "ERROR IGNORED"


