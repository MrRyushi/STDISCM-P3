#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <queue>
#include <mutex>
#include <cstring>
#include <condition_variable>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#include <sstream>
#include <iomanip>
#include <set>

using namespace std;
namespace fs = std::filesystem;

// Global variables from config
unsigned int numProducerThreads = 0;
unsigned int numConsumerThreads = 0;
unsigned int maxQueueSize = 0;

// Server details
#define SERVER_IP "10.147.20.220"  // Replace with your server's IP address
#define SERVER_PORT 8080

queue<string> videoQueue;
mutex queueMutex;
condition_variable queueCV;
set<string> processedFiles; // Set to track added files

// -- Helper Functions for Config File  --

bool isConfigFileValid(ifstream &configFile){
    if (!configFile) {
        cout << "Error: Could not open the file!" << endl;
        return false;
    }
    return true;
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

// -- Producer Thread Function --

void producerThread(int producerId){
    string folderPath = "folder" + to_string(producerId);

    if (!fs::exists(folderPath) || !fs::is_directory(folderPath)) {
        cout << "Folder " << folderPath << " does not exist" << endl;
        return; // Exit if the folder does not exist
    }

    bool filesAdded = false;

    for (const auto& entry : fs::directory_iterator(folderPath)) {
        if (entry.is_regular_file()) {
            string videoFile = entry.path().string();

            unique_lock<mutex> lock(queueMutex);
            if (videoQueue.size() >= maxQueueSize) {
                cout << "Queue full. Dropping: " << videoFile << endl;
                return;
            }

            // Avoid adding duplicate files
            if (processedFiles.find(videoFile) == processedFiles.end()) {
                videoQueue.push(videoFile);
                processedFiles.insert(videoFile);
                cout << "Producer " << producerId << " added: " << videoFile << endl;
                filesAdded = true;
                lock.unlock();
                queueCV.notify_one();
            }
        }
    }

    if (!filesAdded) {
        cout << "No new files found in " << folderPath << endl;
    }

}

// -- File Hash Calculation using Windows CryptoAPI --
string calculateFileHash(const string &filename) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE hash[32];  // SHA-256 produces 32 bytes
    DWORD hashSize = sizeof(hash);
    string result;

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
        throw runtime_error("Cannot open file");
    }
    
    const int bufSize = 4096;
    char buf[bufSize];
    while (file.good()) {
        file.read(buf, bufSize);
        if (!CryptHashData(hHash, reinterpret_cast<BYTE*>(buf), static_cast<DWORD>(file.gcount()), 0)) {
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            throw runtime_error("CryptHashData failed");
        }
    }
    
    // Now retrieve the computed hash
    if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashSize, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        throw runtime_error("CryptGetHashParam failed");
    }
    
    ostringstream oss;
    for (DWORD i = 0; i < hashSize; i++) {
        oss << hex << setw(2) << setfill('0') << (int)hash[i];
    }
    result = oss.str();

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return result;
}

// -- sendFile Function with Hash Transfer --

void sendFile(const string& filename){
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed!" << endl;
        return;
    }
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        cerr << "Error: Could not create socket" << endl;
        return;
    }
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        cerr << "Error: Could not connect to server" << endl;
        closesocket(sockfd);
        return;
    }

    // Calculate file hash and send it first
    string fileHash;
    try {
        fileHash = calculateFileHash(filename);
    } catch (const exception& e) {
        cerr << "Hash calculation failed: " << e.what() << endl;
        closesocket(sockfd);
        return;
    }
    send(sockfd, fileHash.c_str(), fileHash.size(), 0);

    // Wait for the server's response regarding duplicate check
    char response[4] = {0};
    recv(sockfd, response, sizeof(response)-1, 0);
    if (strncmp(response, "DUP", 3) == 0) {
        cout << "Server indicated duplicate file. Aborting transfer for: " << filename << endl;
        closesocket(sockfd);
        WSACleanup();
        return;
    }

    // Send filename
    string filenameOnly = fs::path(filename).filename().string();
    send(sockfd, filenameOnly.c_str(), filenameOnly.size(), 0);
    char ack[3];
    recv(sockfd, ack, 3, 0);

    // Send file data
    ifstream file(filename, ios::binary);
    if (!file.is_open()) {
        cerr << "Error: Could not open file" << endl;
        closesocket(sockfd);
        return;
    }
    char buffer[4096];
    while (!file.eof()) {
        file.read(buffer, sizeof(buffer));
        int bytesSent, totalSent = 0;
        while (totalSent < file.gcount()) {
            bytesSent = send(sockfd, buffer + totalSent, file.gcount() - totalSent, 0);
            if (bytesSent < 0) {
                cerr << "Error sending file data" << endl;
                closesocket(sockfd);
                return;
            }
            totalSent += bytesSent;
        }
    }

    cout << "Uploaded: " << filename << endl;
    file.close();
    closesocket(sockfd);
    WSACleanup();
}

void sendVideos(){
    while (true) {
        string videoFile;
        {
            unique_lock<mutex> lock(queueMutex);
            queueCV.wait(lock, [] { return !videoQueue.empty(); });
            videoFile = videoQueue.front();
            videoQueue.pop();
            cout << "Uploading: " << videoFile << endl;
        }
        sendFile(videoFile);
        queueCV.notify_one();
    }
}

int main(){
    ifstream configFile("config.txt");
    if (!isConfigFileValid(configFile)) return 1;
    string line;
    while(getline(configFile, line)){
        if (line.find("p") != string::npos) {
            numProducerThreads = getValueFromLine(line, "p");
        } else if (line.find("c") != string::npos) {
            numConsumerThreads = getValueFromLine(line, "c");
        } else if (line.find("q") != string::npos) {
            maxQueueSize = getValueFromLine(line, "q");
        } 
    }
    configFile.close();

    vector<thread> producerThreads;
    for(int i = 0; i < numProducerThreads; i++){
        producerThreads.emplace_back(producerThread, i);
    }
    for(auto& t : producerThreads){
        t.join();
    }
    sendVideos();
    return 0;
}
