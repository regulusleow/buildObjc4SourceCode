export MakeInc_cmd=${SRCROOT}/makedefs/MakeInc.cmd
export MakeInc_def=${SRCROOT}/makedefs/MakeInc.def
export MakeInc_rule=${SRCROOT}/makedefs/MakeInc.rule
export MakeInc_dir=${SRCROOT}/makedefs/MakeInc.dir

include $(MakeInc_cmd)
include $(MakeInc_def)

DATAFILES = \
	devfs.h

KERNELFILES = \
	devfs.h \
	fdesc.h \
	devfs_proto.h \
	devfsdefs.h

INSTALL_MI_LIST	= ${DATAFILES}

INSTALL_SF_MI_LCL_LIST = ${DATAFILES}

INSTALL_MI_DIR = miscfs/devfs

INSTALL_KF_MI_LIST = ${DATAFILES}

INSTALL_KF_MI_LCL_LIST = ${DATAFILES} devfs_proto.h devfsdefs.h

EXPORT_MI_LIST	= ${KERNELFILES}

EXPORT_MI_DIR =	miscfs/devfs

include $(MakeInc_rule)
include $(MakeInc_dir)
