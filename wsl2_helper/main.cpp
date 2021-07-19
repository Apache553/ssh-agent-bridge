
/*
 * This program is created with the purpose of forwarding Windows libassuan
 * emulated sockets to WSL unix domain sockets.
 */

#include <cassert>
#include <iostream>
#include <string>
#include <cstring>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <thread>
#include <atomic>

#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>



int PrepareListener();
int ListenLoop(int fd);
void HandleConnection(int fd);
void DoForward(int fd1, int fd2);
void GracefullyHup(pollfd(&fdList)[2], int i);

bool SendBuffer(int fd, const char* buffer, size_t length);
bool ReceiveBuffer(int fd, const char* buffer, size_t length);

void ChildReaper(int signal);
void DeleteSocket();

std::string GetDefaultGateway();
std::string GetTempFilename();

bool ParseCommandLine(int argc, char** argv);
bool ValidateAddress(const std::string& addr);

void SetThreadName(const std::string& name);

void MonitorLoop();
pid_t GetPid();
bool WritePid();

void AddRefHandler(int signal);
void ReleaseHandler(int signal);



static constexpr size_t MAX_BUFFER_SIZE = 8 * 1024;
static constexpr size_t NONCE_LENGTH = 16;

char ioBuffer[MAX_BUFFER_SIZE];

std::string localSocketPath;
std::string remoteSocketPath;
std::string remoteAddress;
std::string pidFilePath;
bool backgroundRunning = false;
bool refcountFlag = false;
bool shouldDeleteSocket = false;

pid_t waitingPid;

std::atomic<int> refcount{ 1 };

int main(int argc, char** argv)
{
	// check arguments
	if (argc <= 1 || !ParseCommandLine(argc, argv))
	{
		std::cerr << "Usage: " << argv[0] << " -r <remote> [-l local] [-a remoteAddress] [-b] [-p pidFile] [-c] [-h]\n" <<
			"Option:\n" <<
			"\t-l local\n\t\tsocket path in wsl environment. generated randomly if not specified, path written to stdout\n" <<
			"\t-r remote\n\t\tsocket file under windows(wsl path style)\n" <<
			"\t-a remoteAddress\n\t\twindows host ip, deduced from default route if not specified\n" <<
			"\t-b\n\t\tfork to background\n" <<
			"\t-p pidFile\n\t\twrite main process pid to file, if process in the file is alive, this process will not do listening\n" <<
			"\t-c\n\t\tenable refcount, increase refcount when started, decrease refcount when parent process exit\n"
			"\t-h\n\t\tdisplay this help message\n" <<
			"Note: most messages are written to stderr\n" <<
			"Example: " << argv[0] << " -l /tmp/ssh-agent.sock -r /mnt/c/Users/John/ssh-bridge.sock\n";
		return 1;
	}

	// set SIGCHLD signal handler
	struct sigaction action;
	std::memset(&action, 0, sizeof(action));
	action.sa_handler = &ChildReaper;
	sigaction(SIGCHLD, &action, NULL);
	// set SIGINT/SIGHUP/SIGTERM handler
	std::memset(&action, 0, sizeof(action));
	action.sa_handler = [](int)
	{
		exit(0);
	};
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGHUP, &action, NULL);
	sigaction(SIGTERM, &action, NULL);
	std::memset(&action, 0, sizeof(action));
	action.sa_handler = AddRefHandler;
	sigaction(SIGUSR1, &action, NULL);
	std::memset(&action, 0, sizeof(action));
	action.sa_handler = ReleaseHandler;
	sigaction(SIGUSR2, &action, NULL);

	if (remoteSocketPath.empty())
	{
		std::cerr << "remote socket path must be specified!\n";
		return 1;
	}

	if (refcountFlag)
	{
		if (!backgroundRunning)
		{
			std::cerr << "must specify -b to use -c\n";
			return 1;
		}
		if (pidFilePath.empty()) {
			std::cerr << "must specify a pid file!\n";
			return 1;
		}
	}

	if (localSocketPath.empty())
	{
		localSocketPath = GetTempFilename();
		if (localSocketPath.empty())
		{
			std::cerr << "cannot generate filename!\n";
			return 1;
		}
		std::cout << localSocketPath << '\n';
	}

	if (remoteAddress.empty())
	{
		remoteAddress = GetDefaultGateway();
		if (remoteAddress.empty())
		{
			std::cerr << "cannot get default gateway! using 127.0.0.1\n";
			remoteAddress = "127.0.0.1";
		}
		else
		{
			std::cerr << "using default gateway " << remoteAddress << '\n';
		}
	}

	if (!ValidateAddress(remoteAddress))
	{
		std::cerr << "invalid remote address!\n";
		return 1;
	}

	waitingPid = getppid();
	if (backgroundRunning)
	{
		// do double fork, then monitor waitingPid to exit
		pid_t childPid;
		childPid = fork();
		if (childPid < 0) {
			std::cerr << "fork() failed!\n";
			return 1;
		}

		if (childPid != 0)
		{
			waitpid(childPid, nullptr, 0);
			// std::cerr << "exit invoked process\n";
			exit(0);
		}

		setsid();

		childPid = fork();
		if (childPid < 0) {
			std::cerr << "fork() failed!\n";
			return 1;
		}
		if (childPid != 0)
		{
			// std::cerr << "exit first child\n";
			exit(0);
		}
		// std::cerr << "starting monitor thread\n";
		std::cerr << "waitingPid = " << waitingPid << '\n';

		// mess up fd
		int nullfd = open("/dev/null", O_WRONLY);
		if (nullfd < 0)
		{
			std::cerr << "cannot open /dev/null\n";
			return 1;
		}
		if ((dup2(nullfd, STDOUT_FILENO) == -1) ||
			(dup2(nullfd, STDERR_FILENO) == -1))
		{
			std::cerr << "dup2() failed!\n";
			return 1;
		}
		close(nullfd);
	}

	if (refcountFlag)
	{
		pid_t pid = GetPid();
		if (pid > 0)
		{
			// pid present
			if (kill(pid, 0) == 0)
			{
				SetThreadName("Monitor");
				// notify new session
				kill(pid, SIGUSR1);
				MonitorLoop();
				// notify session termination
				kill(pid, SIGUSR2);
				exit(0);
			}
		}
	}

	// pid isn't present or process with pid in file exited
	if (!pidFilePath.empty()) {
		// write self pid to file
		if (!WritePid())
		{
			std::cerr << "cannot write pid to file!\n";
			return 1;
		}
	}

	if (refcountFlag)
	{
		std::thread([]()
			{
				SetThreadName("Monitor");
				MonitorLoop();
				raise(SIGUSR2);
			}).detach();
	}

	SetThreadName("Main");
	
	std::atexit(DeleteSocket);

	// prepare listen socket
	int listenSocket = PrepareListener();
	if (listenSocket < 0)
		return 1;

	return ListenLoop(listenSocket);
}

