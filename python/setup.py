from setuptools import setup
import os

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))

SO_PATH = os.path.abspath('{p:s}/../build/libhwang.so'.format(p=SCRIPT_DIR))
DEST_PATH = os.path.abspath('{p:s}/hwang/'.format(p=SCRIPT_DIR))
os.system('ln -s {from_path:s} {to_path:s}'.format(from_path=SO_PATH,
                                         to_path=DEST_PATH))

setup(
    name='hwang',
    version='0.0.1',
    url='https://github.com/scanner-research/hwang',
    author='Alex Poms',
    author_email='apoms@cs.cmu.edu',

    packages=['python/hwang'],
    package_data={
        'python': [
            '*.so',
        ]
    },

    license='Apache 2.0'
)
