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
# ORDER IS SIGNIFICANT - see the note in source/gt4/game.mk.
GAME_SRCS := \
	source/games/tt/main.c \
	source/core/ps2/Memory.c \
	source/core/hooks/HostFs.c \
	source/core/hooks/HOutput.c \
	source/core/game/IO.c \
	source/core/game/FileDevice.c \
	source/core/game/String.c \
	source/core/util/String.c \
	source/core/ps2/Sio.c
