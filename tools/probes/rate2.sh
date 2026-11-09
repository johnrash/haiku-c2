# rate2.sh up <laptop> <port> <send buffer KB> <seconds> | down <seconds>
W=/dev/net/realtekwifi/0
b() { ifconfig $W | grep -a "$1" | sed 's/.*errors, //; s/ bytes.*//'; }
if [ "$1" = up ]; then
  a=$(b Transmit); timeout $5 /boot/home/tcpblast send $2 $3 100000 64 $4 > /dev/null 2>&1; c=$(b Transmit); s=$5
else
  a=$(b Receive); sleep $2; c=$(b Receive); s=$2
fi
echo "$(( ($c-$a)/1000/$s )) kB/s"
