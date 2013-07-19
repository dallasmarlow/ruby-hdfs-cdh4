require 'mkmf'

# debug options
if enable_config 'debug'
  puts 'enabling debug library build configuration.'

  $CFLAGS = CONFIG['CFLAGS'].gsub(/\s\-O\d?\s/, ' -O0 ').
                             gsub(/\s?\-g\w*\s/, ' -ggdb3 ')
  CONFIG['LDSHARED'] = CONFIG['LDSHARED'].gsub(/\s\-s(\s|\z)/, ' ')
end

# setup enviorment
ENV['HADOOP_ENV'] ||= '/etc/hadoop/conf/hadoop-env.sh'

# java home
ENV['JAVA_HOME'] ||= case
when File.readable?(ENV['HADOOP_ENV']) 
  puts 'JAVA_HOME is not set, attempting to read value from ' + ENV['HADOOP_ENV']

  File.read(ENV['HADOOP_ENV']).split("\n").find do |line|
    line.include? 'JAVA_HOME=' and not line.start_with? '#'
  end.split('=').last
else # check common locations
  java_locations = ['/usr/java/default'] # todo: add more locations to check

  puts 'JAVA_HOME is not set, checking common locations: ' + java_locations.join(',')
 
  java_locations.find do |path|
    File.directory?(path) and File.exist? File.join path, 'bin/java'    
  end
end
abort 'unable to find value for JAVA_HOME' unless ENV['JAVA_HOME']

# libjvm
ENV['JAVA_LIB'] ||= File.dirname Dir.glob(File.join(ENV['JAVA_HOME'], '**', 'libjvm.so')).first
abort 'unable to find value for JAVA_LIB or detect location of libjvm.so' unless ENV['JAVA_LIB']

# java include paths
Dir.glob(File.join(ENV['JAVA_HOME'], 'include', '**', '.')).map {|path| File.dirname path}.each do |include_path|
  $INCFLAGS << [' -I', include_path].join
end

dir_config 'hdfs'
['jvm', 'hdfs'].each do |lib|
  find_library lib, nil, ENV['JAVA_LIB']
end

have_library    'c', 'main'
create_makefile '_hdfs'
