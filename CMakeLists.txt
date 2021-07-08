
CMAKE_MINIMUM_REQUIRED(VERSION 2.8.11)

PROJECT(ssh-agent-bridge)

SET(HEADERS
	"protocol_listener_base.h" 
	"protocol_listener_win32_namedpipe.h" 
	"protocol_listener_unix_domain.h"
	"protocol_listener_iocp_connection_manager.h"
	"protocol_listener_pageant.h"
	"protocol_ssh_helper.h"
	"log.h" 
	"util.h" 
	"protocol_client_base.h" 
	"protocol_client_pageant.h"
	"protocol_client_win32_namedpipe.h" 
	"message_dispatcher.h"
	)

SET(SOURCES
	"main.cpp"
	"protocol_listener_base.cpp"
	"protocol_listener_win32_namedpipe.cpp"
	"protocol_listener_unix_domain.cpp"
	"protocol_listener_iocp_connection_manager.cpp"
	"protocol_listener_pageant.cpp"
	"log.cpp" 
	"util.cpp" 
	"protocol_client_pageant.cpp"
	"protocol_client_win32_namedpipe.cpp"
	"message_dispatcher.cpp"
	)

ADD_EXECUTABLE(ssh-agent-bridge WIN32 ${SOURCES} ${HEADERS})
TARGET_LINK_LIBRARIES(ssh-agent-bridge Ws2_32)