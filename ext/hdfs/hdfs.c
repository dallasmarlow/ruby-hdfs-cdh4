#include "ruby.h"
#include "hdfs.h"

#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

static VALUE m_hadoop;
static VALUE m_dfs;
static VALUE c_file;
static VALUE c_file_info;
static VALUE c_file_system;
static VALUE c_file_info_file;
static VALUE c_file_info_directory;
static VALUE e_dfs_exception;
static VALUE e_file_error;
static VALUE e_could_not_open;
static VALUE e_does_not_exist;

static const int32_t HDFS_DEFAULT_BLOCK_SIZE     = 134217728;
static const int16_t HDFS_DEFAULT_REPLICATION    = 3;
static const short HDFS_DEFAULT_MODE             = 0644;
static const char* HDFS_DEFAULT_HOST             = "localhost";
static const int HDFS_DEFAULT_RECURSIVE_DELETE   = 0;
static const int HDFS_DEFAULT_PATH_STRING_LENGTH = 1024;
static const int HDFS_DEFAULT_PORT               = 8020;

/*
 * Data structs
 */

typedef struct FSData {
  hdfsFS fs;
} FSData;

typedef struct FileData {
  hdfsFS fs;
  hdfsFile file;
} FileData;

typedef struct FileInfo {
  char* mName;         /* the name of the file */
  tTime mLastMod;      /* the last modification time for the file in seconds */
  tOffset mSize;       /* the size of the file in bytes */
  short mReplication;  /* the count of replicas */
  tOffset mBlockSize;  /* the block size for the file */
  char* mOwner;        /* the owner of the file */
  char* mGroup;        /* the group associated with the file */
  short mPermissions;  /* the permissions associated with the file */
  tTime mLastAccess;   /* the last access time for the file in seconds */
} FileInfo;

/*
 * Methods called upon freeing of objects.
 */

void free_fs_data(FSData* data) {
  if (data && data->fs != NULL) {
    hdfsDisconnect(data->fs);
    data->fs = NULL;
  }
}

void free_file_data(FileData* data) {
  if (data && data->file != NULL) {
    hdfsCloseFile(data->fs, data->file);
    data->file = NULL;
  }
}

void free_file_info(FileInfo* file_info) {
  if (file_info) {
    free(file_info->mName);
    free(file_info->mOwner);
    free(file_info->mGroup);
    free(file_info);
  }
}

/*
 * Helper functions
 */

// Borrowed from:
// http://www.programiz.com/c-programming/examples/octal-decimal-convert
/* Converts a decimal-formatted integer to an octal-formatted integer. */
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

/* Converts an octal-formatted integer to a decimal-formatted integer. */
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

/*
 * Copies an hdfsFileInfo struct into a Hadoop::DFS::FileInfo derivative
 * object.
 */
VALUE wrap_hdfsFileInfo(hdfsFileInfo* info) {
  // Creates a FileInfo struct, populates it with information from the
  // supplied hdfsFileInfo struct.
  FileInfo* file_info = ALLOC_N(FileInfo, 1);
  file_info->mName = strdup(info->mName);
  file_info->mLastMod = info->mLastMod;
  file_info->mSize = info->mSize;
  file_info->mReplication = info->mReplication;
  file_info->mBlockSize = info->mBlockSize;
  file_info->mOwner = strdup(info->mOwner);
  file_info->mGroup = strdup(info->mGroup);
  file_info->mPermissions = info->mPermissions;
  file_info->mLastAccess = info->mLastAccess;
  // Assigns FileInfo::Info or FileInfo::Directory class based upon the type of
  // the file.
  switch(info->mKind) {
    case kObjectKindDirectory:
      return Data_Wrap_Struct(c_file_info_directory, NULL, free_file_info,
          file_info);
    case kObjectKindFile:
      return Data_Wrap_Struct(c_file_info_file, NULL, free_file_info,
          file_info);
    default:
      rb_raise(e_dfs_exception, "File was not a file or directory: %s",
          RSTRING_PTR(info->mName));
  }
  return Qnil;
}

