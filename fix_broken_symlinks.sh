#!/bin/bash

#set -x

if [ -z "$1" ]; then
  echo "Usage: ./fix_broken_symlinks path/to/sysroot"
  exit 1
fi

SYSROOT=$1

if [ ! -d "$SYSROOT" ]; then
  echo "$SYSROOT: does not exist. Exiting..."
  exit 2 
fi

SYSROOT_DIR=$SYSROOT/usr/lib/arm-linux-gnueabihf

find $SYSROOT_DIR -type l | while read f; do 
  if [ ! -e "$f" ] && [ -e $SYSROOT/$(readlink -n "$f") ]; then
    ln -vbs "$SYSROOT/$(readlink -n "$f")" "$f" 
  fi; 
done

SYSROOT_DIR=$SYSROOT/lib/arm-linux-gnueabihf

find $SYSROOT_DIR -type l | while read f; do 
  if [ ! -e "$f" ] && [ -e $SYSROOT/$(readlink -n "$f") ]; then
    ln -vbs "$SYSROOT/$(readlink -n "$f")" "$f" 
  fi; 
done


