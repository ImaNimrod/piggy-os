#!/bin/sh

if [ "$EUID" -ne 0 ]; then
    echo "please run this script with sudo"
    exit 1
fi

ip tuntap add dev tap0 mode tap user ${SUDO_USER:-$USER}
ip link set tap0 up

ip addr add 192.168.100.1/24 dev tap0

sysctl -w net.ipv4.ip_forward=1

iptables -t nat -A POSTROUTING -o wlan0 -j MASQUERADE
iptables -A FORWARD -i tap0 -j ACCEPT
iptables -A FORWARD -o tap0 -m state --state RELATED,ESTABLISHED -j ACCEPT
