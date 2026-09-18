#####################################################
# Game: Tourist Trophy
#
# The only game-aware build file for this game. The root makefile knows
# nothing about Tourist Trophy; it finds this by globbing source/*/game.mk.
#####################################################

GAME_LONGNAME := Tourist Trophy

# ---- regions ---------------------------------------------------------------
# All three retail builds are resolved. Adding a region means adding a
# BUILD_/ELF_/BASE_ trio here plus its Targets/ header - no C changes.
REGIONS        := US EU JP
DEFAULT_REGION := US

BUILD_US := SCUS-97502
ELF_US   := SCUS_975.02
BASE_US  := 0x7A6A00

BUILD_EU := SCES-53372
ELF_EU   := SCES_533.72
BASE_EU  := 0x7AA800

BUILD_JP := SCPS-15105
ELF_JP   := SCPS_151.05
BASE_JP  := 0x798380

# Bytes reserved for the plugin. Tourist Trophy has no dead region to borrow,
# so it takes this from the game's generic pool - see source/tt/main.c.
PLUGIN_RESERVE := 0x20000

# The injector hooks startup at crt0's `ei` (0x1001F8 in every build), which
# makes the next word the hook's delay slot - and that word is crt0's call into
# the game's early setup, which goes on to POOL_SETUP_FUNC. A jump in a delay
# slot is skipped by PCSX2 but taken by a PS2, where it kept init() from ever
# running. The build makes the slot a nop (tools/startup_slot.py) and INVOKER
# makes the call instead, after init() - see main.c.
STARTUP_SLOT_US := 0x1001FC
STARTUP_CALL_US := 0x45D598
STARTUP_SLOT_EU := 0x1001FC
STARTUP_CALL_EU := 0x4311B8
STARTUP_SLOT_JP := 0x1001FC
STARTUP_CALL_JP := 0x4574F0

# ---- knobs -----------------------------------------------------------------
HOSTFS_PRINT       := 1
PRINT_HOSTFS_READS := 1
USE_DEV_RAM        := 0

# TT links at the pool base, inside the segment whose declared memsz covers it,
# so the injected ELF gets its .bss tail clamped. See tools/clamp_bss.py.
CLAMP_BSS          := 1

HOST_DIR           := VOL_extract

# Region selection is handled by the shared seam - see source/core/Target.h.
GAME_DEFS := -DHOSTFS_DIR='"$(HOST_DIR)"'

# ---- sources ---------------------------------------------------------------
# ORDER IS SIGNIFICANT - see the note in source/games/gt4o/game.mk, which also
# explains the split: GAME_SRCS go into every build, GAME_SRCS_pcsx2 only into
# for:pcsx2 builds, because HostFS needs PCSX2's host: device.
GAME_SRCS := \
	source/games/tt/main.c \
	source/core/ps2/Memory.c \
	source/core/hooks/HOutput.c \
	source/core/game/IO.c \
	source/core/game/String.c \
	source/core/util/String.c \
	source/core/ps2/Sio.c \
	source/core/hooks/MakerList.c \
	source/games/tt/MakerNames.c

GAME_SRCS_pcsx2 := \
	source/core/hooks/HostFs.c \
	source/core/game/FileDevice.c