/*
 * File system interface
 */

VALUE HDFS_File_System_alloc(VALUE klass) {
  FSData* data = ALLOC_N(FSData, 1);
  data->fs = NULL;
  VALUE instance = Data_Wrap_Struct(klass, NULL, free_fs_data, data);
  return instance;
}

/**
 * call-seq:
 *    hdfs.new -> hdfs
 *
 * Creates a new HDFS client connection.
 */
VALUE HDFS_File_System_initialize(VALUE self, VALUE host, VALUE port) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  data->fs = hdfsConnect(
    RTEST(host) ? RSTRING_PTR(host) : HDFS_DEFAULT_HOST,
    RTEST(port) ? NUM2INT(port) : HDFS_DEFAULT_PORT);  
  return self;
}

/**
 * call-seq:
 *    hdfs.disconnect -> nil
 *
 * Disconnects the client connection.
 */
VALUE HDFS_File_System_disconnect(VALUE self) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  if (data->fs != NULL) {
    hdfsDisconnect(data->fs);
    data->fs = NULL;
  }
  return Qnil;
}

/**
 * call-seq:
 *    hdfs.delete(path, recursive=false) -> success
 *
 * Deletes the file at the supplied path, recursively if specified.  Returns
 * True if successful, False if unsuccessful.
 */
VALUE HDFS_File_System_delete(VALUE self, VALUE path, VALUE recursive) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  int success = hdfsDelete(data->fs, RSTRING_PTR(path),
      CheckType(recursive, T_TRUE) ? 1 : HDFS_DEFAULT_RECURSIVE_DELETE);
  return success == 0 ? Qtrue : Qfalse;
}

/**
 * call-seq:
 *    hdfs.rename(from_path, to_path) -> success
 *
 * Renames the file at the supplied path to the file at the destination path.
 * Returns True if successful, False if unsuccessful.
 */
VALUE HDFS_File_System_rename(VALUE self, VALUE from_path, VALUE to_path) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  int success = hdfsRename(data->fs, RSTRING_PTR(from_path), RSTRING_PTR(to_path));
  return success == 0 ? Qtrue : Qfalse;
}

/**
 * call-seq:
 *    hdfs.exist(path) -> file_existence
 *
 * Checks if a file exists at the supplied path.  If file exists, returns True;
 * if not, returns False.
 */
VALUE HDFS_File_System_exist(VALUE self, VALUE path) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  int success = hdfsExists(data->fs, RSTRING_PTR(path));
  return success == 0 ? Qtrue : Qfalse;
}

/**
 * call-seq:
 *    hdfs.create_directory(path) -> success
 *
 * Checks if a file exists at the supplied path.  If file exists, returns True;
 * if not, returns False.
 */
VALUE HDFS_File_System_create_directory(VALUE self, VALUE path) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  int success = hdfsCreateDirectory(data->fs, RSTRING_PTR(path));
  return success == 0 ? Qtrue : Qfalse;
}

/**
 * call-seq:
 *    hdfs.list_directory(path) -> file_infos
 *
 * Lists the directory at the supplied path, returning an Array of
 * Hadoop::DFS::FileInfo objects.  If the directory does not exist, raises
 * a DoesNotExistError.
 */
VALUE HDFS_File_System_list_directory(VALUE self, VALUE path) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  VALUE file_infos = rb_ary_new();
  int num_files = 0;
  hdfsFileInfo* infos = hdfsListDirectory(data->fs, RSTRING_PTR(path),
      &num_files);
  if (infos == NULL) {
    rb_raise(e_does_not_exist, "Directory does not exist: %s",
        RSTRING_PTR(path));
    return Qnil;
  }
  int i;
  for (i = 0; i < num_files; i++) {
    hdfsFileInfo* cur_info = infos + i;
    rb_ary_push(file_infos, wrap_hdfsFileInfo(cur_info));
  }
  hdfsFreeFileInfo(infos, num_files);
  return file_infos;
}

