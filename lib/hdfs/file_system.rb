module HDFS
  class FileSystem

    attr_reader :host, :local, :port, :user

    # Recursively scans HDFS::FileInfo objects under the supplied path using
    # the supplied match proc to determine matching files.  If a match is
    # found, calls the supplied block.
    #
    # @param path [String] DFS path to search for files under
    # @param match_proc [Proc] determines if a HDFS::FileInfo object is a match
    # @param block [Block] called if an HDFS::FileInfo object matches
    def find path, match_proc, &block
      dir_listing = ls path
      dir_listing.each do |item|
        if match_proc.call item
          block.call item
        end
        if item.is_directory?
          find item, match_proc, &block
        end
      end
    end # def find path, match_proc, &block

    # Recursively scans HDFS::FileInfo objects under the supplied path using
    # the supplied match proc to determine matching files, returning a list of
    # all matches.
    #
    # @param path [String] DFS path to search for files under
    # @param match_proc [Proc] determines if a HDFS::FileInfo object is a match
    def find_all path, match_proc
      dir_listing = ls path
      dir_listing.map do |item|
        found_items = []
        if match_proc.call item
          found_items << item
        end
        if item.is_directory?
          found_items << find_all(item, match_proc)
        end
      end.flatten
    end # def find_all path, match_proc

    # Reads the full contents of a DFS file, returning the contents as a
    # String.  This method is not recommended for reading large files, as it
    # will be bound by memory on the local system.
    #
    # @param path [String]
    def read_all path
      file = open path, 'r'
      size = stat(path).size
      contents = ""
      while file.tell < size
        contents << file.read
      end
      file.close
      contents
    end # def read_all path

    # Ensures that a file exists, creating it if it is not present.
    #
    # @param path [String] DFS path under which to ensure file existence
    def touch path
      if not exist? path
        new_file = open path, 'w'
        new_file.close
      end
      true
    end # def touch path

    # Perfoms a recursive depth-first walk of the file hierarchy beneath the
    # supplied DFS path.  At each level, calls the supplied block with the
    # following arguments:
    #
    #   - current DFS path
    #   - an Array of directories directly beneath current DFS path
    #   - an Array of files directly beneath current DFS path
    #
    # @param path [String] DFS path to perform walk against
    # @param block [Block] Block to execute upon each level
    def walk path, &block
      dir_listing = ls path
      components = dir_listing.reduce({:files => [],
          :directories => []}) do |memo, item|
        if item.is_directory?
          memo[:directories] << item
          walk item.name, &block
        else
          memo[:files] << item
        end
        memo
      end
      block.call path, components[:directories], components[:files]
    end # def walk path

  end # class FileSystem
end # module HDFS
