export MakeInc_cmd=${SRCROOT}/makedefs/MakeInc.cmd
export MakeInc_def=${SRCROOT}/makedefs/MakeInc.def
export MakeInc_rule=${SRCROOT}/makedefs/MakeInc.rule
export MakeInc_dir=${SRCROOT}/makedefs/MakeInc.dir

include $(MakeInc_cmd)
include $(MakeInc_def)

DATAFILES = task_corpse.h

INSTALL_MI_LIST	= ${DATAFILES}

INSTALL_SF_MI_LCL_LIST = ${DATAFILES}

INSTALL_MI_DIR = corpses

EXPORT_MI_LIST	= ${DATAFILES}

EXPORT_MI_DIR = corpses

include $(MakeInc_rule)
include $(MakeInc_dir)
