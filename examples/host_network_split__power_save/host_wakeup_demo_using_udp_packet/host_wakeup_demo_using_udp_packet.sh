#!/bin/bash

# Simple script to build and run host_wakeup_demo_using_udp_packet.c

# Check if IP address is provided
if [ $# -eq 0 ]; then
    echo "❌ Error: IP address is required!"
    echo "Usage: $0 <ip_address> [port]"
    echo "Example: $0 192.168.1.100 60000"
    exit 1
fi

IP_ADDRESS="$1"
DEFAULT_PORT="123"

# Check if port is provided as second argument
if [ $# -ge 2 ]; then
    PORT="$2"
else
    PORT="$DEFAULT_PORT"
    echo "No port specified, using default port: $DEFAULT_PORT"
fi

echo "===== Network Split Host Wakeup with UDP Packet Tool ====="
echo "Target: $IP_ADDRESS:$PORT"
echo "Building and running network wakeup utility..."

# Compile the program
gcc -o host_wakeup_demo_using_udp_packet host_wakeup_demo_using_udp_packet.c -pthread

# Check if compilation succeeded
if [ $? -ne 0 ]; then
    echo "❌ Compilation failed!"
    exit 1
fi

echo "✅ Build successful!"
echo "Starting network wakeup utility..."
echo "-------------------------------------------"

# Run the program with IP address and port
./host_wakeup_demo_using_udp_packet $IP_ADDRESS $PORT

# Capture result
RESULT=$?
if [ $RESULT -eq 0 ]; then
    echo "-------------------------------------------"
    echo "✅ Network wakeup packet sent successfully to $IP_ADDRESS:$PORT!"
else
    echo "-------------------------------------------"
    echo "❌ Network wakeup failed with code: $RESULT"
fi

exit $RESULT

