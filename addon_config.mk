# All variables and this file are optional, if they are not present the PG and the
# makefiles will try to parse the correct values from the file system.
#
# Variables that specify exclusions can use % as a wildcard to specify that anything in
# that position will match. A partial path can also be specified to, for example, exclude
# a whole folder from the parsed paths from the file system.
#
# Variables can be specified using = or +=
# = will clear the contents of that variable both specified from the file or the ones parsed
# from the file system.
# += will add the values to the previous ones in the file or the ones parsed from the file
# system.
#
# The PG can be used to detect errors in this file. Just create a new project with this addon
# and the PG will write the kind of error and the line number to the console.

meta:
	ADDON_NAME = ofxVlc4
	ADDON_DESCRIPTION = ofxVlc4 is an openFrameworks wrapper around libVLC 4 with playback, diagnostics, recording, MIDI utilities, and GUI examples
	ADDON_AUTHOR = Jonathan Frank
	ADDON_TAGS = "video,vlc,media,player,recording,midi"
	ADDON_URL = https://github.com/Jonathhhan/ofxVlc4

common:
	ADDON_INCLUDES += libs/libvlc/include
	# Propagate the expected ofxImGui GLFW callback/multi-context configuration.
	ADDON_CFLAGS += -DOFXIMGUI_GLFW_EVENTS_REPLACE_OF_CALLBACKS=1
	ADDON_CFLAGS += -DOFXIMGUI_GLFW_FIX_MULTICONTEXT_PRIMARY_VP=0
	ADDON_CFLAGS += -DOFXIMGUI_GLFW_FIX_MULTICONTEXT_SECONDARY_VP=1

linux64:
	ADDON_PKG_CONFIG_LIBRARIES = libvlc

linux:

linuxarmv6l:

linuxarmv7l:

msys2:

vs:
	# install-libvlc.sh installs one shared Windows runtime under
	# libs/libvlc/runtime/vs/x64 and links examples to it locally.
	ADDON_LIBS += libs/libvlc/lib/vs/libvlc.lib

android/armeabi:

android/armeabi-v7a:

osx:
	ADDON_LIBS += libs/libvlc/lib/osx/lib/libvlc.dylib
	ADDON_LIBS += libs/libvlc/lib/osx/lib/libvlccore.dylib
	ADDON_LDFLAGS += -Wl,-rpath,@executable_path/../Resources/data/libvlc/macos/lib
	ADDON_LDFLAGS += -Wl,-rpath,@executable_path/../data/libvlc/macos/lib

ios:

emscripten:
