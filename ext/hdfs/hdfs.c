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
static VALUE e_connect_error;
static VALUE e_file_error;
static VALUE e_could_not_open;
static VALUE e_does_not_exist;

static const tSize HDFS_DEFAULT_BUFFER_SIZE      = 131072;
static const char* HDFS_DEFAULT_HOST             = "0.0.0.0";
static const short HDFS_DEFAULT_MODE             = 0644;
static const int HDFS_DEFAULT_STRING_LENGTH      = 1024;
static const int HDFS_DEFAULT_PORT               = 8020;
static const int HDFS_DEFAULT_RECURSIVE_DELETE   = 0;
static const int16_t HDFS_DEFAULT_REPLICATION    = 3;
static const char* HDFS_DEFAULT_USER             = NULL;

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

/* Ensures that a file is open; otherwise throws a FileError. */
void ensure_file_open(FileData* data) {
  if (data->file == NULL) {
    rb_raise(e_file_error, "File is closed");
  }
}

/* Returns a string representation of errno in a thread-safe manner. */
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

/* Calls <obj>.to_s and returns a pointer to its underlying char array. */
char* get_string(VALUE string) {
  VALUE string_val = rb_funcall(string, rb_intern("to_s"), 0);
  return RSTRING_PTR(string_val);
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
 *    hdfs.new(options={}) -> hdfs
 *
 * Creates a new HDFS client connection, configured by options, returning a new
 * Hadoop::DFS::FileSystem object if successful.  If this fails, raises a
 * ConnectError.
 *
 * options can have the following keys:
 *
 * * *local*: whether to use the local filesystem instead of HDFS
 *   (default: false)
 * * *host*: hostname or IP address of a Hadoop NameNode (default: '0.0.0.0')
 * * *port*: port through which to connect to Hadoop NameNode (default: 8020)
 * * *user*: user to connect to filesystem as (default: current user)
 */
VALUE HDFS_File_System_initialize(int argc, VALUE* argv, VALUE self) {
  VALUE options;
  rb_scan_args(argc, argv, "01", &options);

  // Sets default values for keyword args, type-checks supplied value.
  options = NIL_P(options) ? rb_hash_new() : options;
  if (TYPE(options) != T_HASH) {
    rb_raise(rb_eArgError, "options must be of type Hash");
  }

  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);

  VALUE r_user = rb_hash_aref(options, rb_eval_string(":user"));
  char* hdfs_user = RTEST(r_user) ? get_string(r_user) : 
      (char*) HDFS_DEFAULT_USER;

  VALUE r_local = rb_hash_aref(options, rb_eval_string(":local"));
  if (r_local == Qtrue) {
    data->fs = hdfsConnectAsUser(NULL, 0, hdfs_user);
  } else {
    VALUE r_host = rb_hash_aref(options, rb_eval_string(":host"));
    VALUE r_port = rb_hash_aref(options, rb_eval_string(":port"));

    // Sets default values for host and port if not supplied by user.
    char* hdfs_host = RTEST(r_host) ? get_string(r_host) : 
        (char*) HDFS_DEFAULT_HOST;
    int hdfs_port   = RTEST(r_port) ? NUM2INT(r_port) :
        HDFS_DEFAULT_PORT;
    data->fs = hdfsConnectAsUser(hdfs_host, hdfs_port, hdfs_user);     
  }
 
  if (data->fs == NULL) {
    rb_raise(e_connect_error, "Failed to connect to HDFS");
    return Qnil;
  } 

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
 * True if successful, raises a DFSException if this fails.
 */
VALUE HDFS_File_System_delete(int argc, VALUE* argv, VALUE self) {
  VALUE path, recursive;
  rb_scan_args(argc, argv, "11", &path, &recursive);
  int hdfs_recursive = HDFS_DEFAULT_RECURSIVE_DELETE;
  if (!NIL_P(recursive)) {
    hdfs_recursive = (recursive == Qtrue) ? 1 : 0;
  }
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  if (hdfsDelete(data->fs, get_string(path), hdfs_recursive) == -1) {
    rb_raise(e_dfs_exception, "Could not delete file at path %s: %s",
        get_string(path), get_error(errno));
    return Qnil;
  }
  return Qtrue;
}

/**
 * call-seq:
 *    hdfs.rename(from_path, to_path) -> success
 *
 * Renames the file at the supplied path to the file at the destination path.
 * Returns True if successful, raises a DFSException if this fails.
 */
VALUE HDFS_File_System_rename(VALUE self, VALUE from_path, VALUE to_path) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  if (hdfsRename(data->fs, get_string(from_path), get_string(to_path)) == -1) {
    rb_raise(e_dfs_exception, "Could not rename path %s to path %s: %s",
        get_string(from_path), get_string(to_path), get_error(errno));
    return Qnil;
  }
  return Qtrue;
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
  int success = hdfsExists(data->fs, get_string(path));
  return success == 0 ? Qtrue : Qfalse;
}

