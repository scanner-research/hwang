#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

POSITIONAL=()

INSTALL_FFMPEG=true
INSTALL_BOOST=true

INSTALL_ALL=false

while [[ $# -gt 0 ]]
do
key="$1"

case $key in
    -a|--install-all)
    INSTALL_ALL=true
    shift # past arg
    ;;
    *)    # unknown option
    POSITIONAL+=("$1") # save it in an array for later
    shift # past argument
    ;;
esac
done

set -- "${POSITIONAL[@]}" # restore positional parameters

mkdir -p $DIR/thirdparty

if [[ $INSTALL_ALL == false ]]; then
    # Ask about each library
    while true; do
        echo -n "Do you have ffmpeg>=3.3.1 installed? [y/N]: "
        read yn
        if [[ $yn == y ]] || [[ $yn == Y ]]; then
            INSTALL_FFMPEG=false
            break
        else 
            INSTALL_FFMPEG=true
            break
        fi
    done
    while true; do
        echo -n "Do you have boost>=1.63 installed? [y/N]: "
        read yn
        if [[ $yn == y ]] || [[ $yn == Y ]]; then
            INSTALL_BOOST=false
            break
        else
            INSTALL_BOOST=true
            break
        fi
    done
fi

if [[ $INSTALL_FFMPEG == true ]]; then
    echo "Installing ffmpeg 3.3.1..."
    # FFMPEG
    cd $DIR/thirdparty
    rm -fr ffmpeg
    git clone -b n3.3.1 https://git.ffmpeg.org/ffmpeg.git && cd ffmpeg && \
    ./configure --prefix=$DIR/thirdparty/install --extra-version=0ubuntu0.16.04.1 \
                --toolchain=hardened --cc=cc --cxx=g++ --enable-gpl \
                --enable-shared --disable-stripping \
                --disable-decoder=libschroedinger \
                --enable-avresample --enable-libx264 --enable-nonfree && \
    make install -j${cores}
    echo "Done installing ffmpeg 3.3.1"
fi

if [[ $INSTALL_BOOST == true ]]; then
    echo "Installing boost 1.63.0..."
    cd $DIR/thirdparty
    rm -fr boost*
    wget "https://dl.bintray.com/boostorg/release/1.63.0/source/boost_1_63_0.tar.gz" && \
        tar -xf boost_1_63_0.tar.gz && cd boost_1_63_0 && ./bootstrap.sh && \
        ./b2 install --prefix=$DIR/thirdparty/install && \
        rm -rf $DIR/thirdparty/boost_1_63_0.tar.gz
    echo "Done installing boost 1.63.0"
fi

echo "Installing googletest..."
cd $DIR/thirdparty
rm -fr googletest
git clone https://github.com/google/googletest && \
    cd googletest && mkdir build && cd build && \
    cmake .. -DCMAKE_INSTALL_PREFIX=$DIR/thirdparty/install && \
    make -j${cores} && make install
echo "Done installing googletest"

echo "Done installing required dependencies!"
