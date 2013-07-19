#include "utils.h"

#include "constants.h"

#include <errno.h>
#include <math.h>


/*
 * Utility functions.
 */

// Borrowed from:
// http://www.programiz.com/c-programming/examples/octal-decimal-convert
int decimal_octal(int n) {
  int rem, i=1, octal=0;
  while (n != 0) {
    rem = n % 8;
    n /= 8;
    octal += rem * i;
    i *= 10;
  }
  return octal;
}

int octal_decimal(int n) {
  int decimal=0, i=0, rem;
  while (n != 0) {
    rem = n % 10;
    n /= 10;
    decimal += rem * pow(8, i);
    ++i;
  }
  return decimal;
}

char* get_error(int errnum) {
  // Renames EINTERNAL to something a bit more intelligible.
  if (errnum == 255) {
    return RSTRING_PTR(rb_str_new2("Internal Error"));
  }
  char* buffer = ALLOC_N(char, HDFS_DEFAULT_STRING_LENGTH);
  char* error_string = strerror_r(errnum, buffer, HDFS_DEFAULT_STRING_LENGTH);
  // Wraps the message in a Ruby string so that it will be garbage collected.
  VALUE error_msg = rb_str_new2(error_string);
  xfree(buffer);
  return RSTRING_PTR(error_msg);
}
