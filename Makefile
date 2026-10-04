# Slippy's macOS builds (arm64), Command Line Tools only - no Xcode. Windows
# builds both plug-ins with CMakeLists.txt.
#
# Slippy for Illustrator (illustrator/ + shared/). Same recipe as RAGE / AA-ReColor.
#   make            build + sign build/Slippy.aip (ad hoc; SIGN_ID="Developer ID ..." to sign for real,
#                   with SIGN_FLAGS="--timestamp --options runtime" to notarize)
#   make install    copy into Illustrator's Plug-ins folder (quit Illustrator first;
#                   the first install needs sudo - the folder is root-owned)
#   make preview    the panel + Slippy in a plain window with made-up calls
#   make clean
#
# Slippy for Photoshop (photoshop/ + shared/):
#   make photoshop          build/ps/Slippy.plugin (arm64, signed ad hoc) and build/ps/Slippy.ccx (the docked panel)
#   make install-photoshop  the plug-in into Photoshop's Plug-ins/Slippy folder (quit Photoshop first; the
#                           first time, that folder must exist and be yours - see the README), the panel
#                           with Adobe's plug-in installer
# The earlier UXP + Node version (photoshop/uxp):
#   make photoshop-uxp / install-photoshop-uxp / test-photoshop-uxp

SDK        ?= $(HOME)/Developer/AdobeIllustratorSDK
AI_APP     ?= /Applications/Adobe Illustrator 2026
SIGN_ID    ?= -
SIGN_FLAGS ?= --timestamp=none

NAME       := Slippy
BUILD      := build
OBJDIR     := $(BUILD)/obj
BUNDLE     := $(BUILD)/$(NAME).aip
EXE        := $(BUNDLE)/Contents/MacOS/$(NAME)

API        := $(SDK)/illustratorapi
COMMON     := $(SDK)/samplecode/common

SOURCES := \
	illustrator/SlippyPlugin.cpp \
	illustrator/SlippySuites.cpp \
	illustrator/Commands.cpp \
	illustrator/Overlay.cpp \
	shared/CrashLog.cpp \
	shared/Mcp.cpp \
	illustrator/Narrate.cpp \
	shared/Server.cpp \
	shared/Json.cpp \
	shared/Platform.cpp \
	illustrator/Raster.cpp \
	illustrator/CmdCatalog.cpp \
	illustrator/CmdSymbols.cpp \
	illustrator/CmdView.cpp \
	illustrator/CmdPaint.cpp \
	illustrator/CmdAppearance.cpp \
	illustrator/CmdShapes.cpp \
	illustrator/CmdText.cpp \
	illustrator/CmdDocument.cpp \
	$(COMMON)/source/Main.cpp \
	$(COMMON)/source/Plugin.cpp \
	$(COMMON)/source/Suites.cpp \
	$(COMMON)/source/AppContext.cpp \
	$(COMMON)/source/IllustratorSDK.cpp \
	$(API)/illustrator/IAIUnicodeString.cpp \
	$(API)/illustrator/IAIFilePath.cpp \
	$(API)/illustrator/IAIArtboards.cpp \
	$(API)/illustrator/AIAssert.cpp \
	$(API)/ate/IText.cpp \
	$(API)/ate/IThrowException.cpp

TERMINAL   := terminal.html xterm.js xterm.css addon-fit.js

MM_SOURCES := \
	illustrator/SlippyPanel.mm \
	shared/SlippyPanelView.mm \
	shared/Terminal.mm

OBJECTS := $(addprefix $(OBJDIR)/,$(notdir $(SOURCES:.cpp=.o) $(MM_SOURCES:.mm=.o)))
vpath %.cpp $(sort $(dir $(SOURCES)))
vpath %.mm $(sort $(dir $(MM_SOURCES)))

CXX      := clang++
ARCH     := -arch arm64 -mmacosx-version-min=12.0
INCLUDES := -Iillustrator -Ishared -I$(BUILD) -I$(COMMON)/includes \
	-I$(API)/illustrator -I$(API)/illustrator/actions -I$(API)/pica_sp -I$(API)/ate
CXXFLAGS := $(ARCH) -std=c++17 -stdlib=libc++ -x objective-c++ -O2 -g \
	-fvisibility=hidden -fvisibility-inlines-hidden \
	-Wno-deprecated-declarations -Wno-unknown-pragmas \
	-include $(COMMON)/includes/IllustratorSDKRelease.pch \
	$(INCLUDES) -MMD -MP
LDFLAGS  := $(ARCH) -bundle -stdlib=libc++ -framework Cocoa -framework QuartzCore -framework CoreFoundation -framework ImageIO -framework WebKit

