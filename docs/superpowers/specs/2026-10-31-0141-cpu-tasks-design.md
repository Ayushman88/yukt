# 0.14.1 CPU tasks — design spec

Canonical copy of the approved 0.14.1 design. The project design file is `docs/YUKT_014_DESIGN.md`. If the two disagree, `docs/YUKT_014_DESIGN.md` wins after this review.

This spec is **0.14.1 only**: reserved words `start` and `wait`, type `task[T]`, move-only arguments and results, existing host thread pool, one allocator mutex, inline `wait` if the job has not started.

Out of scope: mutex, `ref` across threads, async/await, channels, CUDA, `std/array`, cancel/timeout, `start` of `foreign`.

The language surface is now in `docs/LANGUAGE_SPEC.md` § 0.14.1. Implementation notes (claiming, races, allocator mutex, planned-parallel detection) are in `docs/YUKT_014_DESIGN.md`. Implementation is not authorized until the plan is reviewed.

Read `docs/YUKT_014_DESIGN.md` for the full contract, diagnostics table, implementation notes, and regression gate.
