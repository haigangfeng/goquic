#!/bin/sh -e

LIBQUIC_REVISION=679afdae95012ed2ba87d03229da8a4c59a33231
ARCH_TYPE=$(uname -m)
OS_TYPE=$(uname -s)

if [ $ARCH_TYPE = "x86_64" ]; then
    GOARCH="amd64"
elif [ $ARCH_TYPE = "x86" ]; then
    GOARCH="386"
elif [ $ARCH_TYPE = "amd64" ]; then       # freeBSD?
    GOARCH="amd64"
else
    echo "Unknown architecture"
    exit 1
fi

if [ $OS_TYPE = "Linux" ]; then
    GOOS="linux"
elif [ $OS_TYPE = "Darwin" ]; then
    GOOS="darwin"
elif [ $OS_TYPE = "FreeBSD" ]; then
    GOOS="freebsd"
else
    echo "Unknown OS"
    exit 1
fi

if [ "$GOQUIC_BUILD" = "Release" ]; then
    OPT="-DCMAKE_BUILD_TYPE=Release"
else
    OPT=""
fi

echo "GOARCH: $GOARCH"
echo "GOOS: $GOOS"
echo "OPTION: $OPT"

if [ ! -d libquic ]; then
    git clone https://github.com/devsisters/libquic.git
fi

cd libquic
git checkout $LIBQUIC_REVISION
rm -fr build
mkdir -p build
cd build

cmake -GNinja $OPT ..
ninja

cd ../..

TARGET_DIR=lib/${GOOS}_${GOARCH}/
mkdir -p $TARGET_DIR
cp libquic/build/boringssl/crypto/libcrypto.a libquic/build/boringssl/ssl/libssl.a libquic/build/libquic.a libquic/build/protobuf/libprotobuf.a $TARGET_DIR

rm -fr build libgoquic.a

if [ $GOOS = "freebsd" ]; then
    gmake -j
else
    make -j
fi
mv libgoquic.a $TARGET_DIR

echo $TARGET_DIR updated