.PHONY: all clean install preview photoshop install-photoshop photoshop-uxp install-photoshop-uxp test-photoshop-uxp
all: $(BUILD)/.signed

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.mm | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -fobjc-arc -c $< -o $@

$(OBJDIR):
	mkdir -p $@

# The menu commands, action events and tools the SDK names (menu.list...).
$(BUILD)/SdkCatalog.inc: illustrator/sdk_catalog.py
	mkdir -p $(BUILD)
	python3 illustrator/sdk_catalog.py "$(SDK)" $@
$(OBJDIR)/CmdCatalog.o: $(BUILD)/SdkCatalog.inc

# The agents' logos for the panel (shared/resources/agents/*.svg).
$(BUILD)/AgentLogos.inc: tools/agent_logos.py $(wildcard shared/resources/agents/*.svg)
	mkdir -p $(BUILD)
	python3 tools/agent_logos.py shared/resources/agents $@
$(OBJDIR)/SlippyPanelView.o: $(BUILD)/AgentLogos.inc

$(EXE): $(OBJECTS)
	mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@

$(BUILD)/plugin.pipl:
	mkdir -p $(BUILD)
	cd $(BUILD) && python3 $(SDK)/tools/pipl/create_pipl.py -input '[{"name":"$(NAME)"}]'

# Illustrator opens <Executable>.rsrc when loading a plug-in and fails
# ("Plugin issues detected") if it is missing, even when it is empty.
$(BUILD)/$(NAME).rsrc: illustrator/resources/$(NAME).r
	mkdir -p $(BUILD)
	Rez -useDF -o $@ $<

$(BUILD)/.signed: $(EXE) $(wildcard shared/resources/terminal/*) illustrator/resources/Info.plist $(wildcard illustrator/resources/raw/*) $(BUILD)/plugin.pipl $(BUILD)/$(NAME).rsrc
	mkdir -p $(BUNDLE)/Contents/Resources/pipl
	cp $(BUILD)/$(NAME).rsrc $(BUNDLE)/Contents/Resources/$(NAME).rsrc
	cp illustrator/resources/Info.plist $(BUNDLE)/Contents/Info.plist
	printf 'ARPIART5' > $(BUNDLE)/Contents/PkgInfo
	cp $(BUILD)/plugin.pipl $(BUNDLE)/Contents/Resources/pipl/plugin.pipl
	mkdir -p $(BUNDLE)/Contents/Resources/svg $(BUNDLE)/Contents/Resources/txt
	cp illustrator/resources/raw/slippy_panel_light.svg illustrator/resources/raw/slippy_panel_dark.svg $(BUNDLE)/Contents/Resources/svg/
	cp illustrator/resources/raw/IDToFile.txt $(BUNDLE)/Contents/Resources/txt/IDToFile.txt
	mkdir -p $(BUNDLE)/Contents/Resources/terminal
	cp $(addprefix shared/resources/terminal/,$(TERMINAL)) $(BUNDLE)/Contents/Resources/terminal/
	codesign --force --sign "$(SIGN_ID)" $(SIGN_FLAGS) $(BUNDLE)
	codesign --verify --strict $(BUNDLE)
	touch $@

# Plug-ins.localized is root-owned; after the first sudo install the bundle's
# own files are ours, so ditto can refresh it in place without sudo.
install: all
	ditto $(BUNDLE) "$(AI_APP)/Plug-ins.localized/$(NAME).aip"

# The panel in a plain window with made-up calls - watch Slippy without Illustrator.
$(BUILD)/SlippyPreview: $(BUILD)/AgentLogos.inc shared/Preview.mm shared/SlippyPanelView.mm shared/SlippyPanelView.h shared/FrogShape.h shared/AgentLogo.h shared/Terminal.mm shared/Terminal.h illustrator/Narrate.cpp shared/Json.cpp
	mkdir -p $(BUILD)
	$(CXX) $(ARCH) -std=c++17 -fobjc-arc -O2 -Ishared -I$(BUILD) shared/Preview.mm shared/SlippyPanelView.mm shared/Terminal.mm illustrator/Narrate.cpp shared/Json.cpp \
		-framework Cocoa -framework QuartzCore -framework WebKit -o $@

preview: $(BUILD)/SlippyPreview
	./$(BUILD)/SlippyPreview

# ---- Slippy for Photoshop (native)
PS_SDK     ?= $(HOME)/Developer/AdobePhotoshopSDK/pluginsdk/photoshopapi
PS_APP     ?= /Applications/Adobe Photoshop 2026
PS_BUILD   := $(BUILD)/ps
PS_OBJDIR  := $(PS_BUILD)/obj
PS_BUNDLE  := $(PS_BUILD)/$(NAME).plugin
PS_EXE     := $(PS_BUNDLE)/Contents/MacOS/$(NAME)
PS_SOURCES := photoshop/PsPlugin.cpp photoshop/PsCommands.cpp photoshop/PsDescriptor.cpp photoshop/PsSuites.cpp photoshop/PsNarrate.cpp \
	photoshop/PsDockedPanel.cpp \
	shared/Server.cpp shared/Mcp.cpp shared/Json.cpp shared/Platform.cpp shared/CrashLog.cpp
PS_MM      := photoshop/PsPanel.mm shared/SlippyPanelView.mm shared/Terminal.mm
PS_OBJECTS := $(addprefix $(PS_OBJDIR)/,$(notdir $(PS_SOURCES:.cpp=.o) $(PS_MM:.mm=.o)))
PS_FLAGS   := $(ARCH) -std=c++17 -stdlib=libc++ -x objective-c++ -O2 -g -fvisibility=hidden -fvisibility-inlines-hidden \
	-Wno-deprecated-declarations -Wno-multichar -Iphotoshop -Ishared -I$(BUILD) -I$(PS_SDK)/photoshop -I$(PS_SDK)/pica_sp -MMD -MP
vpath %.cpp photoshop
vpath %.mm photoshop

$(PS_OBJDIR)/%.o: %.cpp | $(PS_OBJDIR)
	$(CXX) $(PS_FLAGS) -c $< -o $@

$(PS_OBJDIR)/%.o: %.mm | $(PS_OBJDIR)
	$(CXX) $(PS_FLAGS) -fobjc-arc -c $< -o $@

$(PS_OBJDIR):
	mkdir -p $@

$(PS_OBJDIR)/SlippyPanelView.o: $(BUILD)/AgentLogos.inc

$(PS_EXE): $(PS_OBJECTS)
	mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@

$(PS_BUILD)/.signed: $(PS_EXE) photoshop/Info.plist photoshop/PiPLs.json $(wildcard shared/resources/terminal/*)
	mkdir -p $(PS_BUNDLE)/Contents/Resources/terminal
	cp photoshop/Info.plist $(PS_BUNDLE)/Contents/Info.plist
	printf '8LIZ8BIM' > $(PS_BUNDLE)/Contents/PkgInfo
	cp photoshop/PiPLs.json $(PS_BUNDLE)/Contents/Resources/PiPLs.json
	cp $(addprefix shared/resources/terminal/,$(TERMINAL)) $(PS_BUNDLE)/Contents/Resources/terminal/
	codesign --force --sign "$(SIGN_ID)" $(SIGN_FLAGS) $(PS_BUNDLE)
	codesign --verify --strict $(PS_BUNDLE)
	touch $@

# The docked panel (UXP), with the agents' logos (tools/package_panel.py).
PS_CCX := $(PS_BUILD)/$(NAME).ccx
$(PS_CCX): tools/package_panel.py $(wildcard photoshop/panel/* photoshop/panel/icons/* shared/resources/agents/*.svg)
	python3 tools/package_panel.py $@

photoshop: $(PS_BUILD)/.signed $(PS_CCX)

UPIA := /Library/Application Support/Adobe/Adobe Desktop Common/RemoteComponents/UPI/UnifiedPluginInstallerAgent/UnifiedPluginInstallerAgent.app/Contents/MacOS/UnifiedPluginInstallerAgent

install-photoshop: photoshop
	@test -w "$(PS_APP)/Plug-ins/Slippy" || { echo "Make Photoshop's plug-in folder yours first (once):"; \
		echo '  sudo mkdir -p "$(PS_APP)/Plug-ins/Slippy" && sudo chown $$USER "$(PS_APP)/Plug-ins/Slippy"'; exit 1; }
	ditto $(PS_BUNDLE) "$(PS_APP)/Plug-ins/Slippy/$(NAME).plugin"
	@# The installer won't replace a panel of the same version: take the old one out first.
	-"$(UPIA)" --remove "$(NAME)" >/dev/null 2>&1
	"$(UPIA)" --install "$(CURDIR)/$(PS_CCX)"

# ---- the earlier UXP + Node version
photoshop-uxp:
	photoshop/uxp/scripts/package.sh

install-photoshop-uxp:
	photoshop/uxp/scripts/install.sh

test-photoshop-uxp:
	cd photoshop/uxp/bridge && npm ci && npm test

clean:
	rm -rf $(BUILD) photoshop/uxp/dist

-include $(OBJECTS:.o=.d) $(PS_OBJECTS:.o=.d)