/**
 * call-seq:
 *    hdfs.stat(path) -> file_info
 *
 * Stats the file or directory at the supplied path, returning a
 * Hadoop::DFS:FileInfo object corresponding to it.  If the file or directory
 * does not exist, raises a DoesNotExistError.
 */
VALUE HDFS_File_System_stat(VALUE self, VALUE path) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  hdfsFileInfo* info = hdfsGetPathInfo(data->fs, RSTRING_PTR(path));
  if (info == NULL) {
    rb_raise(e_does_not_exist, "File does not exist: %s", RSTRING_PTR(path));
    return Qnil;
  }
  VALUE file_info = wrap_hdfsFileInfo(info);
  hdfsFreeFileInfo(info, 1);
  return file_info;
}

/**
 * call-seq:
 *    hdfs.set_replication(path, replication) -> success
 *
 * Sets the replication of the following path to the supplied number of nodes
 * it will be replicated against.  Returns True if successful; False if not.
 */
VALUE HDFS_File_System_set_replication(VALUE self, VALUE path, VALUE replication) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  int success = hdfsSetReplication(data->fs, RSTRING_PTR(path),
      RTEST(replication) ? NUM2INT(replication) : HDFS_DEFAULT_REPLICATION);
  return success == 0 ? Qtrue : Qfalse;
}

/**
 * call-seq:
 *    hdfs.cd(path) -> success
 *
 * Changes the current working directory to the supplied path.  Returns True if
 * successful; False if not.
 */
VALUE HDFS_File_System_cd(VALUE self, VALUE path) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  int success = hdfsSetWorkingDirectory(data->fs, RSTRING_PTR(path));
  return success == 0 ? Qtrue : Qfalse;
}

/**
 * call-seq:
 *    hdfs.cwd -> success
 *
 * Changes the current working directory to the supplied path.  Returns True if
 * successful; False if not.
 */
VALUE HDFS_File_System_cwd(VALUE self) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  char* cur_dir = (char *) malloc(
      sizeof(char) * HDFS_DEFAULT_PATH_STRING_LENGTH);
  int success = hdfsGetWorkingDirectory(data->fs, cur_dir,
      HDFS_DEFAULT_PATH_STRING_LENGTH);
  VALUE ruby_cur_dir = rb_str_new2(cur_dir);
  free(cur_dir);
  return ruby_cur_dir;
}

/**
 * call-seq:
 *    hdfs.chgrp(path, group) -> success
 *
 * Changes the group of the supplied path.  Returns True if successful; False
 * if not.
 */
VALUE HDFS_File_System_chgrp(VALUE self, VALUE path, VALUE group) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  int success = hdfsChown(data->fs, RSTRING_PTR(path), NULL,
      RSTRING_PTR(group));
  return success == 0 ? Qtrue : Qfalse;
}

/**
 * call-seq:
 *    hdfs.chgrp(path, mode) -> retval
 *
 * Changes the mode of the supplied path.  Returns True if successful; False
 * if not.
 */
VALUE HDFS_File_System_chmod(VALUE self, VALUE path, VALUE mode) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  int success = hdfsChmod(data->fs, RSTRING_PTR(path),
      (short) RTEST(mode) ? octal_decimal(NUM2INT(mode)) : HDFS_DEFAULT_MODE);
  return success == 0 ? Qtrue : Qfalse;
}

/**
 * call-seq:
 *    hdfs.chown(path, owner) -> retval
 *
 * Changes the owner of the supplied path.  Returns True if successful; False
 * if not.
 */
VALUE HDFS_File_System_chown(VALUE self, VALUE path, VALUE owner) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  int success = hdfsChown(data->fs, RSTRING_PTR(path), RSTRING_PTR(owner),
      NULL);
  return success == 0 ? Qtrue : Qfalse;
}

/**
 * call-seq:
 *    hdfs.copy(from_path, to_path, to_fs=nil) -> retval
 *
 * Copies the file at HDFS location from_path to HDFS location to_path.  If
 * to_fs is specified, copies to this HDFS over the current HDFS.  If
 * successful, returns true; otherwise, returns false.
 */
