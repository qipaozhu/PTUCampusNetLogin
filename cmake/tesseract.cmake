# 可选：验证码 OCR（找到 Tesseract 才会启用）
#
# 查找顺序：手动指定的 TESSERACT_ROOT -> vcpkg 的 vcpkg_installed -> third_party/tesseract
# 想启用 OCR，先在工程根目录执行：
#   "D:/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg/vcpkg.exe" install --triplet x64-windows
#
# 用法：ptu_enable_tesseract(<target>)

# 找到 Tesseract 时：加 HAVE_TESSERACT 宏、链接库、把 DLL 和语言数据复制到 exe 旁边；
# 找不到就只打印一行提示，验证码识别自动退化成手动登录。
function(ptu_enable_tesseract target)
    set(sources_root "${PROJECT_SOURCE_DIR}")
    set(TESSERACT_ROOT "" CACHE PATH "Tesseract installation root directory")
    set(tesseract_hints
        "${TESSERACT_ROOT}"
        "${sources_root}/vcpkg_installed/x64-windows"
        "${sources_root}/third_party/tesseract")

    find_path(TESSERACT_INCLUDE_DIR NAMES tesseract/baseapi.h
        HINTS ${tesseract_hints}
        PATH_SUFFIXES include)

    # vcpkg 的 tesseract 库文件名带版本号（tesseract55.lib / tesseract55d.lib）
    find_library(TESSERACT_LIBRARY_RELEASE
        NAMES tesseract55 tesseract5 tesseract libtesseract55 libtesseract5 libtesseract
        HINTS ${tesseract_hints}
        PATH_SUFFIXES lib)
    find_library(TESSERACT_LIBRARY_DEBUG
        NAMES tesseract55d tesseract5d tesseractd libtesseract55d libtesseract5d libtesseractd
        HINTS ${tesseract_hints}
        PATH_SUFFIXES debug/lib lib)

    if(NOT TESSERACT_INCLUDE_DIR OR NOT TESSERACT_LIBRARY_RELEASE)
        message(STATUS "Tesseract OCR not found, captcha OCR disabled")
        return()
    endif()

    target_compile_definitions(${target} PRIVATE HAVE_TESSERACT)
    target_include_directories(${target} PRIVATE ${TESSERACT_INCLUDE_DIR})

    if(TESSERACT_LIBRARY_DEBUG)
        target_link_libraries(${target} PRIVATE
            optimized ${TESSERACT_LIBRARY_RELEASE}
            debug ${TESSERACT_LIBRARY_DEBUG})
    else()
        target_link_libraries(${target} PRIVATE ${TESSERACT_LIBRARY_RELEASE})
    endif()

    ptu_copy_tesseract_files(${target})
    message(STATUS "Tesseract OCR enabled: ${TESSERACT_LIBRARY_RELEASE}")
endfunction()

# 把运行时 DLL 和语言数据复制到 exe 同级目录（Debug 用 debug/bin，Release 用 bin）
function(ptu_copy_tesseract_files target)
    set(sources_root "${PROJECT_SOURCE_DIR}")
    set(release_bin "${sources_root}/vcpkg_installed/x64-windows/bin")
    set(debug_bin "${sources_root}/vcpkg_installed/x64-windows/debug/bin")
    set(third_party_bin "${sources_root}/third_party/tesseract/bin")

    set(runtime_dir "")
    if(EXISTS "${release_bin}" AND EXISTS "${debug_bin}")
        set(runtime_dir "$<IF:$<CONFIG:Debug>,${debug_bin},${release_bin}>")
    elseif(EXISTS "${release_bin}")
        set(runtime_dir "${release_bin}")
    elseif(EXISTS "${third_party_bin}")
        set(runtime_dir "${third_party_bin}")
    endif()

    if(runtime_dir)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${runtime_dir}" "$<TARGET_FILE_DIR:${target}>"
            COMMENT "复制 Tesseract 运行时 DLL")
    endif()

    # 语言数据放到 exe 同级的 tessdata 目录，CaptchaOcr 会在那里查找 eng.traineddata
    set(data_dir "${sources_root}/third_party/tesseract/tessdata")
    if(EXISTS "${data_dir}/eng.traineddata")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${data_dir}" "$<TARGET_FILE_DIR:${target}>/tessdata"
            COMMENT "复制 Tesseract 语言数据")
    endif()
endfunction()
