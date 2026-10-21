global-incdirs-y += include
global-incdirs-y += ../../../../third_party/QCBOR/inc
global-incdirs-y += ../../../../third_party/t_cose/inc
global-incdirs-y += ../../../../third_party/t_cose/src
global-incdirs-y += ../../../..

cppflags-y += -DQCBOR_DISABLE_FLOAT_HW_USE
cppflags-y += -DT_COSE_DISABLE_ES384
cppflags-y += -DT_COSE_DISABLE_ES512
cppflags-y += -DT_COSE_DISABLE_EDDSA
cppflags-y += -DT_COSE_DISABLE_PS256
cppflags-y += -DT_COSE_DISABLE_PS384
cppflags-y += -DT_COSE_DISABLE_PS512
cppflags-y += -DT_COSE_DISABLE_SHORT_CIRCUIT_SIGN

srcs-y += ta_entry.c
srcs-y += token_builder.c
srcs-y += cose_crypto.c
third-party-root := $(abspath ../../../../third_party)
spec-out-dir := $(out-dir)/third_party
spec-srcs += $(third-party-root)/QCBOR/src/qcbor_encode.c
spec-srcs += $(third-party-root)/QCBOR/src/UsefulBuf.c
spec-srcs += $(third-party-root)/QCBOR/src/ieee754.c
spec-srcs += $(third-party-root)/t_cose/src/t_cose_sign1_sign.c
spec-srcs += $(third-party-root)/t_cose/src/t_cose_util.c
$(foreach f,$(spec-srcs),$(eval cppflags-$(spec-out-dir)/$(notdir $(basename $(f))).o := $(cppflags-y)))
