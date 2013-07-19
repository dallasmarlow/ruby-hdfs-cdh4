#ifndef HDFS_FILE_INFO_H
#define HDFS_FILE_INFO_H

#include "hdfs.h"
#include "ruby.h"


VALUE new_HDFS_File_Info(hdfsFileInfo* info);

void init_file_info(VALUE parent);

#endif /* HDFS_FILE_INFO_H */
