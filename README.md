## experiment

experimental uSockets implementation

##### Disabling the kernel speace TCP stack for a certain port
You will need to disable RST outputting on ports you listen to. Disable RST on port 4000 like so:

`sudo iptables -A OUTPUT -p tcp --sport 4000 --tcp-flags RST RST -j DROP`
