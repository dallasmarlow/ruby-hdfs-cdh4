ENV['CLASSPATH'] = ['/usr/lib/hadoop', '/usr/lib/hadoop/lib', '/usr/lib/hadoop-hdfs'].map do |lib_dir|
  Dir[File.join(lib_dir, '*.jar')]
end.flatten.uniq.join(':')

require 'hdfs'
