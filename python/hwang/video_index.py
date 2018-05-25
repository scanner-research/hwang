from hwang_python import *
import os


@staticmethod
def __VideoIndex_from_file(f):
    return VideoIndex.deserialize(f.read())


def __VideoIndex_to_file(self, f):
    f.write(self.serialize())


setattr(VideoIndex, 'from_file', __VideoIndex_from_file)
setattr(VideoIndex, 'to_file', __VideoIndex_to_file)
