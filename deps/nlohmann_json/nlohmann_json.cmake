# nlohmann_json - header-only JSON library
# https://github.com/nlohmann/json

orcaslicer_add_cmake_project(nlohmann_json
    CMAKE_ARGS
        -DBUILD_TESTING=OFF
        -DJSON_BuildTests=OFF
        -DCMAKE_INSTALL_LIBDIR=lib
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
    URL_HASH SHA256=8c4b26bf4b422252e13f332bc5e388ec0ab5c3443d24399acb675e68278d341f
)
list(APPEND _dep_list dep_nlohmann_json)
