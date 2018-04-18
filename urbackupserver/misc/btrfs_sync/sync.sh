#!/bin/bash
# Copyright (c) 2018 Martin Raiber 
#
# Permission is hereby granted, free of charge, to any person obtaining a 
# copy of this software and associated documentation files (the 
# "Software"), to deal in the Software without restriction, including 
# without limitation the rights to use, copy, modify, merge, publish, 
# distribute, sublicense, and/or sell copies of the Software, and to 
# permit persons to whom the Software is furnished to do so, subject to 
# the following conditions: 
#
# The above copyright notice and this permission notice shall be included 
# in all copies or substantial portions of the Software. 
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS 
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY 
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. 

set -eu -o pipefail

usage () {
	echo "Script to sync a UrBackup btrfs storage to a a separate btrfs storage"
	echo
	echo "USAGE:"
	echo "./sync.sh /source/dir /dest/dir"
	echo
}

if [ "$#" -ne 2 ]; then
    echo "Illegal number of parameters"
	usage
	exit 1
fi

SOURCE=$1
DEST=$2

for client in $(ls $SOURCE)
do		
	mkdir -p $DEST/$client
	test -e "$DEST/$client/busy.file" || touch "$DEST/$client/busy.file"
	flock -n -E 0 "$DEST/$client/busy.file" bash sync_client.sh "$client" "$SOURCE" "$DEST"
done
