# Native monochrome image layout

RAW8 retains `org.mantis.pixels`: scalar u8, shape `[height,width]`, byte strides
`[bytesperline,1]`. Rows may contain driver padding. Published buffers are
immutable and metadata retains the native fourcc.

Packed Y10P uses `org.mantis.image.packed_bytes`: scalar u8, unit `byte`, rank one,
shape `[delivered_byte_count]`, stride `[1]`. This describes bytes honestly; it
does not pretend that one byte is one logical 10-bit sample. Required ImageFrame
metadata is:

| Key | Value |
| --- | --- |
| `fourcc` | `Y10P` |
| `org.mantis.image.layout` | `mipi-raw10-v1` |
| `org.mantis.image.bits_per_sample` | `10` |
| `org.mantis.image.width` | logical sample width, multiple of four |
| `org.mantis.image.height` | logical sample height |
| `org.mantis.image.row_stride_bytes` | actual driver bytesperline |

The [kernel Y10P specification](https://kernel.org/doc/html/v5.7/media/uapi/v4l/pixfmt-y10p.html)
places four samples' upper eight bits in the first four bytes of each group and
packs their low two bits in order into byte five. It is distinct from Y10BPACK.
Minimum row extent is `width/4*5`; row starts use the byte stride. At 1280×720,
the validated external setup reports stride 1600 and sizeimage 1,152,000 bytes.
The buffer may include additional driver padding; recording preserves all delivered
bytes. An inconsistent/truncated layout fails rather than inventing samples.

The Qt-free `mantis::data::Y10PView` validates a byte span and offers 10-bit sample
access without materializing a second image. `image_layout` validates the
namespaced Packet contract. `grayscale_row` is a presentation consumer: Y10P
samples map to `sample >> 2`, using the original high-byte values; RAW8 rows copy
unchanged. Studio fills an owned `QImage::Format_Grayscale8` on its client worker.
No unpacking, QImage, RGB conversion or debayering occurs on the raw path.

Native capture uses V4L2 MMAP and one copy before QBUF into host-owned Mantis
storage. Fan-out shares immutable buffers. Preview conversion/file publication
has separate costs and retains LATEST_ONLY semantics. DMABUF zero-copy is deferred.
No plugin C structure changes, container version changes or project migrations
are necessary: attributes and metadata were already extensible. Consumers must
recognize the explicit packed layout before interpreting logical pixels. Old
RAW8 fixtures and v0.1 ABI/artifact readers remain unchanged.
