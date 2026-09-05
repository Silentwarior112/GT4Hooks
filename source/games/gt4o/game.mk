#####################################################
# Game: Gran Turismo 4 Online
#
# The only game-aware build file for this game. The root makefile knows
# nothing about GT4; it finds this by globbing source/*/game.mk.
#####################################################

GAME_LONGNAME := Gran Turismo 4 Online

# ---- regions ---------------------------------------------------------------
# Only the US Online build is resolved so far. Adding a region means adding a
# BUILD_/ELF_/BASE_ trio here plus its Targets/ header - no C changes.
REGIONS        := US
DEFAULT_REGION := US

BUILD_US := SCUS-97436
ELF_US   := SCUS_974.36
BASE_US  := 0x68A480

# Bytes available at BASE_ADDRESS. GT4 links over the static globals of the
# unused Omron wnn/fep Japanese IME, which run 0x68A480..0x69A790.
PLUGIN_RESERVE := 0x10310

# ---- knobs -----------------------------------------------------------------
# Print host filesystem activity to the TTY.
HOSTFS_PRINT       := 1

# Also print every read. Files arrive in 0x800 chunks, so this is very noisy.
# NOTE: the old GT4 makefile declared this but never passed it to the compiler,
# so it was dead. It is passed now, and pinned to 0 to keep behaviour unchanged.
PRINT_HOSTFS_READS := 0

# Patch the RAM-size global for a 128MB dev console. Also previously dead.
USE_DEV_RAM        := 0

# GT4 links below the pool it clears, so the injected ELF needs no .bss clamp.
CLAMP_BSS          := 0

# Folder holding the loose extracted files, relative to the host filesystem root.
HOST_DIR           := VOL_extract

GAME_DEFS := -DHOSTFS_DIR='"$(HOST_DIR)"'

# ---- sources ---------------------------------------------------------------
# ORDER IS SIGNIFICANT: it determines section layout, and therefore the exact
# bytes of the linked plugin. Keep new entries at the end.
GAME_SRCS := \
	source/games/gt4o/main.c \
	source/core/ps2/Memory.c \
	source/core/hooks/HostFs.c \
	source/core/hooks/HOutput.c \
	source/games/gt4o/CameraSys.c \
	source/games/gt4o/Adhoc.c \
	source/games/gt4o/MStorage.c \
	source/games/gt4o/MCarGarage.c \
	source/core/game/IO.c \
	source/core/game/FileDevice.c \
	source/core/game/String.c \
	source/games/gt4o/GameFunctions/Adhoc.c \
	source/games/gt4o/GameFunctions/MStorage.c \
	source/games/gt4o/GameFunctions/MCarGarage.c \
	source/games/gt4o/GameFunctions/Monitor.c \
	source/games/gt4o/GameFunctions/PolyphonyGL.c \
	source/core/util/String.c \
	source/core/ps2/Sio.c \
	source/games/gt4o/util/Path.c \
	source/games/gt4o/Adhoc/Decl.c \
	source/games/gt4o/Adhoc/MMyModule.c
