# ruby libhdfs client

### requirements
  - ruby    (1.9.2 <=)
  - java    (1.6 <=)
  - libhdfs (2.x)

#### To install libhdfs (on Linux)

Add CDH4 to your apt sources:
```
sudo tee /etc/apt/sources.list.d/cloudera-cdh4-amd64.list <<EOF
deb [arch=amd64] http://archive.cloudera.com/cdh4/ubuntu/precise/amd64/cdh precise-cdh4.5.0 contrib
EOF

sudo apt-get update
```

Install `libhdfs0` and `hadoop-hdfs`:
```
sudo apt-get install hadoop-hdfs libhdfs0 -y
```

Make sure that `ld` can find `libhdfs.so`:

```
sudo ln -s /usr/lib/libhdfs.so.0.0.0 /usr/lib/libhdfs.so

sudo tee -a /etc/ld.so.conf.d/libhdfs.so.conf <<EOF
/usr/lib
EOF

sudo ldconfig
```

### installation
```
gem install ruby-hdfs-cdh4
```

this gem provides defaults for installation on machines using cdh4 and the hadoop-libhdfs cloudera package, but the following environment variables are available for configuration.

  - HADOOP_ENV
  - JAVA_HOME
  - JAVA_LIB
  
for example, on Mac OS X you will need to do:
```
JAVA_HOME=`/usr/libexec/java_home -v 1.6` \
JAVA_LIB=`/usr/libexec/java_home -v 1.6`/../Libraries/libjvm.dylib \
gem install ruby-hdfs-cdh4
```

### usage
to setup your classpath on cdh4 machines require `hdfs/classpath`, or see [classpath.rb](https://github.com/dallasmarlow/ruby-hdfs-cdh4/blob/master/lib/hdfs/classpath.rb) as an example.

```ruby
require 'hdfs/classpath'

# connecting to HDFS
dfs = Hadoop::DFS::FileSystem.new host: 'namenode.domain.tld', port: 8020

dfs.list_directory('/').select(&:is_directory?).first.name
 => 'hdfs://namenode.domain.tld:8020/hbase'

# copying a file from your local file system to hdfs
local = Hadoop::DFS::FileSystem.new local: true
local.copy '/tmp/local_file', '/tmp/remote_file', hdfs
 => true

# copying and moving files from one HDFS to another

another_dfs = Hadoop::DFS::FileSystem.new host: 'namenode2.domain.tld', port: 8020
dfs.copy '/tmp/remote_file', '/tmp/remote_file', another_dfs
 => true

another_dfs.move '/tmp/remote_file', '/tmp/another_remote_file', dfs
 => true

dfs.delete '/tmp/another_remote_file'
 => true

# HDFS file mode, owner, group, and replication modifications

dfs.chmod '/tmp/remote_file', 755
 => true

dfs.chown '/tmp/remote_file', 'hdfs'
 => true

dfs.chgrp '/tmp/remote_file', 'hdfs'
 => true

dfs.set_replication '/tmp/remote_file', 2
 => true

```
