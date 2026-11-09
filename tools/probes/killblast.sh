# killblast.sh [keep]: ends the board's tcpblast programs, except those with
# "keep" on their command line, and prints how many are left.
keep=${1:-no such thing}
list() { ps | grep -a "tcpblast" | grep -a -v "grep\|bash\|killblast\|$keep"; }
for t in $(list | awk '{print $(NF-3)}'); do kill -9 $t; done
sleep 1
list | wc -l
