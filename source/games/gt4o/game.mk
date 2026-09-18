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

# The injector hooks startup at crt0's `ei` (0x1001F8), which makes the next
# word the hook's delay slot - and that word is crt0's `jal 0x4DA490`, the
# game's early setup, which builds the pool. A jump in a delay slot is skipped
# by PCSX2 but taken by a PS2, where it kept init() from ever running. The
# build makes the slot a nop (tools/startup_slot.py) and INVOKER makes the call
# instead, after init() - see main.c.
STARTUP_SLOT_US := 0x1001FC
STARTUP_CALL_US := 0x4DA490

# More room, in code the game can never run: the Medius SDK's billing client
# (MediusBilling.c and its purchase cache mbill_cache.c), which GT4 Online links
# but never calls, and a run of zero padding in .text - 34,040 bytes in three
# runs, proven unreachable by a whole-image reachability analysis and checked
# again independently (see the target header). The build writes the caves'
# bytes over that code in place (tools/inject_cave2.py), so the game's heap is
# exactly retail size. Which objects go to which cave is decided in linkfile,
# in this directory; an object goes whole into one cave.
CAVE2_US       := 0x48D780
CAVE2_RESERVE  := 0x38E0
CAVE2B_US      := 0x494540
CAVE2B_RESERVE := 0x36D8
CAVE2C_US      := 0x5A8AC0
CAVE2C_RESERVE := 0x1540
GAME_LINKFILE  := $(GAME_DIR)/linkfile

# The dev RAM reserve, for pcsx2 builds. With PCSX2's 128 MB option on, init()
# moves the game's pool up into the extra RAM, and the low RAM it used to cover
# is the plugin's: 8 MB of it from just past .bss, loaded straight from the ELF
# like any other segment. Everything built from source/games/gt4o/reserve/ is
# linked there, and runs only when the extra RAM is really present - see
# DevRam.c. A ps2 build has no reserve sources, so nothing is placed there.
DEVRAM_US      := 0x8D1000
DEVRAM_RESERVE := 0x800000

# ---- knobs -----------------------------------------------------------------
# Print host filesystem activity to the TTY.
HOSTFS_PRINT       := 1

# Also print every read. Files arrive in 0x800 chunks, so this is very noisy.
# NOTE: the old GT4 makefile declared this but never passed it to the compiler,
# so it was dead. It is passed now, and pinned to 0 to keep behaviour unchanged.
PRINT_HOSTFS_READS := 0

# No .bss clamp: every cave is inside the file image and written in place.
CLAMP_BSS          := 0

# Folder holding the loose extracted files, relative to the host filesystem root.
HOST_DIR           := VOL_extract

GAME_DEFS := -DHOSTFS_DIR='"$(HOST_DIR)"'

# ---- sources ---------------------------------------------------------------
# ORDER IS SIGNIFICANT: it determines section layout, and therefore the exact
# bytes of the linked plugin. Keep new entries at the end.
#
# GAME_SRCS go into every build, console and PCSX2 alike. GAME_SRCS_pcsx2 go
# only into for:pcsx2 builds, after them:
#   HostFs.c, FileDevice.c   read files through PCSX2's host: device, which a
#                            console does not have (FileDevice.c is HostFS's
#                            own helper; nothing else uses it)
#   DevRam.c                 PCSX2's 128 MB mode
#   reserve/                 the code that lives in the dev RAM reserve
#   LicenseReplays.c         license demo replays from replay/license_pcsx2
GAME_SRCS := \
	source/games/gt4o/main.c \
	source/core/ps2/Memory.c \
	source/core/hooks/HOutput.c \
	source/games/gt4o/CameraSys.c \
	source/games/gt4o/Adhoc.c \
	source/games/gt4o/MStorage.c \
	source/games/gt4o/MCarGarage.c \
	source/core/game/IO.c \
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
	source/games/gt4o/Adhoc/MMyModule.c \
	source/core/hooks/MakerList.c \
	source/games/gt4o/MakerNames.c \
	source/games/gt4o/RaceHud.c \
	source/games/gt4o/RaceAspect.c \
	source/games/gt4o/HeapWatch.c \
	source/games/gt4o/RaceGpb.c

GAME_SRCS_pcsx2 := \
	source/core/hooks/HostFs.c \
	source/core/game/FileDevice.c \
	source/games/gt4o/DevRam.c \
	source/games/gt4o/reserve/Reserve.c \
	source/games/gt4o/reserve/MemDiag.c \
	source/games/gt4o/LicenseReplays.c