int PrepareListener()
{
	constexpr size_t MAX_LENGTH = sizeof(sockaddr_un::sun_path) / sizeof(char);
	if (localSocketPath.size() + 1 > MAX_LENGTH)
	{
		std::cerr << "local socket path too long!\n";
		return -1;
	}

	int listenSocket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listenSocket < 0)
	{
		std::cerr << "socket() failed with " << errno << ' ' << strerror(errno) << '\n';
		return -1;
	}

	sockaddr_un socketAddress;
	std::memset(&socketAddress, 0, sizeof(socketAddress));
	socketAddress.sun_family = AF_UNIX;
	strncpy(socketAddress.sun_path, localSocketPath.c_str(), MAX_LENGTH);

	if (bind(listenSocket, reinterpret_cast<sockaddr*>(&socketAddress), sizeof(socketAddress)) != 0)
	{
		std::cerr << "bind() failed with " << errno << ' ' << strerror(errno) << '\n';
		close(listenSocket);
		return -1;
	}

	chmod(localSocketPath.c_str(), S_IRUSR | S_IWUSR);
	shouldDeleteSocket = true;

	if (listen(listenSocket, 8))
	{
		std::cerr << "listen() failed with " << errno << ' ' << strerror(errno) << '\n';
		close(listenSocket);
		return -1;
	}

	return listenSocket;
}

int ListenLoop(int fd)
{
	bool continueFlag = true;
	while (continueFlag)
	{
		int incoming = accept(fd, NULL, NULL);
		if (incoming < 0)
		{
			switch (errno)
			{
			case EINTR:
				continue;
			default:
				std::cerr << "accept() failed with " << errno << ' ' << strerror(errno) << '\n';
				continueFlag = false;
				break;
			}
		}
		pid_t childPid = fork();
		if (childPid == 0)
		{
			shouldDeleteSocket = false;
			close(fd);
			HandleConnection(incoming);
			return 0;
		}
		else
		{
			close(incoming);
		}
	}
	close(fd);
	return 0;
}

