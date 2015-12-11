#!/bin/bash

cbuild=""
CPWD=`pwd`

copy_cond()
{
	R_CMP=`cmp $1 $2`
	if [ "xR_CMP" != "x0" ]
	then
		cp $1 $2
	fi
}

switch_m_build()
{
	if test -e defaults_$cbuild
	then
		copy_cond defaults_$cbuild defaults
		copy_cond init.d_$cbuild init.d
	fi
}

switch_build()
{
	if test -e Makefile.am_$cbuild
	then
		pwd
		copy_cond Makefile.am_$cbuild Makefile.am
		copy_cond configure.ac_$cbuild configure.ac
		if test -e BUILDID
		then
		    BUILDID=`cat BUILDID`
		    BUILDID=$((BUILDID +1))
		    sed -i "s/BUILDID/$BUILDID/g" configure.ac
		    echo $BUILDID > BUILDID
		else
		    sed -i "s/BUILDID/0/g" configure.ac
		fi
	fi
}

switch()
{
	switch_build
	switch_m_build
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
else
	echo "No valid build environment. Available: client, server"
	exit 0
fi

switch
clean_build
echo "Switched to $cbuild"
