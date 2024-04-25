#include <iostream>
#include <thread>

#include <WinSock2.h>
#include <Ws2tcpip.h>

class Game
{
public:

	SOCKET GameSock;

	addrinfo* dest;
	sockaddr_in local;

	unsigned char selected;
	unsigned char size;
	int color;
};

struct Pixel
{
	short x;
	short y;
	short type;
	short color;

	Pixel()
	{

	}

	Pixel(int x, int y, int c, int co) : x(x), y(y), type(c), color(co) {}
};

constexpr const char* srcIP = "127.0.0.1";
constexpr const char* destIP = "192.168.0.2";
constexpr const char* port = "3900";

constexpr int size_x = 100;
constexpr int size_y = 100;


int win_x = 100;
int win_y = 100;

int BufferSize = size_x * size_y * 6;

char* FinalBuffer = nullptr;
char* OverlayBuffer = nullptr;

int NetworkSize = 0;
Pixel* NetworkBuffer = nullptr;

const unsigned char wall = 219;
const unsigned char wall2 = 178;
const unsigned char wall3 = 177;
const unsigned char wall4 = 176;
const unsigned char shade = 111;
const unsigned char empty = 255;

constexpr const char* RED = "\x1B[31m";
constexpr const char* GRN = "\x1B[32m";
constexpr const char* YEL = "\x1B[33m";
constexpr const char* BLU = "\x1B[34m";
constexpr const char* MAG = "\x1B[35m";
constexpr const char* CYN = "\x1B[36m";
constexpr const char* WHT = "\x1B[37m";

const char* get_color(int c)
{
	switch (c)
	{
	case 0: return WHT;
	case 1: return RED;
	case 2: return GRN;
	case 3: return YEL;
	case 4: return BLU;
	case 5: return MAG;
	case 6: return CYN;
	default:
		return WHT;
	}
}

HANDLE hOut;
HANDLE hIn;

bool quitting = false;

Game gl;

int net_init(Game& game)
{
	WSADATA wsa_data;
	if (WSAStartup(MAKEWORD(1, 1), &wsa_data) != 0) {
		WSACleanup();
		return 1;
	}
	game.local.sin_family = AF_INET;
	inet_pton(AF_INET, srcIP, &game.local.sin_addr.s_addr);
	game.local.sin_port = htons(0);

	struct addrinfo hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	int iResult = getaddrinfo(destIP, port, &hints, &game.dest);
	if (iResult != 0) {
		printf("getaddrinfo failed: %d\n", iResult);
		WSACleanup();
		return 1;
	}

	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) return 2;
	int flag = 1;
	//setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));
	//int value = bind(s, (sockaddr*)&game.local, sizeof(game.local));

	int value = connect(s, game.dest->ai_addr, game.dest->ai_addrlen);

	//u_long iMode = 1;
	//ioctlsocket(s, FIONBIO, &iMode);

	game.GameSock = s;
	return value;
}

int net_close(Game& game)
{
	closesocket(game.GameSock);
	return WSACleanup();
}

