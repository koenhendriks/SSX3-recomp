# SDL2 for the macOS cross build: SDL 2.32 built static from source (deps/sdl2-static, macOS 14+).
if(NOT TARGET SDL2::SDL2)
  add_library(SDL2::SDL2 STATIC IMPORTED)
  set_target_properties(SDL2::SDL2 PROPERTIES
    IMPORTED_LOCATION "/work/deps/sdl2-static/lib/libSDL2.a"
    INTERFACE_INCLUDE_DIRECTORIES "/work/deps/sdl2-static/include;/work/deps/sdl2-static/include/SDL2"
    INTERFACE_LINK_LIBRARIES "m;-liconv;-Wl,-framework,CoreVideo;-Wl,-framework,Cocoa;-Wl,-framework,IOKit;-Wl,-framework,ForceFeedback;-Wl,-framework,Carbon;-Wl,-framework,CoreAudio;-Wl,-framework,AudioToolbox;-Wl,-framework,AVFoundation;-Wl,-framework,Foundation;-Wl,-weak_framework,GameController;-Wl,-weak_framework,Metal;-Wl,-weak_framework,QuartzCore;-Wl,-weak_framework,CoreHaptics")
endif()
set(SDL2_FOUND TRUE)
