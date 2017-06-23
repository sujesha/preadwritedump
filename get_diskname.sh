#!/bin/sh

sudo /sbin/fdisk -l | head -n 2 | tail -n 1 | awk '{print $2}' | awk -F ":" '{print $1}'
