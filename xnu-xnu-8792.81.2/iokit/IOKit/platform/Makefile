export MakeInc_cmd=${SRCROOT}/makedefs/MakeInc.cmd
export MakeInc_def=${SRCROOT}/makedefs/MakeInc.def
export MakeInc_rule=${SRCROOT}/makedefs/MakeInc.rule
export MakeInc_dir=${SRCROOT}/makedefs/MakeInc.dir

include $(MakeInc_cmd)
include $(MakeInc_def)

MI_DIR = platform
NOT_EXPORT_HEADERS =
NOT_KF_MI_HEADERS  =

ALL_HEADERS = $(notdir $(wildcard $(SOURCE)*.h))

INSTALL_IF_MI_LIST	=
INSTALL_IF_MI_LCL_LIST =
INSTALL_MI_DIR = $(MI_DIR)

EXPORT_MI_LIST	= $(filter-out $(NOT_EXPORT_HEADERS), $(ALL_HEADERS))
EXPORT_MI_DIR = IOKit/$(MI_DIR)

INSTALL_KF_MI_LIST = $(filter-out $(NOT_KF_MI_HEADERS), $(ALL_HEADERS))

include $(MakeInc_rule)
include $(MakeInc_dir)
