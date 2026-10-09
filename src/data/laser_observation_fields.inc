// MLOBS001 v1 normative field catalog. No native layout or ABI tables on disk.
template <> struct Fields<spatial::CoordinateFrame> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.id, v.name); }
};
template <> struct Fields<ObservationKey> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.run_id, v.producer_stream, v.producer_generation, v.sequence);
    }
};
template <> struct Fields<EmitterPatternIdentity> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.emitter, v.pattern, v.revision); }
};
template <> struct Fields<LineIdentity> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.emitter, v.pattern, v.pattern_revision, v.local_line);
    }
};
template <> struct Fields<ProgramCorrelation> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.program, v.step); }
};
template <> struct Fields<PreprocessingTransform> {
    template <class A, class V> static void apply(A &a, V &v) {
        for (auto &x : v.original_from_processed)
            a(x);
        a(v.reference);
    }
};
template <> struct Fields<LaserObservationContext> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.source, v.frameset, v.bundle, v.raw_input, v.optical_frame, v.preprocessing, v.requested_emitters,
          v.emitter_evidence, v.emitter_patterns, v.correlation, v.acquisition_evidence, v.triggers,
          v.clock_mappings, v.producer, v.parameters, v.exact_inputs, v.origin, v.producer_completed,
          v.packet_quality_flags);
    }
};
template <> struct Fields<schema::AttributeDescriptor> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.name, v.scalar, v.shape, v.stride, v.unit);
    }
};
template <> struct Fields<Attribute> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.descriptor, v.buffer); }
};
template <> struct Fields<LaserObservation> {
    template <class A, class V> static void apply(A &a, V &v) {
        a(v.type, v.key, v.context, v.disposition, v.sample_count, v.emitter_dictionary, v.line_dictionary,
          v.attributes, v.confidence_interpretation, v.diagnostic);
    }
};
template <> struct Tags<ObservationOrigin> {
    static constexpr std::array values{ObservationOrigin::real, ObservationOrigin::synthetic,
                                       ObservationOrigin::imported};
};
template <> struct Tags<ObservationDisposition> {
    static constexpr std::array values{ObservationDisposition::success,
                                       ObservationDisposition::extractor_failed,
                                       ObservationDisposition::extractor_unavailable};
};
template <> struct Tags<schema::ScalarType> {
    static constexpr std::array values{schema::ScalarType::u8, schema::ScalarType::i64,
                                       schema::ScalarType::f32, schema::ScalarType::f64,
                                       schema::ScalarType::u32};
};
