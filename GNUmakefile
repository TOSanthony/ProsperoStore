# ProsperoStore - App configuration on the unmodified boilerplate build.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

APP_DEFINITIONS := GL_GLEXT_PROTOTYPES=1 STORE_NATIVE_CURL=1 HUI_EXTERNAL_LOG=1
PACBREW_PACKAGES := libcurl
APP_INCLUDE_PATHS := src .deps/ps5-opengl/current/include
APP_STATIC_ARCHIVES := .deps/ps5-opengl/libps5opengl-group.a
APP_IMPORT_STUBS := .deps/ps5-opengl/current/lib/libSceAgc.so .deps/ps5-opengl/current/lib/libSceAgcDriver.so build/system-keyboard/libSceCommonDialog.so
APP_WRAP_SYMBOLS := malloc calloc realloc free posix_memalign malloc_usable_size
APP_HEAP_SIZE := 0x10000000
APP_ROOT_FILES := build/sandbox-elevation/sandbox-elevator.elf
export ELEVATION_TITLE_ID := PPSA99000
DEVELOPMENT ?= 0
ifeq ($(DEVELOPMENT),1)
APP_DEFINITIONS += STORE_DEVELOPMENT=1
ifeq ($(SANDBOX_CONTROL),1)
APP_DEFINITIONS += STORE_SANDBOX_CONTROL=1
endif
endif

include Makefile

.PHONY: opengl host-snapshots foundations-check test-store
opengl:
	@bash tools/prepare-opengl.sh
app ffpkg ffpfsc packages: opengl sandbox-elevation-helper system-keyboard-imports

.PHONY: system-keyboard-imports
system-keyboard-imports:
	@bash examples/system-keyboard/build-import.sh >/dev/null

host-snapshots:
	@bash tools/store-host.sh

foundations-check:
	@python3 tools/check-foundations.py

test-store:
	@bash tools/store-test.sh

test: foundations-check test-store
