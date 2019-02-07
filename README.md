# tun-example

Example program to create a TUN device (virtual IP network interface), configure address/netmask for it, then configure the routing table.

## Usage

Build:

	make

Build with optimisations:

	make O=2

View help:

	./bin/iplink --help

Example:

	# Build
	make

	# Run on one endpoint (change uart arg as needed)
	./bin/iplink --uart=/dev/ttyS0 --addr=10.0.0.1/24

	# Run on other endpoint (change uart arg as needed)
	./bin/iplink --uart=/dev/ttyS0 --addr=10.0.0.2/24

	# The two ends can now communicate with IP, using their respective addresses.
	# You could use some simple network program e.g. netcat to show this.
