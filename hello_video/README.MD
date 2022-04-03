Modified version of hello_video that allow the rotation (and scale) of the H.264 video stream. Made for the DJI FVP video out.

Rotation (CW) : 
- n: no rotation
- r : rotate 90° (right)
- i : rotate 180° (invert)
- l : rotate 270° (left)

usage rotate stream right (90°) :
> hello_video.bin r

usage no rotation 100px top, 100px left and 300px width (maintain aspect ratio)
> hello_video.bin n 100 100 300

This version of hello_video is modified to allow for live video streaming via H.264. 

Thanks to [befinitiv](https://github.com/befinitiv)

The source repository for this modified script, along with instructions for using it is [here](https://github.com/befinitiv/hello_video)
