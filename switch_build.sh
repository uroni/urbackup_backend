#!/bin/bash

cbuild=""
CPWD=`pwd`

switch_m_build()
{
	if test -e defaults_$cbuild
	then
		cp defaults_$cbuild defaults
		cp init.d_$cbuild init.d
	fi
}

switch_build()
{
	if test -e Makefile.am_$cbuild
	then
		pwd
		cp Makefile.am_$cbuild Makefile.am
		cp configure.ac_$cbuild configure.ac
	fi
}

switch()
{
	switch_build
	switch_m_build
	cd fsimageplugin
	switch_build
	cd ../cryptoplugin
	switch_build
}

clean_build()
{
	if test -e "$CPWD/curr_build"
	then
		c=`cat $CPWD/curr_build`
		echo "Last build: $c"
		if [[ "x$c" == "xserver" ]] && [[ "x$cbuild" == "xclient" ]]
		then
			make clean
		fi
		if [[ "x$c" == "xclient" ]] && [[ "x$cbuild" == "xserver" ]]
		then
			make clean
		fi
	fi
	echo "$cbuild" > $CPWD/curr_build
}

if [[ "x$1" == "xserver" ]]
then
	cbuild="server"	
elif [[ "x$1" == "xclient" ]]
then
	cbuild="client"
elif [[ "x$1" == "xbdbplugin" ]]
then
	cbuild="bdbplugin"
else
	echo "No valid build enviroment. Available: client, server"
	exit 0
fi

switch
clean_build
echo "Switched to $cbuild"
