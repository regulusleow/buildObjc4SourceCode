export MakeInc_cmd=${SRCROOT}/makedefs/MakeInc.cmd
export MakeInc_def=${SRCROOT}/makedefs/MakeInc.def
export MakeInc_rule=${SRCROOT}/makedefs/MakeInc.rule
export MakeInc_dir=${SRCROOT}/makedefs/MakeInc.dir

include $(MakeInc_cmd)
include $(MakeInc_def)

DATAFILES = \
	union.h

KERNELFILES = \
	${DATAFILES}

INSTALL_MI_LIST	= ${DATAFILES}

INSTALL_SF_MI_LCL_LIST = ${DATAFILES}

INSTALL_MI_DIR = miscfs/union

EXPORT_MI_LIST = ${KERNELFILES}

EXPORT_MI_DIR = miscfs/union

include $(MakeInc_rule)
include $(MakeInc_dir)
