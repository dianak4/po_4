#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>

using namespace std;

#pragma comment(lib, "Ws2_32.lib")


bool sendMessage(SOCKET sock, const vector<char>& message) {
	uint32_t length = htonl(message.size());
	if (send(sock, (char*)&length, sizeof(length), 0) != sizeof(length)) return false; // відіслати довжину
	if (send(sock, message.data(), message.size(), 0) != message.size()) return false; // відіслати дані
	return true;
}

bool recvAll(SOCKET sock, char* buffer, int length) { // отримання даних
	int totalReceived = 0;
	while (totalReceived < length) { 
		int bytes = recv(sock, buffer + totalReceived, length - totalReceived, 0);
		if (bytes <= 0) return false;
		totalReceived += bytes;
	}
	return true;
}

bool recvMessage(SOCKET sock, vector<char>& message) { // отримання повідомлення
	uint32_t length;
	if (!recvAll(sock, (char*)&length, sizeof(length))) return false;
	length = ntohl(length);
	message.resize(length);
	return recvAll(sock, message.data(), length);
}

void fillMatrix(vector<vector<int>>& matrix) {
	srand(time(0));
	int n = matrix.size();
	for (int i = 0; i < n; ++i) {
		for (int j = 0; j < n; ++j) {
			matrix[i][j] = rand() % 100;
		}
	}
}

void sendMatrix(SOCKET sock, const vector<vector<int>>& matrix, int threads) {
	vector<char> buffer;
	buffer.insert(buffer.end(), "[DATA]", "[DATA]" + 6);

	int n = matrix.size();
	int n_net = htonl(n);
	int t_net = htonl(threads);

	// розмір матриці і кількість потоків у буфер
	buffer.insert(buffer.end(), (char*)&n_net, (char*)&n_net + sizeof(int));
	buffer.insert(buffer.end(), (char*)&t_net, (char*)&t_net + sizeof(int));

	for (int i = 0; i < n; ++i) {
		for (int j = 0; j < n; ++j) {
			int elem = htonl(matrix[i][j]);
			buffer.insert(buffer.end(), (char*)&elem, (char*)&elem + sizeof(int));
		}
	}

	sendMessage(sock, buffer);
}

int main() {
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	sockaddr_in serverAddr; // адрес сервера
	serverAddr.sin_family = AF_INET; 
	serverAddr.sin_port = htons(54000); // порт сервера
	inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr); 

	connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)); // підключення до сервера

	int size, threads;
	cout << "Enter matrix size: ";
	cin >> size;
	cout << "Enter number of threads: ";
	cin >> threads;

	vector<vector<int>> matrix(size, vector<int>(size));
	fillMatrix(matrix);

	cout << "Original Matrix:\n";
	for (auto& row : matrix) {
		for (auto val : row) {
			cout << val << "\t";
		}
		cout << "\n";
	}

	sendMatrix(sock, matrix, threads);

	vector<char> buffer;
	recvMessage(sock, buffer);
	cout << "Server: " << string(buffer.begin(), buffer.end()) << "\n"; // отримання відповіді від сервера

	sendMessage(sock, vector<char>({ '[','S','T','A','R','T',']' })); // команда на обчислення

	while (true) {
		sendMessage(sock, vector<char>({ '[','S','T','A','T','U','S',']' })); // запит статусу
		recvMessage(sock, buffer);

		if (string(buffer.begin(), buffer.begin() + 6) == "[DONE]") {
			int offset = 6;
			cout << "Result Matrix:\n";
			for (int i = 0; i < size; ++i) {
				for (int j = 0; j < size; ++j) {
					int elem;
					memcpy(&elem, buffer.data() + offset, sizeof(int));
					offset += sizeof(int);
					cout << ntohl(elem) << "\t";
				}
				cout << "\n";
			}
			break;
		}
		else if (string(buffer.begin(), buffer.end()) == "[BUSY]") { // сервер зайнятий
			cout << "Server is still calculating...\n";
			Sleep(1000);
		}
	}

	closesocket(sock);
	WSACleanup();
	return 0;
}
