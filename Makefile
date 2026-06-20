# Previous JIT fork — top-level harness Makefile.
#
# This Makefile wraps the cmake build, the headless-VNC test harness, and the
# perf-baseline benchmark so that test runs are reproducible and don't depend
# on shell history.  Every target below is safe to re-run idempotently.
#
# All targets honour these variables (override on the command line):
#
#   BUILD_DIR     cmake build directory                 (default: build-vnc)
#   BIN           emulator binary                       (default: $(BUILD_DIR)/src/Previous)
#   ASSET_ROOT    NeXTSTEP ROMs + backup disk images    (default: /workspace/assets/previous)
#   VNC_PORT      libvncserver listen port              (default: 5901)
#   DISPLAY_NAME  Xvfb display number                   (default: :198)
#   RUNDIR        per-run working dir (cfg + disk copy) (default: /workspace/tmp/previous-interactive)
#   WARM_IMG      disk image copied into RUNDIR         (default: $(RUNDIR)/nextstep33-system-en-run.img)
#   JOBS          parallel build jobs                   (default: $(nproc))
#
# Conventions:
#  * "headless-*" targets need an Xvfb display.  The targets create their own
#    Xvfb on $(DISPLAY_NAME).
#  * Tests are detached (nohup + setsid) so they survive a tool-call boundary,
#    matching the headless-host operating mode.
#  * Logs land in $(RUNDIR)/{previous.log,xvfb.log}; the perf-baseline targets
#    additionally write $(RUNDIR)/bench-*.{run.log,jsonl}.

BUILD_DIR    ?= build-vnc
BIN          ?= $(BUILD_DIR)/src/Previous
ASSET_ROOT   ?= /workspace/assets/previous
VNC_PORT     ?= 5901
DISPLAY_NAME ?= :198
RUNDIR       ?= /workspace/tmp/previous-interactive
WARM_IMG     ?= $(RUNDIR)/nextstep33-system-en-run.img
JOBS         ?= $(shell nproc)

# Canonical JIT+handoff env block — the recipe that boots NeXTSTEP reliably.
JIT_ENV = \
	PREVIOUS_VNC=1 PREVIOUS_VNC_PORT=$(VNC_PORT) \
	PREVIOUS_UAE2026_JIT=1 PREVIOUS_UAE2026_JIT_RAM=1 \
	B2_JIT_RTE_FAULT_HANDOFF=1 \
	PREVIOUS_RTC_UNIX_TIME=0x2ec46472

# Interpreter-only env — used to baseline JIT against the legacy CPU path.
INTERP_ENV = \
	PREVIOUS_VNC=1 PREVIOUS_VNC_PORT=$(VNC_PORT) \
	PREVIOUS_RTC_UNIX_TIME=0x2ec46472

.PHONY: help build rebuild clean \
	headless-jit headless-interp headless-stop \
	headless-oneshot perf-baseline perf-baseline-quick \
	vnc-probe vnc-probe-motion \
	jit-microbench jit-oracle-bisect

help:
	@grep -E '^[a-zA-Z][a-zA-Z0-9_-]+:.*##' $(MAKEFILE_LIST) | \
		awk -F':.*##' '{ printf "  %-22s %s\n", $$1, $$2 }'

# ----------------------------------------------------------------------------
# Build
# ----------------------------------------------------------------------------

$(BUILD_DIR):
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release \
		-DENABLE_EXPERIMENTAL_UAE2026_JIT=ON

# Header-change detection for the unity-compiled JIT translation unit.
#
# `uae2026_compiler_unit.cpp` `#include`s a large tree of vendored .cpp
# files (compemu_support.cpp, compemu.cpp, compemu_arm.cpp, the gapfill
# files, etc.).  cmake's incremental-build dependency scanner does not
# track those secondary `#include`s through the chain, so changes to
# headers like `compemu_arm.h` (where VREGS lives) can be silently
# ignored on rebuild.  This bit me hard during the VREGS 22 -> 32 fix:
# the source said 32 but the binary kept aborting with VREGS=22 baked
# into `set_status`'s format string until I forced a clean rebuild.
#
# Workaround: hash every JIT header + the unity TU on each `make build`.
# If the hash differs from the stored stamp, delete the unit's `.o` so
# cmake re-compiles it; everything else stays incremental.

