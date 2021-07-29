
CMAKE_MINIMUM_REQUIRED(VERSION 2.8.12)

PROJECT(ssh-agent-bridge)

SET(SOURCES
	"main.cpp"
	"log.cpp" 
	"util.cpp" 
	"message_dispatcher.cpp"
	"ini_parse.cpp" 
	"cmdline_option.cpp" 
	"application.cpp" 
	"service_support.cpp" 
	"lxperm.cpp"
	
	"protocol/connection_manager/forwarding.cpp"
	"protocol/connection_manager/proxy.cpp"

	"protocol/namedpipe/client.cpp"
	"protocol/namedpipe/connector.cpp"
	"protocol/namedpipe/listener.cpp"

	"protocol/pageant/client.cpp"
	"protocol/pageant/listener.cpp"

	"protocol/unix/listener.cpp"

	"protocol/libassuan_socket_emulation/client.cpp"
	"protocol/libassuan_socket_emulation/connector.cpp"
	"protocol/libassuan_socket_emulation/listener.cpp"
	
	)

ADD_EXECUTABLE(ssh-agent-bridge WIN32 ${SOURCES})
TARGET_LINK_LIBRARIES(ssh-agent-bridge Ws2_32 Bcrypt)