# PohonMC Launcher - convenience wrapper around the CMake build.
# Works with GNU Make 3.81 (macOS), 4.x (Linux) and MSYS2 (Windows).

# ---------------------------------------------------------------- variables --
PLATFORM   ?=
CONFIG     ?= Debug
BUILD_DIR  ?= build
INSTALL_DIR ?= install
JOBS       ?=
CMAKE_ARGS ?=
RUN_ARGS   ?=

# Platform words that may be given positionally (make build macos).
PLATFORM_WORDS := macos linux windows deb

# ---------------------------------------------------------- host detection --
HOST_UNAME := $(shell uname -s)
ifneq ($(findstring Darwin,$(HOST_UNAME)),)
HOST_PLATFORM := macos
else ifneq ($(findstring Linux,$(HOST_UNAME)),)
HOST_PLATFORM := linux
else ifneq ($(filter MINGW% MSYS% CYGWIN%,$(HOST_UNAME)),)
HOST_PLATFORM := windows
else ifeq ($(OS),Windows_NT)
HOST_PLATFORM := windows
else
HOST_PLATFORM := unknown
endif

# Requested platform: positional word > PLATFORM=... > host.
POSITIONAL := $(filter $(PLATFORM_WORDS),$(MAKECMDGOALS))
ifneq ($(word 2,$(POSITIONAL)),)
$(error Give at most one platform word, got: $(POSITIONAL))
endif
ifneq ($(POSITIONAL),)
TARGET_PLATFORM := $(POSITIONAL)
else ifneq ($(PLATFORM),)
TARGET_PLATFORM := $(PLATFORM)
else
TARGET_PLATFORM := $(HOST_PLATFORM)
endif

# "deb" is Linux plus a .deb package step.
ifeq ($(TARGET_PLATFORM),deb)
TARGET_OS := linux
else
TARGET_OS := $(TARGET_PLATFORM)
endif

# ------------------------------------------------------------- cmake setup --
# LTO is slow to link; only enable it for Release (the presets always use ON).
ifeq ($(CONFIG),Release)
ENABLE_LTO ?= ON
else
ENABLE_LTO ?= OFF
endif

JOBS_FLAG := $(if $(JOBS),-j $(JOBS),)

# Cache variables shared by every configure path (mirrors the "base" preset).
CACHE_ARGS := -DENABLE_LTO=$(ENABLE_LTO) -DLauncher_ENABLE_JAVA_DOWNLOADER=ON

# Dependency lookup. With vcpkg (VCPKG_ROOT set) use the toolchain file,
# like the macos/windows_msvc presets. On macOS without vcpkg, fall back to
# Homebrew; libarchive is keg-only so it needs its own prefix hint.
ifneq ($(VCPKG_ROOT),)
DEP_ARGS := -DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
else ifeq ($(HOST_PLATFORM),macos)
BREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)
DEP_ARGS := "-DCMAKE_PREFIX_PATH=$(BREW_PREFIX);$(BREW_PREFIX)/opt/libarchive"
# The bundled Java helpers compile with -source 7, which JDK 21+ rejects, so
# prefer Homebrew's JDK 17 over whatever JAVA_HOME points at (override: JDK=...).
JDK ?= $(BREW_PREFIX)/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home
ifneq ($(wildcard $(JDK)/bin/javac),)
DEP_ARGS += -DJava_ROOT=$(JDK)
export JAVA_HOME := $(JDK)
endif
else
DEP_ARGS :=
endif

# Use the CMake preset only when it can be used as-is (vcpkg available and the
# preset's fixed binaryDir "build"); otherwise run plain cmake with equivalent
# cache variables.
ifeq ($(TARGET_OS),windows)
PRESET := windows_msvc
else
PRESET := $(TARGET_OS)
endif
ifneq ($(VCPKG_ROOT),)
ifeq ($(BUILD_DIR),build)
USE_PRESET := 1
endif
endif

ifeq ($(USE_PRESET),1)
CONFIGURE_CMD = cmake --preset $(PRESET) $(CACHE_ARGS) $(CMAKE_ARGS)
else
CONFIGURE_CMD = cmake -S . -B $(BUILD_DIR) -G "Ninja Multi-Config" $(CACHE_ARGS) $(DEP_ARGS) $(CMAKE_ARGS)
endif

