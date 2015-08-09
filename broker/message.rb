class Message
  attr_accessor :command
  attr_accessor :pid
  attr_accessor :fd
  attr_accessor :length
  attr_reader :body
  
  def initialize
    @command = 0
    @pid = 0
    @fd = 0
    @length = 0
    @body = "\0"
  end
  
  def body=(v)
    @body = v
    @length = @body.bytesize 
  end
  
  def read(socket, cmd=nil)
    if cmd
      @command = cmd
      @fd, @pid, @length = socket.read(12).unpack('LLL')
    else
      @command, @fd, @pid, @length = socket.read(14).unpack('SLLL')
    end
    @body = socket.read(@length)
    self
  rescue 
    raise IOError, 'Closed'  
  end
  
  def key
    "#{@pid}_#{@fd}"
  end
  
  def key=(s)
    @pid, @fd = s.split('_')
    @pid = @pid.to_i
    @fd = @fd.to_i
  end
  
  def to_s
    [@command, @fd, @pid, @length].pack('SLLLC*') + @body
  end
end #class
