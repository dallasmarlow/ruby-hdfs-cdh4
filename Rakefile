require 'rubygems'
require 'rake'
require 'rake/extensiontask'  # From rake-compiler gem

begin
  require 'jeweler'
  Jeweler::Tasks.new do |gem|
    gem.name = "ruby-hdfs"
    gem.summary = %Q{Native C bindings to Hadoop's libhdfs, for interacting with Hadoop HDFS.}
    gem.description = %Q{Native C bindings to Hadoop's libhdfs, for interacting with Hadoop HDFS.}
    gem.email = "alex@bengler.no"
    gem.homepage = "http://github.com/alexstaubo/ruby-hdfs"
    gem.authors = [ "Alexander Staubo", "Dallas Marlow", "Steve Salevan" ]
    gem.extensions = [ "ext/hdfs/extconf.rb" ]
    gem.require_paths = [ "lib" ]
    # gem is a Gem::Specification... see http://www.rubygems.org/read/chapter/20 for additional settings
  end
  Jeweler::GemcutterTasks.new
rescue LoadError
  puts "Jeweler (or a dependency) not available. Install it with: gem install jeweler"
end

Rake::ExtensionTask.new('hdfs') do |ext|
end

require 'rdoc/task'
Rake::RDocTask.new do |rdoc|
  version = File.exist?('VERSION') ? File.read('VERSION') : ""

  rdoc.rdoc_dir = 'rdoc'
  rdoc.title = "ruby-hdfs #{version}"
  rdoc.rdoc_files.include('README*')
  rdoc.rdoc_files.include('ext/**/*.c')
  rdoc.rdoc_files.include('lib/**/*.rb')
end
