#!/bin/sh
#
# Set-up voidlinux for developing
#
echo y | xbps-install -Syu xbps # make sure xbps is up-to-date first
echo y | xbps-install -Syu
echo y | xbps-install -yu base-devel

echo y | xbps-install -y libblkid libfdisk libmount libsmartcols util-linux

# Install additional dependancies
pkgs=$(find /hostrepo/src -name void-requirements.txt | xargs sed -e 's!#.*$!!')
set -x
echo y | xbps-install -y $pkgs
