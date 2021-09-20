function(set_project_c_warnings library_name)
  option(C_WARNINGS_AS_ERRORS "Treat C compiler warnings as errors" OFF)

  set(CLANG_C_WARNINGS
      -Wall
      -Wextra
      -Wpedantic
  )
  set(GCC_C_WARNINGS
      -Wall
      -Wextra
      -Wpedantic
  )

  if (C_WARNINGS_AS_ERRORS)
    set(CLANG_C_WARNINGS ${CLANG_C_WARNINGS} -Werror)
    set(GCC_C_WARNINGS ${GCC_C_WARNINGS} -Werror)
  endif()

  if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(C_PROJECT_WARNINGS ${CLANG_C_WARNINGS})
  elseif(CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    set(C_PROJECT_WARNINGS ${CLANG_C_WARNINGS})
  elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(C_PROJECT_WARNINGS ${GCC_C_WARNINGS})
  endif()

  target_compile_options(${library_name} INTERFACE ${C_PROJECT_WARNINGS})
endfunction()

function(set_project_cpp_warnings library_name)
  option(CPP_WARNINGS_AS_ERRORS "Treat CPP compiler warnings as errors" OFF)

  set(CLANG_CPP_WARNINGS
      -Wall
      -Wextra
      -Wpedantic
      -Wno-nested-anon-types
  )
  set(GCC_CPP_WARNINGS
      -Wall
      -Wextra
      -Wpedantic
      -Wno-maybe-uninitialized
  )

  if (CPP_WARNINGS_AS_ERRORS)
    set(CLANG_CPP_WARNINGS ${CLANG_CPP_WARNINGS} -Werror)
    set(GCC_CPP_WARNINGS ${GCC_CPP_WARNINGS} -Werror)
  endif()

  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CPP_PROJECT_WARNINGS ${CLANG_CPP_WARNINGS})
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    set(CPP_PROJECT_WARNINGS ${CLANG_CPP_WARNINGS})
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CPP_PROJECT_WARNINGS ${GCC_CPP_WARNINGS})
  endif()

  target_compile_options(${library_name} INTERFACE ${CPP_PROJECT_WARNINGS})
endfunction()
