class Relay

  @@RELAY_CONNECT = 2
  @@RELAY_CLOSE = 5
  @@RELAY_WRITE = 3
  @@RELAY_HOSTBYNAME = 20
  
  attr_accessor :write_queue
  attr_accessor :state

  def initialize(relay_sock, clients, debug=false)
    @sock = relay_sock
    @state = :connected
    @clients = clients
    @write_queue = Queue.new
    @debug = debug
    puts 'RELAY new' if @debug
  end
 
  def start
  puts 'RELAY start' if @debug
    loop do
      r,w,e = select([@sock],[@sock],[])
        raise IOError, 'socket in exception state.' if e.length != 0
        #first send the next waiting message
        w.each { |s| s.send_all(@write_queue.pop.to_s) unless @write_queue.empty? }
        r.each { |s| process_message(Message.new.read(s)) }  
    end
  rescue IOError,Errno::EPIPE => e
    if e.message == 'stream closed'
      puts 'PIPE Stalled sleeping 5'
      sleep(5)
      retry
    end
    puts "RELAY LOOP #{e.message} state: dead" if @debug
    @state = :dead
    @sock.close if @sock
    @sock = nil
    @clients.each { |z,c| c.close }
  rescue StandardError => e
    puts "RELAY LOOP #{e.message}"
    retry
  end

private

 def process_message(message)
   case message.command
   when @@RELAY_CONNECT
     puts "RELAY MESSAGE Connect response message for #{message.key}" if @debug
     @clients[message.key].connect(message.body) #expect state change, write would have raised
  
   when @@RELAY_WRITE
     puts "RELAY MESSAGE Write message to #{message.key}" if @debug
     @clients[message.key].write(message)
       
   when @@RELAY_CLOSE
     puts "RELAY MESSAGE close for socket #{message.key}" if @debug
     @clients[message.key].close
   
   when @@RELAY_HOSTBYNAME
     puts "RELAY MESSAGE response to DNS query for #{message.key}" if @debug
     @clients[message.key].response(message)
       
   else
     puts 'RELAY MESSAGE Unknown command from relay, message discarded' if @debug
   end
 end
end #class