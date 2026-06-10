# hagl_hal: forwarding component that satisfies hagl's REQUIRES hagl_hal
# by exposing hagl_esp_mipi's include directory as an interface.
# hagl_esp_mipi provides the actual HAL implementation and REQUIRES hagl,
# so hagl must NOT directly REQUIRE hagl_esp_mipi (would be circular).
# This thin shim breaks the cycle: hagl_hal has no REQUIRES, no sources.
idf_component_register(
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../hagl_esp_mipi/include"
)
