list(APPEND COMPONENT_ADD_INCLUDEDIRS ./include)
set(COMPONENT_SRCS
./src/server.cpp
./src/request.cpp
./src/response.cpp
)

register_component()