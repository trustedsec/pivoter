class CHash < Object

  def initialize(*args, &blk)
    @hash = Hash.new(*args, &blk)
    @mtex = Mutex.new
  end
  
  def method_missing(fn, *args, &blk)
    if @hash.respond_to? fn
      v = nil
      @mtex.synchronize { v = @hash.send(fn, *args, &blk) }
      return v
    end
    super
  end
end
