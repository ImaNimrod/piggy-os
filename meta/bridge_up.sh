#!/bin/sh

sudo ip tuntap add dev tap0 mode tap user "$USER"
sudo ip link set tap0 up

sudo ip addr add 192.168.100.1/24 dev tap0

sudo iptables -t nat -A POSTROUTING -o wlan0 -j MASQUERADE
sudo iptables -A FORWARD -i tap0 -j ACCEPT
sudo iptables -A FORWARD -o tap0 -m state --state RELATED,ESTABLISHED -j ACCEPT
