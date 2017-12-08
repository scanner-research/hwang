from .libhwang import *

@staticmethod
def __VideoIndex_from_file(f):
    return VideoIndex.deserialize(f.read())

def __VideoIndex_to_file(self, f):
    f.write(self.serialize())

setattr(VideoIndex, 'from_file', __VideoIndex_from_file)
setattr(VideoIndex, 'to_file', __VideoIndex_to_file)

def index_video(f_or_string):
    def w(f):
        indexer = MP4IndexCreator()
        pass

    if isinstance(str, f_or_string):
        with open(f_or_string, 'rb') as f:
            return w(f)
    else:
        return w(f_or_string)
