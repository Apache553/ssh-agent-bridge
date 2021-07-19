
CMAKE_MINIMUM_REQUIRED(VERSION 2.8.11)

PROJECT(ssh-agent-bridge-wsl2-helper)

ADD_EXECUTABLE(ssh-agent-bridge-wsl2-helper "main.cpp")
TARGET_LINK_LIBRARIES(ssh-agent-bridge-wsl2-helper pthread)