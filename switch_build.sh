#!/bin/bash

cbuild=""

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
	cd ../urbackup
	switch_build
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
echo "Switched to $cbuild"
