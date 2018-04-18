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

client=$1
SOURCE=$2
DEST=$3

for backup in $(ls $DEST/$client)
do
		if echo $backup | grep .partial > /dev/null
		then
				echo "partial: $backup"
				backup_name=$(echo $backup | cut -d "." -f 1)
				echo "Deleting partial snapshot $DEST/$client/$backup_name"
				btrfs subvol del $DEST/$client/$backup_name || true
				rm $DEST/$client/$backup
		fi
done

echo "Syncing $client..."

for backup in $(ls $SOURCE/$client | sort)
do
		if [[ "$backup" == current ]] || [[ "$backup" == "busy.file" ]]; then
				continue
		fi

		if echo "$backup" | grep "\.failed" > /dev/null; then
			continue
		fi

		if echo "$backup" | grep "\.partial" > /dev/null; then
			continue
		fi
		
		if [ -e "$DEST/$client/$backup" ]
		then
			continue
		fi

		if [ -e "$DEST/$client/$backup.failed" ]
		then
			continue
		fi
		
		if ! btrfs property get -ts "$SOURCE/$client/$backup" ro | grep "ro=true" > /dev/null
		then
			echo "Not read-only: $SOURCE/$client/$backup"
			continue
		fi
		
		IMAGE=""
		if echo "$backup" | grep Image > /dev/null
		then
			IMAGE=$(echo "$backup" | grep -Eo "Image_.*")
		fi

		last_backup=""
		if [ -e $DEST/$client ]
		then
				for pbackup in $(ls $DEST/$client | sort)
				do
					if [[ "$pbackup" == "busy.file" ]]; then
                        continue
					fi
										
					if [ "x$IMAGE" = x ] && ! echo $pbackup | grep Image > /dev/null || [ "x$IMAGE" != x ] && echo "$pbackup" | grep "$IMAGE" > /dev/null
					then
						if test -e $SOURCE/$client/$pbackup; then
								last_backup=$pbackup
						fi
					fi
				done
		fi

		touch $DEST/$client/$backup.partial
		SUCCESS=true
		if [[ $last_backup == "" ]]; then
				echo "Sending backup $SOURCE/$client/$backup to $DEST/$client/$backup"
				btrfs send $SOURCE/$client/$backup | pv | btrfs receive $DEST/$client/ || SUCCESS=false
		else
				echo "Sending backup $SOURCE/$client/$backup to $DEST/$client/$backup using parent $client/$last_backup"
				btrfs send -p $SOURCE/$client/$last_backup $SOURCE/$client/$backup | pv | btrfs receive -v $DEST/$client/ || SUCCESS=false
		fi

		if [[ $SUCCESS = true ]]
		then
				btrfs fi sync $DEST/$client/$backup
				echo "Backup sent successfully."
				rm $DEST/$client/$backup.partial
		else
				touch $DEST/$client/$backup.failed
				echo "Sending backup failed"
				btrfs subvol del $DEST/$client/$backup || true
				rm $DEST/$client/$backup.partial
		fi
done


