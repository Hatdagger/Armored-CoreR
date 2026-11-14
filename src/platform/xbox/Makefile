# Original Xbox build of the engine core (nxdk). Run with NXDK_DIR set:
#   make NXDK_DIR=/path/to/nxdk
ACR = $(CURDIR)/../../..
XBE_TITLE = Armored-CoreR
GEN_XISO = $(XBE_TITLE).iso
SRCS = $(CURDIR)/main.c \
       $(ACR)/src/core/acr_clock.c $(ACR)/src/core/acr_pools.c \
       $(ACR)/src/gen1/acr_gen1_compile.c $(ACR)/src/gen1/acr_gen1_fit.c \
       $(ACR)/src/gen1/acr_gen1_anim.c $(ACR)/src/gen1/acr_gen1_sim.c \
       $(ACR)/src/gen1/acr_gen1_battle.c $(ACR)/src/render/acr_light.c
CFLAGS += -I$(ACR)/src/core -I$(ACR)/src/gen1 -I$(ACR)/src/render
NXDK_DIR ?= $(CURDIR)/../../../../nxdk
include $(NXDK_DIR)/Makefile
