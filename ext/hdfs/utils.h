#ifndef HDFS_UTILS_H
#define HDFS_UTILS_H

#include "ruby.h"


/* Converts a decimal-formatted integer to an octal-formatted integer. */
int decimal_octal(int n);

/* Converts an octal-formatted integer to a decimal-formatted integer. */
int octal_decimal(int n);

/* Returns a string representation of errno in a thread-safe manner. */
char* get_error(int errnum);

#endif /* HDFS_UTILS_H */
