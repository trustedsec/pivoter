class SocketHost
  @@PROXY_WRITE = 3
  @@PROXY_CLOSE = 5
  
  attr_reader :state #state one of :embryonic, :connected, :closed, or :dead 
  
  def initialize(client, mtu, pid, fd, read_queue)
    @client = client
    @mtu = mtu
    @fd = fd
    @pid = pid
    @write_queue = Queue.new
    @read_queue = read_queue
    @state = :embryonic
  end

  def connect(response)
    @state = :dead
    @state = :connected if 0 == response.unpack('L')[0]
    @client.send_all(response)
    close if @state == :dead
  end
    
  def write(data)
    raise IOError, 'write() on client not in connected state' unless @state == :connected 
    @write_queue.push data.body
  end
  
  def response(rsp)
    @client.send_all(rsp.body)
    close
  end 
  
  def close
    @client.close if @client
  rescue 
    @client = nil
  ensure 
    @client = nil
    @state = :dead  
  end  
 
  
  def start
  loop do
    break unless @client
    r,w,e = select([@client],[@client],[]) #@client])
    raise IOError, 'Socket Exception' if e.length != 0
    r.each { |socket| en_queue(socket.recv_nonblock(@mtu)) }
    break if @state == :dead
    w.each { |socket| socket.send_all(@write_queue.pop) unless @write_queue.empty? }
  end
  rescue IO::WaitReadable
    retry
  rescue IOError, Errno::EPIPE
    @state = :closed  
  end
  
  private 
  
  def en_queue(message)
    m = Message.new
    m.fd = @fd
    m.pid = @pid
    m.command = @@PROXY_WRITE
    if message.bytesize == 0
    	m.command = @@PROXY_CLOSE
    	close
    end	
    m.body = message
    @read_queue.push(m)
  end
    
end #class