void HandleConnection(int fd)
{
	std::ifstream socketFile;
	socketFile.open(remoteSocketPath, std::ios::in | std::ios::binary);
	if (!socketFile.is_open())
	{
		std::cerr << "cannot open remote socket file!\n";
		return;
	}

	int portNumber;
	socketFile >> portNumber;
	assert((portNumber >= 0) && (portNumber <= 65535));
	while (!socketFile.eof() && socketFile.peek() == '\n')
		socketFile.get();

	char nonce[NONCE_LENGTH];
	socketFile.read(nonce, NONCE_LENGTH);
	if (socketFile.gcount() != NONCE_LENGTH)
	{
		std::cerr << "cannot read nonce!\n";
		return;
	}

	sockaddr_in socketAddress;
	std::memset(&socketAddress, 0, sizeof(socketAddress));
	socketAddress.sin_family = AF_INET;
	socketAddress.sin_port = htons(static_cast<in_port_t>(portNumber));
	if (inet_pton(AF_INET, remoteAddress.c_str(), &socketAddress.sin_addr) <= 0)
	{
		std::cerr << "inet_pton failed!\n";
		return;
	}

	int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (remoteSocket < 0)
	{
		std::cerr << "socket() failed with " << errno << ' ' << strerror(errno) << '\n';
		return;
	}

	if (connect(remoteSocket, reinterpret_cast<sockaddr*>(&socketAddress), sizeof(socketAddress)) != 0)
	{
		std::cerr << "connect() failed with " << errno << ' ' << strerror(errno) << '\n';
		return;
	}

	if (!SendBuffer(remoteSocket, nonce, NONCE_LENGTH))
	{
		std::cerr << "cannot send nonce!\n";
		return;
	}

	DoForward(fd, remoteSocket);

	close(remoteSocket);
	close(fd);
	return;
}

void DoForward(int fd1, int fd2)
{
	pollfd fdList[2];
	memset(&fdList, 0, sizeof(fdList));

	fdList[0].fd = fd1;
	fdList[0].events = POLLIN | POLLERR | POLLHUP;
	fdList[1].fd = fd2;
	fdList[1].events = POLLIN | POLLERR | POLLHUP;

	while (true)
	{
		int r = poll(fdList, 2, -1);
		if (r < 0)
		{
			switch (errno)
			{
			case EINTR:
				continue;
			default:
				std::cerr << "poll() failed with " << errno << ' ' << strerror(errno) << '\n';
				return;
			}
		}
		else if (r > 0)
		{
			for (size_t i = 0; i < 2; ++i)
			{
				if (fdList[i].revents & POLLERR)
				{
					std::cerr << "fd " << fdList[i].fd << " encountered an error!\n";
					return;
				}
				else if (fdList[i].revents & POLLIN)
				{
					// read some then write to another fd
					ssize_t rr = recv(fdList[i].fd, ioBuffer, MAX_BUFFER_SIZE, MSG_DONTWAIT);
					if (rr < 0)
					{
						std::cerr << "recv() failed with " << errno << ' ' << strerror(errno) << '\n';
						return;
					}
					else if (rr == 0)
					{
						GracefullyHup(fdList, i);
						return;
					}
					else if ((rr > 0) && !SendBuffer(fdList[1 - i].fd, ioBuffer, rr))
					{
						return;
					}
				}
				else if (fdList[i].revents & POLLHUP)
				{
					// close sockets gracefully
					GracefullyHup(fdList, i);
					return;
				}
			}
		}
	}
}

void ChildReaper(int signal)
{
	pid_t pid;
	int status;

	assert(signal == SIGCHLD);

	// loop to reap all dead child process
	while ((pid = waitpid(-1, &status, WNOHANG)) != -1)
	{
		// do nothing
	}
}

bool SendBuffer(int fd, const char* buffer, size_t length)
{
	size_t sentBytes = 0;
	ssize_t r;
	while (sentBytes != length)
	{
		r = send(fd, buffer + sentBytes, length - sentBytes, 0);
		if (r < 0)
		{
			if (errno == EINTR)continue;
			std::cerr << "send() failed with " << errno << ' ' << strerror(errno) << '\n';
			return false;
		}
		sentBytes += r;
	}
	return true;
}

