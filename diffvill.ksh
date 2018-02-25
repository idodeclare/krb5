#!/bin/ksh

set -o pipefail
DIFFTOOL="${DIFFTOOL:-bcomp}"

# Define a function to show info about an early termination.
src=
tof=
integer i=0
function early_term {
	>&2 echo "Terminated at #$i: $src $tof"
	exit 2
}

# Check required args.
if [ $# -lt 2 ] || [ $# -gt 3 ]; then
	>&2 echo "Usage: $0 <illumos root> <ops file> [<skip #lines>]"
	exit 1
fi
target="$1"
opsf="$2"
integer skipnum=0
skipnum="$3"

# Check requirements to be in the krb5 root directory.
if [ ! -f README ] || [ ! -d src ]; then
	>&2 echo "ERROR: $0 should be run from krb5 root."
	exit 1
fi

# Set a trap for Ctrl-C or TERM.
trap early_term TERM INT

# Read the ops file, and run comparisons.
while read -r src tof
do
	i=$((1 + $i))
	# Skip if $tof is all-caps -- e.g. NOTFOUND or NO.
	[[ "$tof" =~ ^[A-Z]+$ ]] && continue

	fullsrc="./$src"
	fulltof="$target/$tof"
	# Continue silently if no difference exists.
	diff -q "$fullsrc" "$fulltof" >/dev/null 2>&1 && continue

	# Compare interactively unless skipping.
	[ "$i" -gt "$skipnum" ] && "$DIFFTOOL" "$fullsrc" "$fulltof"
	# Display the result of a diff.
	diff -q "$fullsrc" "$fulltof" >/dev/null || \
	    printf "Diff! #%4d: %s -> %s\n" "$i" "$src" "$tof"
done <"$opsf"
