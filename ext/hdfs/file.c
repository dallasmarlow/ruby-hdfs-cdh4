#include "file.h"

#include "constants.h"
#include "utils.h"


typedef struct FileData {
  hdfsFS fs;
  hdfsFile file;
} FileData;

static VALUE c_file;

static VALUE e_file_closed_error;
static VALUE e_file_error;


void free_file_data(FileData* data) {
  if (data && data->file != NULL) {
    hdfsCloseFile(data->fs, data->file);
    data->file = NULL;
  }
}

/* Ensures that a file is open; otherwise throws a FileError. */
void ensure_file_open(FileData* data) {
  if (data->file == NULL) {
    rb_raise(e_file_closed_error, "File is closed");
  }
}

FileData* get_FileData(VALUE rb_object) {
  FileData* data = NULL;
  Data_Get_Struct(rb_object, FileData, data);
  if (data->file == NULL) {
    rb_raise(e_file_closed_error, "File is closed");
  }
  return data;
}

VALUE new_HDFS_File(VALUE path, hdfsFile* file, hdfsFS* fs) {
  FileData* data = ALLOC_N(FileData, 1);
  data->fs = *fs;
  data->file = *file;
  VALUE file_instance = Data_Wrap_Struct(c_file, NULL, free_file_data,
      data);
  rb_iv_set(file_instance, "@path", path);
  return file_instance;
}

/*
 * HDFS::File
 */

/**
 * call-seq:
 *    file.available -> available_bytes
 *
 * Returns the number of bytes that can be read from this file without
 * blocking.  If this fails, raises a FileError.
 */
VALUE HDFS_File_available(VALUE self) {
  FileData* data = get_FileData(self);
  int bytes_available = hdfsAvailable(data->fs, data->file);
  if (bytes_available == -1) {
    rb_raise(e_file_error, "Failed to get available data: %s",
        get_error(errno));
  }
  return INT2NUM(bytes_available);
}

/**
 * call-seq:
 *    file.close -> success
 *
 * Closes the current file.  If this fails, raises a FileError.
 */
VALUE HDFS_File_close(VALUE self) {
  FileData* data = NULL;
  Data_Get_Struct(self, FileData, data);
  if (data->file != NULL) {
    if (hdfsCloseFile(data->fs, data->file) == -1) {
      rb_raise(e_file_error, "Could not close file: %s", get_error(errno));
      return Qnil;
    }
    data->file = NULL;
  }
  return Qtrue;
}

/**
 * call-seq:
 *    file.flush -> success
 *
 * Flushes all buffers currently being written to this file.  If this fails,
 * raises a FileError.
 */
VALUE HDFS_File_flush(VALUE self) {
  FileData* data = get_FileData(self);
  if (hdfsFlush(data->fs, data->file) == -1) {
    rb_raise(e_file_error, "Flush failed: %s", get_error(errno));
  }
  return Qtrue;
}

/**
 * call-seq:
 *    file.hflush -> success
 *
 * Flushes all buffers currently being written to this file.  When this
 * finishes, new readers will see the data that has been written.  If this
 * fails, raises a FileError.
 */
VALUE HDFS_File_hflush(VALUE self) {
  FileData* data = get_FileData(self);
  if (hdfsHFlush(data->fs, data->file) == -1) {
    rb_raise(e_file_error, "HFlush failed: %s", get_error(errno));
  }
  return Qtrue;
}

/**
 * call-seq:
 *    file.read(length=131072) -> retval
 *
 * Reads the number of bytes specified by length from the current file object,
 * returning the bytes read as a String.  If this fails, raises a
 * FileError.
 */ 
VALUE HDFS_File_read(int argc, VALUE* argv, VALUE self) {
  FileData* data = get_FileData(self);
  VALUE length;
  rb_scan_args(argc, argv, "01", &length);
  tSize hdfsLength = NIL_P(length) ? HDFS_DEFAULT_BUFFER_SIZE : NUM2INT(length);
  // Checks whether we're reading more data than HDFS client can support.
  if (hdfsLength > HDFS_DEFAULT_BUFFER_SIZE) {
    rb_raise(e_file_error, "Can only read a max of %u bytes from HDFS",
        HDFS_DEFAULT_BUFFER_SIZE);
    return Qnil;
  }
  char* buffer = ALLOC_N(char, hdfsLength);
  tSize bytes_read = hdfsRead(data->fs, data->file, buffer, hdfsLength);
  if (bytes_read == -1) {
    rb_raise(e_file_error, "Failed to read data: %s", get_error(errno));
  }
  VALUE string_output = rb_tainted_str_new(buffer, bytes_read);
  xfree(buffer);
  return string_output;
}

/**
 * call-seq:
 *    file.read_open? -> open_for_read
 *
 * Returns True if this file is open for reading; otherwise returns False.
 */
VALUE HDFS_File_read_open(VALUE self) {
  FileData* data = NULL;
  Data_Get_Struct(self, FileData, data);
  if (data->file) {
    return hdfsFileIsOpenForRead(data->file) ? Qtrue : Qfalse;
  } else {
    return Qfalse;
  }
}

