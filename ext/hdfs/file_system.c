#include "file_system.h"

#include "constants.h"
#include "file.h"
#include "file_info.h"
#include "utils.h"

#include "hdfs.h"

#include <fcntl.h>


typedef struct FSData {
  hdfsFS fs;
} FSData;

static VALUE c_file_system;

static VALUE e_connect_error;
static VALUE e_could_not_open;
static VALUE e_dfs_exception;
static VALUE e_not_connected;


void free_fs_data(FSData* data) {
  if (data && data->fs != NULL) {
    hdfsDisconnect(data->fs);
    data->fs = NULL;
  }
}

/* Ensures that the DFS is connected; otherwise throws a NotConnectedError. */
FSData* get_FSData(VALUE rb_object) {
  FSData* data = NULL;
  Data_Get_Struct(rb_object, FSData, data);
  if (data->fs == NULL) {
    rb_raise(e_not_connected, "DFS is not connected");
  }
  return data;
}

/*
 * HDFS::FileSystem
 */

VALUE HDFS_File_System_alloc(VALUE klass) {
  FSData* data = ALLOC_N(FSData, 1);
  data->fs = NULL;
  VALUE instance = Data_Wrap_Struct(klass, NULL, free_fs_data, data);
  return instance;
}

/**
 * call-seq:
 *    hdfs.rm(path, recursive=false) -> success
 *
 * Deletes the file at the supplied path, recursively if specified.  Returns
 * True if successful, raises a DFSException if this fails.
 */
