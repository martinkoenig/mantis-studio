# ADR-025: Packed monochrome bytes are distinct from logical pixels

Status: Accepted for v0.2 hardware-gap continuation

Y10P is MIPI RAW10 monochrome: four 10-bit samples occupy five bytes. An ordinary
u8[height,width] attribute with stride [bytesperline,1] would misrepresent its
logical samples and lose or obscure packed row bytes.

Keep ImageFrame schema 1 and the existing C ABI unchanged. Add the namespaced
attribute org.mantis.image.packed_bytes: scalar u8, rank 1, shape [bytesused],
byte stride [1], unit byte. Preserve every delivered byte, including row padding.
The ImageFrame header declares fourcc=Y10P and:

- org.mantis.image.layout = mipi-raw10-v1
- org.mantis.image.bits_per_sample = 10
- org.mantis.image.width / height = logical dimensions
- org.mantis.image.row_stride_bytes = driver bytesperline

These metadata values and the full byte extent are preserved by the unchanged
FrameSet/RawCapture codecs. Existing RAW8 org.mantis.pixels representations stay
unchanged. New readers reject unsupported packing instead of treating it as u8
logical pixels; old readers can retain packed attributes but must not render them
as ordinary grayscale samples. No project/schema migration is required.

The Qt-free Y10PView consumer helper validates rows and exposes exact 10-bit
samples. Preview reduces samples by shifting right two bits, directly into an
8-bit display row, without allocating an intermediate 16-bit image. Acquisition
and the LOSSLESS writer never unpack or alter raw bytes. Buffer mode remains
MMAP plus one explicit acquisition copy.

Packing follows the kernel's
[Y10P specification](https://kernel.org/doc/html/v5.7/media/uapi/v4l/pixfmt-y10p.html):
the first four bytes contain each sample's high eight bits; the fifth contains
the low-bit pairs at bit positions 0, 2, 4 and 6. This differs from Y10BPACK.