VALUE HDFS_File_System_copy(VALUE self, VALUE from_path, VALUE to_path, VALUE to_fs) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  hdfsFS destFS = data->fs;
  if (RTEST(to_fs)) {
    if (CLASS_OF(to_fs) == c_file_system) {
      FSData* destFSData = NULL;
      Data_Get_Struct(to_fs, FSData, destFSData);
      destFS = destFSData->fs;
    } else {
      rb_raise(rb_eArgError, "to_fs must be of type Hadoop::DFS::FileSystem");
      return Qnil;
    }
  }
  int success = hdfsCopy(data->fs, RSTRING_PTR(from_path), destFS,
      RSTRING_PTR(to_fs));
  return success == 0 ? Qtrue : Qfalse;
}

/**
 * call-seq:
 *    hdfs.move(from_path, to_path, to_fs=nil) -> retval
 *
 * Moves the file at HDFS location from_path to HDFS location to_path.  If
 * to_fs is specified, moves to this HDFS over the current HDFS.  If
 * successful, returns true; otherwise, returns false.
 */
VALUE HDFS_File_System_move(VALUE self, VALUE from_path, VALUE to_path, VALUE to_fs) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  hdfsFS destFS = data->fs;
  if (RTEST(to_fs)) {
    if (CLASS_OF(to_fs) == c_file_system) {
      FSData* destFSData = NULL;
      Data_Get_Struct(to_fs, FSData, destFSData);
      destFS = destFSData->fs;
    } else {
      rb_raise(rb_eArgError, "to_fs must be of type Hadoop::DFS::FileSystem");
      return Qnil;
    }
  }
  int success = hdfsMove(data->fs, RSTRING_PTR(from_path), destFS,
      RSTRING_PTR(to_fs));
  return success == 0 ? Qtrue : Qfalse;
}

/**
 * call-seq:
 *    hdfs.capacity -> retval
 *
 * Returns the capacity of this HDFS file system in bytes, raising a
 * DFSException if this was unsuccessful.
 */
VALUE HDFS_File_System_capacity(VALUE self) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  long capacity = hdfsGetCapacity(data->fs);
  if (capacity < 0) {
    rb_raise(e_dfs_exception, "Error while retrieving capacity");
    return Qnil;
  }
  return LONG2NUM(capacity);
}

/**
 * call-seq:
 *    hdfs.capacity -> retval
 *
 * Returns the default block size of this HDFS file system in bytes, raising a
 * DFSException if this was unsuccessful.
 */
VALUE HDFS_File_System_default_block_size(VALUE self) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  long block_size = hdfsGetDefaultBlockSize(data->fs);
  if (block_size < 0) {
    rb_raise(e_dfs_exception, "Error while retrieving default block size");
    return Qnil;
  }
  return LONG2NUM(block_size);
}

/**
 * call-seq:
 *    hdfs.default_block_size_at_path(path) -> default_block_size
 *
 * Returns the default block size at the supplied HDFS path, raising a
 * DFSException if this was unsuccessful.
 */
VALUE HDFS_File_System_default_block_size_at_path(VALUE self, VALUE path) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  long block_size = hdfsGetDefaultBlockSizeAtPath(data->fs, RSTRING_PTR(path));
  if (block_size < 0) {
    rb_raise(e_dfs_exception,
        "Error while retrieving default block size at path: %s", RSTRING_PTR(path));
    return Qnil;
  }
  return LONG2NUM(block_size);
}

/**
 * call-seq:
 *    hdfs.used -> retval
 *
 * Returns the bytes currently in use by this filesystem, raising a
 * DFSException if unsuccessful.
 */
VALUE HDFS_File_System_used(VALUE self) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  long used = hdfsGetUsed(data->fs);
  if (used < 0) {
    rb_raise(e_dfs_exception, "Error while retrieving used capacity");
    return Qnil;
  }
  return LONG2NUM(used);
}

