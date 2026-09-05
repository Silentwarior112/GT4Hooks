#####################################################
# GT4Hooks - shared build driver
#
# This file knows nothing about any particular game. It discovers games by
# globbing source/games/*/game.mk, and everything game-specific lives in that
# file and in the game's Targets/ headers.
#
#   make GAME=gt4o
#   make GAME=tt REGION=EU
#   make GAME=tt print-config
#   make GAME=tt clean
#
# Normally you would use build.bat, which drives this and then runs the plugin
# injector:
#
#   build gt4o US
#   build tt EU
#####################################################

# ---- game discovery -------------------------------------------------------
GAMES := $(sort $(patsubst source/games/%/game.mk,%,$(wildcard source/games/*/game.mk)))

ifeq ($(GAME),)
$(error No GAME set. Available games: $(GAMES).  Try: make GAME=<name>, or use build.bat)
endif

GAME_DIR := source/games/$(GAME)
ifeq ($(wildcard $(GAME_DIR)/game.mk),)
$(error Unknown game '$(GAME)'. Available games: $(GAMES))
endif

# ---- the game supplies its regions, sources and knobs ---------------------
include $(GAME_DIR)/game.mk

REGION ?= $(DEFAULT_REGION)

BUILD        := $(BUILD_$(REGION))
GAME_ELF     := $(ELF_$(REGION))
BASE_ADDRESS := $(BASE_$(REGION))

ifeq ($(BUILD),)
$(error Unknown region '$(REGION)' for game '$(GAME)'. Available regions: $(REGIONS))
endif

# ---- things that live OUTSIDE this repository -------------------------------
# Nothing binary is kept in the repo: not the SDK, not the base images, not the
# build output. Both of these can be overridden from the environment or on the
# make command line if your layout differs. See the README.
PS2SDK_HOME ?= ../ps2sdk-main
WORK        ?= ../GT4Hooks-work

# ---- output layout ---------------------------------------------------------
# Per game AND per region, so building one never clobbers another's objects.
OUTDIR := $(WORK)/out/$(GAME)/$(BUILD)
OBJDIR := $(OUTDIR)/obj
EE_BIN := $(OUTDIR)/plugin.elf
EE_MAP := $(OUTDIR)/plugin.map

# Object names are the source path flattened, so sources from different
# directories cannot collide in one flat obj dir.
OBJ_OF = $(OBJDIR)/$(subst /,_,$(basename $(1))).o
EE_OBJS := $(foreach s,$(GAME_SRCS),$(call OBJ_OF,$(s)))

# ---- flags -----------------------------------------------------------------
NEWLIB_NANO = 0
KERNEL_NOPATCH = 0
# The plugin links against nothing from the SDK: every libc and kernel call it
# makes goes to the GAME's own copy through a function pointer. Current ps2sdk
# no longer ships libps2sdkc / libkernel-nopatch, which is what
# eemakefile.eeglobal's NODEFAULTLIBS path asks for, so both knobs stay off.

EE_LINKFILE = mk/linkfile

# -Isource lets shared headers be reached the same way from any depth, e.g.
# "core/ps2/Memory.h", regardless of how deeply a game nests its sources.
# The Targets include path plus TARGET_HEADER below form the seam described in
# source/core/Target.h: shared code includes "core/Target.h" and lands on the
# selected build's address header without naming a game or region.
# eemakefile.eeglobal appends to EE_INCS, so this must be set before it runs.
EE_INCS := -Isource -I$(GAME_DIR) -I$(GAME_DIR)/Targets

EE_CFLAGS = -fno-zero-initialized-in-bss -O0 \
	-DTARGET_HEADER='"$(BUILD).h"' \
	-DHOSTFS_PRINT=$(HOSTFS_PRINT) \
	-DPRINT_HOSTFS_READS=$(PRINT_HOSTFS_READS) \
	$(GAME_DEFS)

ifeq ($(USE_DEV_RAM),1)
  EE_CFLAGS += -DUSE_DEV_RAM
endif

EE_LDFLAGS = -Wl,--entry=INVOKER -Wl,-Map,$(EE_MAP) \
	-Wl,'--defsym=BASE_ADDRESS=$(BASE_ADDRESS)' \
	-ffunction-sections -Wl,--gc-sections -fdata-sections

all: $(EE_BIN)

# ---- build.bat reads these; keep the names stable --------------------------
.PHONY: print-config games regions clean
print-config:
	@echo GAME=$(GAME)
	@echo REGION=$(REGION)
	@echo BUILD=$(BUILD)
	@echo GAME_ELF=$(GAME_ELF)
	@echo BASE_ADDRESS=$(BASE_ADDRESS)
	@echo OUTDIR=$(OUTDIR)
	@echo OBJDIR=$(OBJDIR)
	@echo EE_BIN=$(EE_BIN)
	@echo CLAMP_BSS=$(CLAMP_BSS)
	@echo PLUGIN_RESERVE=$(PLUGIN_RESERVE)
	@echo REGIONS=$(REGIONS)
	@echo WORK=$(WORK)
	@echo PS2SDK_HOME=$(PS2SDK_HOME)

games:
	@echo $(GAMES)

regions:
	@echo $(REGIONS)

clean:
	-rm -rf $(OUTDIR)

PS2SDK = $(PS2SDK_HOME)/ps2sdk
include $(PS2SDK)/samples/Makefile.pref
include mk/eemakefile.eeglobal

# ---- per-source compile rules ---------------------------------------------
# Generated so each object lands in OBJDIR under its flattened name. These are
# a different target pattern from eemakefile.eeglobal's `%.o: %.c`, so that
# generic rule simply never fires.
#
# The object directory is created once at parse time rather than per recipe, so
# the rules stay quiet whichever shell make picks. build.bat also creates it, so
# this only matters when make is invoked directly.
$(shell mkdir -p "$(OBJDIR)" 2>/dev/null)

define GAME_OBJ_RULE
$(call OBJ_OF,$(1)): $(1)
	$$(EE_CC) $$(EE_CFLAGS) $$(EE_INCS) -c $$< -o $$@
endef
$(foreach s,$(GAME_SRCS),$(eval $(call GAME_OBJ_RULE,$(s))))
