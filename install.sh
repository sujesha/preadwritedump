#!/bin/sh

# Some default values of parameters
UseMount=1
UseLocal=0
PrintUsage=0
ScanOnly=0
IOOnly=1
CaptureKVMOnly=0
EXP_NAME=""

# Parse command line options
while getopts ":hsim:kl:" Option
do
  case $Option in
    m ) UseMount=1
        EXP_NAME=$OPTARG;;
    l ) UseLocal=1
        EXP_NAME=$OPTARG;;
    h ) PrintUsage=1;;
    s ) ScanOnly=1;;
	i ) IOOnly=1;;
	k ) CaptureKVMOnly=1;;
    * ) exit 1;;   # Default.
  esac
done

if [ $PrintUsage -eq 1 -o "$EXP_NAME" = "" ]
then
    echo "Usage: sudo sh install.sh [-m <exp-name>]"
    echo "       sudo sh install.sh [-l <exp-name>]"
    echo "       sudo sh install.sh [-h] for help/usage"
    echo "       sudo sh install.sh [-s] for scanonly"
    echo "       sudo sh install.sh [-i] for IOonly"
    echo "       sudo sh install.sh [-k] for CaptureKVMOnly"
    echo "       sudo sh install.sh"
    exit 0
fi

#Trouble related to “page allocation failure” related to networking. Is this a i
#kernel bug? http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=666021 suggests:-
sysctl vm.min_free_kbytes=65535

hostname=`hostname`
BASE_MOUNT_DIR=/root/nfsdirmount/psiphon-logs
if [ "$hostname" = "oldgen-PM4" -o "$hostname" = "kvmlucidX1" -o "$hostname" = "pro1amd64kvm" -o "$hostname" = "pro2amd64kvm" -o "$hostname" = "pro3amd64kvm2GB" -o "$hostname" = "pro4amd64kvm20GB" -o "$hostname" = "krishna-PM3" -o "$hostname" = "A2" -o "$hostname" = "A4remote" -o "$hostname" = "recycled" ]
then
	BASE_MOUNT_DIR=/root/nfsdirmount/psiphon-logs
elif [ "$hostname" = "intern-vm" -o "$hostname" = "intern-vm-2.irl.in.ibm.com" ]
then
	BASE_MOUNT_DIR=/var/netstore
	#BASE_MOUNT_DIR=/media/Sony_8GM
fi

if [ "$EXP_NAME" = "" ]
then
    EXP_NAME=$hostname
else
    EXP_NAME="$hostname-$EXP_NAME"
fi
echo "Experiment name: $EXP_NAME"

BASE_TRACE_DIR=provided/traces
# All traces for this experiment are store in this directory
TRACE_DIR=$BASE_MOUNT_DIR/$BASE_TRACE_DIR/$EXP_NAME/

KERNEL=`uname -r`

if echo $KERNEL | grep "el"
then
	rhel_flag=1
	ARCH=`uname -a | awk '{print $12}'`
else
	rhel_flag=0
fi

# uninstall if the system exists.
#CLIENT_PID=`ps aux | grep psiphon | grep -v grep | head -n1 | awk '{print $2}'`
#echo CLIENT_PID = $CLIENT_PID
#while [ "$CLIENT_PID" != "" ]
#do
#    kill -15 $CLIENT_PID
#    sleep 1
#    CLIENT_PID=`ps aux | grep psiphon | grep -v grep | head -n1 | awk '{print $2}'`
#done
#echo "Done killing psiphon"

echo "Trying to remove preadwritedump module..."
LOADED=`/sbin/lsmod | /bin/grep preadwritedump | /usr/bin/wc -l`
if [ $LOADED != "0" ]
then
#	rm -f /var/run/blk-tracing.pid
	sudo /sbin/rmmod preadwritedump || exit 1
#	/usr/sbin/update-rc.d -f blk-tracing remove
	echo "sleep 5 after uninstall"
	sleep 5
fi

# uninstall if the system exists.
CLIENT_PID=`ps aux | grep psiphon | grep -v grep | head -n1 | awk '{print $2}'`
echo CLIENT_PID = $CLIENT_PID
while [ "$CLIENT_PID" != "" ]
do
#    kill -15 $CLIENT_PID
	echo "$CLIENT_PID should already have been dead due to signaling!"
    sleep 1
    CLIENT_PID=`ps aux | grep psiphon | grep -v grep | head -n1 | awk '{print $2}'`
done

# First install the packages required.
if [ $rhel_flag -eq 0 ]
then
	if [ ! -d /lib/modules/$KERNEL/build -a ! -d /lib/modules/$KERNEL/source ]
	then
		sudo apt-get install linux-headers-$KERNEL || exit 1
	fi
else
    LOADED=`rpm -qa | /bin/grep kernel-headers | /usr/bin/wc -l`
    if [ $LOADED = "0" ]; then
        echo "Installing kernel-headers"
        sudo yum install kernel-headers || exit 1
    fi
    LOADED=`rpm -qa | /bin/grep kernel-devel | /usr/bin/wc -l`
    if [ $LOADED = "0" ]; then
        echo "Installing kernel-devel"
        sudo yum install kernel-devel || exit 1
    fi