/**
 * call-seq:
 *    hdfs.utime(path, modified_time=nil, access_time=nil) -> retval
 *
 * Changes the last modified and/or last access time in seconds since the Unix
 * epoch for the supplied file.  Returns true if successful; false if not.
 */
VALUE HDFS_File_System_utime(VALUE self, VALUE path, VALUE modified_time, VALUE access_time) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  int success = hdfsUtime(data->fs, RSTRING_PTR(path),
      (tTime) RTEST(modified_time) ? NUM2INT(modified_time) : -1,
      (tTime) RTEST(access_time) ? NUM2INT(access_time) : -1);
  return success == 0 ? Qtrue : Qfalse;
}

/**
 * call-seq:
 *    hdfs.open(path, mode, options = {}) -> file
 *
 * Opens a file.  If the file cannot be opened, raises a CouldNotOpenError;
 * otherwise, returns a Hadoop::DFS::File object corresponding to the file.
 */
VALUE HDFS_File_System_open(VALUE self, VALUE path, VALUE mode, VALUE options) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);

  int flags = 0;
  if (strcmp("r", StringValuePtr(mode)) == 0) {
    flags = O_RDONLY;
  } else if (strcmp("w", StringValuePtr(mode)) == 0) {
    flags = O_WRONLY;
  } else {
    rb_raise(rb_eArgError, "Mode must be 'r' or 'w'");
    return Qnil;
  }
  VALUE r_buffer_size = rb_hash_aref(options, rb_eval_string(":buffer_size"));
  VALUE r_replication = rb_hash_aref(options, rb_eval_string(":replication"));
  VALUE r_block_size = rb_hash_aref(options, rb_eval_string(":block_size"));
  hdfsFile file = hdfsOpenFile(data->fs, RSTRING_PTR(path), flags, 
    RTEST(r_buffer_size) ? NUM2INT(r_buffer_size) : 0, 
    RTEST(r_replication) ? NUM2INT(r_replication) : 0, 
    RTEST(r_block_size) ? NUM2INT(r_block_size) : HDFS_DEFAULT_BLOCK_SIZE);
  if (file == NULL) {
    rb_raise(e_could_not_open, "Could not open file %s", RSTRING_PTR(path));
    return Qnil;
  }

  FileData* file_data = ALLOC_N(FileData, 1);
  file_data->fs = data->fs;
  file_data->file = file;
  VALUE file_instance = Data_Wrap_Struct(c_file, NULL, free_file_data, file_data);
  return file_instance;
}

/*
 * File interface
 */

/**
 * call-seq:
 *    file.read(length) -> retval
 *
 * Reads the number of bytes specified by length from the current file object,
 * returning the bytes read as a String.  If this fails, raises a
 * FileError.
 */ 
VALUE HDFS_File_read(VALUE self, VALUE length) {
  FileData* data = NULL;
  Data_Get_Struct(self, FileData, data);
  char* buffer = ALLOC_N(char, length);
  MEMZERO(buffer, char, length);
  tSize bytes_read = hdfsRead(data->fs, data->file, buffer, NUM2INT(length));
  if (bytes_read == -1) {
    rb_raise(e_file_error, "Failed to read data");
  }
  return rb_tainted_str_new2(buffer);
}

/**
 * call-seq:
 *    file.write(bytes) -> num_bytes_written
 *
 * Writes the string specified by bytes to the current file object, returning
 * the number of bytes read as an Integer.  If this fails, raises a FileError.
 */
VALUE HDFS_File_write(VALUE self, VALUE bytes) {
  FileData* data = NULL;
  Data_Get_Struct(self, FileData, data);
  tSize bytes_written = hdfsWrite(data->fs, data->file, RSTRING_PTR(bytes), RSTRING_LEN(bytes));
  if (bytes_written == -1) {
    rb_raise(e_file_error, "Failed to write data");
  }
  return INT2NUM(bytes_written);
}

/**
 * call-seq:
 *    file.tell -> current_position
 *
 * Returns the current byte position in the file as an Integer.  If this fails,
 * raises a FileError.
 */
