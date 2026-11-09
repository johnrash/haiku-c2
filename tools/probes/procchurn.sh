# procchurn.sh <seconds>: starts programs as fast as it can, no network traffic,
# and prints how many, to see if memory follows the number of teams.
end=$(( $(date +%s) + $1 )); n=0
while [ $(date +%s) -lt $end ]; do
	/bin/true
	n=$((n+1))
done
echo $((n*2))
