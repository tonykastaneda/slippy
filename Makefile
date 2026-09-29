# KAGE native Illustrator plug-in (arm64, macOS). Command Line Tools only -
# no Xcode, no Visual Studio. Same recipe as RAGE / AA-ReColor.
#   make            build + sign build/KAGE.aip (ad hoc; SIGN_ID="Developer ID ..." to sign for real)
#   make install    copy into Illustrator's Plug-ins folder (quit Illustrator first;
#                   the first install needs sudo - the folder is root-owned)
#   make preview    the panel + Kage in a plain window with made-up calls
#   make clean

SDK        ?= $(HOME)/Developer/AdobeIllustratorSDK
AI_APP     ?= /Applications/Adobe Illustrator 2026
SIGN_ID    ?= -

NAME       := KAGE
BUILD      := build
OBJDIR     := $(BUILD)/obj
BUNDLE     := $(BUILD)/$(NAME).aip
EXE        := $(BUNDLE)/Contents/MacOS/$(NAME)

API        := $(SDK)/illustratorapi
COMMON     := $(SDK)/samplecode/common

SOURCES := \
	Source/KAGEPlugin.cpp \
	Source/KAGESuites.cpp \
	Source/Commands.cpp \
	Source/Mcp.cpp \
	Source/Narrate.cpp \
	Source/Server.cpp \
	Source/Json.cpp \
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
	Source/KAGEPanel.mm \
	Source/KAGEPanelView.mm

OBJECTS := $(addprefix $(OBJDIR)/,$(notdir $(SOURCES:.cpp=.o) $(MM_SOURCES:.mm=.o)))
vpath %.cpp $(sort $(dir $(SOURCES)))
vpath %.mm $(sort $(dir $(MM_SOURCES)))

CXX      := clang++
ARCH     := -arch arm64 -mmacosx-version-min=12.0
INCLUDES := -ISource -I$(COMMON)/includes \
	-I$(API)/illustrator -I$(API)/illustrator/actions -I$(API)/pica_sp -I$(API)/ate
CXXFLAGS := $(ARCH) -std=c++17 -stdlib=libc++ -x objective-c++ -O2 -g \
	-fvisibility=hidden -fvisibility-inlines-hidden \
	-Wno-deprecated-declarations -Wno-unknown-pragmas \
	-include $(COMMON)/includes/IllustratorSDKRelease.pch \
	$(INCLUDES) -MMD -MP
LDFLAGS  := $(ARCH) -bundle -stdlib=libc++ -framework Cocoa -framework QuartzCore -framework CoreFoundation

.PHONY: all clean install preview
all: $(BUILD)/.signed

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.mm | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -fobjc-arc -c $< -o $@

$(OBJDIR):
	mkdir -p $@

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

$(BUILD)/.signed: $(EXE) Resources/Info.plist $(BUILD)/plugin.pipl $(BUILD)/$(NAME).rsrc
	mkdir -p $(BUNDLE)/Contents/Resources/pipl
	cp $(BUILD)/$(NAME).rsrc $(BUNDLE)/Contents/Resources/$(NAME).rsrc
	cp Resources/Info.plist $(BUNDLE)/Contents/Info.plist
	printf 'ARPIART5' > $(BUNDLE)/Contents/PkgInfo
	cp $(BUILD)/plugin.pipl $(BUNDLE)/Contents/Resources/pipl/plugin.pipl
	codesign --force --sign "$(SIGN_ID)" --timestamp=none $(BUNDLE)
	codesign --verify --strict $(BUNDLE)
	touch $@

# Plug-ins.localized is root-owned; after the first sudo install the bundle's
# own files are ours, so ditto can refresh it in place without sudo.
install: all
	ditto $(BUNDLE) "$(AI_APP)/Plug-ins.localized/$(NAME).aip"

# The panel in a plain window with made-up calls - watch Kage without Illustrator.
$(BUILD)/KAGEPreview: Source/Preview.mm Source/KAGEPanelView.mm Source/KAGEPanelView.h Source/Narrate.cpp Source/Json.cpp
	mkdir -p $(BUILD)
	$(CXX) $(ARCH) -std=c++17 -fobjc-arc -O2 -ISource Source/Preview.mm Source/KAGEPanelView.mm Source/Narrate.cpp Source/Json.cpp \
		-framework Cocoa -framework QuartzCore -o $@

preview: $(BUILD)/KAGEPreview
	./$(BUILD)/KAGEPreview

clean:
	rm -rf $(BUILD)

-include $(OBJECTS:.o=.d)
