ssh-agent-bridge
=====
为了让 Windows 平台上的 ssh agent 能适配多种 ssh client 和在 wsl 环境下使用的工具

Windows 平台上存在着多种 ssh agent 的实现，由于 Windows 平台的特殊性，这些 agent 程序都有一套自行实现的进程间通信方式作为在 Windows 10 发布之前都还不存在的 Unix 域套接字的替代。这导致了不同生态下的 ssh 客户端无法使用其他生态的 ssh agent。在 Windows 10 发布之后，更是多了 Windows 原生 Unix 域套接字的支持和WSL这一生态。

本程序作为一个转译层，可以适配各种实现中的进程间通信方式，并将数据转发到指定的某一个具体 agent 实现上，从而使不同实现的 ssh 客户端也能够利用同一个 ssh agent。目前支持的通信方式有：

- Windows 10 OpenSSH *
- Putty (Pageant) *
- Windows native unix domain socket
- Libassuan emulated unix domain socket
- HyperV socket

其中带有 * 标记的是支持的具体 agent 实现类型，其余的仅能支持监听请求。

另外，为了方便使用，本程序另外支持了对 gpg-agent 的转发，从而使得在 WSL 环境下也能利用 Windows 中正在运行的 gpg-agent 实例。

如何使用
-----

### 准备工作
编译或下载得到`ssh-agent-bridge.exe`并将它放在你喜欢的位置。
编译需要 msvc 工具链，暂时不支持 mingw。


### 编辑配置
在不指定配置文件位置的情况下，程序将会依次尝试读取`%USERPROFILE%\ssh-agent-bridge\ssh-agent-bridge.ini`和可执行文件所在目录下的`ssh-agent-bridge.ini`。
建议将配置保存到前者的位置，如果对应目录不存在，你需要自己创建相应的目录。
具体配置项请参阅下文和根目录下的配置文件。

### 作为服务运行

用管理员命令行执行
```
ssh-agent-bridge.exe /InstallService
```
将会创建一个Per-user service到系统中，在重新登录后生效，启动模式为手动启动。

你可能需要执行
```
Set-Service ssh-agent-bridge -StartupType auto
```
来让它自动启动，在重新登录后生效。

若需要卸载服务请执行
```
ssh-agent-bridge.exe /UninstallService
```
在重新登录后生效。

如果迁移了可执行文件的位置，请重新安装服务。

### 直接运行

或者有的时候你不想安装服务，你也可以用你喜欢的方式来启动。命令行参数参见下文。

### 日志输出
日志将会被保存到`%APPDATA%\ssh-agent-bridge.log`

如果想要实时观察输出情况，使用 powershell 命令：
```
Start-Process -Wait .\ssh-agent-bridge.exe '/Console'
```
在命令行窗口实时观察输出。

参考
-----

### 命令行选项
```
/Loglevel debug|info|warn|error
设置输出日志等级

/Service
以服务运行所必要的选项

/Debug
通过OutputDebugString输出日志

/Console
创建或附加到一个控制台窗口用于输出日志，以服务运行时不可用。

/Config <path>
指定配置的文件的路径

/InstallService
安装服务

/UninstallService
卸载服务
```

### 配置文件
配置文件为 ini 格式，每一个 section 都指定了一个要适配的通信方式（除了`[general]`）

可选的项要么键名连带值一起指定，要么不写。

示例请参见根目录下的 ssh-agent-bridge.ini