/**
 * call-seq:
 *    hdfs.create_directory(path) -> success
 *
 * Creates a directory at the supplied path.  If successful, returns True;
 * raises a DFSException if this fails.
 */
VALUE HDFS_File_System_create_directory(VALUE self, VALUE path) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  if (hdfsCreateDirectory(data->fs, get_string(path)) < 0) {
    rb_raise(e_dfs_exception, "Could not create directory at path %s: %s",
        get_string(path), get_error(errno));
    return Qnil;
  }
  return Qtrue;
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
  int num_files = -1;
  hdfsFileInfo* infos = hdfsListDirectory(data->fs, get_string(path),
      &num_files);
  if (infos == NULL && num_files == -1) {
    rb_raise(e_dfs_exception, "Failed to list directory %s: %s",
        get_string(path), get_error(errno));
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
  hdfsFileInfo* info = hdfsGetPathInfo(data->fs, get_string(path));
  if (info == NULL) {
    rb_raise(e_does_not_exist, "Failed to stat file %s: %s",
        get_string(path), get_error(errno));
    return Qnil;
  }
  VALUE file_info = wrap_hdfsFileInfo(info);
  hdfsFreeFileInfo(info, 1);
  return file_info;
}

/**
 * call-seq:
 *    hdfs.set_replication(path, replication=3) -> success
 *
 * Sets the replication of the following path to the supplied number of nodes
 * it will be replicated against.  Returns True if successful; raises a
 * DFSException if this fails.
 */
VALUE HDFS_File_System_set_replication(int argc, VALUE* argv, VALUE self) {
  VALUE path, replication;
  rb_scan_args(argc, argv, "11", &path, &replication);
  // If no replication value is supplied, uses default replication value.
  int hdfs_replication = NIL_P(replication) ? HDFS_DEFAULT_REPLICATION :
      NUM2INT(replication);
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  if (hdfsSetReplication(data->fs, get_string(path), hdfs_replication) < 0) {
    rb_raise(e_dfs_exception, "Failed to set replication to %d at path %s: %s",
        hdfs_replication, get_string(path), get_error(errno));
    return Qnil;
  }
  return Qtrue;
}

/**
 * call-seq:
 *    hdfs.cd(path) -> success
 *
 * Changes the current working directory to the supplied path.  Returns True if
 * successful; raises a DFSException if this fails.
 */
VALUE HDFS_File_System_cd(VALUE self, VALUE path) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  if (hdfsSetWorkingDirectory(data->fs, get_string(path)) < 0) {
    rb_raise(e_dfs_exception,
        "Failed to change current working directory to path %s: %s",
        get_string(path), get_error(errno));
    return Qnil;
  }
  return Qtrue;
}

/**
 * call-seq:
 *    hdfs.cwd -> success
 *
 * Displays the current working directory; raises a DFSException if this fails.
 */
VALUE HDFS_File_System_cwd(VALUE self) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  char* hdfsCurDir = ALLOC_N(char, HDFS_DEFAULT_STRING_LENGTH);
  if (hdfsGetWorkingDirectory(data->fs, hdfsCurDir,
          HDFS_DEFAULT_STRING_LENGTH) == NULL) {
    xfree(hdfsCurDir);
    rb_raise(e_dfs_exception, "Failed to get current working directory: %s",
        get_error(errno));
    return Qnil;
  }
  VALUE cur_dir = rb_str_new2(hdfsCurDir);
  xfree(hdfsCurDir);
  return cur_dir;
}

/**
 * call-seq:
 *    hdfs.chgrp(path, group) -> success
 *
 * Changes the group of the supplied path.  Returns True if successful; raises
 * a DFSException if this fails.
 */