/**
 * call-seq:
 *    file.read_pos(position, length=131072) -> retval
 *
 * Positionally reads the number of bytes specified by length at the specified
 * byte offset, returning the bytes read as a String.  If this fails, raises a
 * FileError.
 */ 
VALUE HDFS_File_read_pos(int argc, VALUE* argv, VALUE self) {
  FileData* data = get_FileData(self);
  VALUE position, length;
  rb_scan_args(argc, argv, "11", &position, &length);
  tSize hdfsLength = NIL_P(length) ? HDFS_DEFAULT_BUFFER_SIZE : NUM2INT(length);
  // Checks whether we're reading more data than HDFS client can support.
  if (hdfsLength > HDFS_DEFAULT_BUFFER_SIZE) {
    rb_raise(e_file_error, "Can only read a max of %u bytes from HDFS",
        HDFS_DEFAULT_BUFFER_SIZE);
    return Qnil;
  }
  char* buffer = ALLOC_N(char, hdfsLength);
  tSize bytes_read = hdfsPread(data->fs, data->file, NUM2ULONG(position),
      buffer, hdfsLength);
  if (bytes_read == -1) {
    rb_raise(e_file_error, "Failed to read data: %s", get_error(errno));
  }
  VALUE string_output = rb_tainted_str_new(buffer, bytes_read);
  xfree(buffer);
  return string_output;
}

/**
 * call-seq:
 *    file.seek(offset) -> success
 *
 * Seeks the file pointer to the supplied offset in bytes.  If this fails,
 * raises a FileError.
 */
VALUE HDFS_File_seek(VALUE self, VALUE offset) {
  FileData* data = get_FileData(self);
  if (hdfsSeek(data->fs, data->file, NUM2ULONG(offset)) == -1) {
    rb_raise(e_file_error, "Failed to seek to position %lu: %s",
        NUM2ULONG(offset), get_error(errno));
  }
  return Qtrue;
}

/**
 * call-seq:
 *    file.tell -> current_position
 *
 * Returns the current byte position in bytes of the file as an Integer.
 * If this fails, raises a FileError.
 */
VALUE HDFS_File_tell(VALUE self) {
  FileData* data = get_FileData(self);
  tOffset offset = hdfsTell(data->fs, data->file);
  if (offset == -1) {
    rb_raise(e_file_error, "Failed to read position: %s", get_error(errno));
  }
  return ULONG2NUM(offset);
}

/**
 * call-seq:
 *    file.write(bytes) -> num_bytes_written
 *
 * Writes the string specified by bytes to the current file object, returning
 * the number of bytes written as an Integer.  If this fails, raises a
 * FileError.
 */
VALUE HDFS_File_write(VALUE self, VALUE bytes) {
  FileData* data = get_FileData(self);
  tSize num_bytes = NUM2UINT(rb_funcall(bytes, rb_intern("bytesize"), 0));
  tSize bytes_written = hdfsWrite(data->fs, data->file, StringValuePtr(bytes),
      num_bytes);
  if (bytes_written == -1) {
    rb_raise(e_file_error, "Failed to write data: %s", get_error(errno));
  }
  return UINT2NUM(bytes_written);
}

/**
 * call-seq:
 *    file.write_open? -> open_for_write
 *
 * Returns True if this file is open for writing; otherwise returns False.
 */
VALUE HDFS_File_write_open(VALUE self) {
  FileData* data = NULL;
  Data_Get_Struct(self, FileData, data);
  if (data->file) {
    return hdfsFileIsOpenForWrite(data->file) ? Qtrue : Qfalse;
  } else {
    return Qfalse;
  }
}

VALUE HDFS_File_to_s(VALUE self) {
  VALUE path = rb_iv_get(self, "@path");
  return rb_sprintf("#<HDFS::File: %s>", StringValuePtr(path));
}

void init_file(VALUE parent) {
  c_file = rb_define_class_under(parent, "File", rb_cObject);

  rb_define_method(c_file, "available", HDFS_File_available, 0);
  rb_define_method(c_file, "close", HDFS_File_close, 0);
  rb_define_method(c_file, "flush", HDFS_File_flush, 0);
  rb_define_method(c_file, "hflush", HDFS_File_hflush, 0);
  rb_define_method(c_file, "read", HDFS_File_read, -1);
  rb_define_method(c_file, "read_open?", HDFS_File_read_open, 0);
  rb_define_method(c_file, "read_pos", HDFS_File_read_pos, -1);
  rb_define_method(c_file, "seek", HDFS_File_seek, 1);
  rb_define_method(c_file, "tell", HDFS_File_tell, 0);
  rb_define_method(c_file, "to_s", HDFS_File_to_s, 0);
  rb_define_method(c_file, "write", HDFS_File_write, 1);
  rb_define_method(c_file, "write_open?", HDFS_File_write_open, 0);
  rb_define_method(c_file, "<<", HDFS_File_write, 1);

  e_file_error = rb_define_class_under(parent, "FileError", rb_eException);
  e_file_closed_error = rb_define_class_under(parent, "FileClosedError",
      e_file_error);
}
