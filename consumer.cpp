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
#include <condition_variable>
#include <queue>
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <cstdlib>
#include "compression.cpp"

#pragma comment(lib, "ws2_32.lib") // Link Winsock library

#define SERVER_PORT 8080
#define SAVE_PATH "./received_videos/"

using namespace std;
namespace fs = std::filesystem;

// Global hash set and mutex to protect it
unordered_set<string> receivedHashes;
mutex hashMutex;

// Global queue and synchronization tools
queue<SOCKET> videoQueue;
mutex queueMutex;
condition_variable queueCondVar;
bool serverRunning = true;
bool queueFull = false;
unsigned int totalCurrentVideos = 0;

unsigned int numConsumerThreads = 0;
unsigned int maxQueueSize = 0;

void initializeWinsock() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed" << endl;
        exit(1);
    }
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

bool isNumValid(string value) {
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);
    if (value.empty()) {
        cerr << "Error: Empty input!" << endl;
        return false;
    }
    if (!all_of(value.begin(), value.end(), ::isdigit)) {
        cerr << "Error: Input is not a valid number!" << endl;
        return false;
    }
    try {
        int num = stoi(value);
        if (num <= 0) {
            cerr << "Error: Number must be greater than or equal to 0!" << endl;
            return false;
        }
    } catch (const exception& e) {
        cerr << "Error: Invalid number format!" << endl;
        return false;
    }
    return true;
}

unsigned int getValueFromLine(string line, string key){
    if(line.find(key) != string::npos){
        string value = line.substr(line.find('=') + 1);
        if(isNumValid(value)){
            return stoi(value);
        } else {
            return 1;
        }
    }
    return 0;
}


bool isConfigFileValid(ifstream &configFile){
    if (!configFile) {
        cout << "Error: Could not open the file!" << endl;
        return false;
    }
    return true;
}


void receiveFile(SOCKET clientSocket) {
    // --- Existing code to receive file hash ---
    char fileHash[65] = {0};
    int hashBytes = recv(clientSocket, fileHash, 64, 0);
    if (hashBytes <= 0) {
        cerr << "Error receiving file hash" << endl;
        closesocket(clientSocket);
        return;
    }
    fileHash[64] = '\0';
    {
        lock_guard<mutex> lock(hashMutex);
        if (receivedHashes.find(fileHash) != receivedHashes.end()) {
            send(clientSocket, "DUP", 3, 0);
            cout << "Duplicate file detected (hash: " << fileHash << ")." << endl;
            closesocket(clientSocket);
            return;
        } else {
            receivedHashes.insert(fileHash);
            send(clientSocket, "OK", 2, 0);
        }
    }

    // --- Existing code to receive filename ---
    char filename[256] = {0};
    recv(clientSocket, filename, sizeof(filename), 0);
    send(clientSocket, "ACK", 3, 0);
    string filepath = string(SAVE_PATH) + filename;

    // * New Code Block: Receive expected file size *
    char fileSizeBuffer[32] = {0};
    int sizeBytes = recv(clientSocket, fileSizeBuffer, sizeof(fileSizeBuffer) - 1, 0);
    if (sizeBytes <= 0) {
        cerr << "Error receiving file size" << endl;
        closesocket(clientSocket);
        return;
    }
    fileSizeBuffer[sizeBytes] = '\0'; // null terminate the string
    unsigned long long expectedFileSize = stoull(fileSizeBuffer);
    send(clientSocket, "SIZE_ACK", 8, 0);

    // --- Open the file for writing ---
    ofstream file(filepath, ios::binary);
    if (!file) {
        cerr << "Error creating file: " << filepath << endl;
        closesocket(clientSocket);
        return;
    }

    // --- Modified code: Receive file data with total byte tracking ---
    char buffer[4096];
    int bytesReceived = 0;
    unsigned long long totalReceived = 0;
    while ((bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0)) > 0) {
        file.write(buffer, bytesReceived);
        totalReceived += bytesReceived;
    }
    file.close();

    // --- New Check: Validate complete file reception ---
    if (totalReceived != expectedFileSize) {
        cerr << "Error: File incomplete. Expected " << expectedFileSize 
             << " bytes, but received " << totalReceived << " bytes." << endl;
        fs::remove(filepath); // Optionally delete the incomplete file
        closesocket(clientSocket);
        return;
    }

    cout << "Received file completely: " << filepath << " (" << totalReceived << " bytes)" << endl;

    // --- Existing code to call compression ---
    if (compressVideo(filepath)) {
        cout << "Video compressed successfully." << endl;
    } else {
        cerr << "Video compression failed." << endl;
    }

    queueCondVar.notify_all();
    closesocket(clientSocket);
 }

void workerThread(int workerId) {
    while (serverRunning) {
        SOCKET clientSocket;
        {
            unique_lock<mutex> lock(queueMutex);
            queueCondVar.wait(lock, [] { return !videoQueue.empty() || !serverRunning; });
            
            if (!serverRunning) break;

            clientSocket = videoQueue.front();
            videoQueue.pop();
        }
        
        cout << "Worker " << workerId << " processing video" << endl;
        receiveFile(clientSocket);
    }
}


int main() {
    ifstream configFile("config.txt");
    if (!configFile) {
        cerr << "Error: Could not open config file!" << endl;
        return 1;
    }
    string line;
    while (getline(configFile, line)) {
        if (line.find("c") != string::npos) {
            numConsumerThreads = stoi(line.substr(line.find('=') + 1));
        } else if (line.find("q") != string::npos) {
            maxQueueSize = stoi(line.substr(line.find('=') + 1));
        }
    }
    configFile.close();

    //system("start python gui_server.py");
    //system("start cmd /c node gui_server.js");
    //system("javac MyHttpServer.java");
    //system("java -cp . MyHttpServer");


    //cout << "Starting server..." << endl;
    initializeWinsock();
    fs::create_directories(SAVE_PATH);

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

    vector<thread> workers;
    for (unsigned int i = 0; i < numConsumerThreads; i++) {
        workers.emplace_back(workerThread, i);
    }

    while (serverRunning) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            cerr << "Accept failed" << endl;
            continue;
        }
        // Lock the queueMutex to safely access and modify the queue
        {
            unique_lock<mutex> lock(queueMutex);
            // Check if the queue is full
            cout << "Total current videos: " << totalCurrentVideos << endl;
            if (totalCurrentVideos >= maxQueueSize) {
                cout << "Queue is full, rejecting incoming video..." << endl;
                // Notify producer that the queue is full
                send(clientSocket, "FULL", 4, 0);  
                closesocket(clientSocket);  // Reject the connection
                continue;
            }

            // Add to the queue if there's space
            videoQueue.push(clientSocket);
            totalCurrentVideos++;
        }

        queueCondVar.notify_all(); // Wake up any waiting worker threads

        cout << videoQueue.size() << " videos in queue" << endl;
        
    }

    serverRunning = false;

    for (auto &worker : workers) {
        worker.join();
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}