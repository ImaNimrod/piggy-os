#!/bin/sh

sudo iptables -t nat -D POSTROUTING -o wlan0 -j MASQUERADE
sudo iptables -D FORWARD -i tap0 -j ACCEPT
sudo iptables -D FORWARD -o tap0 -m state --state RELATED,ESTABLISHED -j ACCEPT

sudo ip addr del 192.168.100.1/24 dev tap0

sudo ip link set tap0 down
sudo ip tuntap del dev tap0 mode tap
