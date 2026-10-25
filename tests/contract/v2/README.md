This is the unmodified pre-L2 public header from commit
`c69f9a07efb7d4a8e9ccec715715ee6a388f9cd7`. It complements the older v0.1
`../v1/plugin.h`, which predates the additive acquisition interface.

Do not update either snapshot to make a compatibility test pass. The C fixture
and layout translation unit compile against this header, independently of the
current SDK. Layout checks compare sizes, alignments and every frozen field offset
on the running architecture; no architecture-specific offsets are prescribed.
