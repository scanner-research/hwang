from video_index import *
from decoder import *
import os


def index_video(f_or_string, size=None):
    def w(f, size):
        indexer = MP4IndexCreator(size)
        offset = 0
        size_to_read = 1024
        while not indexer.is_done():
            f.seek(offset, 0)
            data = f.read(size_to_read)
            ret, offset, new_size = indexer.feed(data, size_to_read)
            size_to_read = new_size
        if indexer.is_error():
            raise Exception(indexer.error_message())
        return indexer.get_video_index()

    if isinstance(f_or_string, str):
        with open(f_or_string, 'rb') as f:
            size = os.fstat(f.fileno()).st_size
            return w(f, size)
    else:
        if size is None:
            raise Exception('Must provide file size')
        return w(f_or_string, size)
