# Mosura OS build
#   make sim      core -> sim/mosura.wasm (browser simulator)
#   make device   core -> build/mosura-m7.o (Cortex-M7 relocatable, for the patcher)
#   make test     headless core tests (node)
#   make shots    screenshots of every screen into docs/img
CLANG  ?= clang
CORE   := $(wildcard core/src/*.c)
HDRS   := $(wildcard core/src/*.h core/include/*.h)
WARN   := -Wall -Wextra -Wno-unused-parameter -Werror=implicit-function-declaration
EXPORTS := mos_init mos_tick mos_key mos_pad mos_encoder mos_ctrl mos_render \
           mos_leds mos_bank mos_set_sample sim_buf sim_fb_bytes sim_leds_size

.PHONY: all sim device test shots bundle clean
all: sim device

sim: sim/mosura.wasm
sim/mosura.wasm: $(CORE) $(HDRS) sim/sim_api.c
	$(CLANG) --target=wasm32 -O2 -std=c99 -ffreestanding -nostdlib $(WARN) \
	  -Icore/include -fno-builtin \
	  -Wl,--no-entry -Wl,--allow-undefined -Wl,-z,stack-size=262144 \
	  $(addprefix -Wl$(comma)--export=,$(EXPORTS)) \
	  -o $@ $(CORE) sim/sim_api.c

device: build/mosura-m7.o
build/mosura-m7.o: $(CORE) $(HDRS) port/sp404/port.c
	@mkdir -p build/m7
	for f in $(CORE) port/sp404/port.c; do \
	  $(CLANG) --target=thumbv7em-none-eabihf -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard \
	    -Os -std=c99 -ffreestanding -fno-builtin -ffunction-sections -fdata-sections \
	    -fno-common $(WARN) -Icore/include -c $$f -o build/m7/$$(basename $$f .c).o || exit 1; \
	done
	ld.lld -r build/m7/*.o -o $@
	@llvm-size -A $@ | awk '$$1 ~ /^\.(text|rodata|data|bss)/ {split($$1, a, "."); s[a[2]] += $$2} END {for (k in s) printf "  %-8s %8d bytes\n", k, s[k]}'

test: sim
	node tools/test_core.mjs
	python3 tools/mospatch.py selftest

shots: sim
	node tools/shots.mjs

bundle: sim
	python3 tools/bundle_sim.py

clean:
	rm -rf build sim/mosura.wasm

comma := ,
