# Third-party libraries for CAN ECU project
# QCBOR: RFC 7049 CBOR encode/decode
# t_cose: COSE (CBOR Object Signing and Encryption) sign/verify
# tinycrypt: ECC, ECDSA, SHA-256 for embedded
#
# QCBOR and t_cose are shared with the Linux gateway and live in the
# repository-root third_party/ directory; tinycrypt is MCU-only and stays
# under ThirdParty/ in this tree.

set(SHARED_THIRDPARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)

# ---- isotp-c v1.8.0 ----
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/isotp.cmake)
can_add_isotp(${CMAKE_CURRENT_BINARY_DIR}/isotp-c)

# ---- QCBOR ----
set(QCBOR_SRC
    ${SHARED_THIRDPARTY_DIR}/QCBOR/src/qcbor_decode.c
    ${SHARED_THIRDPARTY_DIR}/QCBOR/src/qcbor_encode.c
    ${SHARED_THIRDPARTY_DIR}/QCBOR/src/ieee754.c
    ${SHARED_THIRDPARTY_DIR}/QCBOR/src/UsefulBuf.c
)
set(QCBOR_INC
    ${SHARED_THIRDPARTY_DIR}/QCBOR/inc
    ${SHARED_THIRDPARTY_DIR}/QCBOR/inc/qcbor
)

add_library(qbor STATIC ${QCBOR_SRC})
target_include_directories(qbor PUBLIC ${QCBOR_INC})
target_compile_definitions(qbor PRIVATE QCBOR_DISABLE_FLOAT_HW_USE)

# ---- tinycrypt ----
set(TINYCRYPT_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/tinycrypt/lib/source/ecc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/tinycrypt/lib/source/ecc_dsa.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/tinycrypt/lib/source/utils.c
)
set(TINYCRYPT_INC
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/tinycrypt/lib/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/tinycrypt/can_compat
)

add_library(tinycrypt STATIC ${TINYCRYPT_SRC})
target_include_directories(tinycrypt PUBLIC ${TINYCRYPT_INC})
target_compile_definitions(tinycrypt PRIVATE TC_NO_PLATFORM)

# ---- t_cose ----
set(TCOSE_SRC
    ${SHARED_THIRDPARTY_DIR}/t_cose/src/t_cose_sign1_sign.c
    ${SHARED_THIRDPARTY_DIR}/t_cose/src/t_cose_sign1_verify.c
    ${SHARED_THIRDPARTY_DIR}/t_cose/src/t_cose_parameters.c
    ${SHARED_THIRDPARTY_DIR}/t_cose/src/t_cose_util.c
    ${SHARED_THIRDPARTY_DIR}/t_cose/src/t_cose_short_circuit.c
    ${SHARED_THIRDPARTY_DIR}/t_cose/crypto_adapters/sha256.c
)
set(TCOSE_INC
    ${SHARED_THIRDPARTY_DIR}/t_cose/inc
    ${SHARED_THIRDPARTY_DIR}/t_cose/src
    ${SHARED_THIRDPARTY_DIR}/t_cose/crypto_adapters
)

add_library(tcose STATIC ${TCOSE_SRC})
target_include_directories(tcose PUBLIC ${TCOSE_INC})
target_compile_definitions(tcose PUBLIC
    T_COSE_USE_B_CON_SHA256
    # The CAN token profile is ES256-only.  Keep the t_cose build aligned with
    # the host tests and avoid pulling in crypto adapter entry points for
    # algorithms that the firmware deliberately rejects.
    T_COSE_DISABLE_ES384
    T_COSE_DISABLE_ES512
    T_COSE_DISABLE_EDDSA
    T_COSE_DISABLE_PS256
    T_COSE_DISABLE_PS384
    T_COSE_DISABLE_PS512
    T_COSE_DISABLE_SHORT_CIRCUIT_SIGN
)
target_link_libraries(tcose PUBLIC qbor tinycrypt)

# ---- RT-Thread ULog (matching Nano v4.1.1) ----
# Do not add console_be.c or file_be.c here.  The logger core and asynchronous
# worker are available, while all output backends remain intentionally absent.
set(RTTHREAD_ULOG_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/rtthread_ulog/ulog.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/rtthread_ulog/src/ringbuffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/rtthread_ulog/src/ringblk_buf.c
)
set(RTTHREAD_ULOG_INC
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/rtthread_ulog
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/rtthread_ulog/include
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${RTTHREAD_ULOG_SRC})
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${RTTHREAD_ULOG_INC})
