from .libhwang import *
import hwang

class Decoder(object):
    def __init__(self, f_or_path, video_index=None):
        if video_index is None:
            video_index = hwang.index_video(f_or_path)
        self.video_index = video_index

        if isinstance(f_or_path, str):
            f = open(f_or_path)
        else:
            f = f_or_path
        self.f = f

        # Setup decoder
        handle = DeviceHandle()
        handle.type = DeviceType.CPU
        handle.id = 0
        self._decoder = DecoderAutomata(handle, 1, VideoDecoderType.SOFTWARE)

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

            data = EncodedData()
            data.width = self.video_index.frame_width
            data.height = self.video_index.frame_height
            data.start_keyframe = start_index
            data.end_keyframe = end_index

            data.sample_offsets = (
                [o - start_offset
                 for o in self.video_index.sample_offsets[
                         start_index:end_index]])
            data.sample_sizes = (
                self.video_index.sample_sizes[start_index:end_index])
            data.valid_frames = valid_frames
            data.keyframes = [k for k in self.video_index.keyframe_indices
                              if k >= start_index and k <= end_index]
            data.encoded_video = encoded_data
            args = [data]
            self._decoder.initialize(args, self.video_index.metadata_bytes)
            frames += self._decoder.get_frames(self.video_index,
                                               len(valid_frames))

        return frames
