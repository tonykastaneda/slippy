# Slippy's Windows builds (x64 and ARM64): Slippy for Illustrator (Slippy.aip)
# and Slippy for Photoshop (Slippy.8li). macOS builds with the Makefile.
#
#   cmake -S . -B build-win -A x64 -DAI_SDK="C:/path/to/AdobeIllustratorSDK" -DPS_SDK="C:/path/to/AdobePhotoshopSDK"
#   cmake --build build-win --config Release
#   -> build-win/illustrator/Release/Slippy.aip, build-win/photoshop/Release/Slippy.8li
#
# Either SDK can be left out to build just the other plug-in. Requires Visual
# Studio 2022 (C++ workload, Windows 10 SDK) and Python 3 on PATH.

cmake_minimum_required(VERSION 3.21)
project(Slippy VERSION 0.1.0 LANGUAGES CXX RC)

if(NOT WIN32)
	message(FATAL_ERROR "CMakeLists.txt is the Windows build. On macOS run `make` (Illustrator) or `make photoshop`.")
endif()

set(AI_SDK "$ENV{AI_SDK}" CACHE PATH "Adobe Illustrator SDK root (contains illustratorapi/ and samplecode/)")
set(PS_SDK "$ENV{PS_SDK}" CACHE PATH "Adobe Photoshop SDK root (contains pluginsdk/)")
set(HAVE_AI OFF)
set(HAVE_PS OFF)
if(EXISTS "${AI_SDK}/illustratorapi/illustrator/AITypes.h")
	set(HAVE_AI ON)
endif()
if(EXISTS "${PS_SDK}/pluginsdk/photoshopapi/photoshop/PIActions.h")
	set(HAVE_PS ON)
endif()
if(NOT HAVE_AI AND NOT HAVE_PS)
	message(FATAL_ERROR "Set AI_SDK (Illustrator SDK root) and / or PS_SDK (Photoshop SDK root). Got AI_SDK='${AI_SDK}', PS_SDK='${PS_SDK}'.")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ---- shared by both: the agents' logos for the panel, the version for the .rc files
file(GLOB AGENT_SVGS "${CMAKE_CURRENT_SOURCE_DIR}/shared/resources/agents/*.svg")
set(LOGOS "${CMAKE_CURRENT_BINARY_DIR}/AgentLogos.inc")
add_custom_command(
	OUTPUT "${LOGOS}"
	COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/agent_logos.py" "${CMAKE_CURRENT_SOURCE_DIR}/shared/resources/agents" "${LOGOS}"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/agent_logos.py" ${AGENT_SVGS}
	COMMENT "Generating the agent logos"
	VERBATIM
)
add_custom_target(SlippyLogos DEPENDS "${LOGOS}")
set_source_files_properties(shared/SlippyPanelWin.cpp PROPERTIES OBJECT_DEPENDS "${LOGOS}" COMPILE_DEFINITIONS "_HAS_STD_BYTE=0")
configure_file(shared/resources/SlippyVersion.h.in "${CMAKE_CURRENT_BINARY_DIR}/SlippyVersion.h" @ONLY)

