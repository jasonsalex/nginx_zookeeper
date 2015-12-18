#! /bin/sh

NGINX_VERSION=$1
PACKAGE_NAME=nginx-${NGINX_VERSION}

wget http://nginx.org/download/${PACKAGE_NAME}.tar.gz || exit 1
tar xvf ${PACKAGE_NAME}.tar.gz || exit 1

exit 0