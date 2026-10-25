/* C-only producer exercising frozen tables and output ownership. Test modes are
 * explicit environment fixtures; no runtime-private types enter the DSO. */
#include <mantis/projected_light.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static const MantisHostV1 *host;
static int mode(void) {
    const char *s = getenv("MANTIS_PROCESSOR_TEST_MODE");
    return s ? atoi(s) : 0;
}
static int describe(uint32_t timeout, MantisNodeDescriptorV1 *d) {
    if (!timeout || !d || d->struct_size < sizeof(*d))
        return 1;
    *d = (MantisNodeDescriptorV1){
        sizeof(*d), 1, "org.example.processor-contract", MANTIS_IMAGE, MANTIS_IMAGE, 1, 1, 1, "cpu"};
    if (mode() == 3)
        d->abi_version = 9;
    if (mode() == 4)
        d->output_schema = 99;
    if (mode() == 5)
        d->backend = NULL;
    if (mode() == 6)
        d->deterministic = 7;
    if (mode() == 7)
        d->id = "";
    if (mode() == 8)
        d->input_type = MANTIS_FRAMESET;
    if (mode() == 9)
        d->input_type = d->output_type = MANTIS_ACQUISITION_BUNDLE;
    if (mode() == 30)
        d->input_type = d->output_type = MANTIS_FRAMESET;
    if (mode() == 31)
        d->input_type = d->output_type = MANTIS_LASER_OBSERVATION;
    if (mode() == 32)
        d->input_type = d->output_type = MANTIS_TRIGGER_EVENT;
    if (mode() == 33)
        d->input_type = d->output_type = MANTIS_ACQUISITION_EVIDENCE;
    if (mode() == 34)
        d->output_type = "org.example.changed-output";
    if (mode() >= 40 && mode() <= 49)
        d->input_type = d->output_type = MANTIS_LASER_OBSERVATION;
    return 0;
}
static int process(const MantisHostV1 *h, const MantisSemanticPacketV1 *input, uint32_t timeout,
                   MantisSemanticEmitV1 emit, void *context) {
    MantisSemanticPacketV1 output = *input;
    MantisDataPacketV1 data;
    MantisAttributeV1 attr;
    int m = mode();
    const char *started = getenv("MANTIS_PROCESSOR_STARTED_FILE");
    if (started && *started) {
        FILE *f = fopen(started, "wb");
        if (f) {
            fputc(1, f);
            fclose(f);
        }
    }
    if (m == 34) {
        data = *input->data;
        data.type.name = "org.example.changed-output";
        output.data = &data;
        return emit(context, &output);
    }
    if (m == 10)
        return 0; /* success without emit */
    if (m == 11) {
        emit(context, input);
        emit(context, input);
        return 0;
    }
    if (m == 12) {
        emit(context, input);
        return 1;
    }
    if (m == 13) {
        output.kind = 99;
        return emit(context, &output);
    }
    if (m == 14) {
        output.laser = (const MantisLaserObservationV1 *)input->data;
        return emit(context, &output);
    }
    if (m == 15) {
        raise(SIGABRT);
        return 1;
    }
    if (m == 16) {
        struct timespec t = {5, 0};
        nanosleep(&t, NULL);
        return 1;
    }
    if (m == 37) {
        const char *release = getenv("MANTIS_PROCESSOR_RELEASE_FILE");
        struct timespec begin, now, pause = {0, 1000000};
        if (!release || clock_gettime(CLOCK_MONOTONIC, &begin))
            return 1;
        for (;;) {
            FILE *f = fopen(release, "rb");
            if (f) {
                fclose(f);
                break;
            }
            if (clock_gettime(CLOCK_MONOTONIC, &now) ||
                (now.tv_sec - begin.tv_sec) * 1000 + (now.tv_nsec - begin.tv_nsec) / 1000000 >= timeout)
                return 1;
            nanosleep(&pause, NULL);
        }
    }
    if (m == 17)
        return 1;
    if (m >= 18 && m <= 24) {
        data = *input->data;
        output.data = &data;
        if (m == 18) {
            data.type.name = MANTIS_POINTS;
        }
        if (m == 19) {
            data.type.version = 99;
        }
        if (m >= 20 && m <= 24) {
            attr = data.attributes[0];
            data.attributes = &attr;
            data.attribute_count = 1;
            if (m == 20)
                attr.bytes = UINT64_MAX;
            if (m == 21)
                attr.offset = UINT64_MAX;
            if (m == 22)
                attr.rank = 5;
            if (m == 23)
                attr.stride[0] = UINT64_MAX;
            if (m == 24)
                attr.buffer = NULL;
        }
        return emit(context, &output);
    }
    if (m >= 40 && m <= 49) {
        MantisLaserObservationV1 laser = *input->laser;
        output.laser = &laser;
        if (m == 40)
            laser.sample_count++;
        if (m == 41) {
            laser.attributes = NULL;
            laser.attributes_count = 0;
        }
        if (m == 42)
            laser.disposition.presence = 99;
        if (m == 43)
            laser.context.producer.implementation = "";
        if (m == 44)
            laser.type.version = 2;
        if (m == 45)
            laser.context.source.frame.stream.generation = "";
        if (m == 46) {
            attr = laser.attributes[0];
            attr.bytes = UINT64_MAX;
            laser.attributes = &attr;
            laser.attributes_count = 1;
        }
        if (m == 47)
            laser.context.source.rig_calibration.presence = 99;
        if (m == 48)
            laser.confidence_interpretation.presence = MANTIS_PRESENCE_UNKNOWN;
        if (m == 49)
            laser.context.source.frame.camera = "";
        return emit(context, &output);
    }
    if (m == 25) {
        data = *input->data;
        output.data = &data;
        data.header.sequence = 991;
        data.header.received = 771;
        return emit(context, &output);
    }
    if (m == 26) {
        MantisBuffer *b = h->allocate(4, 64);
        void *p;
        uint64_t bytes;
        if (!b || h->write_map(b, &p, &bytes))
            return 1;
        memset(p, 47, 4);
        if (h->publish(b))
            return 1;
        data = *input->data;
        attr = data.attributes[0];
        attr.buffer = b;
        attr.offset = 0;
        attr.bytes = 4;
        data.attributes = &attr;
        data.attribute_count = 1;
        output.data = &data;
        {
            int rc = emit(context, &output);
            h->release(b);
            return rc;
        }
    }
    (void)host;
    (void)timeout;
    return emit(context, input); /* host explicitly retains borrowed buffers */
}
static const MantisProcessorV2 processor = {sizeof(processor), 1, describe, process};
static int legacy_describe(MantisNodeDescriptorV1 *d) {
    return describe(1000, d);
}
static int legacy_failure(const MantisHostV1 *h, const MantisPacketV1 *p, MantisEmitV1 emit, void *context) {
    (void)h;
    (void)p;
    (void)emit;
    (void)context;
    return 1; /* distinguish explicit V1 selection from the successful V2 path */
}
static const MantisProcessorV1 legacy = {sizeof(legacy), 1, legacy_describe, legacy_failure};
static int initialize(const MantisHostV1 *h) {
    host = h;
    return 0;
}
static void shutdown(void) {
    host = NULL;
}
static const void *query(const char *id) {
    if (id && !strcmp(id, MANTIS_PROCESSOR_V1) && mode() == 35)
        return &legacy;
    if (!id || strcmp(id, MANTIS_PROCESSOR_V2))
        return NULL;
    static const MantisProcessorV2 small = {4, 1, describe, process};
    static const MantisProcessorV2 version = {sizeof(processor), 9, describe, process};
    static const MantisProcessorV2 no_process = {sizeof(processor), 1, describe, NULL};
    static const MantisProcessorV2 no_describe = {sizeof(processor), 1, NULL, process};
    if (mode() == 1)
        return &small;
    if (mode() == 2)
        return &version;
    if (mode() == 27)
        return &no_process;
    if (mode() == 28)
        return &no_describe;
    return &processor;
}
static const MantisPluginV1 plugin = {
    sizeof(plugin), 1, "org.example.processor-contract", "1.0.0", initialize, shutdown, query};
MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t abi) {
    return abi == 1 ? &plugin : NULL;
}
