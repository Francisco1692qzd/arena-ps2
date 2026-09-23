# TEAM BATTLE ARENA 3D - PS2 Full Port (3 maps, oriented barriers, shop)
# Requires ps2dev toolchain: https://github.com/ps2dev/ps2dev
EE_BIN = team-battle-arena.elf
EE_OBJS = main.o
EE_LIBS = -lpad -lgs -ldma -lgskit -ldmakit -ldebug -lm -lc
EE_INCS = -I$(PS2SDK)/ports/include -I$(GSKIT)/include -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include
EE_LDFLAGS = -L$(PS2SDK)/ports/lib -L$(GSKIT)/lib -L$(PS2SDK)/ee/lib

all: $(EE_BIN)

%.o: %.c
	ee-gcc -O2 -G0 -Wall -g $(EE_INCS) -c $< -o $@

$(EE_BIN): $(EE_OBJS)
	ee-gcc -T$(PS2SDK)/ee/startup/linkfile -O2 -G0 -o $@ $^ $(EE_LDFLAGS) $(EE_LIBS)

clean:
	rm -f $(EE_OBJS) $(EE_BIN) $(EE_BIN).packed arena.iso

iso: $(EE_BIN)
	ps2-packer $(EE_BIN) $(EE_BIN).packed
	cp $(EE_BIN).packed TBAARENA.ELF
	mkisofs -o arena.iso SYSTEM.CNF TBAARENA.ELF
	rm TBAARENA.ELF
	@echo "ISO ready: arena.iso - PCSX2: CDVD -> ISO Selector -> arena.iso"
	@echo "OPL USB: copy arena.iso to USB:/DVD/"

run: $(EE_BIN)
	ps2client execee host:$(EE_BIN)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
