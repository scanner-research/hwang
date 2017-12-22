#!/bin/bash

cores=$(nproc)

LOCAL_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR=$LOCAL_DIR/thirdparty/build
DEFAULT_INSTALL_DIR=$LOCAL_DIR/thirdparty/install
FILES_DIR=$LOCAL_DIR/thirdparty/resources

POSITIONAL=()

# Ask if installed
INSTALL_BOOST=true
INSTALL_FFMPEG=true

INSTALL_PREFIX=$DEFAULT_INSTALL_DIR

INSTALL_ALL=false

while [[ $# -gt 0 ]]
do
key="$1"

case $key in
    -c|--cores)
        cores="$2"
        shift # past arg
        shift # past value
        ;;
    -p|--prefix)
        INSTALL_PREFIX="$2"
        shift # past arg
        shift # past value
        ;;
    -a|--install-all)
        INSTALL_ALL=true
        shift # past arg
        ;;
    --with-boost)
        WITH_BOOST="$2"
        shift # past arg
        shift # past value
        ;;
    --with-ffmpeg)
        WITH_FFMPEG="$2"
        shift # past arg
        shift # past value
        ;;
    *)    # unknown option
        POSITIONAL+=("$1") # save it in an array for later
    shift # past argument
    ;;
esac
done

set -- "${POSITIONAL[@]}" # restore positional parameters

mkdir -p $BUILD_DIR
mkdir -p $INSTALL_PREFIX

if [[ $INSTALL_ALL == false ]]; then
    # Ask about each library
    if [[ -z ${WITH_FFMPEG+x} ]]; then
        echo -n "Do you have ffmpeg>=3.3.1 installed? [y/N]: "
        read yn
        if [[ $yn == y ]] || [[ $yn == Y ]]; then
            INSTALL_FFMPEG=false
            break
        else
            INSTALL_FFMPEG=true
            break
        fi
    else
        INSTALL_FFMPEG=false
        FFMPEG_DIR=$WITH_FFMPEG
    fi
    if [[ -z ${WITH_BOOST+x} ]]; then
        echo -n "Do you have boost>=1.63 installed? [y/N]: "
        read yn
        if [[ $yn == y ]] || [[ $yn == Y ]]; then
            INSTALL_BOOST=false
            break
        else
            INSTALL_BOOST=true
            break
        fi
    else
        INSTALL_BOOST=false
        BOOST_DIR=$WITH_BOOST
    fi
fi

if [[ $INSTALL_FFMPEG == true ]]; then
    echo "Installing ffmpeg 3.3.1..."
    # FFMPEG
    cd $BUILD_DIR
    rm -fr ffmpeg
    git clone -b n3.3.1 https://git.ffmpeg.org/ffmpeg.git && cd ffmpeg && \
    ./configure --prefix=$INSTALL_PREFIX --extra-version=0ubuntu0.16.04.1 \
                --toolchain=hardened --cc=cc --cxx=g++ --enable-gpl \
                --enable-shared --disable-stripping \
                --disable-decoder=libschroedinger \
                --enable-avresample --enable-libx264 --enable-nonfree && \
    make install -j${cores}
    echo "Done installing ffmpeg 3.3.1"
fi

if [[ $INSTALL_BOOST == true ]]; then
    echo "Installing boost 1.63.0..."
    cd $BUILD_DIR
    rm -fr boost*
    wget "https://dl.bintray.com/boostorg/release/1.63.0/source/boost_1_63_0.tar.gz" && \
        tar -xf boost_1_63_0.tar.gz && cd boost_1_63_0 && ./bootstrap.sh && \
        ./b2 install --prefix=$INSTALL_PREFIX && \
        rm -rf $BUILD_DIR/boost_1_63_0.tar.gz
    echo "Done installing boost 1.63.0"
fi

echo "Installing googletest..."
cd $BUILD_DIR
rm -fr googletest
git clone https://github.com/google/googletest && \
    cd googletest && mkdir build && cd build && \
    cmake .. -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX && \
    make -j${cores} && make install
echo "Done installing googletest"

DEP_FILE=$LOCAL_DIR/dependencies.txt
rm -f $DEP_FILE
echo "BOOST_ROOT=$BOOST_DIR" >> $DEP_FILE
echo "FFMPEG_DIR=$FFMPEG_DIR" >> $DEP_FILE

echo "Done installing required dependencies!"
echo "Add $INSTALL_PREFIX/lib to your LD_LIBRARY_PATH so the installed "
echo "dependencies can be found!"
echo "e.g. export LD_LIBRARY_PATH=$INSTALL_PREFIX/lib:\$LD_LIBRARY_PATH"