JIT_HEADER_HASH_FILE := $(BUILD_DIR)/.jit-header-hash
JIT_UNIT_OBJ         := $(BUILD_DIR)/src/cpu/CMakeFiles/UaeCpu.dir/uae2026_compiler_unit.cpp.o
JIT_HEADER_SOURCES   := $(shell find src/cpu/uae_cpu_2026 \
                          \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' -o -name '*.c' \) \
                          2>/dev/null | sort) \
                        src/cpu/uae2026_compiler_unit.cpp \
                        src/cpu/uae2026_compiler_prefs_shim.cpp \
                        src/cpu/uae2026_jit_bridge.cpp \
                        src/cpu/uae2026_linker_stubs.cpp

.PHONY: jit-rebuild-check
jit-rebuild-check: $(BUILD_DIR) ## Force re-compile of the JIT unit when its headers/sources change
	@new_hash=$$(cat $(JIT_HEADER_SOURCES) 2>/dev/null | sha256sum | awk '{print $$1}'); \
	old_hash=$$(cat $(JIT_HEADER_HASH_FILE) 2>/dev/null || true); \
	if [ "$$new_hash" != "$$old_hash" ]; then \
		if [ -n "$$old_hash" ]; then \
			echo "jit-rebuild-check: JIT sources changed - dropping stale $$(basename $(JIT_UNIT_OBJ))"; \
			rm -f $(JIT_UNIT_OBJ) $(JIT_UNIT_OBJ).d; \
		fi; \
		mkdir -p $$(dirname $(JIT_HEADER_HASH_FILE)); \
		echo "$$new_hash" > $(JIT_HEADER_HASH_FILE); \
	fi

build: $(BUILD_DIR) jit-rebuild-check  ## Configure + build the emulator (Release, JIT enabled)
	cmake --build $(BUILD_DIR) -j$(JOBS)

rebuild: ## Force-rebuild without reconfiguring
	cmake --build $(BUILD_DIR) -j$(JOBS) --clean-first

clean: ## Remove the cmake build tree
	rm -rf $(BUILD_DIR)

# ----------------------------------------------------------------------------
# Headless boot targets (long-running; detached)
# ----------------------------------------------------------------------------
# These spawn Xvfb + Previous in a detached process group via setsid so the
# emulator outlives the make invocation.  Stop them with `make headless-stop`.

$(RUNDIR):
	mkdir -p $(RUNDIR)/home/.previous

$(WARM_IMG): $(RUNDIR)
	@test -f $(WARM_IMG) || { \
		src=$$(ls -dt $(ASSET_ROOT)/images/nextstep33-system-en-backup-*.img 2>/dev/null | head -1); \
		test -n "$$src" || { echo "no backup disk image under $(ASSET_ROOT)/images"; exit 2; }; \
		cp --sparse=always --reflink=auto $$src $(WARM_IMG); \
	}

$(RUNDIR)/home/.previous/previous.cfg: $(RUNDIR)
	@./tools/headless-write-cfg.sh \
		$(RUNDIR)/home/.previous/previous.cfg \
		$(WARM_IMG) \
		$(ASSET_ROOT)

headless-jit: build $(WARM_IMG) $(RUNDIR)/home/.previous/previous.cfg ## Detached JIT+handoff headless boot on $(DISPLAY_NAME)/$(VNC_PORT)
	@./tools/headless-launch.sh $(DISPLAY_NAME) $(VNC_PORT) $(RUNDIR) $(BIN) jit

lockstep-sweep: build ## REGONLY register/next_pc-ONLY lockstep sweep past c74 (LOCKSTEP_WIN=range)
	@./tools/lockstep-sweep.sh

lockstep-selftest: build ## Lockstep tracer trustworthiness self-test on the c74 known-good block
	@./tools/lockstep-selftest.sh

headless-interp: build $(WARM_IMG) $(RUNDIR)/home/.previous/previous.cfg ## Detached interpreter-only headless boot
	@./tools/headless-launch.sh $(DISPLAY_NAME) $(VNC_PORT) $(RUNDIR) $(BIN) interp

# One-shot handoff: JIT hands off to interpreter on the first RTE fault, then
# resumes after RESUME_INSNS interpreter instructions.  Used to test whether
# JIT can survive past the cmd-185 stall window when interpreter walks the
# kernel fault recovery path for it.
RESUME_INSNS ?= 1000000
headless-oneshot: build $(WARM_IMG) $(RUNDIR)/home/.previous/previous.cfg ## Detached JIT with one-shot interp handoff (RESUME_INSNS=$(RESUME_INSNS))
	@./tools/headless-launch.sh $(DISPLAY_NAME) $(VNC_PORT) $(RUNDIR) $(BIN) oneshot $(RESUME_INSNS)

headless-stop: ## Kill the detached headless emulator and its Xvfb
	-DISP_SLUG=$$(echo $(DISPLAY_NAME) | tr -d ':'); \
	  systemctl --user stop "previous-xvfb-$$DISP_SLUG.service" "previous-emulator-$$DISP_SLUG.service" 2>/dev/null; \
	  systemctl --user reset-failed "previous-xvfb-$$DISP_SLUG.service" "previous-emulator-$$DISP_SLUG.service" 2>/dev/null
	-pkill -9 -x Xvfb 2>/dev/null
	-BIN_BASENAME=$$(basename $(BIN)); pkill -9 -x "$$BIN_BASENAME" 2>/dev/null
	@sleep 1
	@echo "headless-stop: done"

# ----------------------------------------------------------------------------
# Perf baseline
# ----------------------------------------------------------------------------
# perf-baseline runs the 4-configuration benchmark matrix
# (current+JIT, current+interp, baseline+JIT, baseline+interp) and emits a
# CSV with boot time, idle/motion VNC bytes per second, and emulator CPU %.

PERF_OUT      ?= /workspace/tmp/perf-baseline
PERF_BASELINE ?= /workspace/tmp/previous-baseline/build-vnc/src/Previous

perf-baseline: build ## Full 4-config perf matrix (~25 min); writes $(PERF_OUT)/results.csv
	@./tools/perf-baseline.sh $(PERF_OUT) $(BIN) $(PERF_BASELINE)

perf-baseline-quick: build ## Just current+JIT vs current+interp (~8 min)
	@./tools/perf-baseline.sh --quick $(PERF_OUT) $(BIN) $(PERF_BASELINE)

# ----------------------------------------------------------------------------
# VNC probes
# ----------------------------------------------------------------------------

VNC_HOST ?= 127.0.0.1
vnc-probe: ## Connect to $(VNC_HOST):$(VNC_PORT), report encoding negotiation
	@python3 ./tools/vnc-probe.py --host $(VNC_HOST) --port $(VNC_PORT)

vnc-probe-motion: ## Sample VNC bytes/sec under scripted cursor motion
	@python3 ./tools/vnc-probe.py --host $(VNC_HOST) --port $(VNC_PORT) --motion 12

# ----------------------------------------------------------------------------
# JIT benchmarks
# ----------------------------------------------------------------------------

ITERATIONS ?= 20000000
jit-microbench: build ## Tight-loop JIT vs interpreter throughput (default ITERATIONS=20M)
	@ITERATIONS=$(ITERATIONS) ./tools/jit-microbench.sh

jit-oracle-bisect: build ## Diff JIT vs interp REGDUMP for a hex blob: make jit-oracle-bisect HEX="e388 e214"
	@./tools/jit-oracle-bisect.sh "$(HEX)"
