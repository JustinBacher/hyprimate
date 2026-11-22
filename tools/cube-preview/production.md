# Offscreen verification of the production cube material

Use a build configured with `BUILD_TESTING=ON`. From the repository root:

```sh
cmake --build BUILD_DIR --target cube-renderer-tests cube-production-driver
ctest --test-dir BUILD_DIR -R '^cube-renderer-tests$' --output-on-failure
python tools/cube-preview/production.py /path/to/new/verification \
  --driver BUILD_DIR/cube-production-driver
```

The driver and pixel tests need surfaceless EGL/GLES 3; they do not open a compositor
or load a plugin. The Python command uses the existing R2 fixture, slate and native
controller sampler (Python with Pillow, NumPy and ModernGL, DejaVu Sans, a C++20
compiler and FFmpeg with libx264). `--only solid` or `--only inside-out` limits the
encode to one approved variant. It writes `production-solid.mp4` and
`production-inside-out.mp4` without replacing the preview reels or an existing output.
Both use the original 1920×1080 fixture, 60 fps, cue holds, and forward, backward and
interrupted shots at both speeds. Lettering is never reflected: backward changes the
capture order and direction, while an interruption keeps the original owners.

The real GLES material renders all motion frames and supplies the held endpoints.
The encoder first checks exact RGB endpoints and the nearby rendered poses in both
directions, before lossy encoding. The driver accepts two packed, top-first RGBA8
images on stdin, followed by direction (0 forward, 1 backward) and progress pairs.
Each request emits one packed RGB24 frame to stdout. Pixels travel through pipes,
not raw intermediate files. These reels verify the material, not compositor
presentation, scheduling or GPU performance.
