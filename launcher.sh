#!/bin/bash


# Change this to your netid
netid=axc142430

#
# Root directory of your project
PROJDIR=$HOME/aos

#
# This assumes your config file is named "config.txt"
# and is located in your project directory
#
CONFIG=$PROJDIR/config3.txt

#
# Directory your java classes are in
#
BINDIR=$PROJDIR

#
# Your main project class
#
PROG=dist_node

n=1

cat $CONFIG | sed -e "s/#.*//" | sed -e "/^\s*$/d" |
(
    read i
    echo $i
	while read line 
	    do
	    	 
		    host=$( echo $line | awk '{ print $2 }' )
            	    echo $host
		    ssh -l $netid $host "cd $BINDIR;./$PROG $n" &
		    n=$(( n + 1 ))
	    done
					       
)