VALUE HDFS_File_System_chgrp(VALUE self, VALUE path, VALUE group) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  if (hdfsChown(data->fs, get_string(path), NULL, get_string(group)) == -1) {
    rb_raise(e_dfs_exception, "Failed to chgrp path %s to group %s: %s",
        get_string(path), get_string(group), get_error(errno));
    return Qnil;
  }
  return Qtrue;
}

/**
 * call-seq:
 *    hdfs.chgrp(path, mode=644) -> retval
 *
 * Changes the mode of the supplied path.  Returns True if successful; raises
 * a DFSException if this fails.
 */
VALUE HDFS_File_System_chmod(int argc, VALUE* argv, VALUE self) {
  VALUE path, mode;
  rb_scan_args(argc, argv, "11", &path, &mode);
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  // Sets default mode if none is supplied.
  short hdfs_mode = NIL_P(mode) ? HDFS_DEFAULT_MODE : 
      octal_decimal(NUM2INT(mode));
  if (hdfsChmod(data->fs, get_string(path), hdfs_mode) == -1) {
    rb_raise(e_dfs_exception, "Failed to chmod path %s to mode %d: %s",
        get_string(path), decimal_octal(hdfs_mode), get_error(errno));
    return Qnil;
  }
  return Qtrue;
}

/**
 * call-seq:
 *    hdfs.chown(path, owner) -> retval
 *
 * Changes the owner of the supplied path.  Returns True if successful; raises
 * a DFSException if this fails.
 */
VALUE HDFS_File_System_chown(VALUE self, VALUE path, VALUE owner) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  if (hdfsChown(data->fs, get_string(path), get_string(owner), NULL) == -1) {
    rb_raise(e_dfs_exception, "Failed to chown user path %s to user %s: %s",
        get_string(path), get_string(owner), get_error(errno));
    return Qnil;
  }
  return Qtrue;
}

/**
 * call-seq:
 *    hdfs.copy(from_path, to_path, to_fs=nil) -> retval
 *
 * Copies the file at HDFS location from_path to HDFS location to_path.  If
 * to_fs is specified, copies to this HDFS over the current HDFS.  If
 * successful, returns True; otherwise, raises a DFSException.
 */
VALUE HDFS_File_System_copy(int argc, VALUE* argv, VALUE self) {
  VALUE from_path, to_path, to_fs;
  rb_scan_args(argc, argv, "21", &from_path, &to_path, &to_fs);
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  hdfsFS destFS = data->fs;
  // If no to_fs is supplied, copies to the current file system.
  if (!NIL_P(to_fs)) {
    if (CLASS_OF(to_fs) == c_file_system) {
      FSData* destFSData = NULL;
      Data_Get_Struct(to_fs, FSData, destFSData);
      destFS = destFSData->fs;
    } else {
      rb_raise(rb_eArgError, "to_fs must be of type Hadoop::DFS::FileSystem");
      return Qnil;
    }
  }
  if (hdfsCopy(data->fs, get_string(from_path), destFS,
          get_string(to_path)) == -1) {
    rb_raise(e_dfs_exception, "Failed to copy path: %s to path: %s: %s",
        get_string(from_path), get_string(to_path), get_error(errno));
    return Qnil;
  }
  return Qtrue;
}

/**
 * call-seq:
 *    hdfs.move(from_path, to_path, to_fs=nil) -> retval
 *
 * Moves the file at HDFS location from_path to HDFS location to_path.  If
 * to_fs is specified, moves to this HDFS over the current HDFS.  If
 * successful, returns true; otherwise, returns false.
 */
VALUE HDFS_File_System_move(int argc, VALUE* argv, VALUE self) {
  VALUE from_path, to_path, to_fs;
  rb_scan_args(argc, argv, "21", &from_path, &to_path, &to_fs);
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  hdfsFS destFS = data->fs;
  // If no to_fs is supplied, moves to the current file system.
  if (!NIL_P(to_fs)) {
    if (CLASS_OF(to_fs) == c_file_system) {
      FSData* destFSData = NULL;
      Data_Get_Struct(to_fs, FSData, destFSData);
      destFS = destFSData->fs;
    } else {
      rb_raise(rb_eArgError, "to_fs must be of type Hadoop::DFS::FileSystem");
      return Qnil;
    }
  }
  if (hdfsMove(data->fs, get_string(from_path), destFS,
          get_string(to_path)) == -1) {
    rb_raise(e_dfs_exception, "Error while moving path %s to path %s: %s",
        get_string(from_path), get_string(to_path), get_error(errno));
    return Qnil;
  }
  return Qtrue;
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
    rb_raise(e_dfs_exception, "Error while retrieving capacity: %s",
        get_error(errno));
    return Qnil;
  }
  return LONG2NUM(capacity);
}

