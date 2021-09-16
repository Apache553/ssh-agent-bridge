ssh-agent-bridge
=====
A tool that enable multiple ssh client implementations to use a common ssh agent on Windows.

The tool provides a daemon can forward ssh agent requests sent via different IPC methods to a common agent instance.

Current Supported Methods:
 - Windows 10 OpenSSH *
 - Putty (Pageant) *
 - Windows Unix Domain Socket
 - libassuan emulated Unix Domain Socket
 - HyperV Socket
 - Cygwin Socket

Methods with asterisk(*) means it can be an upstream agent instance, or it can listen requests only.

Additionally, the tool provides a way to forward GnuPG sockets on Windows. You can use your gpg-agent instance under WSL environment.

How to use
-----

### Prerequisite
Download pre-build binary or build your own, put it in the folder you prefer.
Building requires MSVC toolchain. MinGW is not supported.

### Create your config
The tool will try reading config from `%USERPROFILE%\ssh-agent-bridge\ssh-agent-bridge.ini` first if no config path is specified in command line. If that failed, it will try reading `ssh-agent-bridge.ini` in the directory of the executable.
You need to create those folders manually if they don't exist.
Refer to the example config in the project root and descriptions below for details.

### Auto start
You can use various methods to start the tool. 
The tool supports running as a service for better management.

To install service, execute in administrator shell:
```
ssh-agent-bridge.exe /InstallService
```
The default mode of the service is OnDemand, you need to start the service manually.
Run following command to set the mode to auto:
```
Set-Service ssh-agent-bridge -StartupType auto
```

To uninstall service, execute in administrator shell:
```
ssh-agent-bridge.exe /UninstallService
```

Those operations all requires a re-login to take effect.
If you changed the path of the executable, you need to uninstall then install the service again.

### Manual start
You can start the tool directly if you don't want to taint Windows.
In that case, a script might be needed to pass command line parameters to the tool.

### Logging
Log will be written to `%APPDATA%\ssh-agent-bridge.log`.

In case of realtime inspection, use powershell command:
```
Start-Process -Wait .\ssh-agent-bridge.exe '/Console'
```

### Aggregation
Create more than one client in config to enable aggregation feature.
The feature will make the tool to request all configured upstream agents in lexicographical order respectively until succeeded or get enough infomation, then assemble replies into one reply.

Some agent implementations (like gpg-agent) have strange behaviors when add/remove keys to/from it. You may avoid doing such operations on those agents.

#### Supported Operations
Although ssh agent may have many features, only those operations are supported.

- Add a key
- Remove a key
- Remove all keys
- List keys
- Sign with a key

Details
-----

### Command Line Options
```
/Loglevel debug|info|warn|error
```
Override log output level.

```
/Service
```
Required when running as a service.

```
/Debug
```
Output log with `OutputDebugString`

```
/Console
```
Create or attach to a console to output log. Illegal when running as a service.

```
/Config <path>
```
Override default config path.

```
/InstallService
```
Install service.

```
/UninstallService
```
Uninstall service.

### Config File
Config file a simple ini file.
Sections except `[general]` define a client/listener.

The descriptions about `[general]` section:
```
[general]
; Set log output level
; Optional
; Available Options:
;   - debug
;   - info [default]
;   - warn
;   - error
loglevel = info
```

To define a client/listener:
```
; Define a client/listener with name 'namedpipe'
[namedpipe]

; Set type
; Required
; Available Options:
;   - namedpipe    ; listener, client
;                  ; Windows 10 OpenSSH
;   - pageant      ; listener, client
;                  ; Putty
;   - unix         ; listener
;                  ; Windows Unix Domain Socket
;   - assuan_emu   ; listener
;                  ; libassuan emulated Unix Domain Socket, can be used for gpg-agent and WSL
;   - hyperv       ; listener
;                  ; HyperV Socket
;   - cygwin       ; listener
;                  ; Cygwin Socket
; NOTE: You need to guarantee the whole config is legal.
type = namedpipe

; Set role
; Required
; Available Options:
;   - client       ; A client forwards requests to a running agent.
;   - listener     ; A listener receives requests from ssh clients.
role = client

; Set socket path
; Required
; Apply to: namedpipe, pageant, unix, assuan_emu, cygwin
; NOTE: For clients, the path specifies the agent instance they forwards requests to.
;       For listeners, the path specifies the socket file's path.
path = \\.\pipe\openssh-ssh-agent

; Enable permission checks, denying access from other users
; Optional
; Apply to: namedpipe, pageant, unix, assuan_emu, cygwin
; Available Options:
;   - true [default]
;   - false
enable-permission-check = true

; Set listening address of socket
; Optional
; Apply to: assuan_emu, hyperv
; NOTE: For assuan_emu, it means the listening address. 
;         The default value is '0.0.0.0'.
;       For hyperv, it means virtual mechine id which is allowed to connect. 
;         The default value is '{00000000-0000-0000-0000-000000000000}' (HV_GUID_WILDCARD).
;         You can also put several placeholders there: wildcard, children, loopback, wsl2
;         Or a GUID represents a virtual mechine.
;         When set to 'wsl2', the tool will detect WSL2 virtual mechine id automatically.
;         For details, refer to https://bit.ly/3Cnml21
listen-address = 0.0.0.0

; Set listening port
; Optional
; Apply to: hyperv
; Default Value: 0x44417A9F
listen-port = 0x44417A9F

; Allow non-elevated process to access when tool process is elevated
; Optional
; Apply to: pageant
; Available Options:
;   - true
;   - false [default] ; Pageant's behavior
allow-non-elevated-access = false

; Restrict pageant client's target process name
; Optional
; Apply to: pageant
; NOTE: The tool always ignores self when requesting pageant upstream.
restrict-process = pageant.exe

; Set the gpg socket path
; Optional
; Apply to: unix, assuan_emu, hyperv, cygwin
; NOTE: If you don't want to use the listener to forward gpg socket, you must not set this property.
forward-socket-path = %APPDATA%\gnupg\S.gpg-agent

; Write LXSS metadata
; Optional
; Apply to: unix, assuan_emu
; Available Options:
;   - true
;   - false [default]
; NOTE: For 'unix' listeners, the metadata will be written to its parent directory.
;       For other listeners, the metadata will be written to the file.
write-lxss-metadata = false

; LXSS metadata will be written
; Options
; Apply to: unix, assuan_emu
; NOTE: Not specified metadata property or null property will be written.
metadata-uid = 1000
metadata-gid = 1000
metadata-mode = 0600
```

