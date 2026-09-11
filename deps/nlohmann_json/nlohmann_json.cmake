# nlohmann_json - header-only JSON library
# https://github.com/nlohmann/json

orcaslicer_add_cmake_project(nlohmann_json
    CMAKE_ARGS
        -DBUILD_TESTING=OFF
        -DJSON_BuildTests=OFF
        -DCMAKE_INSTALL_LIBDIR=lib
)
list(APPEND _dep_list dep_nlohmann_json)