bool ReceiveBuffer(int fd, const char* buffer, size_t length)
{
	size_t readBytes = 0;
	ssize_t r;
	while (readBytes != length)
	{
		r = send(fd, buffer + readBytes, length - readBytes, 0);
		if (r < 0)
		{
			if (errno == EINTR)continue;
			std::cerr << "send() failed with " << errno << ' ' << strerror(errno) << '\n';
			return false;
		}
		readBytes += r;
	}
	return true;
}

void GracefullyHup(pollfd(&fdList)[2], int i)
{
	//std::cout << "gracefully close sockets~\n";
	ssize_t rr;
	do {
		rr = recv(fdList[i].fd, ioBuffer, MAX_BUFFER_SIZE, 0);
		if ((rr > 0) && !SendBuffer(fdList[1 - i].fd, ioBuffer, rr))
			return;
	} while (rr > 0);
	shutdown(fdList[i].fd, SHUT_RD);
	shutdown(fdList[1 - i].fd, SHUT_WR);
	do
	{
		rr = recv(fdList[1 - i].fd, ioBuffer, MAX_BUFFER_SIZE, 0);
		if ((rr > 0) && !SendBuffer(fdList[i].fd, ioBuffer, rr))
			return;
	} while (rr > 0);
	shutdown(fdList[1 - i].fd, SHUT_RD);
	shutdown(fdList[i].fd, SHUT_WR);
	return;
}

void DeleteSocket()
{
	if (shouldDeleteSocket)
		remove(localSocketPath.c_str());
}

std::string GetDefaultGateway()
{
	std::ifstream route;
	route.open("/proc/net/route", std::ios::in);
	if (!route.is_open())
	{
		return {};
	}
	std::string Iface, Destination, Gateway, Flags, RefCnt, Use, Metric, Mask, Mtu, Window, IRTT;
	do
	{
		route >> Iface >> Destination >> Gateway >> Flags >> RefCnt >> Use >> Metric >> Mask >> Mtu >> Window >> IRTT;
		if ((Destination == "00000000") && (Mask == "00000000"))
		{
			std::istringstream iss(Gateway);
			in_addr addr;
			iss >> std::hex >> addr.s_addr;
			return inet_ntoa(addr);
		}
	} while (!route.eof());
	return {};
}

bool ParseCommandLine(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i)
	{
		std::string option = argv[i];
		std::string* target = nullptr;
		if (option == "-l")
			target = &localSocketPath;
		else if (option == "-r")
			target = &remoteSocketPath;
		else if (option == "-a")
			target = &remoteAddress;
		else if (option == "-p")
			target = &pidFilePath;
		else if (option == "-b")
		{
			backgroundRunning = true;
			continue;
		}
		else if (option == "-c")
		{
			refcountFlag = true;
			continue;;
		}
		else if (option == "-h")
		{
			return false;
		}
		else
		{
			std::cerr << "unknown option\n";
			return false;
		}
		if (i + 1 >= argc)
		{
			std::cerr << "option expect a argument\n";
			return false;
		}
		*target = argv[i + 1];
		++i;
	}
	return true;
}

std::string GetTempFilename()
{
	auto name = std::tmpnam(nullptr);
	if (name == nullptr)
		return std::string();
	return std::string(name) + ".socket";
}

bool ValidateAddress(const std::string& addr)
{
	in_addr iaddr;
	if (inet_pton(AF_INET, remoteAddress.c_str(), &iaddr) <= 0)
	{
		return false;
	}
	return true;
}

void MonitorLoop()
{
	pid_t masterPid = GetPid();
	if (masterPid <= 0)
		return;
	while (
		(kill(waitingPid, 0) == 0) &&
		(kill(masterPid, 0) == 0))
		std::this_thread::sleep_for(std::chrono::seconds(1));
}

pid_t GetPid()
{
	pid_t pid = 0;
	std::ifstream file;
	file.open(pidFilePath, std::ios::in);
	if (!file.is_open())
		return -1;
	file >> pid;
	if (pid <= 0)
		return -1;
	return pid;
}

bool WritePid()
{
	pid_t pid = getpid();
	std::ofstream file;
	file.open(pidFilePath.c_str());
	if (!file.is_open())
		return false;
	file << pid << '\n';
	return true;
}


void AddRefHandler(int signal)
{
	++refcount;
}

void ReleaseHandler(int signal)
{
	int v = --refcount;
	if (v == 0)
		exit(0);
}

void SetThreadName(const std::string& name)
{
	pthread_setname_np(pthread_self(), name.c_str());
}