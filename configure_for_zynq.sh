#!/bin/bash

#stop on error
set -e

source cross-env.sh 

if [ ! -d "/usr/lib/arm-linux-gnueabihf" ] || [ ! -d "/lib/arm-linux-gnueabihf" ]; then
  echo "You must create a couple of softlinks to your system:"
  echo "sudo ln -s /usr/arm-linux-gnueabihf/lib /lib/arm-linux-gnueabihf"
  echo "sudo ln -s /usr/arm-linux-gnueabihf/lib /usr/lib/arm-linux-gnueabihf"
  exit 3 
fi


echo "Configuring with:"
echo "--prefix=$PREFIX"
echo "--with-sysroot=$SYSROOT"
echo "--host=$COMPILER_NAME_PREFIX"

#read -p "Continue? (y/N): " -n 1
#case $(echo $REPLY | tr '[A-Z]' '[a-z]') in
#  y|yes) echo "yes" ;;
#  *)     exit 0;;
#esac

automake --add-missing

#verbose
set -x
sudo ./fix_broken_symlinks.sh $SYSROOT
./configure --host=$COMPILER_NAME_PREFIX --with-sysroot=$SYSROOT --prefix=$PREFIX --with-gfxdrivers=unema --enable-fusiondale --disable-fusionsound --enable-sawman --enable-multi --enable-network --enable-one --enable-debug-support="no"

echo "Configure done successfully. Now you may run ./build_for_zynq.sh"

