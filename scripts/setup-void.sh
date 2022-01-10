#!/bin/sh
#
# Set-up voidlinux for developing
#
echo y | xbps-install -Syu base-devel

echo y | xbps-install -y libblkid libfdisk libmount libsmartcols util-linux

# Install additional dependancies
pkgs=$(find /hostrepo/src -name void-requirements.txt | xargs sed -e 's!#.*$!!')
set -x
echo y | xbps-install -y $pkgs
