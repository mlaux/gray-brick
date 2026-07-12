CC = cc
CFLAGS = -Wall -Wextra -g -O1 -MMD -MP \
         -Ishim-include -I../src -I../compiler -I../compiler/musashi

BUILD = build

MUSASHI_DIR = ../compiler/musashi
MUSASHI_OBJS = $(MUSASHI_DIR)/m68kcpu.o $(MUSASHI_DIR)/m68kops.o \
               $(MUSASHI_DIR)/softfloat/softfloat.o

SRC_NAMES = dmg lcd lcd_cgb mbc cgb audio rom rom_patches
SRC_OBJS = $(SRC_NAMES:%=$(BUILD)/src_%.o)

COMP_NAMES = compiler emitters branches flags interop cb_prefix reg_loads \
             alu stack instructions timing
COMP_OBJS = $(COMP_NAMES:%=$(BUILD)/comp_%.o)

SYS6_OBJS = $(BUILD)/sys6_cache.o

HOST_NAMES = gb6run shims m68k_mem host_jit
HOST_OBJS = $(HOST_NAMES:%=$(BUILD)/host_%.o)

ALL_OBJS = $(SRC_OBJS) $(COMP_OBJS) $(SYS6_OBJS) $(HOST_OBJS)

gb6run: $(ALL_OBJS)
	$(CC) $(CFLAGS) -o $@ $(ALL_OBJS) $(MUSASHI_OBJS) -lm

$(BUILD)/src_%.o: ../src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/comp_%.o: ../compiler/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/sys6_cache.o: ../system6/cache.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/host_%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

make_smoke_rom: make_smoke_rom.c
	$(CC) -Wall -Wextra -o $@ $<

smoke.gb: make_smoke_rom
	./make_smoke_rom $@

-include $(ALL_OBJS:.o=.d)

clean:
	rm -rf $(BUILD) gb6run make_smoke_rom smoke.gb

# scoreboard: run the compat suite and diff against the committed baseline
fetch-roms:
	bash compat/fetch_roms.sh

compat: gb6run
	bash compat/run_compat.sh results.txt
	@diff -u compat/baseline.txt compat/results.txt \
		&& echo "compat: matches baseline"

# rewrite the committed baseline from the latest results
baseline: compat/results.txt
	cp compat/results.txt compat/baseline.txt

.PHONY: clean compat baseline fetch-roms
