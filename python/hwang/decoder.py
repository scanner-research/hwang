from .libhwang import *
import hwang

class Decoder(object):
    def __init__(self, f_or_path, video_index=None, size=None):
        if video_index is None:
            video_index = hwang.index_video(f_or_path, size=size)
        self.video_index = video_index

        if isinstance(f_or_path, str):
            f = open(f_or_path)
        else:
            f = f_or_path
        self.f = f

    def retrieve(self, rows):
        # Grab video index intervals
        video_intervals = slice_into_video_intervals(
            self.video_index, rows)
        frames = []
        for (start_index, end_index), valid_frames in video_intervals:
            # Figure out start and end offsets
            start_offset = self.video_index.sample_offsets[start_index]
            end_offset = (
                self.video_index.sample_offsets[end_index] +
                self.video_index.sample_sizes[end_index])
            # Read data buffer
            self.f.seek(start_offset, 0)
            encoded_data = self.f.read(end_offset - start_offset)
        return frames
