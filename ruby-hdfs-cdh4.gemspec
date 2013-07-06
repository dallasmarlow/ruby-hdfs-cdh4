Gem::Specification.new do |gem|
  gem.name     = 'ruby-hdfs-cdh4'
  gem.version  = File.read File.join File.dirname(__FILE__), 'VERSION'
  gem.date     = Time.now.strftime '%Y-%m-%d'

  gem.authors  = ['Alexander Staubo', 'Steve Salevan', 'Dallas Marlow']
  gem.email    = ['alex@bengler.no', 'steve.salevan@gmail.com', 'dallasmarlow@gmail.com']

  gem.homepage = 'http://github.com/dallasmarlow/ruby-hdfs-cdh4'
  gem.summary  = 'ruby hadoop libhdfs client with support for cdh4'
  gem.description = gem.summary

  gem.files = [
    'LICENSE',
    'ext/hdfs/constants.h',
    'ext/hdfs/exceptions.c',
    'ext/hdfs/exceptions.h',
    'ext/hdfs/extconf.rb',
    'ext/hdfs/file.c',
    'ext/hdfs/file.h',
    'ext/hdfs/file_info.c',
    'ext/hdfs/file_info.h',
    'ext/hdfs/file_system.c',
    'ext/hdfs/file_system.h',
    'ext/hdfs/hdfs.c',
    'ext/hdfs/hdfs.h',
    'ext/hdfs/utils.c',
    'ext/hdfs/utils.h',
    'lib/hdfs/classpath.rb',
  ]

  gem.extensions       = ['ext/hdfs/extconf.rb']
  gem.require_paths    = ['lib']
  gem.required_rubygems_version = '>= 1.8.10'
end

