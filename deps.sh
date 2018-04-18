#!/bin/bash

cores=$(nproc)

LOCAL_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR=$LOCAL_DIR/thirdparty/build
DEFAULT_INSTALL_DIR=$LOCAL_DIR/thirdparty/install
FILES_DIR=$LOCAL_DIR/thirdparty/resources

POSITIONAL=()

# Ask if installed
INSTALL_FFMPEG=true
INSTALL_PROTOBUF=true

# Assume not installed
INSTALL_PYBIND=true

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
    --with-ffmpeg)
        WITH_FFMPEG="$2"
        shift # past arg
        shift # past value
        ;;
    --with-protobuf)
        WITH_PROTOBUF="$2"
        shift # past arg
        shift # past value
        ;;
    *)    # unknown option
        POSITIONAL+=("$1") # save it in an array for later
    shift # past argument
    ;;
esac
done

echo "--------------------------------------------------------------"
echo "|            Hwang Dependency Installation Script            |"
echo "--------------------------------------------------------------"
echo "The script will ask if required dependencies are installed and"
echo "then install missing dependencies to "
echo "$INSTALL_PREFIX"
echo "(customized by specifying (--prefix <dir>)"

set -- "${POSITIONAL[@]}" # restore positional parameters

FFMPEG_DIR=$INSTALL_PREFIX
PROTOBUF_DIR=$INSTALL_PREFIX
PYBIND_DIR=$INSTALL_PREFIX

export C_INCLUDE_PATH=$INSTALL_PREFIX/include:$C_INCLUDE_PATH
export LD_LIBRARY_PATH=$INSTALL_PREFIX/lib:$LD_LIBRARY_PATH
export PATH=$INSTALL_PREFIX/bin:$PATH
export PKG_CONFIG_PATH=$INSTALL_PREFIX/lib/pkgconfig:$PGK_CONFIG_PATH

mkdir -p $BUILD_DIR
mkdir -p $INSTALL_PREFIX

if [[ ! -z ${WITH_FFMPEG+x} ]]; then
    INSTALL_FFMPEG=false
    FFMPEG_DIR=$WITH_FFMPEG
fi
if [[ ! -z ${WITH_PROTOBUF+x} ]]; then
    INSTALL_PROTOBUF=false
    PROTOBUF_DIR=$WITH_PROTOBUF
fi

if [[ $INSTALL_ALL == false ]]; then
    # Ask about each library
    if [[ -z ${WITH_FFMPEG+x} ]]; then
        echo -n "Do you have ffmpeg>=3.3.1 installed? [y/N]: "
        read yn
        if [[ $yn == y ]] || [[ $yn == Y ]]; then
            INSTALL_FFMPEG=false
            echo -n "Where is your ffmpeg install? [/usr/local]: "
            read install_location
            if [[ $install_location == "" ]]; then
                FFMPEG_DIR=/usr/local
            else
                FFMPEG_DIR=$install_location
            fi
        else
            INSTALL_FFMPEG=true
        fi
    fi
    if [[ -z ${WITH_PROTOBUF+x} ]]; then
        echo -n "Do you have protobuf>=3.40 installed? [y/N]: "
        read yn
        if [[ $yn == y ]] || [[ $yn == Y ]]; then
            INSTALL_PROTOBUF=false
            echo -n "Where is your protobuf install? [/usr/local]: "
            read install_location
            if [[ $install_location == "" ]]; then
                PROTOBUF_DIR=/usr/local
            else
                PROTOBUF_DIR=$install_location
            fi
        else
            INSTALL_PROTOBUF=true
        fi
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

if [[ $INSTALL_PROTOBUF == true ]] && [[ ! -f $BUILD_DIR/protobuf.done ]] ; then
    # protobuf 3.4.1
    echo "Installing protobuf 3.4.1..."
    cd $BUILD_DIR
    rm -fr protobuf
    git clone -b v3.4.1 https://github.com/google/protobuf.git && \
        cd protobuf && bash ./autogen.sh && \
        ./configure --prefix=$INSTALL_PREFIX && make -j$cores && \
        make install && touch $BUILD_DIR/protobuf.done \
            || { echo 'Installing protobuf failed!' ; exit 1; }
    echo "Done installing protobuf 3.4.1"
fi

if [[ $INSTALL_PYBIND == true ]] && [[ ! -f $BUILD_DIR/pybind.done ]] ; then
    echo "Installing pybind..."
    rm -fr pybind11
    git clone -b v2.2.2 https://github.com/pybind/pybind11 && \
        cd pybind11 && \
        mkdir build && cd build && \
        cmake .. -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX -DPYBIND11_TEST=Off && \
        make install -j${cores} && cd ../../ && \
        touch $BUILD_DIR/pybind.done \
            || { echo 'Installing pybind failed!' ; exit 1; }
    echo "Done installing pybind"
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
echo "FFMPEG_DIR=$FFMPEG_DIR" >> $DEP_FILE
echo "PROTOBUF_DIR=$PROTOBUF_DIR" >> $DEP_FILE

echo "Done installing required dependencies!"
echo "Add $INSTALL_PREFIX/lib to your LD_LIBRARY_PATH so the installed "
echo "dependencies can be found!"
echo "e.g. export LD_LIBRARY_PATH=$INSTALL_PREFIX/lib:\$LD_LIBRARY_PATH"