```
[general]
; 设置日志输出等级
; 可选
; 可用的选项：
;   - debug 
;   - info [默认]
;   - warn
;   - error
loglevel = info

; 定义一种通信方式
; section 的名称可以修改为不重复的任意有效字符串，这里起名为 namedpipe
[namedpipe]
; 指定类型
; 必须指定
; 可用的选项：
;   - namedpipe    ; listener, client
;                  ; Windows 10 原生提供的 ssh-agent 所使用的通信方式
;   - pageant      ; listener, client
;                  ; Putty 使用的通信方式
;   - unix         ; listener
;                  ; Unix 域套接字，可以在 WSL 1 下直接使用
;   - assuan_emu   ; listener
;                  ; 与 libassuan 的模拟 Unix 域套接字兼容的通信方式，用于兼容 gpg 和实现 WSL 2 的支持
;   - hyperv       ; listener
;                  ; 使用 AF_HYPERV 与 AF_VSOCK 实现的 WSL2/Hyper-V 虚拟机与宿主之间的通信支持
; 注意： 需要你自己保证各个通信方式之间没有冲突
type = namedpipe

; 指定其工作的角色
; 必须指定
; 可用的选项:
;   - client       ; 作为客户端运行，收到的请求将会通过他向实际工作的 agent 转发
;   - listener     ; 作为监听者运行，用于接收请求
role = client

; 套接字路径
; 必须指定
; 适用于： namedpipe, unix, assuan_emu
; 注意： 对于监听者，这个路径将会是其监听的套接字/管道的路径。
;       对于客户端，这个路径将会是要连接到的 agent 的路径
;       支持%VAR%格式的环境变量
path = \\.\pipe\openssh-ssh-agent

; 启用权限检查，通过设置对应文件的ACL和对请求发起者身份的检查来阻止其他用户的访问
; 可选
; 适用于： namedpipe, pageant, unix, assuan_emu
; 可用的选项：
;   - true         ; 默认
;   - false
enable-permission-check = true

; 设置套接字监听的地址
; 可选
; 适用于： assuan_emu, hyperv
; 注意： 对于 assuan_emu 方式：
;           默认值：0.0.0.0
;           由于WSL2采用了虚拟机的方案，虚拟机的网卡很可能被 Windows 识别为公共网络。
;           请针对此调整 Windows 防火墙，允许程序在公共网络上的访问，否则 WSL2 无法连接。
;       对于 hyperv 方式：
;           默认值：{00000000-0000-0000-0000-000000000000} 即 HV_GUID_WILDCARD
;           支持几个预定义的值：wildcard, children, loopback, wsl2
;           或者是形如 {00000000-0000-0000-0000-000000000000} 的 GUID
;           设置为 wsl2 时，将会自动识别并使用 wsl2 虚拟机的 ID
;           参阅：https://bit.ly/3Cnml21
listen-address = 0.0.0.0

; 设置监听的端口
; 可选
; 适用于： hyperv
; 默认值： 0x44417A9F
listen-port = 0x44417A9F

; 允许非特权进程访问在特权上下文中运行的 agent
; 可选
; 适用于： pageant
; 可用的选项：
;   - true
;   - false        ; 默认，Pageant 默认行为
allow-non-elevated-access = false

; 指定想要转发的 gpg 套接字位置
; 可选
; 适用于： unix, assuan_emu, hyperv
; 注意： 如果你想转发 gpg 套接字，请指定此选项。目标将作为 libassuan 模拟的 Unix 域套接字被连接。
forward-socket-path = %APPDATA%\gnupg\S.gpg-agent

; 写入 LXSS 元数据
; 可选
; 适用于： unix, assuan_emu
; 可用选项：
;   - true
;   - false        ; 默认
; 注意： 对于'unix'通信方式，对应的元数据将被写入至其所在目录。
;       而对于'assuan_emu'通信方式，对应的元数据将被写入至文件本身。
write-lxss-metadata = false

; 将被写入的 LXSS 元数据
; 可选
; 适用于： unix, assuan_emu
; 注意： 未指定或者留空的项将不会被写入。unix套接字需要父目录有执行权限才能运作。
metadata-uid = 1000
metadata-gid = 1000
metadata-mode = 0600
```

## WSL 支持

### WSL1 支持

WSL1 支持 Windows 原生 Unix 域套接字，所以可以在配置中定义`unix`方式的通信方式，然后在 WSL 中访问即可。

### WSL2 支持

由于 WSL 2 移除了`AF_UNIX`在 WSL 与 Windows 的互操作能力以及 WSL 的网络栈与宿主相对隔离，要正常使用功能需要一个工具程序。

目前可以使用 socat 通过`AF_VSOCK`转发或使用本项目实现的 helper 来转发。

#### socat

socat 可以通过 AF_VSOCK 来连接到宿主系统上的 Integration Service。要求在宿主系统上写入对应的注册表项，在本程序中定义`hyperv`方式的通信方式。

根目录下有一 powershell 脚本`hyperv_register.ps1`可以用于帮助您写入相应的注册表项和生成对应的 socat 命令行选项。

##### 注意事项

由于 Microsoft 的限制，在供 WSL2 使用时必须显式指定其 VmId，而通常的 HyperV Linux虚拟机使用全零 GUID wildcard 就足够了。为了获得 WSL2 所在虚拟机的 ID，在 WSL2 运行中的情况下，在管理员权限命令行中执行：

