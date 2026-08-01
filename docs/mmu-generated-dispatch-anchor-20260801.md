# Generated MMU dispatch at a bounded guest-I/O anchor — 2026-08-01

## Scope

This validates the first generated MMU-dispatch tranche against its exact C-dispatch inverse. It is a deterministic correctness/census A/B, not a desktop benchmark or a claim about the invalid copied-disk boot fixture.

Code checkpoint: `82aa3715075ebe53ec1a57290039cd26900150ee`

Binary SHA-256: `deff7c1749515619d2b2ad426fa69b9d9fd43c188be87be0ef86dbb1a98b576c`

Artifacts:

- inverse control: `/workspace/tmp/mmu-generated-dispatch-anchor-control-82aa371-20260801-083517`
- generated candidate: `/workspace/tmp/mmu-generated-dispatch-anchor-candidate-82aa371-20260801-083556`

## Implementation boundary

`PREVIOUS_UAE2026_JIT_MMU_FAST_DISPATCH=1` enables an AArch64 epilogue that transfers directly only when all of these conditions hold after the existing block-boundary tick and countdown charge:

- countdown is non-negative;
- `regs.spcflags == 0`;
- the MMU is still enabled;
- the cacheline has a non-null primary block;
- translated host PC, logical guest PC and supervisor context match;
- MMU identity is valid and the block generation equals the current bridge-owned generation;
- the complete checksum/source footprint is one span inside the entry 4 KiB page;
- block status is exactly `BI_ACTIVE`;
- the installed cache handler equals `handler_to_use` and is not the execute-normal, recompile or checksum specialty.

Every miss uses the unchanged C dispatcher. Collision-list searches, stale-generation single-page promotion, checksum/revalidation states, mapping/context changes, interrupts and page-boundary blocks therefore retain existing policy. The emitted checks use loads, XORs, `CBZ`/`CBNZ`/`TBNZ`, and `BR`; they do not modify guest NZCV.

Unset or `PREVIOUS_UAE2026_JIT_MMU_FAST_DISPATCH=0` is the exact inverse and remains the default.

## Focused gate

Both inverse and candidate passed `tools/uae2026-mmu-fast-smoke.sh` at RAM test address `0x04008000`:

| Arm | Total | Pass | Fail | Infrastructure failures |
|---|---:|---:|---:|---:|
| C dispatch | 67 | 67 | 0 | 0 |
| Generated dispatch | 67 | 67 | 0 | 0 |

Artifacts:

- `/workspace/tmp/mmu-generated-dispatch-control-20260801-082415`
- `/workspace/tmp/mmu-generated-dispatch-candidate-20260801-082858`

## Deterministic 256-I/O result

Both arms used the same clean SHA, binary, immutable read-only source image, CPU 11 and fixed `2600198 kHz` policy. The manifests differ only by timestamp. The harness restored `schedutil` with the original `799865–2600198 kHz` limits and left no emulator process running.

Exact parity:

- complete 256-entry SCSI stream SHA-256: `b5d2f64a2d9758140cdbad775b33c67926925cfd3a2ef61bd85e1916bcccb735` in both arms;
- first 400 CycInt handler/delay pairs SHA-256: `19b06717fe1d305e333f2d26dcbd2bdd86df0825e4f3e5e8e47324664e2b3c23` in both arms;
- 194 delivered exceptions in both arms;
- identical handler arm/fire counts;
- interpreter/JIT cycles `287,616,170 / 287,618,155` (6 ppm) in both arms;
- interpreter/JIT retirements `84,592,715 / 84,593,458` (8 ppm) in both arms;
- 1,681 compiles, 1,528 fresh, 153 recompiles, 24 generation rejects and 177 safe stale-generation exemptions in both arms.

Dispatch census:

| Counter | C-dispatch inverse | Generated candidate |
|---|---:|---:|
| Generated direct hits | 0 | 22,820,437 |
| Generated safe fallbacks | 0 | 18,085,706 |
| C `execute_normal` calls | 40,907,268 | 18,086,831 |
| C cache hits | 40,905,587 | 18,085,150 |

The generated path therefore removes 22.82 million C lookup round trips at this anchor while preserving every architectural/timing gate. The larger inline generated code footprint is visible in average block code (`2338.515 B` inverse vs `2770.629 B` candidate) and peak cache (`3,931,044 B` vs `4,657,428 B`); this tranche is accepted for semantic correctness and dispatch elimination, not yet as a wall-time speed claim.

## Shared-dispatch follow-up (`cb30ff3`)

The safe short-block follow-up factors the same accepted full-identity predicate into one popall-space thunk. `PREVIOUS_UAE2026_JIT_MMU_FAST_DISPATCH_SHARED=0` preserves the inline implementation as the exact inverse; unset/`1` selects the shared thunk whenever fast dispatch is enabled.

At the same 256-I/O anchor:

- direct hits remain exactly `22,820,437` and safe C fallbacks `18,085,706`;
- SCSI/CycInt hashes, 194 exceptions, handler counts and cycle/retirement totals remain identical;
- average generated block falls `2770.629 B → 2338.515 B` (`−432.114 B`, `−15.60%`);
- peak cache falls `4,657,428 B → 3,931,044 B` (`−726,384 B`, `−15.60%`);
- deferred per-block branch flushes fall `55,095 → 18,285`.

Artifacts:

- inline inverse: `/workspace/tmp/mmu-dispatch-shared-anchor-inline-cb30ff3-20260801-085353`
- shared candidate: `/workspace/tmp/mmu-dispatch-shared-anchor-candidate-cb30ff3-20260801-085425`

No block-formation policy changed. Constant-jump MMU fetch/span protection and the existing flush-before-`raise_in_cl_list()` publication boundary remain intact.

## Reproduction

```bash
# Clean/pushed immutable SHA required.
PREVIOUS_UAE2026_JIT_MMU_FAST_DISPATCH=0 \
OUTDIR=/workspace/tmp/mmu-generated-dispatch-control-$(git rev-parse --short HEAD)-$(date +%Y%m%d-%H%M%S) \
  ./tools/jit-timing-anchor.sh

PREVIOUS_UAE2026_JIT_MMU_FAST_DISPATCH=1 \
OUTDIR=/workspace/tmp/mmu-generated-dispatch-candidate-$(git rev-parse --short HEAD)-$(date +%Y%m%d-%H%M%S) \
  ./tools/jit-timing-anchor.sh

# Same fast predicate, inline inverse versus shared thunk.
PREVIOUS_UAE2026_JIT_MMU_FAST_DISPATCH=1 \
PREVIOUS_UAE2026_JIT_MMU_FAST_DISPATCH_SHARED=0 \
OUTDIR=/workspace/tmp/mmu-dispatch-shared-inline-$(git rev-parse --short HEAD)-$(date +%Y%m%d-%H%M%S) \
  ./tools/jit-timing-anchor.sh

PREVIOUS_UAE2026_JIT_MMU_FAST_DISPATCH=1 \
PREVIOUS_UAE2026_JIT_MMU_FAST_DISPATCH_SHARED=1 \
OUTDIR=/workspace/tmp/mmu-dispatch-shared-candidate-$(git rev-parse --short HEAD)-$(date +%Y%m%d-%H%M%S) \
  ./tools/jit-timing-anchor.sh
```
