from setuptools import setup
import os

os.system('ln -s build/libhwang.so python')

setup(
    name='hwang',
    version='0.0.1',
    url='https://github.com/scanner-research/hwang',
    author='Alex Poms',
    author_email='apoms@cs.cmu.edu',

    packages=['hwang'],
    package_data={
        'hwang': [
            '*.so',
        ]
    },

    license='Apache 2.0'
)
