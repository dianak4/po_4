#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>

using namespace std;

#pragma comment(lib, "Ws2_32.lib")

mutex coutMutex;

struct ClientSession { // структура для зберігання інформації про сесію клієнта
    SOCKET clientSocket;
    vector<vector<int>> matrix;
    int matrixSize;
    int threadCount;
    bool calculating = false; // чи виконується обчислення
    bool done = false;
};

bool recvAll(SOCKET sock, char* buffer, int length) { // функція для отримання всіх байтів
    int totalReceived = 0;
    while (totalReceived < length) {
        int bytes = recv(sock, buffer + totalReceived, length - totalReceived, 0); // отримуємо дані від сокету
        if (bytes <= 0) return false; // false, якщо не вдалося отримати всі байти (наприклад, з'єднання перерване)
        totalReceived += bytes;
    }
    return true;
}

bool recvMessage(SOCKET sock, vector<char>& message) {
	uint32_t length; // довжина повідомлення. uint32_t - ЗАВЖДИ 4 байти
    if (!recvAll(sock, (char*)&length, sizeof(length))) return false;
    length = ntohl(length); //перетворення байт в біг ендіан
    message.resize(length); // динамічне розширення
    return recvAll(sock, message.data(), length);
}

bool sendMessage(SOCKET sock, const vector<char>& message) {
    uint32_t length = htonl(message.size()); //перетворення в біг ендіан
    if (send(sock, (char*)&length, sizeof(length), 0) != sizeof(length)) return false; // відправляємо довжину  
    if (send(sock, message.data(), message.size(), 0) != message.size()) return false; // відправляємо саме повідомлення
    return true;
}

void calculateMatrix(ClientSession* session) { // обчислення матриці багатопоточно
    vector<thread> workers;
    //розмір, посилання на матрицю, к-ть потоків
    int n = session->matrixSize;
    auto& mat = session->matrix;
    int threads = session->threadCount;

    auto worker = [&](int start, int step) {
        for (int i = start; i < n; i += step) {
            int sum = 0;
            for (int j = 0; j < n; ++j) {
                sum += mat[i][j];
            }
            mat[i][i] = sum;
        }
        };

    for (int i = 0; i < threads; ++i) {
        workers.emplace_back(worker, i, threads);
    }
    for (auto& w : workers) {
        w.join();
    }
    session->done = true;
    session->calculating = false;
}

void handleClient(SOCKET clientSocket) {
    ClientSession session{ clientSocket }; // створюємо сесію для клієнта

    while (true) {
        vector<char> buffer;
        if (!recvMessage(clientSocket, buffer)) break;

        string command(buffer.data(), buffer.size()); // перетворюємо буфер в рядок

        if (command.rfind("[DATA]", 0) == 0) { // чи рядок починається з [DATA]
            int offset = 6; // пропускаємо [DATA]
            int n, threads;

            memcpy(&n, buffer.data() + offset, sizeof(int)); // копіюємо розмір
            offset += sizeof(int);
            memcpy(&threads, buffer.data() + offset, sizeof(int)); // копіюємо кількість потоків
            offset += sizeof(int);

            // перетворюємо в мережевий порядок байтів
            n = ntohl(n);
            threads = ntohl(threads);

            session.matrixSize = n;
            session.threadCount = threads;
            session.matrix.resize(n, vector<int>(n));

            for (int i = 0; i < n; ++i) { // заповнюємо матрицю даними з буфера
                for (int j = 0; j < n; ++j) {
                    int elem;
                    memcpy(&elem, buffer.data() + offset, sizeof(int));
                    offset += sizeof(int);
                    session.matrix[i][j] = ntohl(elem);
                }
            }
            lock_guard<mutex> lock(coutMutex);
            cout << "Received matrix " << n << "x" << n << " threads: " << threads << "\n";

            sendMessage(clientSocket, vector<char>({ '[', 'O', 'K', ']' })); // повідомлення, що дані прийнято
        }
        else if (command.rfind("[START]", 0) == 0) { // чи рядок починається з [START] - тобто виконання обчислення
            if (!session.calculating && !session.done) {
                session.calculating = true;
                thread(calculateMatrix, &session).detach(); // новий потік для обчислення матриці
                sendMessage(clientSocket, vector<char>({ '[','C','A','L','C','_','S','T','A','R','T','E','D',']' }));
            }
        }
        else if (command.rfind("[STATUS]", 0) == 0) { // чи рядок починається з [STATUS] - запит на статус
            if (session.done) {
                vector<char> sendBuf; // буфер відповіді
                sendBuf.insert(sendBuf.end(), "[DONE]", "[DONE]" + 6);
                int n = session.matrixSize;
                for (int i = 0; i < n; ++i) { // додаємо матрицю в буфер
                    for (int j = 0; j < n; ++j) {
                        int elem = htonl(session.matrix[i][j]);
                        char* p = (char*)&elem;
                        sendBuf.insert(sendBuf.end(), p, p + sizeof(int));
                    }
                }
                sendMessage(clientSocket, sendBuf);
            }
            else if (session.calculating) { // обчислення триває
                sendMessage(clientSocket, vector<char>({ '[','B','U','S','Y',']' }));
            }
            else { // обчислення не почалося
                sendMessage(clientSocket, vector<char>({ '[','N','O','T','_','S','T','A','R','T','E','D',']' }));
            }
        }
    }

    closesocket(clientSocket);
    lock_guard<mutex> lock(coutMutex);
    cout << "Client disconnected.\n";
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData); // запускає бібліотеку WinSock

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); //слухає TCP-з'єднання на порту 54000

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(54000); // перетворює номер порту в мережевий порядок байтів
    serverAddr.sin_addr.s_addr = INADDR_ANY; // приймає з'єднання з будь-якої адреси

    bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)); // прив'язує сокет до адреси
    listen(listenSocket, SOMAXCONN); // починає слухати вхідні з'єднання

    cout << "Server started on port 54000\n";

    while (true) { //  кожен новий клієнт обробляється в окремому потоці.
        SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
        thread(handleClient, clientSocket).detach();
    }

    WSACleanup();
    return 0;
}
