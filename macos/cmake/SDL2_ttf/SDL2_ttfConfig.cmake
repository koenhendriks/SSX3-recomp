# Minimal SDL2_ttf package for the macOS cross build (static, vendored freetype).
if(NOT TARGET SDL2_ttf::SDL2_ttf)
  add_library(SDL2_ttf::SDL2_ttf INTERFACE IMPORTED)
  set_target_properties(SDL2_ttf::SDL2_ttf PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "/work/deps/prefix-arm64/include/SDL2"
    INTERFACE_LINK_LIBRARIES "/work/deps/prefix-arm64/lib/libSDL2_ttf.a;/work/deps/prefix-arm64/lib/libfreetype.a;SDL2::SDL2")
endif()
set(SDL2_ttf_FOUND TRUE)
