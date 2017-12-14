# Hwang

> Whenever Hwang goes to sleep, he jumps forward in time. This is a problem.
> This is not a problem that is going to solve itself.
> - from [Hwang's Billion Brilliant Daugthers](http://www.lightspeedmagazine.com/fiction/hwangs-billion-brilliant-daughters)

Hwang is a library for performing fast decode of frames from h.264
encoded video (most mp4s). Hwang provides both a Python and C++ API. Hwang
decodes on the CPU (using ffmpeg) or on the GPU (using the NVIDIA hardware
decoder).

## Setup

Hwang only supports python2 for now, so please make sure to be using a python2
environment. The following has been tested on Ubuntu 16.04.

## Linux Pre-Requisites
Install system wide packages
```
[sudo] apt-get install git cmake libgoogle-glog-dev libgflags-dev python2.7-dev python-pip yasm libx264-dev build-essential wget
```

Then install python packages
```
pip install numpy
```

## Install

If you want to make Hwang available as a local python package and C++ library,
you must build Hwang from source. First install the dependencies by running:
```bash
git clone https://github.com/scanner-research/hwang.git
cd hwang
bash deps.sh
```
This script will ask a few questions and install all the required dependencies
to a local directory.

Then, build and install the Hwang package:
```bash
mkdir build
cd build
cmake ..
make -j
cd ../python
python setup.py bdist_wheel
pip install dist/hwang-0.1.0-py2-none-any.whl
```
