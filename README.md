# swlib

![release](https://github.com/TortugaLabs/swlib/actions/workflows/release.yaml/badge.svg)
![static-checks](https://github.com/TortugaLabs/swlib/actions/workflows/static-checks.yaml/badge.svg)

Software Library collection


1. release
2. kicks build - upload artifacts
3. download artifacts : gh release upload
  https://cli.github.com/manual/gh_release_upload


# checks

- technical debt
- php -l
- python -m py_compile scrypt.py
- sh -n script.sh

# todo

- check if there are pre-releases and nuke them when a release is made.


# notes

Add: https://gitlab.archlinux.org/archlinux/packaging/packages/xpad/-/blob/main/PKGBUILD?ref_type=heads
./autogen.sh
./configure --prefix=/usr/local
make
make DESTDIR=$fakeroot install

# Save all required libraries
for j in $(ldd xpad | awk '{print $3}') ; do cp "$j" . ; done
LD_LIBRARY_PATH=.... xpad



