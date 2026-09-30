# Slippy native Illustrator plug-in (arm64, macOS). Command Line Tools only -
# no Xcode, no Visual Studio. Same recipe as RAGE / AA-ReColor.
#   make            build + sign build/Slippy.aip (ad hoc; SIGN_ID="Developer ID ..." to sign for real,
#                   with SIGN_FLAGS="--timestamp --options runtime" to notarize)
#   make install    copy into Illustrator's Plug-ins folder (quit Illustrator first;
#                   the first install needs sudo - the folder is root-owned)
#   make preview    the panel + Slippy in a plain window with made-up calls
#   make clean
# Windows builds with CMakeLists.txt (see there).

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
	Source/SlippyPlugin.cpp \
	Source/SlippySuites.cpp \
	Source/Commands.cpp \
	Source/Overlay.cpp \
	Source/Mcp.cpp \
	Source/Narrate.cpp \
	Source/Server.cpp \
	Source/Json.cpp \
	Source/Platform.cpp \
	Source/Raster.cpp \
	Source/CmdCatalog.cpp \
	Source/CmdSymbols.cpp \
	Source/CmdView.cpp \
	Source/CmdPaint.cpp \
	Source/CmdAppearance.cpp \
	Source/CmdShapes.cpp \
	Source/CmdText.cpp \
	Source/CmdDocument.cpp \
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

MM_SOURCES := \
	Source/SlippyPanel.mm \
	Source/SlippyPanelView.mm

OBJECTS := $(addprefix $(OBJDIR)/,$(notdir $(SOURCES:.cpp=.o) $(MM_SOURCES:.mm=.o)))
vpath %.cpp $(sort $(dir $(SOURCES)))
vpath %.mm $(sort $(dir $(MM_SOURCES)))

CXX      := clang++
ARCH     := -arch arm64 -mmacosx-version-min=12.0
INCLUDES := -ISource -I$(BUILD) -I$(COMMON)/includes \
	-I$(API)/illustrator -I$(API)/illustrator/actions -I$(API)/pica_sp -I$(API)/ate
CXXFLAGS := $(ARCH) -std=c++17 -stdlib=libc++ -x objective-c++ -O2 -g \
	-fvisibility=hidden -fvisibility-inlines-hidden \
	-Wno-deprecated-declarations -Wno-unknown-pragmas \
	-include $(COMMON)/includes/IllustratorSDKRelease.pch \
	$(INCLUDES) -MMD -MP
LDFLAGS  := $(ARCH) -bundle -stdlib=libc++ -framework Cocoa -framework QuartzCore -framework CoreFoundation -framework ImageIO

.PHONY: all clean install preview
all: $(BUILD)/.signed

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.mm | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -fobjc-arc -c $< -o $@

$(OBJDIR):
	mkdir -p $@

# The menu commands, action events and tools the SDK names (menu.list...).
$(BUILD)/SdkCatalog.inc: tools/sdk_catalog.py
	mkdir -p $(BUILD)
	python3 tools/sdk_catalog.py "$(SDK)" $@
$(OBJDIR)/CmdCatalog.o: $(BUILD)/SdkCatalog.inc

$(EXE): $(OBJECTS)
	mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@

$(BUILD)/plugin.pipl:
	mkdir -p $(BUILD)
	cd $(BUILD) && python3 $(SDK)/tools/pipl/create_pipl.py -input '[{"name":"$(NAME)"}]'

# Illustrator opens <Executable>.rsrc when loading a plug-in and fails
# ("Plugin issues detected") if it is missing, even when it is empty.
$(BUILD)/$(NAME).rsrc: Resources/$(NAME).r
	mkdir -p $(BUILD)
	Rez -useDF -o $@ $<

$(BUILD)/.signed: $(EXE) Resources/Info.plist Resources/raw/slippy_panel_light.svg Resources/raw/slippy_panel_dark.svg Resources/raw/IDToFile.txt $(BUILD)/plugin.pipl $(BUILD)/$(NAME).rsrc
	mkdir -p $(BUNDLE)/Contents/Resources/pipl
	cp $(BUILD)/$(NAME).rsrc $(BUNDLE)/Contents/Resources/$(NAME).rsrc
	cp Resources/Info.plist $(BUNDLE)/Contents/Info.plist
	printf 'ARPIART5' > $(BUNDLE)/Contents/PkgInfo
	cp $(BUILD)/plugin.pipl $(BUNDLE)/Contents/Resources/pipl/plugin.pipl
	mkdir -p $(BUNDLE)/Contents/Resources/svg $(BUNDLE)/Contents/Resources/txt
	cp Resources/raw/slippy_panel_light.svg Resources/raw/slippy_panel_dark.svg $(BUNDLE)/Contents/Resources/svg/
	cp Resources/raw/IDToFile.txt $(BUNDLE)/Contents/Resources/txt/IDToFile.txt
	codesign --force --sign "$(SIGN_ID)" $(SIGN_FLAGS) $(BUNDLE)
	codesign --verify --strict $(BUNDLE)
	touch $@

# Plug-ins.localized is root-owned; after the first sudo install the bundle's
# own files are ours, so ditto can refresh it in place without sudo.
install: all
	ditto $(BUNDLE) "$(AI_APP)/Plug-ins.localized/$(NAME).aip"

# The panel in a plain window with made-up calls - watch Slippy without Illustrator.
$(BUILD)/SlippyPreview: Source/Preview.mm Source/SlippyPanelView.mm Source/SlippyPanelView.h Source/FrogShape.h Source/Narrate.cpp Source/Json.cpp
	mkdir -p $(BUILD)
	$(CXX) $(ARCH) -std=c++17 -fobjc-arc -O2 -ISource Source/Preview.mm Source/SlippyPanelView.mm Source/Narrate.cpp Source/Json.cpp \
		-framework Cocoa -framework QuartzCore -o $@

preview: $(BUILD)/SlippyPreview
	./$(BUILD)/SlippyPreview

clean:
	rm -rf $(BUILD)

-include $(OBJECTS:.o=.d)
