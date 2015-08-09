#!/usr/bin/ruby

require 'thread'
require 'getoptlong'
require 'socket'
require_relative 'message'
require_relative 'sockethost'
require_relative 'listener'
require_relative 'relay'
require_relative 'chash'

class IPSocket
  def send_all(message, flags=0)
    start = 0
    cnt = 0
    begin
      start = self.send(message.byteslice(start, message.bytesize), flags)
    end until start >= message.bytesize
  end
end

options = GetoptLong.new(
  ['--help', '-h', GetoptLong::NO_ARGUMENT],
  ['--debug','-d', GetoptLong::NO_ARGUMENT],  
  ['--mtu', '-m', GetoptLong::REQUIRED_ARGUMENT],
)

debug = false
mtu = 500 #bytes to read() at a time
options.each do |option, argument|
    case option
    when '--help'
      usage
    when '--debug'
      debug = true
    when '--mtu'
      mtu = argument.to_i
    else 
      usage   
    end
end
 
def usage
 puts "--debug, -d  Enables debug output (slow)\n--mtu <value>, -m<value> set packet size"
 puts 'The environment must specify PROXIFY_ADDR, and PROXIFY_PORT'
end

requests_thread = nil
trap('INT') do
  requests_thread.kill if requests_thread
end    
  
#driver code
begin
  debug = true if ('YES' == ENV['PROXIFY_DEBUG']) #override the switch
  bind_addr = ENV['PROXIFY_ADDR']
  bind_port = ENV['PROXIFY_PORT']
  unless (bind_addr and bind_port) 
    usage
    exit 1
  end
  controller = Listener.new(bind_addr, bind_port, mtu, debug)
  requests_thread = Thread.new { controller.processRequests }
rescue => e
  puts e.message
  exit 1  
end
requests_thread.join
