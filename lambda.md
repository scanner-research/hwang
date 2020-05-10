# Deploy Hwang to AWS Lambda

This tutorial shows how to deploy Hwang to AWS Lambda.

The only thing Lambda accepts is a zip file consisting of all code and dependencies. There's no `apt-get` or `make` directly available on Lambda. So the hardest part of getting something to work on Lambda is to enumerate all the dependencies and make sure they work on Lambda's environment. The general idea of this tutorial is to simulate Lambda's working environment, build Hwang and all its dependencies to a specific path on that environment and package them to a zip file so they are available at runtime.

## Simulate Lambda environment

There are two possible ways that we can simulate the Lambda execution environment:

1. Create an AWS EC2 instance with Amazon Linux AMI installed; OR
2. Build a local Docker image of Amazon Linux AMI

Note that we should choose the same version as [Lambda Execution Environment](https://docs.aws.amazon.com/lambda/latest/dg/current-supported-versions.html).

## Install system-wide dependencies

We need some system-wide dependencies to build Hwang. But they are not necessary anymore after Hwang is built. So we can just install them system-wide and don't need to care about the path they are installed to.

`yum install glibc gcc gcc-c++ autoconf automake libtool pkgconfig unzip yasm`

Apart from that, the `yum` version of cmake is 2.8.12.2 but we need cmake 3.2.0. So we remove the old version and compile the new one from source.

```bash
yum remove cmake
wget https://cmake.org/files/v3.2/cmake-3.2.0.tar.gz
tar -xvzf cmake-3.2.0.tar.gz
cd cmake-3.2.0
./bootstrap
make
make install
```
We also need to build nasm system-wide from source for the same reason.

```bash
cd /opt
wget http://www.nasm.us/pub/nasm/releasebuilds/2.13.01/nasm-2.13.01.tar.xz
tar -xf nasm-2.13.01.tar.xz
cd nasm-2.13.01
./configure --prefix=/usr && make && make install
```

## Enumerate dependencies of Hwang

Hwang has a number of dependencies. Except for FFmpeg, all other dependencies can be installed via `deps.sh` on Amazon Linux the same way we build Hwang on Ubuntu. FFmpeg is different because it has a lot of dependencies itself. Most of them can be easily installed on Ubuntu by running a one-line command like `apt-get install libgoogle-glog-dev libgflags-dev yasm libx264-dev`, but it doesn't work on Amazon Linux because most of them are not included in Amazon Linux's `yum`. So let's build them from source code.

## Build FFmpeg and its dependencies

We need FFmpeg and all its dependencies installed to a specific path so we don't mix them up with system-wide libraries when packing them up later. Here we suppose that specific path is `$HOME/FFmpeg_build` and the following scripts are executed as root.

1. Build libx264 to `$HOME/FFmpeg_build` because it's not included in Amazon Linux's `yum`

   Note that we must choose `--enable-shared` because we can't import static libraries to python and Hwang needs shared libraries as dependencies.

   ```bash
   cd /opt
   git clone git://git.videolan.org/x264.git
   cd x264
   ./configure --prefix="$HOME/FFmpeg_build" --bindir="$HOME/bin" --enable-shared && make install
   ```

2. We can optionally build other libraries such as `xvidcore`, `libogg`, `libvorbis`, `libtheora`, `fdk-aac`, `lame`, `libvpx`, etc. as add-ons of FFmpeg. They may or may not work if we `--enable-shared` for all of them. But Hwang doesn't seem to require any of them.

3. Let FFmpeg know about the libraries we just built

   ```bash
   export LD_LIBRARY_PATH=/usr/local/lib/:$HOME/FFmpeg_build/lib/
   echo /usr/local/lib >> /etc/ld.so.conf.d/custom-libs.conf
   echo $HOME/FFmpeg_build/lib/ >> /etc/ld.so.conf.d/custom-libs.conf
   ldconfig
   PKG_CONFIG_PATH="$HOME/FFmpeg_build/lib/pkgconfig"
   export PKG_CONFIG_PATH
   ```

4. Build FFmpeg to `$HOME/FFmpeg_build`

   ```bash
   cd /opt
   git clone git://source.FFmpeg.org/FFmpeg.git
   cd FFmpeg
   git checkout n3.3.1
   ```

   From my experience, we need to hack the source code a little bit to make it work on Amazon Linux.

   First, add `-fPIC` to `CFLAGS` so shared libraries can be built on top of static libraries. Change line 121 of `Makefile` from `$$(OBJS-$(1)): CFLAGS  += $(CFLAGS-$(1))` to `$$(OBJS-$(1)): CFLAGS  += -fPIC $(CFLAGS-$(1))`.

   Second, `libx264.c` seems to reference a variable that does not exist. Here's a simple hack: change line 282 from `    if (x264_bit_depth > 8)` to `    if (X264_BIT_DEPTH > 8)`; change line 892 from `    if (x264_bit_depth == 8)` to `    if (X264_BIT_DEPTH == 8)`; change line 894 from `    if (x264_bit_depth == 9)` to `    if (X264_BIT_DEPTH == 9)`; change line 896 from`    if (x264_bit_depth == 10)` to `    if (X264_BIT_DEPTH == 10)`.

   Then build it!

   ```bash
   ./configure --prefix=$HOME/FFmpeg_build \
               --extra-cflags="-I$HOME/FFmpeg_build/include" \
               --extra-ldflags="-L$HOME/FFmpeg_build/lib" --bindir="$HOME/bin" \
               --extra-libs=-ldl --strip=STRIP \
               --toolchain=hardened --cc=cc --cxx=g++ --enable-gpl \
               --enable-shared --disable-decoder=libschroedinger \
               --enable-avresample --enable-libx264 --enable-nonfree
    make install
   ```

Now, we should have FFmpeg in `$HOME/FFmpeg_build`. We will let Hwang know about this in the next step. Later we also need to export all shared libraries to a separate folder to let AWS Lambda know.

## Build Hwang

Because of [this issue of pip](https://github.com/pypa/pip/issues/4464), we have to first `unset PYTHON_INSTALL_LAYOUT`.

Before building Hwang, it's a good idea to set up virtualenv as follows. This helps us identify our newly-added python packages simply by looking at `env/lib/python3.6/dist-packages` instead of packing all systemwide python libraries up as well.

```bash
virtualenv -p python3 env
source env/bin/activate
pip3 install numpy setuptools
```

Then run the Hwang's dependencies installation script:

```bash
git clone https://github.com/scanner-research/hwang.git
cd hwang
git checkout 8e89cc8
bash deps.sh
```

Now that we already have FFmpeg installed in `$HOME/FFmpeg_build`, let's tell the `deps.sh` about that and it will install other stuff such as protobuf and googletest.

We also need glog as one of Hwang's dependencies. On Ubuntu, that's simply `apt-get install libgoogle-glog-dev`, but here we have to build it from source because it's not included in Amazon Linux's `yum`. Let's install glog directly to Hwang's thirdparty folder:

```bash
git clone https://github.com/google/glog.git
cd glog
git checkout 367518f
./autogen.sh
./configure --prefix=/home/ec2-user/hwang/thirdparty/install
make && make install
```

For some reason, some libraries are installed to `hwang/thirdparty/install/lib64` instead of `hwang/thirdparty/install/lib`. Those libraries may not be recognized by Hwang. A simple workaround is to copy all everything in `lib64` back to `lib`.

Now we should be able to build Hwang.

```bash
mkdir build
cd build
cmake ..  # if this fails, run the same command again and it might work
make -j
cd ..
bash build.sh
```

## Let AWS Lambda know

Now Hwang should be installed in `env/lib/python3.6/dist-packages`. Since Lambda only accepts a zip file consisting of all code and dependencies, we need to put everything in one place. Let's export all these python libraries as well as `.so` shared libraries to a separate folder and name it `bundle`.

```bash
cp -r env/lib/python3.6/dist-packages/* ~/bundle/
cp -r env/lib/python3.6/site-packages/* ~/bundle/
cp -r env/lib64/python3.6/dist-packages/* ~/bundle/
cp -r env/lib64/python3.6/site-packages/* ~/bundle/
```

To let Lambda know about our `.so` libraries, we will have a folder served as `LD_LIBRARY_PATH` consisting of all these libraries. Let's create a subdirectory inside `bundle` and name it `lib`.

```bash
mkdir ~/bundle/lib
cp -r hwang/thirdparty/install/lib/* ~/bundle/lib/
cp -r hwang/thirdparty/install/lib64/* ~/bundle/lib/
cp -r $HOME/FFmpeg_build/lib/* ~/bundle/lib/
```

## Sample AWS Lambda function

To test all our work above, let's write a sample Lambda function `main.py` as follows:

```python
import os
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
LIB_DIR = os.path.join(SCRIPT_DIR, 'lib')

def lambda_handler(event=None, context=None):
    # bundle/lib/ serves as LD_LIBRARY_PATH so we have .so libraries in decode.py
    command = 'LD_LIBRARY_PATH={} python3 decode.py'.format(LIB_DIR)
    try:
        output = subprocess.check_output(command, shell=True)
        print(output)
    except subprocess.CalledProcessError as e:
        print(e.output)

    return "main.py returned."
```

We can see from the above code that `main.py` calls another file `decode.py` with all our libraries in `lib/` set as `LD_LIBRARY_PATH`. That's how we integrate our `.so` libraries to python programs on AWS Lambda. 

The `decode.py` is as follows:

```python
from hwang import decoder
import boto3
import numpy as np
import urllib.request

s3 = boto3.resource('s3')

# Load an arbitrary video to decode
local_filename, _ = urllib.request.urlretrieve('https://www.sample-videos.com/video/mp4/720/big_buck_bunny_720p_1mb.mp4')

d = decoder.Decoder(local_filename)
result = d.retrieve((0,100))  # decode 0 to 100 frames
partial_sum = np.sum(result)
partial_sum.dump("/tmp/part1.dat")

# Write the 100 frames as a dumped numpy array to S3
s3.Object('bucket-name', 'part1.dat').put(Body=open('/tmp/part1.dat', 'rb'))
print("Finished")
```

## Zip and deploy

Having done all these, we are ready to zip all our files, upload it to S3 and deploy it!

Before calling Amazon's API, make sure you have [access keys for AWS account](https://docs.aws.amazon.com/general/latest/gr/managing-aws-access-keys.html) set up by running `aws configure`.

```bash
zip --symlink -r bundle.zip . && aws s3 cp bundle.zip s3://bucket-name
aws lambda update-function-code --function-name awesome-lambda-function --s3-bucket bucket-name --s3-key bundle.zip --publish
```

## References

[alexandrusavin](https://gist.github.com/alexandrusavin) / [install-FFmpeg-amazon-linux.sh](https://gist.github.com/alexandrusavin/2d2a91fcc35faf1c9f828b350e13c3bd)

[Hassle-Free Python Lambda Deployment](https://joarleymoraes.com/hassle-free-python-lambda-deployment/)

[Using moviepy, scipy and numpy in amazon lambda](https://stackoverflow.com/questions/34749806/using-moviepy-scipy-and-numpy-in-amazon-lambda)
