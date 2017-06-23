#!/bin/sh

# uninstall if the system exists.
#CLIENT_PID=`ps aux | grep psiphon | grep -v grep | head -n1 | awk '{print $2}'`
#echo CLIENT_PID = $CLIENT_PID
#while [ "$CLIENT_PID" != "" ]
#do
#	kill -15 $CLIENT_PID
#	sleep 1
#	CLIENT_PID=`ps aux | grep psiphon | grep -v grep | head -n1 | awk '{print $2}'`
#done

LOADED=`/sbin/lsmod | /bin/grep preadwritedump | /usr/bin/wc -l`
if [ $LOADED != "0" ]
then
	sudo /sbin/rmmod preadwritedump
fi

