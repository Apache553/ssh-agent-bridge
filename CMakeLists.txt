
CMAKE_MINIMUM_REQUIRED(VERSION 2.8.11)

PROJECT(ssh-agent-bridge)

SET(HEADERS
	"protocol_listener_base.h" 
	"protocol_listener_win32_namedpipe.h" 
	"protocol_ssh_helper.h"
	"log.h" 
	"util.h")

SET(SOURCES
	"main.cpp"
	"protocol_listener_win32_namedpipe.cpp"
	"log.cpp" 
	"util.cpp")



ADD_EXECUTABLE(ssh-agent-bridge WIN32 ${SOURCES} ${HEADERS})