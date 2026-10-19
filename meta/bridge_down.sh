#!/bin/sh

if [ "$EUID" -ne 0 ]; then
    echo "please run this script with sudo"
    exit 1
fi

iptables -t nat -D POSTROUTING -o wlan0 -j MASQUERADE
iptables -D FORWARD -i tap0 -j ACCEPT
iptables -D FORWARD -o tap0 -m state --state RELATED,ESTABLISHED -j ACCEPT

sysctl -w net.ipv4.ip_forward=0

ip addr del 192.168.100.1/24 dev tap0

ip link set tap0 down
ip tuntap del dev tap0 mode tap
