# Searches common locations for necessary Hadoop JARs to add to CLASSPATH.
HADOOP_PREFIX = ENV['HADOOP_PREFIX'] || '/opt/hadoop'
COMMON_HADOOP_LOCATIONS = [
  '/usr/lib/hadoop',
  '/usr/lib/hadoop-hdfs',
  '/usr/lib/hadoop-0.20',
  "#{HADOOP_PREFIX}/share/hadoop-common",
  "#{HADOOP_PREFIX}/share/hadoop-hdfs",
]
EXTRA_HADOOP_DIRS = ENV['HADOOP_DIRS'].to_s.split ':'
HADOOP_LOCATIONS = COMMON_HADOOP_LOCATIONS + EXTRA_HADOOP_DIRS

ALL_JARS = HADOOP_LOCATIONS.map do |lib_dir|
  Dir[File.join(lib_dir, '*.jar')] + Dir[File.join(lib_dir, '**', '*.jar')]
end.flatten.uniq.join(':')

# Adds Hadoop jars to $CLASSPATH environment variable.
ENV['CLASSPATH'] = [ENV['CLASSPATH'].to_s + ALL_JARS].join ':'

$:.unshift File.join File.dirname(__FILE__)

require '_hdfs'

%w[file file_system].each do |file|
  require "hdfs/#{file}"
end

module HDFS
  module Version

    def self.to_s
      path = ::File.absolute_path(
      	  ::File.join(::File.dirname(__FILE__), '..', 'VERSION'))
      if ::File.exists?(path) then
        ::File.read(path)
      else
        "0.0-unknown"
      end
    end # def self.to_s

  end # module Version
end # module HDFS
