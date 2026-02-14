#!/bin/bash

# Exit if no proper arg received
if [ $# -lt 2 ]; then
    echo "Usage: $0 <drone ip> <ground ip> [port (default 5000)]"
    exit 1
fi

# Exit on error
set -e

# Namespace names
NS1="Drone"
NS2="GroundBase"

# Veth pair names
VETH1="veth1"
VETH2="veth2"

# IP addresses
IP1="$1/24"
IP2="$2/24"

# Port
PORT="${3:-5000}"

echo "Creating network namespaces..."
ip netns add $NS1
ip netns add $NS2

echo "Creating veth pair..."
ip link add $VETH1 type veth peer name $VETH2

echo "Assigning veth interfaces to namespaces..."
ip link set $VETH1 netns $NS1
ip link set $VETH2 netns $NS2

echo "Bringing up loopback interfaces..."
ip netns exec $NS1 ip link set lo up
ip netns exec $NS2 ip link set lo up

echo "Bringing up veth interfaces and assigning IPs..."
ip netns exec $NS1 ip addr add $IP1 dev $VETH1
ip netns exec $NS1 ip link set $VETH1 up

ip netns exec $NS2 ip addr add $IP2 dev $VETH2
ip netns exec $NS2 ip link set $VETH2 up

echo "Setup complete! Starting server and client..."

# Run server in NS1 in background
ip netns exec $NS1 bash -c "./build/drone_server $PORT"

# Give server a second to start
sleep 1

ip netns delete $NS1
ip netns delete $NS2

echo "Done!"

