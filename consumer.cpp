#include <iostream>
#include <fstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <thread>
#include <filesystem>
#include <unordered_set>
#include <mutex>

#pragma comment(lib, "ws2_32.lib") // Link Winsock library

#define SERVER_PORT 8080
#define SAVE_PATH "./received_videos/"

using namespace std;
namespace fs = std::filesystem;

// Global hash set and mutex to protect it
unordered_set<string> receivedHashes;
mutex hashMutex;

void initializeWinsock() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed" << endl;
        exit(1);
    }
}

void receiveFile(SOCKET clientSocket) {
    // Receive file hash first (expecting a 64-character SHA-256 hex string)
    char fileHash[65] = {0};
    int hashBytes = recv(clientSocket, fileHash, 64, 0);
    if (hashBytes <= 0) {
        cerr << "Error receiving file hash" << endl;
        closesocket(clientSocket);
        return;
    }
    fileHash[64] = '\0'; // Ensure null termination

    {
        lock_guard<mutex> lock(hashMutex);
        if (receivedHashes.find(fileHash) != receivedHashes.end()) {
            // Duplicate file detected; notify client and abort transfer.
            send(clientSocket, "DUP", 3, 0);
            cout << "Duplicate file detected (hash: " << fileHash << ")." << endl;
            closesocket(clientSocket);
            return;
        } else {
            // New file: add hash and allow transfer.
            receivedHashes.insert(fileHash);
            send(clientSocket, "OK", 2, 0);
        }
    }

    // Now receive filename
    char filename[256] = {0};
    recv(clientSocket, filename, sizeof(filename), 0);
    send(clientSocket, "ACK", 3, 0); // Acknowledge receipt of filename

    string filepath = string(SAVE_PATH) + filename;
    ofstream file(filepath, ios::binary);
    if (!file) {
        cerr << "Error creating file: " << filepath << endl;
        closesocket(clientSocket);
        return;
    }

    //cout << "Receiving file: " << filename << endl;

    char buffer[4096];
    int bytesReceived;
    while ((bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0)) > 0) {
        //cout << "Receiving data for: " << filename << endl;
        file.write(buffer, bytesReceived);
    }

    cout << "Received: " << filepath << endl;
    file.close();
    closesocket(clientSocket);
}

int main() {
    initializeWinsock();
    fs::create_directories(SAVE_PATH); // Ensure save folder exists

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        cerr << "Socket creation failed\n";
        WSACleanup();
        return 1;
    }

    struct sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "Bind failed\n";
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, 5) == SOCKET_ERROR) {
        cerr << "Listen failed\n";
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    cout << "Server listening on port " << SERVER_PORT << "...\n";

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            cerr << "Accept failed\n";
            continue;
        }
        thread(receiveFile, clientSocket).detach();
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
