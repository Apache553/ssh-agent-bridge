
CMAKE_MINIMUM_REQUIRED(VERSION 2.8.11)

PROJECT(ssh-agent-bridge)

SET(HEADERS
	"protocol_listener_base.h" 
	"protocol_listener_win32_namedpipe.h" 
	"protocol_ssh_helper.h"
	"log.h" 
	"util.h" 
	"protocol_client_base.h" 
	"protocol_client_pageant.h" 
	"protocol_listener_iocp_connection_manager.h")

SET(SOURCES
	"main.cpp"
	"protocol_listener_base.cpp"
	"protocol_listener_win32_namedpipe.cpp"
	"protocol_listener_iocp_connection_manager.cpp"
	"log.cpp" 
	"util.cpp" 
	"protocol_client_pageant.cpp")

ADD_EXECUTABLE(ssh-agent-bridge WIN32 ${SOURCES} ${HEADERS})
TARGET_LINK_LIBRARIES(ssh-agent-bridge Ws2_32)