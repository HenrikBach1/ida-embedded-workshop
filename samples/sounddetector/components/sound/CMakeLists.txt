# Pure decision logic for the sound detector: no hardware, no RTOS, host-tested.
idf_component_register(
    SRCS "detector.c" "event_log.c" "event_time.c"
    INCLUDE_DIRS "include"
)