int setup_console()
{
	HWND consoleWindow = GetConsoleWindow();
	SetWindowLong(consoleWindow, GWL_STYLE, GetWindowLong(consoleWindow, GWL_STYLE) & ~WS_MAXIMIZEBOX & ~WS_SIZEBOX);

	hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	hIn = GetStdHandle(STD_INPUT_HANDLE);

	CONSOLE_FONT_INFOEX fontInfo;
	fontInfo.cbSize = sizeof(CONSOLE_FONT_INFOEX);
	GetCurrentConsoleFontEx(hOut, false, &fontInfo);
	COORD font = { 8, 8 };
	fontInfo.dwFontSize = font;
	fontInfo.FontFamily = FF_ROMAN;
	fontInfo.FontWeight = FW_BOLD;
	wcscpy_s(fontInfo.FaceName, L"Terminal");//SimSun-ExtB
	SetCurrentConsoleFontEx(hOut, false, &fontInfo);

	CONSOLE_SCREEN_BUFFER_INFOEX SBInfo;

	SBInfo.cbSize = sizeof(CONSOLE_SCREEN_BUFFER_INFOEX);
	GetConsoleScreenBufferInfoEx(hOut, &SBInfo);
	SBInfo.dwSize.X = size_x;
	SBInfo.dwSize.Y = size_y;
	SBInfo.srWindow.Bottom = size_y;
	SBInfo.srWindow.Right = size_x - 1;
	SBInfo.srWindow.Top = 0;
	SBInfo.srWindow.Left = 0;

	SetConsoleScreenBufferInfoEx(hOut, &SBInfo);

	CONSOLE_CURSOR_INFO     cursorInfo;

	GetConsoleCursorInfo(hOut, &cursorInfo);
	cursorInfo.bVisible = false;
	SetConsoleCursorInfo(hOut, &cursorInfo);

	DWORD prev_mode;
	GetConsoleMode(hIn, &prev_mode);
	SetConsoleMode(hIn, ENABLE_EXTENDED_FLAGS |
		(prev_mode & ~(ENABLE_QUICK_EDIT_MODE | ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_PROCESSED_INPUT)) | ENABLE_MOUSE_INPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
	GetConsoleMode(hOut, &prev_mode);
	SetConsoleMode(hOut, prev_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);

	GetConsoleScreenBufferInfoEx(hOut, &SBInfo);

	win_x = SBInfo.dwSize.X;
	win_y = SBInfo.dwSize.Y;

	BufferSize = win_x * win_y * 6;

	return 0;
}

void write_pixel(int x, int y, unsigned char c, int color = 0, bool replicated = true)
{
	int index = size_x * y + x;
	if (index * 6 < BufferSize) {
		strncpy_s(&OverlayBuffer[index * 6], 6, get_color(color), 6);
		OverlayBuffer[index * 6 + 5] = c;
		if (replicated) {
			bool found = false;
			for (int i = 0; i < NetworkSize; ++i) {
				if (NetworkBuffer[i].x == x && NetworkBuffer[i].y == y) {
					NetworkBuffer[i].color = color;
					NetworkBuffer[i].type = c;
					found = true;
					break;
				}
			}
			if (!found) {
				NetworkBuffer[NetworkSize].type = c;
				NetworkBuffer[NetworkSize].color = color;
				NetworkBuffer[NetworkSize].x = x;
				NetworkBuffer[NetworkSize].y = y;
				NetworkSize++;
			}
		}
	}
}

void write_text(int x, int y, const char* text, int color = 0, bool replicated = true)
{
	int dx = x;
	int dy = y;
	const char* ptr = text;
	while (*ptr != '\0') {
		write_pixel(dx, dy, *ptr, color, replicated);
		ptr++;
		dx++;
	}
	
}

void write_text_centered(int x, int y, const char* text, int color = 0, bool replicated = true)
{
	int dx = x - strlen(text) / 2;
	int dy = y;
	const char* ptr = text;
	while (*ptr != '\0') {
		write_pixel(dx, dy, *ptr, color, replicated);
		ptr++;
		dx++;
	}

}

void input()
{
	DWORD cNumRead, i;
	INPUT_RECORD irInBuf[128];

	while (!quitting) {
		ReadConsoleInput(hIn, irInBuf, 128, &cNumRead);
		for (i = 0; i < cNumRead; i++)
		{
			switch (irInBuf[i].EventType)
			{
			case KEY_EVENT:
				switch (irInBuf->Event.KeyEvent.wVirtualKeyCode)
				{
				case 0x31: gl.selected = wall; break;
				case 0x32: gl.selected = wall3; break;
				case 0x33: gl.selected = wall4; break;
				case 0x34: gl.selected = shade; break;

				case VK_TAB: {
					if (irInBuf->Event.KeyEvent.bKeyDown)
						if (++gl.color > 6) gl.color = 0;
				} break;

				case VK_ESCAPE: quitting = true; break;
				default:
					break;
				}
				break;
			case MOUSE_EVENT:
				if ((irInBuf->Event.MouseEvent.dwButtonState == 0x001 || irInBuf->Event.MouseEvent.dwButtonState == 0x002)) {
					unsigned char p = irInBuf->Event.MouseEvent.dwButtonState == 0x002 ? empty : gl.selected;
					int halfSize = gl.size / 2;
					int sx = irInBuf->Event.MouseEvent.dwMousePosition.X;
					int sy = irInBuf->Event.MouseEvent.dwMousePosition.Y;
					for (int y = 0; y < gl.size; ++y) {
						for (int x = 0; x < gl.size; ++x) {
							int dx = sx + x - halfSize;
							int dy = sy + y - halfSize;
							if (dx >= 0 && dx < win_x && dy >= 0 && dy < win_y)
								write_pixel(dx, dy, p, gl.color);
						}
					}
				}
				else if (irInBuf->Event.MouseEvent.dwEventFlags & MOUSE_WHEELED) {
					int status = (short)HIWORD(irInBuf->Event.MouseEvent.dwButtonState);
					gl.size += status >= 0 ? 1 : -1;
					if (gl.size > 8) gl.size = 8;
					else if (gl.size < 1) gl.size = 1;
				}
				break;
			default:
				break;
			}
		}
	}
}

void replicate()
{
	if (NetworkSize == 0) return;

	send(gl.GameSock, (char*)NetworkBuffer, sizeof(Pixel) * NetworkSize, 0);

	NetworkSize = 0;
}

void receive()
{
	Pixel* ReceiveBuffer = new Pixel[win_x * win_y]();
	//Pixel ReceiveBuffer;
	int size = 0;
	while (!quitting) {
		size = 0;
		do { size += recv(gl.GameSock, (char*)ReceiveBuffer + size, sizeof(Pixel) * win_x * win_y, 0); }
		while (size % sizeof(Pixel) != 0 && !(size <= 0));
		//size = recv(gl.GameSock, (char*)&ReceiveBuffer, sizeof(Pixel), 0);

		if (size == 0 || size == -1) {
			quitting = true;
			continue;
		}

		if (size < (int)sizeof(Pixel))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			continue;
		}

		int count = size / sizeof(Pixel);

		for (int i = 0; i < count; i++) {
			Pixel& p = ReceiveBuffer[i];
			write_pixel(p.x, p.y, p.type, p.color, false);
		}
	}
	delete[] ReceiveBuffer;
}

