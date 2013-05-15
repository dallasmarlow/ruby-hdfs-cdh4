Gem::Specification.new do |gem|
  gem.name     = 'ruby-hdfs-cdh4'
  gem.version  = '0.0.2'
  gem.date     = Time.now.strftime '%Y-%m-%d'

  gem.authors  = ['Alexander Staubo', 'Steve Salevan', 'Dallas Marlow']
  gem.email    = ['alex@bengler.no', 'steve.salevan@gmail.com', 'dallasmarlow@gmail.com']

  gem.homepage = 'http://github.com/dallasmarlow/ruby-hdfs-cdh4'
  gem.summary  = 'ruby hadoop libhdfs client with support for cdh4'
  gem.description = gem.summary

  gem.files = [
    'LICENSE',
    'ext/hdfs/extconf.rb',
    'ext/hdfs/hdfs.c',
    'ext/hdfs/hdfs.h',
    'lib/hdfs/classpath.rb',
  ]

  gem.extensions       = ['ext/hdfs/extconf.rb']
  gem.require_paths    = ['lib']
  gem.required_rubygems_version = '>= 1.9.2'
end

