# Slippy native Illustrator plug-in, Windows build (x64 and ARM64).
# macOS builds with the Makefile in this directory.
#
#   cmake -S . -B build-win -A x64 -DAI_SDK="C:/path/to/AdobeIllustratorSDK"
#   cmake --build build-win --config Release
#   -> build-win/Release/Slippy.aip
#
# Requires Visual Studio 2022 (C++ workload, Windows 10 SDK) and Python 3 on
# PATH (the SDK's create_pipl.py generates the Windows PiPL at build time).

cmake_minimum_required(VERSION 3.21)
project(Slippy VERSION 0.1.0 LANGUAGES CXX RC)

if(NOT WIN32)
	message(FATAL_ERROR "CMakeLists.txt is the Windows build. On macOS run `make` in this directory.")
endif()

set(AI_SDK "$ENV{AI_SDK}" CACHE PATH "Adobe Illustrator SDK root (contains illustratorapi/ and samplecode/)")
if(NOT EXISTS "${AI_SDK}/illustratorapi/illustrator/AITypes.h")
	message(FATAL_ERROR "Set AI_SDK to the Adobe Illustrator SDK root (got '${AI_SDK}').")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(API "${AI_SDK}/illustratorapi")
set(COMMON "${AI_SDK}/samplecode/common")

add_library(Slippy SHARED
	Source/SlippyPlugin.cpp
	Source/SlippySuites.cpp
	Source/Commands.cpp
	Source/Overlay.cpp
	Source/Mcp.cpp
	Source/Narrate.cpp
	Source/Server.cpp
	Source/Json.cpp
	Source/Platform.cpp
	Source/Raster.cpp
	Source/CmdCatalog.cpp
	Source/CmdSymbols.cpp
	Source/CmdView.cpp
	Source/CmdPaint.cpp
	Source/CmdAppearance.cpp
	Source/CmdShapes.cpp
	Source/CmdText.cpp
	Source/SlippyPanelWin.cpp
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
	Resources/Win/Slippy.rc
	${COMMON}/win/PluginMain.def
)

set_target_properties(Slippy PROPERTIES
	SUFFIX ".aip"
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
	MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"	# static CRT: no VC++ redistributable needed
)

target_include_directories(Slippy PRIVATE
	Source
	Resources/raw
	${COMMON}/includes
	${COMMON}/win
	${API}/illustrator
	${API}/illustrator/actions
	${API}/illustrator/legacy
	${API}/pica_sp
	${API}/ate
	${API}/adm
)

target_compile_definitions(Slippy PRIVATE
	WIN_ENV _WINDOWS NOMINMAX _USE_MATH_DEFINES
	_WIN32_WINNT=0x0A00 WINVER=0x0A00	# Windows 10: GetDpiForWindow, Windows.Data.Pdf
	$<$<CONFIG:Debug>:_DEBUG>
	$<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

# pragma.h silences SDK warnings (as in the SDK's own projects); /utf-8 keeps
# the feed's “quotes” and the panel's glyphs intact.
target_compile_options(Slippy PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8 /EHsc /W3 /FI${COMMON}/win/pragma.h /wd4996>)
# C++/WinRT (Windows.Data.Pdf for PNG / JPEG export) wants these, and C++20:
# under C++17 it reaches for the removed <experimental/coroutine>.
set_source_files_properties(Source/Raster.cpp PROPERTIES COMPILE_OPTIONS "/std:c++20;/bigobj;/permissive-;/Zc:__cplusplus")
# The SDK headers' std names meet GDI+'s 'byte'; the panel doesn't need std::byte.
set_source_files_properties(Source/SlippyPanelWin.cpp PROPERTIES COMPILE_DEFINITIONS "_HAS_STD_BYTE=0")

target_link_libraries(Slippy PRIVATE user32 gdi32 gdiplus comctl32 ole32 shell32 shcore ws2_32 bcrypt windowscodecs windowsapp)

# Windows PiPL: create_pipl.py writes a platform-specific binary, so it runs
# here rather than being checked in. The .rc embeds it as resource "plugin".
set(PIPL "${CMAKE_CURRENT_BINARY_DIR}/plugin.pipl")
add_custom_command(
	OUTPUT "${PIPL}"
	COMMAND "${Python3_EXECUTABLE}" "${AI_SDK}/tools/pipl/create_pipl.py" -input "[{\"name\":\"Slippy\"}]"
	WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
	COMMENT "Generating Windows PiPL"
	VERBATIM
)
add_custom_target(SlippyPipl DEPENDS "${PIPL}")
add_dependencies(Slippy SlippyPipl)
set_source_files_properties(Resources/Win/Slippy.rc PROPERTIES OBJECT_DEPENDS "${PIPL};${CMAKE_CURRENT_SOURCE_DIR}/Resources/raw/IDToFile.txt;${CMAKE_CURRENT_SOURCE_DIR}/Resources/raw/slippy_panel_light.svg;${CMAKE_CURRENT_SOURCE_DIR}/Resources/raw/slippy_panel_dark.svg")
target_include_directories(Slippy PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# The menu commands, action events and tools the SDK names (menu.list...).
set(CATALOG "${CMAKE_CURRENT_BINARY_DIR}/SdkCatalog.inc")
add_custom_command(
	OUTPUT "${CATALOG}"
	COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/sdk_catalog.py" "${AI_SDK}" "${CATALOG}"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/sdk_catalog.py"
	COMMENT "Generating the SDK catalog"
	VERBATIM
)
add_custom_target(SlippyCatalog DEPENDS "${CATALOG}")
add_dependencies(Slippy SlippyCatalog)
set_source_files_properties(Source/CmdCatalog.cpp PROPERTIES OBJECT_DEPENDS "${CATALOG}")

configure_file(Resources/Win/SlippyVersion.h.in "${CMAKE_CURRENT_BINARY_DIR}/SlippyVersion.h" @ONLY)
