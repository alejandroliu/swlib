#!/bin/sh
#
# Setup alpine build environmnet
#
apk update
apk add build-base strace sudo bash
pkgs=$(find /hostrepo/src -name alpine-requirements.txt | xargs sed -e 's!#.*$!!')
apk add $pkgs
