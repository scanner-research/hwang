from setuptools import setup
import shutil
import os
from sys import platform

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))

if platform == 'linux' or platform == 'linux2':
    EXT = '.so'
else:
    EXT = '.dylib'

SO_PATH = os.path.abspath('{p:s}/../build/libhwang{e:s}'.format(p=SCRIPT_DIR,
                                                                e=EXT))
DEST_PATH = os.path.abspath('{p:s}/hwang/libhwang.so'.format(p=SCRIPT_DIR))
shutil.copyfile(SO_PATH, DEST_PATH)

setup(
    name='hwang',
    version='0.0.2',
    url='https://github.com/scanner-research/hwang',
    author='Alex Poms',
    author_email='apoms@cs.cmu.edu',
    packages=['hwang'],
    include_package_data=True,
    package_data={
        'hwang': [
            './*.so',
        ]
    },
    zip_safe=False,
    license='Apache 2.0'
)
