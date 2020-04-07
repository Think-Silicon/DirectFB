# cross-env.sh

set -e

#edit this:
PREFIX=${PREFIX:=where_to_install}
#for example
#PREFIX=/usr

# ----------------------
# You must set the correct PREFIX path and comment out the following lines!
if [ -z ${PREFIX+x} ]; then
  echo "Please edit cross-env.sh and set the PREFIX path"
  exit 1
fi
# ----------------------
if [ ! -d "$PREFIX" ]; then
  echo "PREFIX Variable is set to $PREFIX"
  echo "$PREFIX does not exist"
  echo "Please edit cross-env.sh and set the PREFIX path"
  exit 1
fi

# This is where the cross compiler lives
COMPILER_NAME_PREFIX=${COMPILER_NAME_PREFIX:=arm-linux-gnueabihf}

export FOO=$COMPILER_NAME_PREFIX-gcc
if ! type "$FOO" > /dev/null; then
  echo "$FOO compiler is not installed in your system."
  echo "Please install with: \"apt-get install g++-arm-linux-gnueabihf\""
  exit 1
fi


# Sets its binaries to be CC, LD etc.
# (examples from an ARM cross build)
#export PATH=$PATH
CC=${CC:=$COMPILER_NAME_PREFIX-gcc}
CXX=${CXX:=$COMPILER_NAME_PREFIX-g++}
AR=${AR:=$COMPILER_NAME_PREFIX-ar}
RANLIB=${RANLIB:=$COMPILER_NAME_PREFIX-ranlib}
LD=${LD:=$COMPILER_NAME_PREFIX-ld}

echo CC is $CC

if ! type "$CC" > /dev/null; then
  echo "$FOO compiler is not installed in your system."
  echo "Please install with: \"apt-get install g++-arm-linux-gnueabihf\""
  exit 1
fi

#this is needed by libpng
#In libpng.pc there's the following line:
#Libs.private: -lz -lm
#but pkg-config 0.26 doesn't support Libs.private
#so we add -lz explicitly
export LIBS="-lz -lm"

# This is where the libraries of the target platform live
SYSROOT=${SYSROOT:=/tools/filesystems_sysroot/linaro_sysroot}

if [ ! -d "$SYSROOT" ]; then
  echo "SYSROOT variable in cross-env.sh is not set correctly"
  echo "$SYSROOT: does not exist. Exiting..."
  exit 2
fi


# We do NOT set PATH and LD_LIBRARY_PATH here because we
# can't run the binaries from SYSROOT on our build machine,
# but we want to link against libraries that are installed
# there. We also set pkg-config's default directory to the
# one in the SYSROOT, and clear the environment from any
# other local-machine architecture pkg-config path
# directories.
export PKG_CONFIG_SYSROOT_DIR=$SYSROOT
export PKG_CONFIG_LIBDIR=$SYSROOT/usr/lib/pkgconfig
export PKG_CONFIG_PATH="$SYSROOT/usr/lib/arm-linux-gnueabihf/pkgconfig"
export ACLOCAL_FLAGS="-I $SYSROOT/usr/share/aclocal"
export CPPFLAGS="-I$SYSROOT/usr/include --sysroot=$SYSROOT"
export LDFLAGS="--sysroot=$SYSROOT" # -L$SYSROOT/usr/lib -L$SYSROOT/lib/arm-linux-gnueabihf/ -L$SYSROOT/usr/lib/arm-linux-gnueabihf/"

# This is our local prefix where we will install all
# cross-built stuff first, in order to verify that all
# builds fine. We set it up as proper devel prefix because
# later built packages might need the libraries we have
# installed there earlier. We also don't want to pollute
# SYSROOT with our own stuff.
CROSS_PREFIX=$PREFIX

# We do NOT set PATH and LD_LIBRARY_PATH here because we can't
# run the binaries from CROSS_PREFIX on our build machine
export PKG_CONFIG_PATH=$CROSS_PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH
export ACLOCAL_FLAGS="-I $CROSS_PREFIX/share/aclocal $ACLOCAL_FLAGS"
export CPPFLAGS="-I$CROSS_PREFIX/include $CPPFLAGS"
export LDFLAGS="-L$CROSS_PREFIX/lib $LDFLAGS"

# set the terminal title and prompt so we always see where we are
echo -en "\033]0;$CROSS_PREFIX - cross ARM env\a"
export PS1="[cross ARM] $PS1"
