#ifndef HDFS_CONSTANTS_H
#define HDFS_CONSTANTS_H

#include "hdfs.h"

#include <ctype.h>


static const tSize HDFS_DEFAULT_BUFFER_SIZE    = 131072;
static const char* HDFS_DEFAULT_HOST           = "0.0.0.0";
static const short HDFS_DEFAULT_MODE           = 0644;
static const int HDFS_DEFAULT_PORT             = 8020;
static const int HDFS_DEFAULT_RECURSIVE_DELETE = 0;
static const int16_t HDFS_DEFAULT_REPLICATION  = 3;
static const int HDFS_DEFAULT_STRING_LENGTH    = 1024;
static const char* HDFS_DEFAULT_USER           = NULL;

#endif /* HDFS_CONSTANTS_H */
