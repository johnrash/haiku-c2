# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# A CMake build of qt6-haikuplugins (threedeyes, GPL v3; HaikuPorts' second
# qt6_base source), whose qmake projects want a native qmake (PLAN.md M83):
# the Haiku platform plugin, the Haiku style and the HVIF image format, from
# the same sources with the same libraries. The Qt6Configurator preferences
# application is not built.
cmake_minimum_required(VERSION 3.22)
project(qt6-haikuplugins LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets CorePrivate GuiPrivate
	WidgetsPrivate)
find_package(Freetype REQUIRED)

set(platform src/platform)
qt_add_plugin(qhaiku PLUGIN_TYPE platforms CLASS_NAME QHaikuExpIntegrationPlugin)
target_sources(qhaiku PRIVATE
	${platform}/main.cpp ${platform}/qhaikuapplication.cpp
	${platform}/qhaikubackingstore.cpp ${platform}/qhaikuclipboard.cpp
	${platform}/qhaikucursor.cpp ${platform}/qhaikuglcontext.cpp
	${platform}/qhaikuintegration.cpp ${platform}/qhaikunativeinterface.cpp
	${platform}/qhaikuoffscreensurface.cpp
	${platform}/qhaikuplatformdialoghelpers.cpp
	${platform}/qhaikuplatformfontdatabase.cpp ${platform}/qhaikuscreen.cpp
	${platform}/qhaikuservices.cpp ${platform}/qhaikusystemlocale.cpp
	${platform}/qhaikusystemtrayicon.cpp ${platform}/qhaikutheme.cpp
	${platform}/qhaikuview.cpp ${platform}/qhaikuwindow.cpp
	3rdparty/simplecrypt/simplecrypt.cpp)
target_include_directories(qhaiku PRIVATE ${platform} 3rdparty/simplecrypt)
target_link_libraries(qhaiku PRIVATE Qt6::CorePrivate Qt6::GuiPrivate
	Qt6::WidgetsPrivate Freetype::Freetype be root tracker game GL GLU)

set(style src/style)
qt_add_plugin(qhaikustyle PLUGIN_TYPE styles CLASS_NAME QHaikuStylePlugin)
target_sources(qhaikustyle PRIVATE ${style}/qhaikustyle.cpp ${style}/plugin.cpp
	${style}/helper/qstylehelper.cpp)
target_include_directories(qhaikustyle PRIVATE ${style} ${style}/helper
	${platform})
target_link_libraries(qhaikustyle PRIVATE Qt6::CorePrivate Qt6::GuiPrivate
	Qt6::WidgetsPrivate be root)

set(hvif src/imageformats/hvif)
qt_add_plugin(qhvif PLUGIN_TYPE imageformats CLASS_NAME QHvifPlugin)
target_sources(qhvif PRIVATE ${hvif}/qhvifplugin.cpp ${hvif}/qhvifhandler.cpp)
target_include_directories(qhvif PRIVATE ${hvif})
target_link_libraries(qhvif PRIVATE Qt6::CorePrivate Qt6::Gui be translation
	tracker)

install(TARGETS qhaiku LIBRARY DESTINATION add-ons/Qt6/platforms)
install(TARGETS qhaikustyle LIBRARY DESTINATION add-ons/Qt6/styles)
install(TARGETS qhvif LIBRARY DESTINATION add-ons/Qt6/imageformats)
