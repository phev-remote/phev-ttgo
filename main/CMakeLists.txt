set(COMPONENT_SRCS "ppp.c" "ttgo.c" "main.c" "mqtt.c" "ota.c")
set(COMPONENT_ADD_INCLUDEDIRS ".")
set(COMPONENT_EMBED_TXTFILES "../pem/fullchain.pem")

register_component()