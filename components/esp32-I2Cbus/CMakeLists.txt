set(COMPONENT_SRCS
    "src/I2Cbus.cpp")
set(COMPONENT_ADD_INCLUDEDIRS "include")
set(COMPONENT_REQUIRES
    driver
)

register_component()

component_compile_options(PUBLIC "-DI2CBUS_COMPONENT_TRUE=1")
