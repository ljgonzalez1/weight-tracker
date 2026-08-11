# Shared warning/optimisation settings applied to every target of the project.
function(weight_apply_compiler_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual
            -Wcast-qual -Wdouble-promotion)
    endif()

    # Qt headers are noisy about deprecations from other Qt versions; keep the
    # project's own code strict without failing on third-party headers.
    target_compile_definitions(${target} PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_USE_QSTRINGBUILDER)
endfunction()
