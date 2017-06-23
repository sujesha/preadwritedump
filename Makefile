obj-m       := preadwritedump.o
preadwritedump-objs    := kernel_signal_usr.o harddisk-info.o pdd_scandisk.o relayframe.o prwd_events.o prwd_iodisk.o pcollectfuncs.o main_preadwritedump.o 

KDIR    := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

BLKSIZE := 4096
MINCHUNKSIZE := 512
MAXCHUNKSIZE := 65536
#HASHALGO := MD5
#CHUNKINGALGO := Rabin
HOSTNAME := $(shell hostname)
NOHASHMAGIC := 1
DISKNAME := $(shell /home/sujesha/provided-eval/datadumping/preadwritedump/get_diskname.sh)

# Note about relay channel buffers ----------------------------------------
# The allocated space is from the kernel vmalloc space and the total
# vmalloc space available in system by default is 128MB on 32-bit machines
# and 1GB on 64-bit machines.
# Allocating too much can cause kernel to run out of vmalloc space, hence 
# resulting in segmentation fault in the pdatadump kernel module. Hence, 
# while # setting the values below, please ensure that the values are not so
# large as to cause this out-of-space problem because although it is
# theoretically possible to increase the vmalloc space to 190MB, etc,
# it is only possible via a kernel boot, which we would like to avoid.
# Currently, scanning uses "relay_reserve", so even small buffers will 
# do fine because buffer availability will govern the scanning rate.
# But I/O speed is out of our control, hence allocate as big a buffer as
# possible for tracing I/O, since we want to avoid drops. However, if drops
# do occur, we just note the number of times it happened.

# size of relay channel subbuffer --- in terms of MB
# value of 0 implies that default size of 512KB should be used for scanning
# and 1 MB for I/O tracing.
PARM_SUBBUF_SIZE_SCAN := 0
PARM_SUBBUF_SIZE_IO := 0

# number of relay channel subbuffers
# value of 0 implies that default number of 4 should be used for scanning
# and 48/98 MB for 32-bit/64-bit I/O tracing.
PARM_N_SUBBUFS_SCAN := 0
PARM_N_SUBBUFS_IO := 0

SUBBUF_CONF_FLAGS += -DPARM_SUBBUF_SIZE_SCAN="$(PARM_SUBBUF_SIZE_SCAN)" -DPARM_N_SUBBUFS_SCAN="$(PARM_N_SUBBUFS_IO)" -DPARM_SUBBUF_SIZE_IO="$(PARM_SUBBUF_SIZE_IO)" -DPARM_N_SUBBUFS_IO="$(PARM_N_SUBBUFS_IO)"

#Only one of the following should be one
RHELFLAG := 0
UBUNTUFLAG := 1

#Only one of the following should be one!!!!! not checked anywhere, so
#very important to get it right here.
NOHASHTAB := 1 
YESHASHTAB := 0

# Use different log levels in dmesg command as required.
#define KERN_EMERG    "<0>"  /* system is unusable               */
#define KERN_ALERT    "<1>"  /* action must be taken immediately */
#define KERN_CRIT     "<2>"  /* critical conditions              */
#define KERN_ERR      "<3>"  /* error conditions                 */
#define KERN_WARNING  "<4>"  /* warning conditions               */
#define KERN_NOTICE   "<5>"  /* normal but significant condition */
#define KERN_INFO     "<6>"  /* informational                    */
#define KERN_DEBUG    "<7>"  /* debug-level messages             */
#EXTRA_CFLAGS += -DDEBUG_SS

EXTRA_CFLAGS += -Werror

EXTRA_CFLAGS    += -DBLKSIZE=$(BLKSIZE) -DMINCHUNKSIZE=$(MINCHUNKSIZE) -DMAXCHUNKSIZE=$(MAXCHUNKSIZE) -DHOSTNAME="$(HOSTNAME)" -DNOHASHMAGIC=$(NOHASHMAGIC) -DDISKNAME="$(DISKNAME)" -DRHELFLAG="$(RHELFLAG)" -DUBUNTUFLAG="$(UBUNTUFLAG)" -DCONFIG_LBDAF $(SUBBUF_CONF_FLAGS) -DNOHASHTAB=$(NOHASHTAB) -DYESHASHTAB=$(YESHASHTAB) -DPREADWRITEDUMP

ifeq ($(IOONLY), 1)
EXTRA_CFLAGS += -DIOONLY=$(IOONLY)
endif
ifeq ($(CAPTURE_KVM_ONLY), 1)
EXTRA_CFLAGS += -DCAPTURE_KVM_ONLY=$(CAPTURE_KVM_ONLY)
endif

#-DHASHALGO=$($HASHALGO) -DCHUNKINGALGO=$(CHUNKINGALGO)

#flags from psiphon's Makefile
CC  = gcc
ALL_CFLAGS = -Wall -O2 -g -W $(SUBBUF_CONF_FLAGS) -D_GNU_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -DFLUSHDEBUG_SS -DHASHTAB_SIZE_KB=$(HASHTAB_SIZE_KB) -DHASHTAB_SIZE_MB=$(HASHTAB_SIZE_MB) #-DDEBUG_SS
LIBS    = -lpthread

ifeq ($(IOONLY), 1)
ALL_CFLAGS += -DIOONLY
endif
ifeq ($(CAPTURE_KVM_ONLY), 1)
ALL_CFLAGS += -DCAPTURE_KVM_ONLY=$(CAPTURE_KVM_ONLY)
endif

default:
	cd psiphon && rm -rf *.o psiphon && $(CC) $(ALL_CFLAGS) -I../ -o psiphon endianness.c recv_kernel_sig.c main_psiphon.c $(LIBS)
	/bin/rm -rf *.*o .tmp_versions Module.symvers .*.cmd preadwritedump.mod.c modules.order *.d cscope.out Module.markers
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	/bin/rm -rf *.*o .tmp_versions Module.symvers .*.cmd preadwritedump.mod.c modules.order *.d tags cscope.out Module.markers

kvalloc:
	cd kvmalloc_limits && make
