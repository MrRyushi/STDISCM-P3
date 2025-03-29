#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <random>
#include <queue>
#include <mutex>
#include <cstring>
#include <condition_variable>
#include <winsock2.h>
#include <ws2tcpip.h>


using namespace std;
namespace fs = std::filesystem;

// user inputs from config file
unsigned int numProducerThreads = 0;
unsigned int numConsumerThreads = 0;
unsigned int maxQueueSize = 0;

// Server details
#define SERVER_IP "192.168.56.1" 
#define SERVER_PORT 8080

// Constants
queue<string> videoQueue;
mutex queueMutex;
condition_variable queueCV;

bool isConfigFileValid(std::ifstream &configFile){
    // Check if file was opened successfully
    if (!configFile) {
        std::cout << "Error: Could not open the file!" << std::endl;
        return false;
    }
    return true;
}

bool isNumValid(std::string value) {
    // Trim leading/trailing spaces
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);

    // Check if the value is empty
    if (value.empty()) {
        std::cerr << "Error: Empty input!" << std::endl;
        return false;
    }

    // Check if value consists only of digits
    if (!std::all_of(value.begin(), value.end(), ::isdigit)) {
        std::cerr << "Error: Input is not a valid number!" << std::endl;
        return false;
    }

    try {
        int num = std::stoi(value);
        if (num <= 0) {
            std::cerr << "Error: Number must be greater than or equal to 0!" << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: Invalid number format!" << std::endl;
        return false;
    }

    return true;
}

unsigned int getValueFromLine(std::string line, std::string key){
    if(line.find(key) != std::string::npos){
        std::string value = line.substr(line.find('=') + 1);
        if(isNumValid(value)){
            return std::stoi(value);
        } else {
            return 1;
        }
    }
    return 0;
}

void producerThread(int producerId){
    string folderPath = "folder" + to_string(producerId);
    while(true){
        // check if the folder exist
        if (!fs::exists(folderPath) || !fs::is_directory(folderPath)){
            cout << "Folder " << folderPath << " does not exist" << endl;
            this_thread::sleep_for(chrono::seconds(1));
            continue;
        }

        // Iterate over files in the folder
        for (const auto& entry : fs::directory_iterator(folderPath)) {
            if (entry.is_regular_file()) {
                string videoFile = entry.path().string();

                // Lock the queue
                unique_lock<mutex> lock(queueMutex);
                if (videoQueue.size() >= maxQueueSize) {
                    cout << "Queue full. Dropping: " << videoFile << endl;
                    return; 
                }

                // Add the video file to the queue
                videoQueue.push(videoFile);
                cout << "Producer " << producerId << " added: " << videoFile << endl;

                // Unlock and notify consumers
                lock.unlock();
                queueCV.notify_one();
            }
        }
    }
    
}

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

    ifstream file(filename, ios::binary);
    if (!file.is_open()) {
        cerr << "Error: Could not open file" << endl;
        closesocket(sockfd);
        return;
    }

    // send filename first
    string filenameOnly = fs::path(filename).filename().string();
    send(sockfd, filenameOnly.c_str(), filenameOnly.size(), 0);
    char ack[3];
    recv(sockfd, ack, 3, 0);

    // send file data
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
    
    WSACleanup(); // Cleanup Winsock before exiting
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
    // exit the program if the config file is not valid
    if (!isConfigFileValid(configFile)) return 1;

    // validate the input from config file
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