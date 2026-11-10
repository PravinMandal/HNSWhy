option(HNSWHY_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(HNSWHY_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
