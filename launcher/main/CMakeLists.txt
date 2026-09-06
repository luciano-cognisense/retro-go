file(GLOB COMPONENT_SRCS "*.c")
list(REMOVE_ITEM COMPONENT_SRCS "${COMPONENT_DIR}/images_old.c")
set(COMPONENT_ADD_INCLUDEDIRS ".")
register_component()
rg_setup_compile_options(-O2 -Wno-error=format -Wno-error=char-subscripts)

# Keep the boot signature inside the launcher partition so it is available even
# when no SD card is installed. The sample is signed 16-bit PCM, mono, 22050 Hz.
target_add_binary_data(${COMPONENT_TARGET} "assets/boot_c3.pcm" BINARY)

# add_custom_command(OUTPUT images.c
#                    COMMAND python ${COMPONENT_DIR}/gen_images.py
#                    VERBATIM)
# add_custom_target(images DEPENDS images.c)
# add_dependencies(${COMPONENT_LIB} images)
