#!/bin/bash
# test_kvalloc.sh [start_numbytes] [step_factor]

#Borrowed from http://kaiwantech.wordpress.com/2011/08/17/kmalloc-and-vmalloc-linux-kernel-memory-allocation-api-limits/ and tweaked slightly by Sujesha.

if [ $# -lt 1 ]; then
	echo "Usage: $(basename $0) {k|v} [start_numbytes] [step_factor]"
	echo " k : test the KMALLOC limit"
	echo " v : test the VMALLOC limit"
	exit 1
fi

[ $1 != "k" -a $1 != "v" ] && {
	echo "$0: invalid choice '$1' (should be either 'k' or 'v')"
	exit 1
}
[ $1 = "k" ] && KVALLOC_PROCFILE=/proc/driver/kmalloc_test
[ $1 = "v" ] && KVALLOC_PROCFILE=/proc/driver/vmalloc_test
[ -e $KVALLOC_PROCFILE ] || {
	echo "Error! procfs file does not exist. Driver loaded? Aborting..."
	exit 1
}
#sss echo "KVALLOC_PROCFILE = $KVALLOC_PROCFILE"

# defaults
numbytes=1024
step_factor=2

if [ $# -eq 2 ]; then
	numbytes=$1
	step_factor=$2
elif [ $# -eq 3 ]; then
	numbytes=$2
	step_factor=$3
fi
#sss echo "
#sss Running:"
#sss [ $1 = "k" ] && echo "KMALLOC TEST"
#sss [ $1 = "v" ] && echo "VMALLOC TEST"
#sss echo "$(basename $0) $numbytes $step_factor
#sss "

while [ true ]
do
	let kb=$numbytes/1024
	let mb=$numbytes/1048576
 	echo "Attempting to alloc $numbytes bytes ($kb KB, $mb MB)"
	echo $numbytes > $KVALLOC_PROCFILE || {
		let kb_fail=$numbytes/1024
		let mb_fail=$numbytes/1048576
		echo "FAILURE! AT $numbytes bytes = $kb_fail KB = $mb_fail MB. Aborting..."
		let kb_success=$kb_fail/2
		echo $kb_success > /tmp/hashtab_size.txt
	   	echo $kb_success | awk '{print $1/1024}' >> /tmp/hashtab_size.txt
		echo "==> SUCCESS! AT $kb_success KB. Written to /tmp/hashtab_size.txt"
		exit 1
	}
	let numbytes=numbytes*step_factor
done
exit 0