fi

#if [ $rhel_flag -eq 0 ]
#then
#	sudo cp get_diskname.sh /lib/modules/$KERNEL/build/
#	sudo chmod a+x /lib/modules/$KERNEL/build/get_diskname.sh
#else
#	sudo cp get_diskname.sh /usr/src/kernels/$KERNEL-$ARCH/
#	sudo chmod a+x /usr/src/kernels/$KERNEL-$ARCH/get_diskname.sh
#fi


if ! mount | grep debugfs > /dev/null
then
    if ! cat /etc/fstab | grep debugfs > /dev/null
    then
        echo "mounting debugfs"
        sudo echo "nodev   /sys/kernel/debug       debugfs defaults        0       0" >> /etc/fstab
        sudo mount -a
    fi
fi

# Compile kvmalloc_limits module
#sudo make kvalloc || exit 1
#sudo /sbin/insmod kvmalloc_limits/kvalloc.ko
#echo "Since older kernels can kmalloc only 128KB in one shot, while newer kernels can manage upto 4MB, we need to know the limits for current kernel. Hence, using kernel module kvalloc.ko for checking kmalloc() limits."
#sudo bash ./kvmalloc_limits/test_kvalloc.sh k 2> /dev/null
#sudo /sbin/rmmod kvalloc
#echo "sleep 5 after checking kmalloc() limits"
#sleep 5

# Then, compile module
if [ $IOOnly -eq 1 -a $CaptureKVMOnly -eq 1 ]
then
	make IOONLY=1 CAPTURE_KVM_ONLY=1 || exit 1
elif [ $CaptureKVMOnly -eq 1 ]
then
	make CAPTURE_KVM_ONLY=1 || exit 1
elif [ $IOOnly -eq 1 ]
then 
	make IOONLY=1 || exit 1
else
	make || exit 1
fi

# Copy module to /lib/modules/
sudo mkdir -p /lib/modules/$KERNEL/misc/
sudo cp preadwritedump.ko /lib/modules/$KERNEL/misc/
sudo /sbin/depmod

# Include the script in init
#/usr/sbin/update-rc.d blk-tracing defaults 98 02

# Finally, start collection
#/etc/init.d/blk-tracing start

# Prepare directories for trace collection
if [ $UseMount -eq 1 ] || [ $UseLocal -eq 1 ]
then
# Check if base dir for trace collection exists
    if [ ! -d "$BASE_MOUNT_DIR" ]
    then
        echo "Creating $BASE_MOUNT_DIR.."
        sudo mkdir -p $BASE_MOUNT_DIR
    fi
fi
 
if [ $UseMount -eq 1 ]
then
    # Check if some volume is mounted at BASE_MOUNT_DIR
    mount_dir=`mount | grep $BASE_MOUNT_DIR | awk '{print $3}'`
    if [ -z "$mount_dir" ] || [ "$BASE_MOUNT_DIR" != "$mount_dir" ]
    then
        echo "Mounting $BASE_MOUNT_DIR.."
#        sudo mount -t nfs 9.126.108.241:/data $BASE_MOUNT_DIR
        sudo mount -a
        ret=$?
        if [ $ret -ne 0 ]
        then
            echo "Mount failed. Sleep, unload module and abort!"
            sleep 5
            sudo /sbin/rmmod preadwritedump
            exit $ret
        fi
    fi
fi

if [ $IOOnly -eq 1 -a $ScanOnly -eq 1 ]
then
	echo "Only one among IOOnly and ScanOnly may be 1 at a time."
	exit
fi

if [ $IOOnly -eq 0 ]
then
	InputOpts="-i pscanevents"
else
	InputOpts=""
fi

if [ $ScanOnly -eq 0 ]
then
    InputOpts=`echo "$InputOpts -i pioevents"`
fi

TRACE_DEV=`./get_diskname.sh`
echo "TRACE_DEV = $TRACE_DEV"
if [ $UseMount -eq 1 ] || [ $UseLocal -eq 1 ]
then
    # Output to a file
    # Check if the trace collection directory exists
    if [ ! -d "$TRACE_DIR" ]
    then
        echo "Creating $TRACE_DIR.."
        sudo mkdir -p $TRACE_DIR
    fi
    echo "Traces will be stored in $TRACE_DIR"
    InputOpts=`echo "$InputOpts -c -o f -D $TRACE_DIR"`
else
    # Output to STDOUT
    InputOpts=`echo "$InputOpts -o -"`
fi

# Insert the kernel module
echo "sleep 5 before installing module preadwritedump"
sleep 5
sudo /sbin/insmod preadwritedump.ko || exit $?


echo "psiphon Input options: $InputOpts"
#echo "sleep 2 before starting psiphon"
#sleep 2

# Start the collection agent
./psiphon/psiphon -p preadwritedump $InputOpts -t $TRACE_DEV &