set(SHARED_SOURCES
	shared/Server.cpp
	shared/Mcp.cpp
	shared/Json.cpp
	shared/Platform.cpp
	shared/CrashLog.cpp
	shared/SlippyPanelWin.cpp
)
set(WIN_DEFINES NOMINMAX _USE_MATH_DEFINES _WIN32_WINNT=0x0A00 WINVER=0x0A00	# Windows 10: GetDpiForWindow
	$<$<CONFIG:Debug>:_DEBUG> $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
set(WIN_LIBS user32 gdi32 gdiplus comctl32 ole32 shell32 shcore ws2_32 bcrypt)

# ================================================================ Illustrator
if(HAVE_AI)
set(API "${AI_SDK}/illustratorapi")
set(COMMON "${AI_SDK}/samplecode/common")

add_library(SlippyIllustrator SHARED
	illustrator/SlippyPlugin.cpp
	illustrator/SlippySuites.cpp
	illustrator/Commands.cpp
	illustrator/Overlay.cpp
	illustrator/Narrate.cpp
	illustrator/Raster.cpp
	illustrator/CmdCatalog.cpp
	illustrator/CmdSymbols.cpp
	illustrator/CmdView.cpp
	illustrator/CmdPaint.cpp
	illustrator/CmdAppearance.cpp
	illustrator/CmdShapes.cpp
	illustrator/CmdText.cpp
	illustrator/CmdDocument.cpp
	illustrator/SlippyPanelHostWin.cpp
	${SHARED_SOURCES}
	${COMMON}/source/Main.cpp
	${COMMON}/source/Plugin.cpp
	${COMMON}/source/Suites.cpp
	${COMMON}/source/AppContext.cpp
	${COMMON}/source/IllustratorSDK.cpp
	${API}/illustrator/IAIUnicodeString.cpp
	${API}/illustrator/IAIFilePath.cpp
	${API}/illustrator/IAIArtboards.cpp
	${API}/illustrator/AIAssert.cpp
	${API}/ate/IText.cpp
	${API}/ate/IThrowException.cpp
	illustrator/resources/Win/Slippy.rc
	${COMMON}/win/PluginMain.def
)

set_target_properties(SlippyIllustrator PROPERTIES
	OUTPUT_NAME Slippy
	SUFFIX ".aip"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/illustrator"
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
	MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"	# static CRT: no VC++ redistributable needed
)

target_include_directories(SlippyIllustrator PRIVATE
	illustrator
	shared
	illustrator/resources/raw
	${COMMON}/includes
	${COMMON}/win
	${API}/illustrator
	${API}/illustrator/actions
	${API}/illustrator/legacy
	${API}/pica_sp
	${API}/ate
	${API}/adm
)

target_compile_definitions(SlippyIllustrator PRIVATE WIN_ENV _WINDOWS ${WIN_DEFINES})

# pragma.h silences SDK warnings (as in the SDK's own projects); /utf-8 keeps
# the feed's “quotes” and the panel's glyphs intact.
target_compile_options(SlippyIllustrator PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8 /EHsc /W3 /FI${COMMON}/win/pragma.h /wd4996>)
# C++/WinRT (the PNG / JPEG export worker thread) wants these, and C++20:
# under C++17 it reaches for the removed <experimental/coroutine>.
set_source_files_properties(illustrator/Raster.cpp PROPERTIES COMPILE_OPTIONS "/std:c++20;/bigobj;/permissive-;/Zc:__cplusplus")

target_link_libraries(SlippyIllustrator PRIVATE ${WIN_LIBS} windowscodecs windowsapp)

# Windows PiPL: create_pipl.py writes a platform-specific binary, so it runs
# here rather than being checked in. The .rc embeds it as resource "plugin".
set(PIPL "${CMAKE_CURRENT_BINARY_DIR}/illustrator-pipl/plugin.pipl")
add_custom_command(
	OUTPUT "${PIPL}"
	COMMAND "${Python3_EXECUTABLE}" "${AI_SDK}/tools/pipl/create_pipl.py" -input "[{\"name\":\"Slippy\"}]"
	WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/illustrator-pipl"
	COMMENT "Generating Windows PiPL"
	VERBATIM
)
add_custom_target(SlippyPipl DEPENDS "${PIPL}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/illustrator-pipl")
add_dependencies(SlippyIllustrator SlippyPipl)
set_source_files_properties(illustrator/resources/Win/Slippy.rc PROPERTIES OBJECT_DEPENDS "${PIPL};${CMAKE_CURRENT_SOURCE_DIR}/illustrator/resources/raw/IDToFile.txt;${CMAKE_CURRENT_SOURCE_DIR}/illustrator/resources/raw/slippy_panel_light.svg;${CMAKE_CURRENT_SOURCE_DIR}/illustrator/resources/raw/slippy_panel_dark.svg")
target_include_directories(SlippyIllustrator PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/illustrator-pipl")

# The menu commands, action events and tools the SDK names (menu.list...).
set(CATALOG "${CMAKE_CURRENT_BINARY_DIR}/SdkCatalog.inc")
add_custom_command(
	OUTPUT "${CATALOG}"
	COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/illustrator/sdk_catalog.py" "${AI_SDK}" "${CATALOG}"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/illustrator/sdk_catalog.py"
	COMMENT "Generating the SDK catalog"
	VERBATIM
)
add_custom_target(SlippyCatalog DEPENDS "${CATALOG}")
add_dependencies(SlippyIllustrator SlippyCatalog)
set_source_files_properties(illustrator/CmdCatalog.cpp PROPERTIES OBJECT_DEPENDS "${CATALOG}")

add_dependencies(SlippyIllustrator SlippyLogos)
endif()

# ================================================================ Photoshop
if(HAVE_PS)
set(PSAPI "${PS_SDK}/pluginsdk/photoshopapi")

add_library(SlippyPhotoshop SHARED
	photoshop/PsPlugin.cpp
	photoshop/PsCommands.cpp
	photoshop/PsDescriptor.cpp
	photoshop/PsSuites.cpp
	photoshop/PsNarrate.cpp
	photoshop/PsDockedPanel.cpp
	photoshop/PsPanelWin.cpp
	${SHARED_SOURCES}
	photoshop/Slippy.rc
)

set_target_properties(SlippyPhotoshop PROPERTIES
	OUTPUT_NAME Slippy
	SUFFIX ".8li"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/photoshop"
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
	MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_include_directories(SlippyPhotoshop PRIVATE
	photoshop
	shared
	"${CMAKE_CURRENT_BINARY_DIR}"
	${PSAPI}/photoshop
	${PSAPI}/pica_sp
)

# As the SDK's own Windows projects define them.
target_compile_definitions(SlippyPhotoshop PRIVATE WIN32=1 _WINDOWS ISOLATION_AWARE_ENABLED=1 _CRT_SECURE_NO_DEPRECATE ${WIN_DEFINES})
target_compile_options(SlippyPhotoshop PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8 /EHsc /W3 /wd4996 /wd4068>)
target_link_libraries(SlippyPhotoshop PRIVATE ${WIN_LIBS})
set_source_files_properties(photoshop/Slippy.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/photoshop/PiPLs.json")
add_dependencies(SlippyPhotoshop SlippyLogos)
endif()
