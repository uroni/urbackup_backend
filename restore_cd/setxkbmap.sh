#!/bin/bash

FIFOFN=/home/urbackup/setxkbmap

if [ -e $FIFOFN ]
then
	exit 1
fi

mkfifo $FIFOFN
chmod 777 $FIFOFN

while true
do
	if read LAYOUT < $FIFOFN
	then
		echo "New keyboard layout: $LAYOUT"
		setxkbmap "$LAYOUT"
	fi
done
