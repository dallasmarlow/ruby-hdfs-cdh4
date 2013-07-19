#include "file_info.h"

#include "constants.h"
#include "utils.h"

#include <string.h>
#include <stdio.h>


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

static VALUE c_file_info;
static VALUE c_file_info_file;
static VALUE c_file_info_directory;


/*
 * HDFS::FileInfo
 */

void free_file_info(FileInfo* file_info) {
  if (file_info) {
    free(file_info->mName);
    free(file_info->mOwner);
    free(file_info->mGroup);
    free(file_info);
  }
}

/*
 * Copies an hdfsFileInfo struct into a Hadoop::DFS::FileInfo derivative
 * object.
 */
VALUE new_HDFS_File_Info(hdfsFileInfo* info) {
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
      rb_raise(rb_eTypeError, "FileInfo was not a file or directory: %s",
          info->mName);
  }
  return Qnil;
}

/**
 * HDFS File Info interface
 */

/**
 * call-seq:
 *    file_info.atime -> retval
 *
 * Returns the time of last file access as a Time object.
 */
VALUE HDFS_File_Info_atime(VALUE self) {
  FileInfo* file_info = NULL;
  Data_Get_Struct(self, FileInfo, file_info);
  return rb_time_new(file_info->mLastAccess, 0);
}

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
 *    file_info.mtime -> retval
 *
 * Returns the time of last file modification as a Time object.
 */
VALUE HDFS_File_Info_mtime(VALUE self) {
  FileInfo* file_info = NULL;
  Data_Get_Struct(self, FileInfo, file_info);
  return rb_time_new(file_info->mLastMod, 0);
}

/**
 * call-seq:
 *    file_info.mode -> retval
 *
 * Returns the mode of the file as an Integer.
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
  return rb_str_new2(file_info->mName);
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
  return rb_str_new2(file_info->mOwner);
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
 * Returns a human-readable representation of an HDFS::FileSystem object as a
 * String.
 */
VALUE HDFS_File_Info_to_s(VALUE self) {
  FileInfo* file_info = NULL;
  Data_Get_Struct(self, FileInfo, file_info);
  // Introspects current class, returns it as a String.
  VALUE class_string = rb_funcall(rb_funcall(self, rb_intern("class"), 0),
      rb_intern("to_s"), 0);
  return rb_sprintf("#<%s: %s, mode=%d, owner=%s, group=%s>",
      RSTRING_PTR(class_string), file_info->mName,
      decimal_octal(file_info->mPermissions), file_info->mOwner,
      file_info->mGroup);
}

void init_file_info(VALUE parent) {
  c_file_info = rb_define_class_under(parent, "FileInfo", rb_cObject);

  rb_define_method(c_file_info, "atime", HDFS_File_Info_atime, 0);
  rb_define_method(c_file_info, "block_size", HDFS_File_Info_block_size, 0);
  rb_define_method(c_file_info, "group", HDFS_File_Info_group, 0);
  rb_define_method(c_file_info, "is_directory?", HDFS_File_Info_is_directory,
      0);
  rb_define_method(c_file_info, "is_file?", HDFS_File_Info_is_file, 0);
  rb_define_method(c_file_info, "mtime", HDFS_File_Info_mtime, 0);
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
}
