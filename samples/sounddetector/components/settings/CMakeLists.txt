# Room level + sensitivity in NVS, written by a debounced task.
idf_component_register(
    SRCS "settings.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash
)