VALUE HDFS_File_tell(VALUE self) {
  FileData* data = NULL;
  Data_Get_Struct(self, FileData, data);
  tSize offset = hdfsTell(data->fs, data->file);
  if (offset == -1) {
    rb_raise(e_file_error, "Failed to read position");
  }
  return INT2NUM(offset);
}

/**
 * call-seq:
 *    file.seek(offset) -> success
 *
 * Seeks the file pointer to the supplied offset.  If this fails, raises a
 * FileError.
 */
VALUE HDFS_File_seek(VALUE self, VALUE offset) {
  FileData* data = NULL;
  Data_Get_Struct(self, FileData, data);
  int result = hdfsSeek(data->fs, data->file, NUM2INT(offset));
  if (result != 0) {
    rb_raise(e_file_error, "Failed to seek to position %d", NUM2INT(offset));
  }
  return Qtrue;
}

/**
 * call-seq:
 *    file.flush -> success
 *
 * Flushes all buffers currently being written to this file.  When this
 * finishes, new readers will see the data written.  If this fails, raises a
 * FileError.
 */
VALUE HDFS_File_flush(VALUE self) {
  FileData* data = NULL;
  Data_Get_Struct(self, FileData, data);
  int result = hdfsFlush(data->fs, data->file);
  if (result != 0) {
    rb_raise(e_file_error, "Flush failed");
  }
  return Qtrue;
}

/**
 * call-seq:
 *    file.available -> available_bytes
 *
 * Returns the number of bytes that can be read from this file without
 * blocking.  If this fails, raises a FileError.
 */
VALUE HDFS_File_available(VALUE self) {
  FileData* data = NULL;
  Data_Get_Struct(self, FileData, data);
  int result = hdfsAvailable(data->fs, data->file);
  if (result == -1) {
    rb_raise(e_file_error, "Failed to get available data");
  }
  return INT2NUM(result);
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
    hdfsCloseFile(data->fs, data->file);
    data->file = NULL;
  }
  return Qtrue;
}

/**
 * HDFS File Info interface
 */

/**
 * call-seq:
 *    file_info.block_size -> retval
 *
 * Returns the block size of the file described by this object.
 */
VALUE HDFS_File_Info_block_size(VALUE self) {
  FileInfo* file_info = NULL;
  Data_Get_Struct(self, FileInfo, file_info);
  return INT2NUM(file_info->mBlockSize);
}

/**
 * call-seq:
 *    file_info.group -> retval
 *
 * Returns the group of the file described by this object.
 */
VALUE HDFS_File_Info_group(VALUE self) {
  FileInfo* file_info = NULL;
  Data_Get_Struct(self, FileInfo, file_info);
  return rb_str_new(file_info->mGroup, strlen(file_info->mGroup));
}

/**
 * call-seq:
 *    file_info.is_directory? -> retval
 *
 * Returns True if the file described by this object is a directory; otherwise,
 * returns False.
 */
VALUE HDFS_File_Info_is_directory(VALUE self) {
  return Qfalse;
}

/**
 * call-seq:
 *    file_info.is_file? -> retval
 *
 * Returns True if the file described by this object is a file; otherwise,
 * returns False.
 */
VALUE HDFS_File_Info_is_file(VALUE self) {
  return Qfalse;
}

VALUE HDFS_File_Info_Directory_is_directory(VALUE self) {
  return Qtrue;
}

VALUE HDFS_File_Info_File_is_file(VALUE self) {
  return Qtrue;
}

/**
 * call-seq:
 *    file_info.last_access -> retval
 *
 * Returns the time of last access as an Integer representing seconds since the
 * epoch.
 */
VALUE HDFS_File_Info_last_access(VALUE self) {
  FileInfo* file_info = NULL;
  Data_Get_Struct(self, FileInfo, file_info);
  return LONG2NUM(file_info->mLastAccess);
}

/**
 * call-seq:
 *    file_info.last_modified -> retval
 *
 * Returns the time of last modification as an Integer representing seconds
 * since the epoch for the file
 */