## WSL Support

### WSL1 Support
In WSL1 environment, the Windows Unix Domain Socket can interoperate with WSL.
Define a `unix` listener, then set right environment variable to the socket should work.

### WSL2 Support
In WSL2 environment, the only ways to communicate with the host is HyperV Sockets and Tcp/Ip Sockets.
So a helper program is needed to create a Unix Domain Socket.

#### HyperV Socket: socat
In this way, you need to register your own Integration Service with powershell script `hyperv_register.ps1`.
The script will also output the parameter used by socat to connect the host.

##### Example
Put the snippet into `.bashrc`:
```
export SSH_AUTH_SOCK=$HOME/.ssh/agent.socket
SOCAT_OPT="SOCKET-CONNECT:40:0:x0000x9f7a4144x02000000x00000000"
ss -lnx | grep -q $SSH_AUTH_SOCK
if [ $? -ne 0 ]; then
	rm -f $SSH_AUTH_SOCK
    (setsid nohup socat UNIX-LISTEN:$SSH_AUTH_SOCK,fork $SOCAT_OPT >/dev/null 2>&1 &)
fi
```
**Don't forget to replace `SOCAT_OPT`'s content with the script's output.**

#### Tcp/Ip Socket: simple helper
To use this method, you need to compile program under `wsl2_helper` directory.
The helper programs can be used in both WSL1 and WSL2 since it uses libassuan's scheme to work.
And it is possible to forward gpg-agent standalone.

##### Usage
```
Usage: ./ssh-agent-bridge-wsl2-helper -r <remote> [-l local] [-a remoteAddress] [-b] [-p pidFile] [-c] [-h]
Option:
	    -l local
		        socket path in wsl environment. generated randomly if not specified, path written to stdout
	    -r remote
		        socket path of listener in wsl environment
	    -a remoteAddress
		        windows host ip, deduced from default route if not specified
	    -d
		        delete local socket file if exists
        -b
		        fork to background
	    -p pidFile
		        write main process pid to file, if process in the file is alive, this instance will exit.
	    -c
		        enable refcount, increase refcount when started, decrease refcount when parent process exit
	    -h
		        display this help message
```

##### Example
Put the snippet into `.bashrc`:

```
export SSH_AUTH_SOCK=$HOME/.ssh/agent.socket
ssh-agent-bridge-wsl2-helper -b \
    -l $SSH_AUTH_SOCK \ 
    -r /mnt/c/Users/John/ssh-agent-bridge/wsl2-ssh-agent.socket \
    -p $HOME/.ssh/helper-ssh-agent.pid 2>/dev/null
```
**Don't forget to replace the path with your own one.**

## GPG Forwarding
Set `forward-socket-path` property in listener's config to make the listener a gpg forwarding listener.

In case of WSL1, you can use the helper directly or setup a `unix` listener.

In case of WSL2, you can only setup a `hyperv` or `assuan_emu` listener and setup the corresponding helper program to make forwarding work.

### Redirect GPG Socket
GPG can only access socket files placed under `$GNUPGHOME` (or `$HOME/.gnupg` in case the environment variable is not set). When the socket is placed in other place, you need some extra work to redirect the socket.

Create a plain text file with the same file name with the socket under `$GNUPGHOME`.
Write the snippet to the file:
```
%Assuan%
socket=/home/user/whatever.socket
```
Then accesses to the socket will be redirected to `/home/user/whatever.socket`.