```
hcsdiag.exe list
```

可以得到类似以下的输出：

```
XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
    VM,                         Running, XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX, WSL
```

`XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX`就是对应的 VmId，将`{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}`填入相应配置的`listen-address`即可。

##### 示例

在`.bashrc`中写入

```
export SSH_AUTH_SOCK=$HOME/.ssh/agent.socket
SOCAT_OPT="SOCKET-CONNECT:40:0:x0000x9f7a4144x02000000x00000000"
ss -lnx | grep -q $SSH_AUTH_SOCK
if [ $? -ne 0 ]; then
	rm -f $SSH_AUTH_SOCK
    (setsid nohup socat UNIX-LISTEN:$SSH_AUTH_SOCK,fork $SOCAT_OPT >/dev/null 2>&1 &)
fi
```

用脚本的输出内容的`*-CONNECT:*`内容替换`SOCAT_OPT`的内容（根据 socat 的版本选择对应的项），即可在登录时自动启动 socat 并设置环境变量。

#### Helper

此 helper 可以在内核不支持`VSOCK`之时使用。要求在配置中定义`assuan_emu`方式的通信方式。

在 linux 环境下编译`wsl2_helper`目录下的程序，得到的程序`ssh-agent-bridge-wsl2-helper`就是用来解决这个问题的。

```
Usage: ./ssh-agent-bridge-wsl2-helper -r <remote> [-l local] [-a remoteAddress] [-b] [-p pidFile] [-c] [-h]
Option:
        -l local
                指定 WSL 环境中的 socket 路径
        -r remote
                指定 WSL 环境中的 windows 侧 socket 路径
        -a remoteAddress
                windows 的 ip 地址，未指定时将从默认路由中获取，若失败则设置为 127.0.0.1
        -d
                在创建 socket 时删除已存在的 socket 文件
        -b
                fork 到后台运行
        -p pidFile
                指定 pid 文件，程序会将自身的 pid 写入文件中。在启动时检查该文件中的 pid 所代表的进程是否存活，若存活则直接退出。
        -c
                使用引用计数，必须与 -b 和 -p 一起使用。使得程序在启动时增加计数，父进程退出时减少计数。计数到 0 时后台程序退出。
        -h
                显示帮助信息
```

##### 示例

在`.bashrc`中写入

```
export SSH_AUTH_SOCK=$HOME/.ssh/agent.socket
ssh-agent-bridge-wsl2-helper -b \
    -l $SSH_AUTH_SOCK \ 
    -r /mnt/c/Users/John/ssh-agent-bridge/wsl2-ssh-agent.socket \
    -p $HOME/.ssh/helper-ssh-agent.pid 2>/dev/null
```

即可在登录时自动启动 helper 并设置环境变量。

## GPG 转发

在使用 GPG 转发时，对于不同的 WSL 版本有不同的方法。

在 WSL1 中，由于 helper 使用的通信方式是`assuan_emu`，与 GPG4Win 的协议是兼容的，所以可以直接将`-r`选项中的路径指向 gpg-agent 创建的 socket 文件（在`%APPDATA%\gnupg`中），并将`-l`选项中的路径设置为`$HOME/.gnupg/S.gpg-agent`或者其它对应的 socket 路径。
或者如果你不想用 helper，编写使用`unix`通信方式的配置将 gpg-agent 的 socket 转发为 Unix 域套接字。然后再编写下面提到的`%Assuan%`文件来重定向 gpg 所使用的套接字。

在 WSL2 中，由于虚拟机网络隔离，而 gpg-agent 只监听于 localhost(127.0.0.1)，虚拟机中无法访问 Windows 的 localhost。所以不能直接只使用 helper，必须在定义对应的通信方式后使用 socat 或者 helper 来帮助转发。

### GPG 套接字重定向

由于 gpg 不支持指定 socket 的路径，只会打开 `$GNUPGHOME`或`$HOME/.gnupg` 目录下的 socket，即 `S.gpg-agent` 等文件。

你可以通过在该位置创建名字相同的普通文件，内容写入纯文本：

```
%Assuan%
socket=/home/user/whatever.socket
```

来将对 socket 的访问重定向到 `/home/user/whatever.socket`来实现自行指定 socket 路径。