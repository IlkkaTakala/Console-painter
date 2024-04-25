#include <stdio.h> 
#include <stdlib.h> 
#include <unistd.h> 
#include <string.h> 
#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <netinet/in.h> 
#include <list>
#include <netinet/tcp.h>
#include <thread>

#define PORT     3900 
#define MAXLINE 1024 

struct Pixel
{
	short x;
	short y;
	short type;
	short color;

	Pixel()
	{
		x = 0;
		y = 0;
		type = 0;
		color = 0;
	}

	Pixel(int x, int y, int c, int co) : x(x), y(y), type(c), color(co) {}
};

Pixel* NetworkBuffer = nullptr;
Pixel* SavedBuffer = nullptr;

constexpr int size_x = 100;
constexpr int size_y = 100;
int BufferSize = size_x * size_y;

bool quitting = false;
int exit_pipe[2];

int sockfd;
std::list<int> clients;

void error(const char* msg)
{
	perror(msg);
	exit(1);
}

void initiate_client(int sock)
{
	printf("Sending items\n");
	send(sock, SavedBuffer, BufferSize * sizeof(Pixel), 0);
}

void input_thread()
{
	char input[128];
	while (!quitting)
	{
		fgets(input, 128, stdin);

		if (strncmp(input, "exit", 4) == 0) {
			quitting = true;
			write(exit_pipe[1], "\0", 1);
			continue;
		}
	}
}

int main() {

	printf("Server starting!\n");

	pipe(exit_pipe);

	std::thread inputter = std::thread(input_thread);

	int rc, numsocks = 0, maxsocks = 10;

	fd_set fds, readfds;
	struct sockaddr_in serv_addr, cli_addr;
	int clientaddrlen = 0;

	SavedBuffer = new Pixel[BufferSize]();
	NetworkBuffer = new Pixel[BufferSize]();
	printf("Buffers created\n");

	sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd < 0)
		error("ERROR opening socket");

	bzero((char*)&serv_addr, sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(PORT);


	if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
		error("ERROR on binding");


	listen(sockfd, 5);
	printf("Server listening...\n");

	FD_ZERO(&fds);
	FD_SET(sockfd, &fds);
	FD_SET(exit_pipe[0], &fds);

	while (!quitting) {

		readfds = fds;
		rc = select(FD_SETSIZE, &readfds, NULL, NULL, NULL);

		if (rc == -1) {
			perror("Select");
			break;
		}

		for (int i = 0; i < FD_SETSIZE; i++) {
			if (FD_ISSET(i, &readfds)) {
				if (i == sockfd) {
					if (numsocks < maxsocks) {
						int cli = accept(sockfd, (struct sockaddr*)&cli_addr, (socklen_t*)&clientaddrlen);
						if (cli == -1) {
							printf("Error while accepting");
						}
						else {
							initiate_client(cli);
							FD_SET(cli, &fds);
							clients.push_back(cli);
							numsocks++;
							char ip[30];
							inet_ntop(AF_INET, &(cli_addr.sin_addr), ip, 30);
							printf("Client connected from: %s\n", ip);
						}
					}
					else {
						printf("Max clients reached.\n");
					}
				}
				else {

					int in = recv(i, NetworkBuffer, BufferSize * sizeof(Pixel), 0);

					if (in == 0 || in == -1)
					{
						clients.remove(i);
						numsocks--;
						FD_ZERO(&fds);
						FD_SET(sockfd, &fds);
						FD_SET(exit_pipe[0], &fds);
						for (auto& cli : clients) FD_SET(cli, &fds);
						printf("Client disconnected, remaining clients: %d\n", (int)clients.size());
					}
					else {
						int count = in / sizeof(Pixel);
						printf("Pixels saved: %d\n", count);

						for (int c = 0; c < count; ++c) {
							Pixel& p = NetworkBuffer[c];
							int index = size_x * p.y + p.x;
							SavedBuffer[index] = p;
						}

						for (auto& c : clients) {
							if (c == i) continue;
							send(c, NetworkBuffer, in, 0);
						}
					}
				}
			}
		}
	}

	printf("Server exiting!\n");
	inputter.join();

	for (auto& c : clients) close(c);
	close(sockfd);

	delete[] NetworkBuffer;
	delete[] SavedBuffer;

	return 0;
}