# ruby libhdfs client

### requirements
  - ruby    (1.9.2 <=)
  - java    (1.6 <=)
  - libhdfs (2.x)

### installation
```
gem install ruby-hdfs-cdh4
```

this gem provides defaults for installation on machines using cdh4 and the hadoop-libhdfs cloudera package, but the following environment variables are available for configuration.

  - HADOOP_ENV
  - JAVA_HOME
  - JAVA_LIB

### usage
to setup your classpath on cdh4 machines require `hdfs/classpath`, or see [classpath.rb](https://github.com/dallasmarlow/ruby-hdfs-cdh4/blob/master/lib/hdfs/classpath.rb) as an example.

```ruby
require 'hdfs/classpath'

# connecting to HDFS

dfs = HDFS::FileSystem.new host: 'namenode.domain.tld', port: 8020

dfs.ls('/').select(&:is_directory?).first.name
 => 'hdfs://namenode.domain.tld:8020/hbase'

# using Ruby APIs to interact with HDFS files

IO.copy_stream File.open('/tmp/local_file', 'rb'),
               dfs.open('/tmp/remote_file', 'w', replication: 3)
 => 36986

# copying and moving files from one HDFS to another

another_dfs = HDFS::FileSystem.new host: 'namenode2.domain.tld', port: 8020
dfs.cp '/tmp/remote_file', '/tmp/remote_file', another_dfs
 => true

another_dfs.mv '/tmp/remote_file', '/tmp/another_remote_file', dfs
 => true

dfs.rm '/tmp/another_remote_file'
 => true

# testing for file existence

dfs.exist? '/tmp/remote_file'
 => true

dfs.exist? '/tmp/another_remote_file'
 => false

# HDFS file mode, owner, group, and replication modifications

dfs.chmod '/tmp/remote_file', 755
 => true

dfs.chown '/tmp/remote_file', 'hdfs'
 => true

dfs.chgrp '/tmp/remote_file', 'hdfs'
 => true

dfs.set_replication! '/tmp/remote_file', 2
 => true

# setting time of last access and last modification

dfs.utime '/tmp/remote_file', mtime: Time.now
 => true

dfs.utime '/tmp/remote_file', atime: Time.now
 => true

# copying files from local filesystem to HDFS through native Hadoop interface

local_fs = HDFS::FileSystem.new local: true

local_fs.cp '/etc/hosts', '/tmp/hosts', dfs
 => true

# retrieving DFS usage statistics in bytes

dfs.capacity
 => 44396488159232

dfs.used
 => 19962943483904
```
