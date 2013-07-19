module HDFS
  class FileSystem

    def find path, &block
      dir_listing = ls path
      dir_listing.each do |item|
        if block.call item
          yield item
        end
        if item.is_directory?
          find path, &block
        end
      end
    end # def find path, &block

    def find_all path, &block
      dir_listing = ls path
      dir_listing.map do |item|
        found_items = []
        if block.call item
          found_items << item
        end
        if item.is_directory?
          found_items << find_all(path, &block)
        end
      end.flat_map
    end # def find_all path, &block

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

    def touch path
      if not exist? path
        new_file = open path, 'w'
        new_file.close
      end
      true
    end # def touch path

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
