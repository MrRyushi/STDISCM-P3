#include <iostream>
#include <fstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <thread>
#include <filesystem>
#include <unordered_set>
#include <sstream>
#include <mutex>
#include <windows.h>
#include <wincrypt.h>

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

    char buffer[4096];
    int bytesReceived;
    while ((bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0)) > 0) {
        file.write(buffer, bytesReceived);
    }

    cout << "Received: " << filepath << endl;
    file.close();
    closesocket(clientSocket);
}

// -- File Hash Calculation using Windows CryptoAPI --
string calculateFileHash(const string &filename) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    const DWORD hashByteLen = 32;  // SHA-256 produces 32 bytes
    BYTE hash[hashByteLen];
    DWORD hashSize = hashByteLen;
    
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        throw runtime_error("CryptAcquireContext failed");
    }
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        throw runtime_error("CryptCreateHash failed");
    }
    
    ifstream file(filename, ios::binary);
    if (!file) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        throw runtime_error("Cannot open file: " + filename);
    }
    
    const int bufSize = 4096;
    char buf[bufSize];
    // Read full buffers
    while (file.read(buf, bufSize)) {
        if (!CryptHashData(hHash, reinterpret_cast<BYTE*>(buf), static_cast<DWORD>(file.gcount()), 0)) {
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            throw runtime_error("CryptHashData failed");
        }
    }
    // Process any remaining bytes
    if (file.gcount() > 0) {
        if (!CryptHashData(hHash, reinterpret_cast<BYTE*>(buf), static_cast<DWORD>(file.gcount()), 0)) {
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            throw runtime_error("CryptHashData failed on final block");
        }
    }
    
    // Retrieve the computed hash
    if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashSize, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        throw runtime_error("CryptGetHashParam failed");
    }
    
    ostringstream oss;
    for (DWORD i = 0; i < hashSize; i++) {
        oss << hex << setw(2) << setfill('0') << (int)hash[i];
    }
    string result = oss.str();

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return result;
}

void populateHashList() {
    if (!fs::exists(SAVE_PATH)) {
        cout << "Directory " << SAVE_PATH << " does not exist." << endl;
        return;
    }
    for (const auto& entry : fs::directory_iterator(SAVE_PATH)) {
        if (entry.is_regular_file()) {
            try {
                string filePath = entry.path().string();
                string hash = calculateFileHash(filePath);
                {
                    lock_guard<mutex> lock(hashMutex);
                    receivedHashes.insert(hash);
                }
                cout << "Loaded hash for " << filePath << ": " << hash << endl;
            } catch (const exception& e) {
                cerr << "Error processing " << entry.path().string() << ": " << e.what() << endl;
            }
        }
    }
}

int main() {
    initializeWinsock();
    // Ensure the save folder exists
    fs::create_directories(SAVE_PATH);
    // Populate the hash list from the files already in the folder
    populateHashList();

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        cerr << "Socket creation failed" << endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "Bind failed" << endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, 5) == SOCKET_ERROR) {
        cerr << "Listen failed" << endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    cout << "Server listening on port " << SERVER_PORT << "..." << endl;

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            cerr << "Accept failed" << endl;
            continue;
        }
        thread(receiveFile, clientSocket).detach();
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
