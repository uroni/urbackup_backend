#!/bin/sh

cbuild=""

switch_m_build()
{
	cp defaults_$cbuild defaults
	cp init.d_$cbuild init.d
}

switch_build()
{
	cp Makefile.am_$cbuild Makefile.am
	cp configure.ac_$cbuild Makefile.am
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

if [ "x$1" == "xserver" ]
	cbuild="server"
then
	
elif [ "x$1" =="xclient" ]
	cbuild="client"
else
	echo "No valid build enviroment. Available: client, server"
	exit 0
fi

switch
echo "Switched to $cbuild"