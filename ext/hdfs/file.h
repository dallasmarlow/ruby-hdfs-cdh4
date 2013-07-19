#ifndef HDFS_FILE_H
#define HDFS_FILE_H

#include "hdfs.h"
#include "ruby.h"


VALUE new_HDFS_File(VALUE path, hdfsFile* file, hdfsFS* fs);

void init_file(VALUE parent);

#endif /* HDFS_FILE_H */