void render_frame()
{
	SetConsoleCursorPosition(hOut, COORD());
	for (int i = 0; i < BufferSize; i += 6)
	{
		if (OverlayBuffer[i])
			for (int f = 0; f < 6; f++)
				FinalBuffer[i + f] = OverlayBuffer[i + f];
		else
			FinalBuffer[i + 5] = empty;
	}
	std::cout.write(FinalBuffer, BufferSize - 1);
}

int main()
{
	printf("\x1B[0m");

	setup_console();

	FinalBuffer = new char[BufferSize + 1]();
	OverlayBuffer = new char[BufferSize + 1]();
	NetworkBuffer = new Pixel[win_x * win_y]();
	memset(FinalBuffer, 0, BufferSize + 1);
	for (int i = 0; i < BufferSize; i += 6) {
		strncpy_s(&FinalBuffer[i], 6, WHT, 6);
		FinalBuffer[i + 5] = empty;
	}
	memset(OverlayBuffer, 0, BufferSize + 1);

	gl.selected = wall;
	gl.size = 1;
	gl.color = 0;
	char sizeText[3];

	write_text_centered(50, 50, "Connecting...", 0, false);
	render_frame();

	if (net_init(gl)) {
		write_text_centered(50, 50, "Unable to connect to server", 0, false);
	}
	else {

		memset(OverlayBuffer, 0, BufferSize + 1);

		std::thread input_thread(&input);
		std::thread receiver(&receive);

		while (!quitting)
		{
			replicate();

			sprintf_s(sizeText, "%d", gl.size);
			for (int y = 0; y < 9; ++y) {
				for (int x = 0; x < 15; ++x) {
					write_pixel(x, y, empty, 0, false);
				}
			}
			write_text(2, 2, "Selected: ", 0, false); write_pixel(13, 2, gl.selected, 0, false);
			write_text(2, 4, "Size: ", 0, false); write_text(13, 4, sizeText, 0, false);
			write_text(2, 6, "Color: ", 0, false); write_pixel(13, 6, wall, gl.color, false);

			render_frame();

			Sleep(10);
		}

		input_thread.join();
		receiver.join();

		SetConsoleCursorPosition(hOut, COORD());
		memset(OverlayBuffer, 0, BufferSize + 1);
		for (int i = 0; i < BufferSize; i += 6) {
			strncpy_s(&FinalBuffer[i], 6, WHT, 6);
			FinalBuffer[i + 5] = empty;
		}

		write_text_centered(50, 50, "Disconnected from server");
	}
	
	net_close(gl);
	render_frame();

	std::this_thread::sleep_for(std::chrono::seconds(5));

	delete[] FinalBuffer;
	delete[] OverlayBuffer;
	delete[] NetworkBuffer;

	printf("\x1B[0m");

	return 0;
}