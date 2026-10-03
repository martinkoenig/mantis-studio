#ifndef MANTIS_PLUGIN_H
#define MANTIS_PLUGIN_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#if defined(_WIN32)
#define MANTIS_EXPORT __declspec(dllexport)
#else
#define MANTIS_EXPORT __attribute__((visibility("default")))
#endif
#define MANTIS_ABI_V1 1u
#define MANTIS_DEVICE_V1 "org.mantis.device.v1"
#define MANTIS_PROCESSOR_V1 "org.mantis.processor.v1"
#define MANTIS_EXPORTER_V1 "org.mantis.exporter.v1"
#define MANTIS_IMAGE_STREAM_V1 "org.mantis.camera.image-stream.v1"
#define MANTIS_IMAGE "org.mantis.ImageFrame"
#define MANTIS_POINTS "org.mantis.PointCloud"
/* All strings UTF-8. All borrowed pointers valid only during the call.
 * Functions return zero on success, nonzero on failure. No exception may cross this boundary.
 * Tables live until shutdown. The plugin root is borrowed; device instances are owned and destroyed via their
 * table.
 */
typedef struct MantisBuffer MantisBuffer;
typedef struct MantisHostV1 {
    uint32_t struct_size, abi_version;
    MantisBuffer *(*allocate)(uint64_t bytes, uint64_t alignment);
    void (*retain)(MantisBuffer *);
    void (*release)(MantisBuffer *);
    int (*write_map)(MantisBuffer *, void **data, uint64_t *bytes);
    int (*read_map)(const MantisBuffer *, const void **data, uint64_t *bytes);
    int (*publish)(MantisBuffer *);
    void (*log)(const char *level, const char *component, const char *message);
} MantisHostV1;
typedef struct MantisAttributeV1 {
    uint32_t struct_size, abi_version;
    const char *name;
    const char *unit;
    uint32_t scalar_type, rank;
    uint64_t shape[4], stride[4];
    MantisBuffer *buffer;
    uint64_t offset, bytes;
} MantisAttributeV1;
typedef struct MantisPacketV1 {
    uint32_t struct_size, abi_version;
    const char *type_id;
    uint32_t schema_version;
    uint64_t sequence;
    int64_t device_time_ns;
    const char *clock_id;
    const char *calibration_id;
    uint64_t calibration_revision;
    const char *coordinate_frame;
    const MantisAttributeV1 *attributes;
    uint32_t attribute_count;
} MantisPacketV1;
/* Emit is synchronous. Receiver retains buffers if it keeps them; producer releases its own references. */
typedef int (*MantisEmitV1)(void *context, const MantisPacketV1 *packet);
typedef struct MantisDeviceDescriptorV1 {
    uint32_t struct_size, abi_version;
    const char *id;
    const char *name;
    const char *const *capabilities;
    uint32_t capability_count;
} MantisDeviceDescriptorV1;
typedef struct MantisDeviceV1 {
    uint32_t struct_size, abi_version;
    int (*describe)(MantisDeviceDescriptorV1 *);
    int (*create)(const MantisHostV1 *, void **instance);
    void (*destroy)(void *instance);
    int (*start)(void *instance);
    int (*next)(void *, MantisEmitV1, void *);
    int (*stop)(void *);
} MantisDeviceV1;
typedef struct MantisNodeDescriptorV1 {
    uint32_t struct_size, abi_version;
    const char *id;
    const char *input_type;
    const char *output_type;
    uint32_t input_schema, output_schema, deterministic;
    const char *backend;
} MantisNodeDescriptorV1;
typedef struct MantisProcessorV1 {
    uint32_t struct_size, abi_version;
    int (*describe)(MantisNodeDescriptorV1 *);
    int (*process)(const MantisHostV1 *, const MantisPacketV1 *, MantisEmitV1, void *);
} MantisProcessorV1;
typedef struct MantisExporterV1 {
    uint32_t struct_size, abi_version;
    const char *input_type;
    uint32_t input_schema;
    int (*export_file)(const MantisHostV1 *, const MantisPacketV1 *, const char *utf8_path);
} MantisExporterV1;
typedef struct MantisPluginV1 {
    uint32_t struct_size, abi_version;
    const char *id;
    const char *version;
    int (*initialize)(const MantisHostV1 *);
    void (*shutdown)(void);
    const void *(*query_interface)(const char *versioned_id);
} MantisPluginV1;
typedef const MantisPluginV1 *(*MantisPluginEntryV1)(uint32_t requested_abi);
MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t requested_abi);
#ifdef __cplusplus
}
#endif
#endif