# ------------------------------------------------------------ output paths --
OUT_DIR := $(BUILD_DIR)/$(CONFIG)
ifeq ($(TARGET_OS),macos)
APP_BUNDLE := $(OUT_DIR)/PohonMC.app
RUN_BIN := $(APP_BUNDLE)/Contents/MacOS/PohonMC
else ifeq ($(TARGET_OS),windows)
RUN_BIN := $(OUT_DIR)/pohonmc.exe
else
RUN_BIN := $(OUT_DIR)/pohonmc
endif

# Package metadata for "deb", derived from existing project files.
deb_ver = $(shell sed -n 's/^set.Launcher_VERSION_$(1) \([0-9]*\).*/\1/p' CMakeLists.txt)
DEB_NAME    = $(shell sed -n 's/^set.Launcher_APP_BINARY_NAME "\([^"]*\)".*/\1/p' CMakeLists.txt)
DEB_VERSION = $(call deb_ver,MAJOR).$(call deb_ver,MINOR).$(call deb_ver,PATCH)
METAINFO    = program_info/io.github.kodin00.PohonMCLauncher.metainfo.xml.in
DEB_MAINT   = $(shell sed -n '/<developer/,/<\/developer>/s:.*<name>\(.*\)</name>.*:\1:p' $(METAINFO))
DEB_SUMMARY = $(shell sed -n 's:.*<summary>\(.*\)</summary>.*:\1:p' $(METAINFO))
DEB_STAGE   = $(INSTALL_DIR)/deb-root

# ----------------------------------------------------------------- targets --
.DEFAULT_GOAL := help
.PHONY: help configure build run test package clean check-platform \
        package-macos package-linux package-windows package-deb $(PLATFORM_WORDS)

help:
	@echo "PohonMC Launcher - CMake wrapper"
	@echo
	@echo "Usage: make <target> [platform]   e.g. make build macos, make package deb"
	@echo
	@echo "Targets:"
	@echo "  help       Show this help (default)"
	@echo "  configure  Run the CMake configure step (inits submodules if empty)"
	@echo "  build      Configure if needed, then build"
	@echo "  run        Build, then launch the launcher (RUN_ARGS=... to pass args)"
	@echo "  test       Build, then run ctest"
	@echo "  package    Build, then package for the platform (see below)"
	@echo "  clean      Remove BUILD_DIR and INSTALL_DIR/deb-root"
	@echo
	@echo "Platforms (positional word or PLATFORM=...): macos linux windows deb"
	@echo "  Auto-detected from uname -s. Cross-compiling is not supported: a"
	@echo "  platform that differs from the host fails early. deb counts as linux."
	@echo
	@echo "Variables:"
	@echo "  PLATFORM    Override platform (default: host, now: $(HOST_PLATFORM))"
	@echo "  CONFIG      Debug|Release (default: Debug)"
	@echo "  BUILD_DIR   Build directory (default: build)"
	@echo "  INSTALL_DIR Package output directory (default: install)"
	@echo "  JOBS        Parallel jobs (default: cmake's choice)"
	@echo "  CMAKE_ARGS  Extra arguments for the configure step"
	@echo "  ENABLE_LTO  ON|OFF (default: ON for Release, OFF for Debug)"
	@echo "  JDK         JDK 7-19 home for the Java helpers (macOS default: Homebrew openjdk@17)"
	@echo "  RUN_ARGS    Arguments passed to the launcher by 'make run'"
	@echo
	@echo "Dependencies: if VCPKG_ROOT is set and BUILD_DIR=build, the CMake preset"
	@echo "is used; otherwise plain cmake is run (vcpkg toolchain if VCPKG_ROOT is"
	@echo "set, Homebrew prefixes on macOS). Re-run 'make configure' after changing"
	@echo "CMAKE_ARGS, ENABLE_LTO or the dependency setup. Java must be JDK 7-19"
	@echo "(javac -source 7); on macOS Homebrew's openjdk@17 is used if present."
	@echo
	@echo "Packages:"
	@echo "  macos    cmake --install -> $(INSTALL_DIR)/macos/PohonMC.app"
	@echo "  linux    cmake --install -> $(INSTALL_DIR)/linux (prefix tree)"
	@echo "  windows  cmake --install -> $(INSTALL_DIR)/windows (install tree)"
	@echo "  deb      Stage with prefix /usr, then dpkg-deb. The project has no CPack"
	@echo "           rules, so only Package/Version/Architecture/Maintainer/"
	@echo "           Description are generated (from project files). There is no"
	@echo "           Depends: line - runtime Qt libraries are not declared."
	@echo "           Needs dpkg-deb (Debian/Ubuntu)."

