
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

#include <unistd.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <poll.h>

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

static constexpr size_t MAX_BUFFER_SIZE = 8 * 1024;
static constexpr size_t NONCE_LENGTH = 16;

char ioBuffer[MAX_BUFFER_SIZE];

std::string localSocketPath;
std::string remoteSocketPath;
std::string remoteAddress = "127.0.0.1";

bool shouldDeleteSocket = false;

int main(int argc, char** argv)
{
	// check arguments
	if (argc < 3)
	{
		std::cout << "Usage: " << argv[0] << " <local> <remote> [remoteAddress]\n" <<
			"Option:\n" <<
			"\tlocal : socket path in wsl environment\n" <<
			"\tremote: socket file under windows(wsl path style)\n" <<
			"\tremoteAddress: windows host ip, deduced from default route if not specified"
			"Example: " << argv[0] << " /tmp/ssh-agent.sock /mnt/c/Users/John/ssh-bridge.sock\n";
		return 255;
	}

	localSocketPath = argv[1];
	remoteSocketPath = argv[2];
	if (argc >= 4) {
		remoteAddress = argv[3];
		std::cout << "specified remote address " << remoteAddress << '\n';
	}
	else
	{
		remoteAddress = GetDefaultGateway();
		if (remoteAddress.empty())
		{
			std::cout << "cannot get default gateway! using 127.0.0.1\n";
		}
		else
		{
			std::cout << "using default gateway " << remoteAddress << '\n';
		}
	}

	// set SIGCHLD signal handler
	struct sigaction chldAction;
	std::memset(&chldAction, 0, sizeof(chldAction));
	chldAction.sa_handler = &ChildReaper;
	sigaction(SIGCHLD, &chldAction, NULL);
	// set SIGINT/SIGHUP/SIGTERM handler
	struct sigaction exitAction;
	std::memset(&exitAction, 0, sizeof(exitAction));
	exitAction.sa_handler = [](int) {exit(0); };
	sigaction(SIGINT, &exitAction, NULL);
	sigaction(SIGHUP, &exitAction, NULL);
	sigaction(SIGTERM, &exitAction, NULL);

	std::atexit(DeleteSocket);

	// prepare listen socket
	int listenSocket = PrepareListener();
	if (listenSocket < 0)
		return 1;
	ListenLoop(listenSocket);

	return 0;
}

int PrepareListener()
{
	constexpr size_t MAX_LENGTH = sizeof(sockaddr_un::sun_path) / sizeof(char);
	if (localSocketPath.size() + 1 > MAX_LENGTH)
	{
		std::cout << "local socket path too long!\n";
		return -1;
	}

	int listenSocket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listenSocket < 0)
	{
		std::cout << "socket() failed with " << errno << ' ' << strerror(errno) << '\n';
		return -1;
	}

	sockaddr_un socketAddress;
	std::memset(&socketAddress, 0, sizeof(socketAddress));
	socketAddress.sun_family = AF_UNIX;
	strncpy(socketAddress.sun_path, localSocketPath.c_str(), MAX_LENGTH);

	if (bind(listenSocket, reinterpret_cast<sockaddr*>(&socketAddress), sizeof(socketAddress)) != 0)
	{
		std::cout << "bind() failed with " << errno << ' ' << strerror(errno) << '\n';
		close(listenSocket);
		return -1;
	}

	shouldDeleteSocket = true;

	if (listen(listenSocket, 8))
	{
		std::cout << "listen() failed with " << errno << ' ' << strerror(errno) << '\n';
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
				std::cout << "accept() failed with " << errno << ' ' << strerror(errno) << '\n';
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
		std::cout << "cannot open remote socket file!\n";
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
		std::cout << "cannot read nonce!\n";
		return;
	}

	sockaddr_in socketAddress;
	std::memset(&socketAddress, 0, sizeof(socketAddress));
	socketAddress.sin_family = AF_INET;
	socketAddress.sin_port = htons(static_cast<in_port_t>(portNumber));
	if (inet_pton(AF_INET, remoteAddress.c_str(), &socketAddress.sin_addr) <= 0)
	{
		std::cout << "inet_pton failed!\n";
		return;
	}

	int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (remoteSocket < 0)
	{
		std::cout << "socket() failed with " << errno << ' ' << strerror(errno) << '\n';
		return;
	}

	if (connect(remoteSocket, reinterpret_cast<sockaddr*>(&socketAddress), sizeof(socketAddress)) != 0)
	{
		std::cout << "connect() failed with " << errno << ' ' << strerror(errno) << '\n';
		return;
	}

	if (!SendBuffer(remoteSocket, nonce, NONCE_LENGTH))
	{
		std::cout << "cannot send nonce!\n";
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
				std::cout << "poll() failed with " << errno << ' ' << strerror(errno) << '\n';
				return;
			}
		}
		else if (r > 0)
		{
			for (size_t i = 0; i < 2; ++i)
			{
				if (fdList[i].revents & POLLERR)
				{
					std::cout << "fd " << fdList[i].fd << " encountered an error!\n";
					return;
				}
				else if (fdList[i].revents & POLLIN)
				{
					// read some then write to another fd
					ssize_t rr = recv(fdList[i].fd, ioBuffer, MAX_BUFFER_SIZE, MSG_DONTWAIT);
					if (rr < 0)
					{
						std::cout << "recv() failed with " << errno << ' ' << strerror(errno) << '\n';
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
			std::cout << "send() failed with " << errno << ' ' << strerror(errno) << '\n';
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
			std::cout << "send() failed with " << errno << ' ' << strerror(errno) << '\n';
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
