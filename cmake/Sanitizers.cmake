option(VF_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(VF_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