VALUE HDFS_File_Info_last_modified(VALUE self) {
  FileInfo* file_info = NULL;
  Data_Get_Struct(self, FileInfo, file_info);
  return LONG2NUM(file_info->mLastMod);
}

/**
 * call-seq:
 *    file_info.last_modified -> retval
 *
 * Returns the time of last modification as an Integer representing seconds
 * since the epoch.
 */
VALUE HDFS_File_Info_mode(VALUE self) {
  FileInfo* file_info = NULL;
  Data_Get_Struct(self, FileInfo, file_info);
  return INT2NUM(decimal_octal(file_info->mPermissions));
}

/**
 * call-seq:
 *    file_info.name -> retval
 *
 * Returns the name of the file as a String.
 */
VALUE HDFS_File_Info_name(VALUE self) {
  FileInfo* file_info = NULL;
  Data_Get_Struct(self, FileInfo, file_info);
  return rb_str_new(file_info->mName, strlen(file_info->mName));
}

/**
 * call-seq:
 *    file_info.owner -> retval
 *
 * Returns the owner of the file as a String.
 */
VALUE HDFS_File_Info_owner(VALUE self) {
  FileInfo* file_info = NULL;
  Data_Get_Struct(self, FileInfo, file_info);
  return rb_str_new(file_info->mOwner, strlen(file_info->mOwner));
}

/**
 * call-seq:
 *    file_info.replication -> retval
 *
 * Returns the replication factor of the file as an Integer.
 */
VALUE HDFS_File_Info_replication(VALUE self) {
  FileInfo* file_info = NULL;
  Data_Get_Struct(self, FileInfo, file_info);
  return INT2NUM(file_info->mReplication);
}

/**
 * call-seq:
 *    file_info.name -> retval
 *
 * Returns the size of the file as an Integer.
 */
VALUE HDFS_File_Info_size(VALUE self) {
  FileInfo* file_info = NULL;
  Data_Get_Struct(self, FileInfo, file_info);
  return LONG2NUM(file_info->mSize);
}

/**
 * call-seq:
 *    file_info.to_s -> retval
 *
 * Returns a human-readable representation of a Hadoop::DFS::FileSystem object
 * as a String.
 */
VALUE HDFS_File_Info_to_s(VALUE self) {
  FileInfo* file_info = NULL;
  Data_Get_Struct(self, FileInfo, file_info);
  // Introspects current class, returns it as a String.
  VALUE class_string = rb_funcall(
      rb_funcall(self, rb_intern("class"), 0),
      rb_intern("to_s"), 0);
  char* output;
  VALUE string_value = rb_str_new2("");
  // If asprintf was successful, creates a Ruby String.
  if (asprintf(&output, "#<%s: %s, mode=%d, owner=%s, group=%s>",
          RSTRING_PTR(class_string), file_info->mName,
          decimal_octal(file_info->mPermissions), file_info->mOwner,
          file_info->mGroup) >= 0) {
    string_value = rb_str_new(output, strlen(output));
  }
  free(output);
  return string_value;
}

/*
 * Extension initialization
 */
 