# Positional platform words are no-ops; they are read from MAKECMDGOALS above.
$(PLATFORM_WORDS):
	@:

check-platform:
	@if [ "$(HOST_PLATFORM)" = unknown ]; then \
	  echo "error: unsupported host OS '$(HOST_UNAME)'" >&2; exit 1; fi
	@case "$(TARGET_PLATFORM)" in macos|linux|windows|deb) ;; \
	  *) echo "error: unknown platform '$(TARGET_PLATFORM)' (use: $(PLATFORM_WORDS))" >&2; exit 1;; esac
	@if [ "$(TARGET_OS)" != "$(HOST_PLATFORM)" ]; then \
	  echo "error: cannot build for '$(TARGET_PLATFORM)' on a $(HOST_PLATFORM) host - cross-compiling is not supported" >&2; exit 1; fi

# Real file target: configure runs only when the build dir has no cache yet.
$(BUILD_DIR)/CMakeCache.txt: | check-platform
	@if [ ! -e libraries/libnbtplusplus/CMakeLists.txt ]; then \
	  echo "Initialising git submodules..."; git submodule update --init || exit 1; fi
	$(CONFIGURE_CMD)

configure: check-platform
	@if [ ! -e libraries/libnbtplusplus/CMakeLists.txt ]; then \
	  echo "Initialising git submodules..."; git submodule update --init || exit 1; fi
	$(CONFIGURE_CMD)

build: check-platform $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR) --config $(CONFIG) $(JOBS_FLAG)

run: build
	@test -x "$(RUN_BIN)" || { echo "error: $(RUN_BIN) not found" >&2; exit 1; }
	"$(RUN_BIN)" $(RUN_ARGS)

test: build
	ctest --test-dir $(BUILD_DIR) -C $(CONFIG) --output-on-failure -E '^example64|example$$' $(JOBS_FLAG)

package: package-$(TARGET_PLATFORM)

package-macos: build
	cmake --install $(BUILD_DIR) --config $(CONFIG) --prefix $(INSTALL_DIR)/macos
	@echo "Package: $(INSTALL_DIR)/macos/PohonMC.app"

package-linux: build
	cmake --install $(BUILD_DIR) --config $(CONFIG) --prefix $(INSTALL_DIR)/linux
	@echo "Package: $(INSTALL_DIR)/linux"

package-windows: build
	cmake --install $(BUILD_DIR) --config $(CONFIG) --prefix $(INSTALL_DIR)/windows
	@echo "Package: $(INSTALL_DIR)/windows"

package-deb: build
	@command -v dpkg-deb >/dev/null || { echo "error: dpkg-deb not found (apt install dpkg)" >&2; exit 1; }
	rm -rf $(DEB_STAGE)
	cmake --install $(BUILD_DIR) --config $(CONFIG) --prefix $(DEB_STAGE)/usr
	mkdir -p $(DEB_STAGE)/DEBIAN
	printf 'Package: %s\nVersion: %s\nArchitecture: %s\nMaintainer: %s\nDescription: %s\n' \
	  "$(DEB_NAME)" "$(DEB_VERSION)" "$$(dpkg --print-architecture)" "$(DEB_MAINT)" "$(DEB_SUMMARY)" \
	  > $(DEB_STAGE)/DEBIAN/control
	dpkg-deb --root-owner-group --build $(DEB_STAGE) $(INSTALL_DIR)/$(DEB_NAME)_$(DEB_VERSION)_$$(dpkg --print-architecture).deb
	@echo "Package: $(INSTALL_DIR)/$(DEB_NAME)_$(DEB_VERSION)_*.deb"

clean:
	rm -rf $(BUILD_DIR) $(DEB_STAGE)