VALUE HDFS_File_System_rm(int argc, VALUE* argv, VALUE self) {
  FSData* data = get_FSData(self);
  VALUE path, recursive;
  rb_scan_args(argc, argv, "11", &path, &recursive);
  int hdfs_recursive = HDFS_DEFAULT_RECURSIVE_DELETE;
  if (!NIL_P(recursive)) {
    hdfs_recursive = (recursive == Qtrue) ? 1 : 0;
  }
  if (hdfsDelete(data->fs, StringValuePtr(path), hdfs_recursive) == -1) {
    rb_raise(e_dfs_exception, "Could not delete file at path %s: %s",
        StringValuePtr(path), get_error(errno));
    return Qnil;
  }
  return Qtrue;
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
 *    hdfs.capacity -> retval
 *
 * Returns the capacity of this HDFS file system in bytes, raising a
 * DFSException if this was unsuccessful.
 */
VALUE HDFS_File_System_capacity(VALUE self) {
  FSData* data = get_FSData(self);
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
 *    hdfs.cd(path) -> success
 *
 * Changes the current working directory to the supplied path.  Returns True if
 * successful; raises a DFSException if this fails.
 */
VALUE HDFS_File_System_cd(VALUE self, VALUE path) {
  FSData* data = get_FSData(self);
  if (hdfsSetWorkingDirectory(data->fs, StringValuePtr(path)) < 0) {
    rb_raise(e_dfs_exception,
        "Failed to change current working directory to path %s: %s",
        StringValuePtr(path), get_error(errno));
    return Qnil;
  }
  return Qtrue;
}

/**
 * call-seq:
 *    hdfs.chgrp(path, group) -> success
 *
 * Changes the group of the supplied path.  Returns True if successful; raises
 * a DFSException if this fails.
 */
VALUE HDFS_File_System_chgrp(VALUE self, VALUE path, VALUE group) {
  FSData* data = get_FSData(self);
  if (hdfsChown(data->fs, StringValuePtr(path), NULL,
          StringValuePtr(group)) == -1) {
    rb_raise(e_dfs_exception, "Failed to chgrp path %s to group %s: %s",
        StringValuePtr(path), StringValuePtr(group), get_error(errno));
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
  FSData* data = get_FSData(self);
  VALUE path, mode;
  rb_scan_args(argc, argv, "11", &path, &mode);
  // Sets default mode if none is supplied.
  short hdfs_mode = NIL_P(mode) ? HDFS_DEFAULT_MODE : 
      octal_decimal(NUM2INT(mode));
  if (hdfsChmod(data->fs, StringValuePtr(path), hdfs_mode) == -1) {
    rb_raise(e_dfs_exception, "Failed to chmod path %s to mode %d: %s",
        StringValuePtr(path), decimal_octal(hdfs_mode), get_error(errno));
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
  FSData* data = get_FSData(self);
  if (hdfsChown(data->fs, StringValuePtr(path), StringValuePtr(owner), NULL) == -1) {
    rb_raise(e_dfs_exception, "Failed to chown user path %s to user %s: %s",
        StringValuePtr(path), StringValuePtr(owner), get_error(errno));
    return Qnil;
  }
  return Qtrue;
}

/**
 * call-seq:
 *    hdfs.cp(from_path, to_path, to_fs=nil) -> retval
 *
 * Copies the file at HDFS location from_path to HDFS location to_path.  If
 * to_fs is specified, copies to this HDFS over the current HDFS.  If
 * successful, returns True; otherwise, raises a DFSException.
 */
VALUE HDFS_File_System_cp(int argc, VALUE* argv, VALUE self) {
  FSData* data = get_FSData(self);
  VALUE from_path, to_path, to_fs;
  rb_scan_args(argc, argv, "21", &from_path, &to_path, &to_fs);
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
  if (hdfsCopy(data->fs, StringValuePtr(from_path), destFS,
          StringValuePtr(to_path)) == -1) {
    rb_raise(e_dfs_exception, "Failed to copy path: %s to path: %s: %s",
        StringValuePtr(from_path), StringValuePtr(to_path), get_error(errno));
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
  FSData* data = get_FSData(self);
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
 *    hdfs.default_block_size -> retval
 *
 * Returns the default block size of this HDFS file system in bytes, raising a
 * DFSException if this was unsuccessful.
 */
VALUE HDFS_File_System_default_block_size(VALUE self) {
  FSData* data = get_FSData(self);
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
  FSData* data = get_FSData(self);
  long block_size = hdfsGetDefaultBlockSizeAtPath(data->fs,
      StringValuePtr(path));
  if (block_size == -1) {
    rb_raise(e_dfs_exception,
        "Error while retrieving default block size at path %s: %s",
        StringValuePtr(path), get_error(errno));
    return Qnil;
  }
  return LONG2NUM(block_size);
}

/**
 * call-seq:
 *    hdfs.exist?(path) -> file_existence
 *
 * Checks if a file exists at the supplied path.  If file exists, returns True;
 * if not, returns False.
 */
VALUE HDFS_File_System_exist(VALUE self, VALUE path) {
  FSData* data = get_FSData(self);
  int success = hdfsExists(data->fs, StringValuePtr(path));
  return success == 0 ? Qtrue : Qfalse;
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
  FSData* data = get_FSData(self);
  char*** hosts = hdfsGetHosts(data->fs, StringValuePtr(path), NUM2LONG(start),
      NUM2LONG(length));
  if (hosts == NULL) {
    rb_raise(e_dfs_exception,
        "Error while retrieving hosts at path: %s, start: %s, length: %lu: %s",
        StringValuePtr(path), StringValuePtr(start), NUM2LONG(length),
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
 *    hdfs.new(options={}) -> hdfs
 *
 * Creates a new HDFS client connection, configured by options, returning a new
 * HDFS::FileSystem object if successful.  If this fails, raises a
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
  char* hdfs_user = RTEST(r_user) ? StringValuePtr(r_user) : 
      (char*) HDFS_DEFAULT_USER;

  VALUE r_local = rb_hash_aref(options, rb_eval_string(":local"));
  if (r_local == Qtrue) {
    data->fs = hdfsConnectAsUser(NULL, 0, hdfs_user);
  } else {
    VALUE r_host = rb_hash_aref(options, rb_eval_string(":host"));
    VALUE r_port = rb_hash_aref(options, rb_eval_string(":port"));

    // Sets default values for host and port if not supplied by user.
    char* hdfs_host = RTEST(r_host) ? StringValuePtr(r_host) : 
        (char*) HDFS_DEFAULT_HOST;
    int hdfs_port   = RTEST(r_port) ? NUM2INT(r_port) :
        HDFS_DEFAULT_PORT;
    data->fs = hdfsConnectAsUser(hdfs_host, hdfs_port, hdfs_user);     
  }
 
  if (data->fs == NULL) {
    rb_raise(e_connect_error, "Failed to connect to HDFS: %s",
        get_error(errno));
    return Qnil;
  } 

  return self;
}

/**
 * call-seq:
 *    hdfs.ls(path) -> file_infos
 *
 * Lists the directory at the supplied path, returning an Array of
 * HDFS::FileInfo objects.  If this fails, raises a DFSException.
 */
VALUE HDFS_File_System_ls(VALUE self, VALUE path) {
  FSData* data = get_FSData(self);
  VALUE file_infos = rb_ary_new();
  int num_files = -1;
  hdfsFileInfo* infos = hdfsListDirectory(data->fs, StringValuePtr(path),
      &num_files);
  if (infos == NULL && num_files == -1) {
    rb_raise(e_dfs_exception, "Failed to list directory %s: %s",
        StringValuePtr(path), get_error(errno));
    return Qnil;
  }
  int i;
  for (i = 0; i < num_files; i++) {
    hdfsFileInfo* cur_info = infos + i;
    rb_ary_push(file_infos, new_HDFS_File_Info(cur_info));
  }
  hdfsFreeFileInfo(infos, num_files);
  return file_infos;
}

/**
 * call-seq:
 *    hdfs.mv(from_path, to_path, to_fs=nil) -> retval
 *
 * Moves the file at HDFS location from_path to HDFS location to_path.  If
 * to_fs is specified, moves to this HDFS over the current HDFS.  If
 * successful, returns true; otherwise, returns false.
 */
VALUE HDFS_File_System_mv(int argc, VALUE* argv, VALUE self) {
  FSData* data = get_FSData(self);
  VALUE from_path, to_path, to_fs;
  rb_scan_args(argc, argv, "21", &from_path, &to_path, &to_fs);
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
  if (hdfsMove(data->fs, StringValuePtr(from_path), destFS,
          StringValuePtr(to_path)) == -1) {
    rb_raise(e_dfs_exception, "Error while moving path %s to path %s: %s",
        StringValuePtr(from_path), StringValuePtr(to_path), get_error(errno));
    return Qnil;
  }
  return Qtrue;
}

/**
 * call-seq:
 *    hdfs.mkdir(path) -> success
 *
 * Creates a directory at the supplied path.  If successful, returns True;
 * raises a DFSException if this fails.
 */
VALUE HDFS_File_System_mkdir(VALUE self, VALUE path) {
  FSData* data = get_FSData(self);
  if (hdfsCreateDirectory(data->fs, StringValuePtr(path)) < 0) {
    rb_raise(e_dfs_exception, "Could not create directory at path %s: %s",
        StringValuePtr(path), get_error(errno));
    return Qnil;
  }
  return Qtrue;
}

/**
 * call-seq:
 *    hdfs.open(path, mode='r', options={}) -> file
 *
 * Opens a file using the supplied mode and options.  If the file cannot be
 * opened, raises a CouldNotOpenError; otherwise, returns a HDFS::File
 * object corresponding to the file.
 *
 * modes can be one of the following:
 *
 * * *'a'*: Opens file for append access
 * * *'r'*: Opens file for read access
 * * *'w'*: Opens file for write access
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
  FSData* data = get_FSData(self);
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
    } else if (strcmp("a", StringValuePtr(mode)) == 0) {
      flags = O_WRONLY | O_APPEND;
    } else {
      rb_raise(rb_eArgError, "Mode must be 'r', 'w', or 'a'");
      return Qnil;
    }
  }
  VALUE r_buffer_size = rb_hash_aref(options, rb_eval_string(":buffer_size"));
  VALUE r_replication = rb_hash_aref(options, rb_eval_string(":replication"));
  VALUE r_block_size = rb_hash_aref(options, rb_eval_string(":block_size"));
  hdfsFile file = hdfsOpenFile(data->fs, StringValuePtr(path), flags,
      RTEST(r_buffer_size) ? NUM2INT(r_buffer_size) : 0,
      RTEST(r_replication) ? NUM2INT(r_replication) : 0,
      RTEST(r_block_size) ? NUM2INT(r_block_size) : 0);
  if (file == NULL) {
    rb_raise(e_could_not_open, "Could not open file %s: %s", StringValuePtr(path),
        get_error(errno));
    return Qnil;
  }
  return new_HDFS_File(path, &file, &data->fs);
}

/**
 * call-seq:
 *    hdfs.rename(from_path, to_path) -> success
 *
 * Renames the file at the supplied path to the file at the destination path.
 * Returns True if successful, raises a DFSException if this fails.
 */
VALUE HDFS_File_System_rename(VALUE self, VALUE from_path, VALUE to_path) {
  FSData* data = get_FSData(self);
  if (hdfsRename(data->fs, StringValuePtr(from_path), StringValuePtr(to_path)) == -1) {
    rb_raise(e_dfs_exception, "Could not rename path %s to path %s: %s",
        StringValuePtr(from_path), StringValuePtr(to_path), get_error(errno));
    return Qnil;
  }
  return Qtrue;
}

/**
 * call-seq:
 *    hdfs.set_replication!(path, replication=3) -> success
 *
 * Sets the replication of the following path to the supplied number of nodes
 * it will be replicated against.  Returns True if successful; raises a
 * DFSException if this fails.
 */
VALUE HDFS_File_System_set_replication(int argc, VALUE* argv, VALUE self) {
  FSData* data = get_FSData(self);
  VALUE path, replication;
  rb_scan_args(argc, argv, "11", &path, &replication);
  // If no replication value is supplied, uses default replication value.
  int hdfs_replication = NIL_P(replication) ? HDFS_DEFAULT_REPLICATION :
      NUM2INT(replication);
  if (hdfsSetReplication(data->fs, StringValuePtr(path), hdfs_replication) == -1) {
    rb_raise(e_dfs_exception, "Failed to set replication to %d at path %s: %s",
        hdfs_replication, StringValuePtr(path), get_error(errno));
    return Qnil;
  }
  return Qtrue;
}

/**
 * call-seq:
 *    hdfs.stat(path) -> file_info
 *
 * Stats the file or directory at the supplied path, returning a
 * Hadoop::DFS::FileInfo object corresponding to it.  If this fails, raises a
 * DFSException.
 */
VALUE HDFS_File_System_stat(VALUE self, VALUE path) {
  FSData* data = get_FSData(self);
  hdfsFileInfo* info = hdfsGetPathInfo(data->fs, StringValuePtr(path));
  if (info == NULL) {
    rb_raise(e_dfs_exception, "Failed to stat file %s: %s",
        StringValuePtr(path), get_error(errno));
    return Qnil;
  }
  VALUE file_info = new_HDFS_File_Info(info);
  hdfsFreeFileInfo(info, 1);
  return file_info;
}

/**
 * call-seq:
 *    hdfs.used -> retval
 *
 * Returns the bytes currently in use by this filesystem, raising a
 * DFSException if unsuccessful.
 */
VALUE HDFS_File_System_used(VALUE self) {
  FSData* data = get_FSData(self);
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
 *    hdfs.utime(path, options={}) -> retval
 *
 * Changes the last modified and/or last access time in seconds since the Unix
 * epoch for the supplied file.  Returns true if successful; raises a
 * DFSException if this fails.
 *
 * options can have the following keys:
 *
 * * *atime*: Time object or Integer representing seconds since the Unix epoch
 *   to set as the time of last file access.
 * * *mtime*: Time object or Integer representing seconds since the Unix epoch
 *   to set as the time of last file modification.
 */
VALUE HDFS_File_System_utime(int argc, VALUE* argv, VALUE self) {
  FSData* data = get_FSData(self);
  VALUE path, options;
  rb_scan_args(argc, argv, "11", &path, &options);
  // Sets default values for keyword args, type-checks supplied value.
  options = NIL_P(options) ? rb_hash_new() : options;
  if (TYPE(options) != T_HASH) {
    rb_raise(rb_eArgError, "options must be of type Hash");
    return Qnil;
  }
  VALUE r_atime = rb_hash_aref(options, rb_eval_string(":atime"));
  VALUE r_mtime = rb_hash_aref(options, rb_eval_string(":mtime"));
  // Converts any Time objects to seconds since the Unix epoch.
  if (CLASS_OF(r_atime) == rb_cTime) {
    r_atime = rb_funcall(r_atime, rb_intern("to_i"), 0);
  }
  if (CLASS_OF(r_mtime) == rb_cTime) {
    r_mtime = rb_funcall(r_mtime, rb_intern("to_i"), 0);
  }
  // Sets default values for last modified and/or last access time.
  tTime hdfsAccessTime = NIL_P(r_atime) ? -1 : NUM2LONG(r_atime);
  tTime hdfsModifiedTime = NIL_P(r_mtime) ? -1 : NUM2LONG(r_mtime);
  if (hdfsUtime(data->fs, StringValuePtr(path), hdfsModifiedTime,
          hdfsAccessTime) == -1) {
    rb_raise(e_dfs_exception,
        "Error while setting modified time %lu, access time %lu at path %s: %s",
        (long) hdfsModifiedTime, (long) hdfsAccessTime, StringValuePtr(path),
        get_error(errno));
    return Qnil;
  }
  return Qtrue;
}

void init_file_system(VALUE parent) {
  c_file_system = rb_define_class_under(parent, "FileSystem", rb_cObject);
  rb_define_alloc_func(c_file_system, HDFS_File_System_alloc);

  rb_define_method(c_file_system, "capacity", HDFS_File_System_capacity, 0);
  rb_define_method(c_file_system, "cd", HDFS_File_System_cd, 1);
  rb_define_method(c_file_system, "chgrp", HDFS_File_System_chgrp, 2);
  rb_define_method(c_file_system, "chmod", HDFS_File_System_chmod, -1);
  rb_define_method(c_file_system, "chown", HDFS_File_System_chown, 2);
  rb_define_method(c_file_system, "cp", HDFS_File_System_cp, -1);
  rb_define_method(c_file_system, "cwd", HDFS_File_System_cwd, 0);
  rb_define_method(c_file_system, "disconnect", HDFS_File_System_disconnect,
      0);
  rb_define_method(c_file_system, "exist?", HDFS_File_System_exist, 1);
  rb_define_method(c_file_system, "default_block_size",
      HDFS_File_System_default_block_size, 0);
  rb_define_method(c_file_system, "default_block_size_at_path",
      HDFS_File_System_default_block_size_at_path, 1);
  rb_define_method(c_file_system, "get_hosts", HDFS_File_System_get_hosts, 3);
  rb_define_method(c_file_system, "initialize", HDFS_File_System_initialize,
      -1);
  rb_define_method(c_file_system, "ls", HDFS_File_System_ls, 1);
  rb_define_method(c_file_system, "mkdir", HDFS_File_System_mkdir, 1);
  rb_define_method(c_file_system, "mv", HDFS_File_System_mv, -1);
  rb_define_method(c_file_system, "open", HDFS_File_System_open, -1);
  rb_define_method(c_file_system, "rename", HDFS_File_System_rename, 2);
  rb_define_method(c_file_system, "rm", HDFS_File_System_rm, -1);
  rb_define_method(c_file_system, "stat", HDFS_File_System_stat, 1);
  rb_define_method(c_file_system, "set_replication!",
      HDFS_File_System_set_replication, -1);
  rb_define_method(c_file_system, "used", HDFS_File_System_used, 0);
  rb_define_method(c_file_system, "utime", HDFS_File_System_utime, -1);

  e_dfs_exception = rb_define_class_under(parent, "DFSException",
      rb_eException);
  e_connect_error = rb_define_class_under(parent, "ConnectError",
      e_dfs_exception);  
  e_could_not_open = rb_define_class_under(parent, "CouldNotOpenFileError",
      e_dfs_exception);
  e_not_connected = rb_define_class_under(parent, "NotConnectedError",
      e_dfs_exception);
}
