cmake_minimum_required(VERSION 3.15)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
project(ddraw_wrapper C)

# ── Target platform ───────────────────────────────────────────────────────────
# The EFZ game executables are 32-bit (PE32 i386).
# When using MinGW-w64, invoke cmake with:
#   cmake -G "MinGW Makefiles" -DCMAKE_C_FLAGS="-m32" ..
# When using MSVC (cross-compile or native x86 toolset), set:
#   cmake -G "Visual Studio 17 2022" -A Win32 ..

# ── DLL target ────────────────────────────────────────────────────────────────
add_library(ddraw SHARED
    ddraw.c
    ddraw.def          # provides the __declspec-less ordinal export table
)

# Windows subsystem + no default lib noise
target_compile_definitions(ddraw PRIVATE
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:DDRAW_WRAPPER_ENABLE_LOGGING=1>
    $<$<CONFIG:Release>:DDRAW_WRAPPER_ENABLE_LOGGING=0>
)

# Compiler-specific flags
if(MSVC)
    set_property(TARGET ddraw PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    # Force 32-bit (set via -A Win32 on the cmake invocation, but guard here too)
    target_compile_options(ddraw PRIVATE
        /W3           # warning level
        /GS-          # no security cookies (unnecessary overhead for a thin shim)
        /wd4100       # suppress unreferenced formal parameter
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/Zi>
        $<$<CONFIG:Release>:/O2>
    )
    # Link only what we actually need
    target_link_libraries(ddraw PRIVATE
        kernel32
        user32
        gdi32
    )
    target_link_options(ddraw PRIVATE
        /MACHINE:X86
        /DYNAMICBASE
        /NXCOMPAT
        /SUBSYSTEM:WINDOWS,6.0
        $<$<CONFIG:Debug>:/DEBUG>
    )
else()
    # MinGW / GCC
    target_compile_options(ddraw PRIVATE
        -Wall
        -Wno-unused-parameter
        -m32                       # must match the 32-bit game process
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Release>:-O2>
    )
    target_link_options(ddraw PRIVATE
        -m32
        -static
        -static-libgcc             # bundle libgcc so the game dir needs no runtime
        -Wl,--kill-at              # strip stdcall @N suffix from exports
        -Wl,--enable-stdcall-fixup
    )
    target_link_libraries(ddraw PRIVATE
        kernel32
        user32
        gdi32
    )
endif()

# Output the DLL as "ddraw.dll" (not "libddraw.dll")
set_target_properties(ddraw PROPERTIES
    OUTPUT_NAME   "ddraw"
    PREFIX        ""
    SUFFIX        ".dll"
)

# ── Install ───────────────────────────────────────────────────────────────────
# cmake --install . --prefix <game_dir>
# copies ddraw.dll next to the target EXE.
install(TARGETS ddraw RUNTIME DESTINATION .)