/**
 * call-seq:
 *    hdfs.default_block_size -> retval
 *
 * Returns the default block size of this HDFS file system in bytes, raising a
 * DFSException if this was unsuccessful.
 */
VALUE HDFS_File_System_default_block_size(VALUE self) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  long block_size = hdfsGetDefaultBlockSize(data->fs);
  if (block_size == -1) {
    rb_raise(e_dfs_exception, "Error while retrieving default block size: %s",
        get_error(errno));
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
  long block_size = hdfsGetDefaultBlockSizeAtPath(data->fs,
      get_string(path));
  if (block_size == -1) {
    rb_raise(e_dfs_exception,
        "Error while retrieving default block size at path %s: %s",
        get_string(path), get_error(errno));
    return Qnil;
  }
  return LONG2NUM(block_size);
}

/**
 * call-seq:
 *    hdfs.get_hosts(path, start, length) -> retval
 *
 * Returns the hostnames of the DataNodes which serve the portion of the file
 * between the provided start and length bytes.  Raises a DFSException if this
 * fails.
 */
VALUE HDFS_File_System_get_hosts(VALUE self, VALUE path, VALUE start,
    VALUE length) {
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  char*** hosts = hdfsGetHosts(data->fs, get_string(path), NUM2LONG(start),
      NUM2LONG(length));
  if (hosts == NULL) {
    rb_raise(e_dfs_exception,
        "Error while retrieving hosts at path: %s, start: %lu, length: %lu: %s",
        get_string(path), get_string(start), NUM2LONG(length),
        get_error(errno));
    return Qnil;
  }
  // Builds a Ruby Array object out of the hosts reported by HDFS.
  VALUE hosts_array = rb_ary_new();
  size_t i, j;
  for (i = 0; hosts[i]; i++) {
    VALUE cur_block_hosts = rb_ary_new();
    for (j = 0; hosts[i][j]; j++) {
      rb_ary_push(cur_block_hosts, rb_str_new2(hosts[i][j]));
    }
    rb_ary_push(hosts_array, cur_block_hosts);
  }
  return hosts_array;
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
  tOffset used = hdfsGetUsed(data->fs);
  if (used == -1) {
    rb_raise(e_dfs_exception, "Error while retrieving used capacity: %s",
        get_error(errno));
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
VALUE HDFS_File_System_utime(int argc, VALUE* argv, VALUE self) {
  VALUE path, modified_time, access_time;
  tTime hdfs_modified_time, hdfs_access_time;
  rb_scan_args(argc, argv, "12", &path, &modified_time, &access_time);
  // Sets default values for last modified and/or last access time.
  tTime hdfsModifiedTime = NIL_P(modified_time) ? -1 : NUM2LONG(modified_time);
  tTime hdfsAccessTime = NIL_P(access_time) ? -1 : NUM2LONG(access_time);
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  if (hdfsUtime(data->fs, get_string(path), hdfsModifiedTime,
          hdfsAccessTime) == -1) {
    rb_raise(e_dfs_exception,
        "Error while setting modified time %lu, access time %lu at path %s: %s",
        (long) hdfsModifiedTime, (long) hdfsAccessTime, get_string(path),
        get_error(errno));
    return Qnil;
  }
  return Qtrue;
}

/**
 * call-seq:
 *    hdfs.open(path, mode='r', options={}) -> file
 *
 * Opens a file using the supplied mode and options.  If the file cannot be
 * opened, raises a CouldNotOpenError; otherwise, returns a Hadoop::DFS::File
 * object corresponding to the file.
 *
 * options can have the following keys:
 *
 * * *buffer_size*: size in bytes of buffer to use for file accesses
 *   (default: default buffer size as configured by HDFS)
 * * *replication*: the number of nodes this file should be replicated against
 *   (default: default replication as configured by HDFS)
 * * *block_size*: the HDFS block size in bytes to use for this file
 *   (default: default block size as configured by HDFS)
 */
VALUE HDFS_File_System_open(int argc, VALUE* argv, VALUE self) {
  VALUE path, mode, options;
  int flags = O_RDONLY;
  rb_scan_args(argc, argv, "12", &path, &mode, &options);
  options = NIL_P(options) ? rb_hash_new() : options;
  // Sets file open mode if one is provided by the user.
  if (!NIL_P(mode)) {
    if (strcmp("r", StringValuePtr(mode)) == 0) {
      flags = O_RDONLY;
    } else if (strcmp("w", StringValuePtr(mode)) == 0) {
      flags = O_WRONLY;
    } else {
      rb_raise(rb_eArgError, "Mode must be 'r' or 'w'");
      return Qnil;
    }
  }
  VALUE r_buffer_size = rb_hash_aref(options, rb_eval_string(":buffer_size"));
  VALUE r_replication = rb_hash_aref(options, rb_eval_string(":replication"));
  VALUE r_block_size = rb_hash_aref(options, rb_eval_string(":block_size"));
  FSData* data = NULL;
  Data_Get_Struct(self, FSData, data);
  hdfsFile file = hdfsOpenFile(data->fs, get_string(path), flags,
      RTEST(r_buffer_size) ? NUM2INT(r_buffer_size) : 0,
      RTEST(r_replication) ? NUM2INT(r_replication) : 0,
      RTEST(r_block_size) ? NUM2INT(r_block_size) : 0);
  if (file == NULL) {
    rb_raise(e_could_not_open, "Could not open file %s: %s", get_string(path),
        get_error(errno));
    return Qnil;
  }
  FileData* file_data = ALLOC_N(FileData, 1);
  file_data->fs = data->fs;
  file_data->file = file;
  VALUE file_instance = Data_Wrap_Struct(c_file, NULL, free_file_data,
      file_data);
  return file_instance;
}

/*
 * File interface
 */

/**
 * call-seq:
 *    file.read(length=131072) -> retval
 *
 * Reads the number of bytes specified by length from the current file object,
 * returning the bytes read as a String.  If this fails, raises a
 * FileError.
 */ 
VALUE HDFS_File_read(int argc, VALUE* argv, VALUE self) {
  VALUE length;
  rb_scan_args(argc, argv, "01", &length);
  tSize hdfsLength = NIL_P(length) ? HDFS_DEFAULT_BUFFER_SIZE : NUM2INT(length);
  // Checks whether we're reading more data than HDFS client can support.
  if (hdfsLength > HDFS_DEFAULT_BUFFER_SIZE) {
    rb_raise(e_file_error, "Can only read a max of %u bytes from HDFS",
        HDFS_DEFAULT_BUFFER_SIZE);
    return Qnil;
  }
  FileData* data = NULL;
  Data_Get_Struct(self, FileData, data);
  ensure_file_open(data);
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
 *    file.read_pos(position, length=131072) -> retval
 *
 * Positionally reads the number of bytes specified by length at the specified
 * byte offset, returning the bytes read as a String.  If this fails, raises a
 * FileError.
 */ 
VALUE HDFS_File_read_pos(int argc, VALUE* argv, VALUE self) {
  VALUE position, length;
  rb_scan_args(argc, argv, "11", &position, &length);
  tSize hdfsLength = NIL_P(length) ? HDFS_DEFAULT_BUFFER_SIZE : NUM2INT(length);
  // Checks whether we're reading more data than HDFS client can support.
  if (hdfsLength > HDFS_DEFAULT_BUFFER_SIZE) {
    rb_raise(e_file_error, "Can only read a max of %u bytes from HDFS",
        HDFS_DEFAULT_BUFFER_SIZE);
    return Qnil;
  }
  FileData* data = NULL;
  Data_Get_Struct(self, FileData, data);
  ensure_file_open(data);
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
 *    file.write(bytes) -> num_bytes_written
 *
 * Writes the string specified by bytes to the current file object, returning
 * the number of bytes written as an Integer.  If this fails, raises a
 * FileError.
 */
VALUE HDFS_File_write(VALUE self, VALUE bytes) {
  FileData* data = NULL;
  Data_Get_Struct(self, FileData, data);
  ensure_file_open(data);
  tSize bytes_written = hdfsWrite(data->fs, data->file, RSTRING_PTR(bytes),
      RSTRING_LEN(bytes));
  if (bytes_written == -1) {
    rb_raise(e_file_error, "Failed to write data: %s", get_error(errno));
  }
  return UINT2NUM(bytes_written);
}

/**
 * call-seq:
 *    file.tell -> current_position
 *
 * Returns the current byte position in bytes of the file as an Integer.
 * If this fails, raises a FileError.
 */
VALUE HDFS_File_tell(VALUE self) {
  FileData* data = NULL;
  Data_Get_Struct(self, FileData, data);
  ensure_file_open(data);
  tOffset offset = hdfsTell(data->fs, data->file);
  if (offset == -1) {
    rb_raise(e_file_error, "Failed to read position: %s", get_error(errno));
  }
  return ULONG2NUM(offset);
}

/**
 * call-seq:
 *    file.seek(offset) -> success
 *
 * Seeks the file pointer to the supplied offset in bytes.  If this fails,
 * raises a FileError.
 */
VALUE HDFS_File_seek(VALUE self, VALUE offset) {
  FileData* data = NULL;
  Data_Get_Struct(self, FileData, data);
  ensure_file_open(data);
  if (hdfsSeek(data->fs, data->file, NUM2ULONG(offset)) == -1) {
    rb_raise(e_file_error, "Failed to seek to position %lu: %s",
        NUM2ULONG(offset), get_error(errno));
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
  FileData* data = NULL;
  Data_Get_Struct(self, FileData, data);
  ensure_file_open(data);
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
  FileData* data = NULL;
  Data_Get_Struct(self, FileData, data);
  ensure_file_open(data);
  if (hdfsHFlush(data->fs, data->file) == -1) {
    rb_raise(e_file_error, "HFlush failed: %s", get_error(errno));
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
  ensure_file_open(data);
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

/**
 * HDFS File Info interface
 */

/**
 * call-seq:
 *    file_info.block_size -> retval
 *
 * Returns the block size in bytes of the file described by this object.
 */
VALUE HDFS_File_Info_block_size(VALUE self) {
  FileInfo* file_info = NULL;
  Data_Get_Struct(self, FileInfo, file_info);
  return LONG2NUM(file_info->mBlockSize);
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
  return rb_str_new2(file_info->mGroup);
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

/**
 * call-seq:
 *    file_info.is_directory? -> retval
 *
 * Returns True for this directory.
 */
VALUE HDFS_File_Info_Directory_is_directory(VALUE self) {
  return Qtrue;
}

/**
 * call-seq:
 *    file_info.is_file? -> retval
 *
 * Returns True for this file.
 */
VALUE HDFS_File_Info_File_is_file(VALUE self) {
  return Qtrue;
}

/**
 * call-seq:
 *    file_info.last_access -> retval
 *
 * Returns the time of last access as an Integer representing seconds since the
 * UNIX epoch.
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
 * since the UNIX epoch for the file
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
 * since the UNIX epoch.
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
  VALUE class_string = rb_funcall(rb_funcall(self, rb_intern("class"), 0),
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
  rb_define_method(c_file_system, "initialize", HDFS_File_System_initialize,
      -1);
  rb_define_method(c_file_system, "disconnect", HDFS_File_System_disconnect,
      0);
  rb_define_method(c_file_system, "open", HDFS_File_System_open, -1);
  rb_define_method(c_file_system, "delete", HDFS_File_System_delete, -1);
  rb_define_method(c_file_system, "rename", HDFS_File_System_rename, 2);
  rb_define_method(c_file_system, "exist?", HDFS_File_System_exist, 1);
  rb_define_method(c_file_system, "create_directory",
      HDFS_File_System_create_directory, 1);
  rb_define_method(c_file_system, "list_directory",
      HDFS_File_System_list_directory, 1);
  rb_define_method(c_file_system, "stat", HDFS_File_System_stat, 1);
  rb_define_method(c_file_system, "set_replication",
      HDFS_File_System_set_replication, -1);
  rb_define_method(c_file_system, "cd", HDFS_File_System_cd, 1);
  rb_define_method(c_file_system, "cwd", HDFS_File_System_cwd, 0);
  rb_define_method(c_file_system, "chgrp", HDFS_File_System_chgrp, 2);
  rb_define_method(c_file_system, "chmod", HDFS_File_System_chmod, -1);
  rb_define_method(c_file_system, "chown", HDFS_File_System_chown, 2);
  rb_define_method(c_file_system, "copy", HDFS_File_System_copy, -1);
  rb_define_method(c_file_system, "capacity", HDFS_File_System_capacity, 0);
  rb_define_method(c_file_system, "default_block_size",
      HDFS_File_System_default_block_size, 0);
  rb_define_method(c_file_system, "default_block_size_at_path",
      HDFS_File_System_default_block_size_at_path, 1);
  rb_define_method(c_file_system, "get_hosts", HDFS_File_System_get_hosts, 3);
  rb_define_method(c_file_system, "move", HDFS_File_System_move, -1);
  rb_define_method(c_file_system, "used", HDFS_File_System_used, 0);
  rb_define_method(c_file_system, "utime", HDFS_File_System_utime, -1);

  c_file = rb_define_class_under(m_dfs, "File", rb_cObject);
  rb_define_method(c_file, "read", HDFS_File_read, -1);
  rb_define_method(c_file, "read_pos", HDFS_File_read_pos, -1);
  rb_define_method(c_file, "write", HDFS_File_write, 1);
  rb_define_method(c_file, "<<", HDFS_File_write, 1);
  rb_define_method(c_file, "seek", HDFS_File_seek, 1);
  rb_define_method(c_file, "tell", HDFS_File_tell, 0);
  rb_define_method(c_file, "flush", HDFS_File_flush, 0);
  rb_define_method(c_file, "hflush", HDFS_File_hflush, 0);
  rb_define_method(c_file, "available", HDFS_File_available, 0);
  rb_define_method(c_file, "close", HDFS_File_close, 0);
  rb_define_method(c_file, "read_open?", HDFS_File_read_open, 0);
  rb_define_method(c_file, "write_open?", HDFS_File_write_open, 0);

  c_file_info = rb_define_class_under(m_dfs, "FileInfo", rb_cObject);
  rb_define_method(c_file_info, "block_size", HDFS_File_Info_block_size, 0);
  rb_define_method(c_file_info, "group", HDFS_File_Info_group, 0);
  rb_define_method(c_file_info, "is_directory?", HDFS_File_Info_is_directory,
      0);
  rb_define_method(c_file_info, "is_file?", HDFS_File_Info_is_file, 0);
  rb_define_method(c_file_info, "last_access", HDFS_File_Info_last_access, 0);
  rb_define_method(c_file_info, "last_modified", HDFS_File_Info_last_modified,
      0);
  rb_define_method(c_file_info, "mode", HDFS_File_Info_mode, 0);
  rb_define_method(c_file_info, "name", HDFS_File_Info_name, 0);
  rb_define_method(c_file_info, "owner", HDFS_File_Info_owner, 0);
  rb_define_method(c_file_info, "replication", HDFS_File_Info_replication, 0);
  rb_define_method(c_file_info, "size", HDFS_File_Info_size, 0);
  rb_define_method(c_file_info, "to_s", HDFS_File_Info_to_s, 0);

  c_file_info_file = rb_define_class_under(c_file_info, "File", c_file_info);
  rb_define_method(c_file_info_file, "is_file?", HDFS_File_Info_File_is_file,
      0);

  c_file_info_directory = rb_define_class_under(c_file_info, "Directory",
      c_file_info);
  rb_define_method(c_file_info_directory, "is_directory?",
      HDFS_File_Info_Directory_is_directory, 0);

  e_dfs_exception = rb_define_class_under(m_dfs, "DFSException",
      rb_eStandardError);
  e_connect_error = rb_define_class_under(m_dfs, "ConnectError",
      e_dfs_exception);
  e_file_error = rb_define_class_under(m_dfs, "FileError", e_dfs_exception);
  e_could_not_open = rb_define_class_under(m_dfs, "CouldNotOpenFileError",
      e_file_error);
  e_does_not_exist = rb_define_class_under(m_dfs, "DoesNotExistError",
      e_file_error); 
}
