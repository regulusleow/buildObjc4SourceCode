export MakeInc_cmd=${SRCROOT}/makedefs/MakeInc.cmd
export MakeInc_def=${SRCROOT}/makedefs/MakeInc.def
export MakeInc_rule=${SRCROOT}/makedefs/MakeInc.rule
export MakeInc_dir=${SRCROOT}/makedefs/MakeInc.dir

include $(MakeInc_cmd)
include $(MakeInc_def)

DATAFILES= \

KERNELFILES= \

PRIVATE_DATAFILES = \
	classq.h classq_blue.h classq_red.h classq_rio.h classq_sfb.h \
	if_classq.h classq_fq_codel.h

PRIVATE_KERNELFILES = ${KERNELFILES}

INSTALL_MI_LIST	= ${DATAFILES}

INSTALL_MI_DIR = net/classq

EXPORT_MI_LIST	= ${INSTALL_MI_LIST} ${KERNELFILES}

EXPORT_MI_DIR = ${INSTALL_MI_DIR}

INSTALL_SF_MI_LCL_LIST = ${DATAFILES} ${PRIVATE_DATAFILES}

INSTALL_KF_MI_LCL_LIST = ${DATAFILES} ${PRIVATE_DATAFILES} ${PRIVATE_KERNELFILES}

include $(MakeInc_rule)
include $(MakeInc_dir)
