function(can_add_isotp binary_dir)
    set(isotpc_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
    set(isotpc_PAD_CAN_FRAMES ON CACHE BOOL "" FORCE)
    set(isotpc_CAN_FRAME_PAD_VALUE "0x00" CACHE STRING "" FORCE)
    add_subdirectory(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../ThirdParty/isotp-c"
        "${binary_dir}"
    )
    target_compile_definitions(isotp PUBLIC
        ISO_TP_DEFAULT_BLOCK_SIZE=8U
        ISO_TP_DEFAULT_ST_MIN_US=2000U
        ISO_TP_DEFAULT_RESPONSE_TIMEOUT_US=1000000U
        ISO_TP_RECEIVE_COMPLETE_CALLBACK
    )
endfunction()
