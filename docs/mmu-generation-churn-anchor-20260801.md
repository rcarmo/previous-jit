# MMU generation-key churn at a bounded guest-I/O anchor — 2026-08-01

## Scope

This is a deterministic census of the existing single-page MMU generation-key
exemption against its blanket-key inverse. It is not a desktop benchmark: both
arms terminate after exactly 256 guest-driven SCSI transactions. The copied
fixture used at this checkpoint later reached an IPC-rights panic; a different,
immutable post-logout fixture subsequently passed exact-by-default full-boot
acceptance. See [`current-jit-status.md`](current-jit-status.md).

Checkpoint: `9a2d29fb1035d01e6270f855b2ff196bce1e7d84`

Binary SHA-256:
`675338dde039ed16ec148d42401f02766650cd06c596a0a4b4a1cf07c2982a18`

Read-only source-image SHA-256:
`1e85ffb4b7c464f9a96dd2ded4bb1168f60c30a3c3cd5cc7389ad89c399c9d18`

Artifact:
`/workspace/tmp/mmu-churn-ab-9a2d29f-20260801-012942`

## Method

Both runs used `tools/jit-timing-anchor.sh`, CPU 11 pinned at fixed
`2600198 kHz`, the same immutable read-only image and the same binary. Host
policy was restored to `schedutil` afterward.

Run order:

1. default single-page exemption;
2. `PREVIOUS_UAE2026_JIT_MMU_GEN_KEY_1PAGE=0` blanket-key inverse.

Both policies passed the complete timing-anchor acceptance gate:

- identical 256-entry SCSI sequence/type/LBA/content-hash stream;
- identical first 400 CycInt handler/requested-delay pairs;
- identical per-handler arm/fire counts;
- identical 194 delivered exceptions;
- identical final emulated cycles and bounded timing summary.

The SCSI and CycInt stream files are byte-identical across the two policies.

## Census

| Counter | Default one-page exemption | Blanket key | Default delta |
|---|---:|---:|---:|
| Compiles | 1,681 | 1,756 | −75 (−4.27%) |
| Fresh compiles | 1,528 | 1,692 | −164 (−9.69%) |
| Recompiles | 153 | 64 | +89 |
| Trace retirements | 7,885 | 8,202 | −317 (−3.86%) |
| Native retirements | 84,585,573 | 84,585,256 | +317 |
| Generation-only rejections | 24 | 330 | −306 (−92.73%) |
| Safe single-page exemptions | 177 | 0 | +177 |
| Deferred icache publications | 18,285 | 19,048 | −763 (−4.01%) |
| Immediate icache flushes | 3,764 | 4,118 | −354 (−8.60%) |
| Peak code cache | 3,931,044 B | 4,093,612 B | −162,568 B (−3.97%) |

The two policies execute the same total observed instructions. The exemption
turns 317 first-pass trace retirements into native retirements at this anchor.

## Safety boundary

Both policies see exactly 18 MMU generation bumps:

- source `ATCA`: 10;
- source `TTR`: 8.

These are global invalidation sources; neither names a page that could support
more selective invalidation.

Under the blanket inverse, 302 of 330 generation-only rejections satisfy both
the 4 KiB and 8 KiB single-page predicates. With the default exemption, those
safe page-local blocks are retained and only 24 generation rejections remain.
All 24 are outside both single-page predicates: their compiled code footprint
crosses a page or contains multiple spans. Entry-page retranslation cannot
validate their other logical page mappings or aliases, so the generation key
remains load-bearing.

Therefore no additional generation-key weakening is justified by this census.
The existing default already removes every safe page-local rejection observed
at this anchor, while retaining the blanket-key inverse control and keying all
cross-/multi-page blocks.

## Reproduction

```bash
# Clean checkpoint and immutable binary first.
ROOT_OUT=/workspace/tmp/mmu-churn-ab-$(git rev-parse --short HEAD)-$(date +%Y%m%d-%H%M%S)

OUTDIR="$ROOT_OUT/default" \
ANCHOR_IO=256 CYCARM_LIMIT=400 TIMEOUT_SEC=300 BENCH_CPU=11 \
  ./tools/jit-timing-anchor.sh

PREVIOUS_UAE2026_JIT_MMU_GEN_KEY_1PAGE=0 \
OUTDIR="$ROOT_OUT/blanket" \
ANCHOR_IO=256 CYCARM_LIMIT=400 TIMEOUT_SEC=300 BENCH_CPU=11 \
  ./tools/jit-timing-anchor.sh
```

The next throughput lever is not weaker generation keying. At this anchor the
JIT still makes roughly 40.9 million C-dispatch round trips for 84.6 million
retired instructions. Generated dispatch with inline full-identity checks and a
safe C fallback is the next plan item.
