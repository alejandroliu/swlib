#!/bin/sh
#
# Set-up voidlinux for developing
#
## UPDATE: fix xbps install and URL pointer
echo repository=https://repo-default.voidlinux.org/current/musl > /etc/xbps.d/10-repository-main.conf
echo y | env XBPS_ARCH=x86_64-musl xbps-install -y -A -S -u xbps
rm -f /etc/xbps.d/10-repository-main.conf || :

# For some reason we can't upgrade base-files normally in a docker build
echo y | xbps-remove -F base-files
echo y | env XBPS_ARCH=x86_64-musl xbps-install -y -A -S base-files
echo y | env XBPS_ARCH=x86_64-musl xbps-install -y -A -S -u
echo y | env XBPS_ARCH=x86_64-musl xbps-install -y -A bash
echo y | env XBPS_ARCH=x86_64-musl xbps-install -y -A \
    base-voidstrap acl-progs dialog netcat p7zip patch pwgen pv \
    rsync wget unzip zip iputils dcron void-repo-nonfree \
    screen tmux

svdir=/etc/runit/runsvdir/default
for sv in crond uuidd statd rpcbind dbus udevd elogind sshd
do
  [ -e /etc/sv/\$sv ] || continue
  rm -f \$svdir/\$sv
  ln -s /etc/sv/\$sv \$svdir/\$sv
done

rm -fv /etc/runit/core-services/*-filesystems.sh
rm -fv /etc/runit/runsvdir/default/agetty-*

#######################################################################

echo y | xbps-install -yu base-devel
echo y | xbps-install -y libblkid libfdisk libmount libsmartcols util-linux

# Install additional dependancies
pkgs=$(find /hostrepo/src -name void-requirements.txt | xargs sed -e 's!#.*$!!')
set -x
echo y | xbps-install -y $pkgs