void Init_hdfs() {
  m_hadoop = rb_define_module("Hadoop");
  m_dfs = rb_define_module_under(m_hadoop, "DFS");

  c_file_system = rb_define_class_under(m_dfs, "FileSystem", rb_cObject);
  rb_define_alloc_func(c_file_system, HDFS_File_System_alloc);
  rb_define_method(c_file_system, "initialize", HDFS_File_System_initialize, 2);
  rb_define_method(c_file_system, "disconnect", HDFS_File_System_disconnect, 0);
  rb_define_method(c_file_system, "open", HDFS_File_System_open, 3);
  rb_define_method(c_file_system, "delete", HDFS_File_System_delete, 1);
  rb_define_method(c_file_system, "rename", HDFS_File_System_rename, 2);
  rb_define_method(c_file_system, "exist?", HDFS_File_System_exist, 1);
  rb_define_method(c_file_system, "create_directory", HDFS_File_System_create_directory, 1);
  rb_define_method(c_file_system, "list_directory", HDFS_File_System_list_directory, 1);
  rb_define_method(c_file_system, "stat", HDFS_File_System_stat, 1);
  rb_define_method(c_file_system, "set_replication", HDFS_File_System_set_replication, 2);
  rb_define_method(c_file_system, "cd", HDFS_File_System_cd, 1);
  rb_define_method(c_file_system, "cwd", HDFS_File_System_cwd, 0);
  rb_define_method(c_file_system, "chgrp", HDFS_File_System_chgrp, 2);
  rb_define_method(c_file_system, "chmod", HDFS_File_System_chmod, 2);
  rb_define_method(c_file_system, "chown", HDFS_File_System_chown, 2);
  rb_define_method(c_file_system, "copy", HDFS_File_System_copy, 2);
  rb_define_method(c_file_system, "capacity", HDFS_File_System_capacity, 0);
  rb_define_method(c_file_system, "default_block_size",
      HDFS_File_System_default_block_size, 0);
  rb_define_method(c_file_system, "default_block_size_at_path",
      HDFS_File_System_default_block_size_at_path, 1);
  rb_define_method(c_file_system, "move", HDFS_File_System_move, 2);
  rb_define_method(c_file_system, "used", HDFS_File_System_used, 0);
  rb_define_method(c_file_system, "utime", HDFS_File_System_utime, 3);

  c_file = rb_define_class_under(m_dfs, "File", rb_cObject);
  rb_define_method(c_file, "read", HDFS_File_read, 1);
  rb_define_method(c_file, "write", HDFS_File_write, 1);
  rb_define_method(c_file, "<<", HDFS_File_write, 1);
  rb_define_method(c_file, "seek", HDFS_File_seek, 1);
  rb_define_method(c_file, "tell", HDFS_File_tell, 0);
  rb_define_method(c_file, "flush", HDFS_File_flush, 0);
  rb_define_method(c_file, "available", HDFS_File_available, 0);
  rb_define_method(c_file, "close", HDFS_File_close, 0);

  c_file_info = rb_define_class_under(m_dfs, "FileInfo", rb_cObject);
  rb_define_method(c_file_info, "block_size", HDFS_File_Info_block_size, 0);
  rb_define_method(c_file_info, "group", HDFS_File_Info_group, 0);
  rb_define_method(c_file_info, "is_directory?", HDFS_File_Info_is_directory, 0);
  rb_define_method(c_file_info, "is_file?", HDFS_File_Info_is_file, 0);
  rb_define_method(c_file_info, "last_access", HDFS_File_Info_last_access, 0);
  rb_define_method(c_file_info, "last_modified", HDFS_File_Info_last_modified, 0);
  rb_define_method(c_file_info, "mode", HDFS_File_Info_mode, 0);
  rb_define_method(c_file_info, "name", HDFS_File_Info_name, 0);
  rb_define_method(c_file_info, "owner", HDFS_File_Info_owner, 0);
  rb_define_method(c_file_info, "replication", HDFS_File_Info_replication, 0);
  rb_define_method(c_file_info, "size", HDFS_File_Info_size, 0);
  rb_define_method(c_file_info, "to_s", HDFS_File_Info_to_s, 0);

  c_file_info_file = rb_define_class_under(c_file_info, "File", c_file_info);
  rb_define_method(c_file_info_file, "is_file?", HDFS_File_Info_File_is_file, 0);

  c_file_info_directory = rb_define_class_under(c_file_info, "Directory", c_file_info);
  rb_define_method(c_file_info_directory, "is_directory?", HDFS_File_Info_Directory_is_directory, 0);

  e_dfs_exception = rb_define_class_under(m_dfs, "DFSException", rb_eStandardError);
  e_file_error = rb_define_class_under(m_dfs, "FileError", e_dfs_exception);  
  e_could_not_open = rb_define_class_under(m_dfs, "CouldNotOpenFileError", e_file_error);
  e_does_not_exist = rb_define_class_under(m_dfs, "DoesNotExistError", e_file_error); 
}
