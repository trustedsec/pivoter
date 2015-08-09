class DefaultRoute 
  def initialize(debug)
    @debug = debug
  end
  
  def method_missing(sym, *args, &block)
   puts "#{sym} called by RELAY for non-client" if @debug
  end
end #class

class Listener

  @@PROXY_CONNECT = 1
  @@PROXY_CLOSE = 5
  @@PROXY_HOSTBYNAME = 20
  @@RELAY_OPEN = 16384
  
  @@ECONNREFUSED = 111
  @@EHOSTUNREACH = 113
  @@ENORECOVERY = 3
  
  def initialize(bind_addr, bind_port, mtu,  debug)
    @debug = debug
    @bind_addr = bind_addr
    @bind_port = bind_port.to_i
    @relay = nil
    @mtu = mtu
    @client_sockets = CHash.new
    @client_sockets.default = DefaultRoute.new @debug
    @server = TCPServer.new(@bind_addr, @bind_port) 
  end
  
  
  def processRequests 
    puts 'CONNECTION PROCESSING Started' if @debug
  #A new socket needs to tell us what to connect to after that the possibilities are
  #really read(), write(), or close()
  #which we want to handle as agressively as possible.
    loop do
      Thread.start(@server.accept) do |client| 
        #first we read the command
        puts 'CONNECTION PROCESSING client connect' if @debug
        cmd = client.read(2).unpack('S')[0]
        case cmd
        
        when @@PROXY_CONNECT
          proxy_connect(client)
        when @@RELAY_OPEN
	  @relay.start if relay_open(client) #we can begin pushing/poping requests if the relay is ready
	when @@PROXY_HOSTBYNAME
	  proxy_hostbyname(client)
	#when @@something
	  #build a Message and send it to the relay queue   
        else
          puts "command unknown #{cmd}" if @debug
          client.close  
        end
          
        #A little cleanup
        @client_sockets.select { |k,v| v.state == :closed }.each { proxy_close(key, obj) }
        @client_sockets.delete_if {|k,v| v.state == :dead }
      end 
    end
  rescue StandardError => e 
    puts "CONNECTION PROCESSING #{e.message}" if @debug
    retry   
  end

private

  def proxy_close(key, obj)
    m = Message.new
    m.key = key
    m.command = @@PROXY_CLOSE
    printf('PROXY_CLOSE ')
    if @relay
      printf("for fd:%d pid:%d\n",  m.fd, m.pid) if @debug
      @relay.write_queue.push(m)
    else
      puts 'error no relay online' if @debug
    end  
    obj.close
  end
  
  def proxy_connect(client)
    m = Message.new
    printf('PROXY_CONNECT ') if @debug
    m.read(client, @@PROXY_CONNECT)
    ip, port = m.body.unpack('LS')
    printf("for fd:%d pid:%d trying ip(network order):%x port(network order):%x\n", m.fd, m.pid, ip, port) if @debug
    if @relay
      @relay.write_queue.push(m)
      @client_sockets[m.key] = SocketHost.new(client, @mtu, m.pid, m.fd, @relay.write_queue)
      @client_sockets[m.key].start
    else
      puts 'PROXY_CONNECT host unreachable because no relay online' if @debug
      client.write([@@ECONNREFUSED].pack('L'))
    end     
  end
  
  def proxy_hostbyname(client)
    m = Message.new
    printf('PROXY NAME LOOKUP ') if @debug
    m.read(client, @@PROXY_HOSTBYNAME)
    printf("for fd:%d pid:%d %s\n", m.fd, m.pid, m.body) if @debug
    if @relay
      @relay.write_queue.push(m)
      @client_sockets[m.key] = SocketHost.new(client, @mtu, m.pid, m.fd, @relay.write_queue)
    else
      puts "PROXY NAME LOOKUP can't forward because no relay online" if @debug
      client.write(Array.new(8,0).pack('L*')) #Send a null list to the client
    end
  end  
  
  def relay_open(server)
    if @relay
      puts '*WARNING* REALY_OPEN attempt but relay is already connected!' if @debug
      return false unless @relay.state == :dead
      puts 'RELAY_OPEN existing relay appeared dead allowing new connection' if @debug 
    end
    @relay = Relay.new(server, @client_sockets, @debug) 
    puts "RELAY_OPEN from #{server.peeraddr(false)[2]}" #always print
    true
  end
end #class
