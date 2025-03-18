#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <algorithm>
#include <vector>
#include <random>
#include <mutex>

using namespace std;

// user inputs from config file
int producerThreads = 0;
int consumerThreads = 0;
int queueLength = 0;

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

int getValueFromLine(std::string line, std::string key){
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

int main(){
    ifstream configFile("config.txt");
    // exit the program if the config file is not valid
    if (!isConfigFileValid(configFile)) return 1;

    // validate the input from config file
    string line;
    while(getline(configFile, line)){
        if (line.find("p") != string::npos) {
            producerThreads = getValueFromLine(line, "p");
        } else if (line.find("c") != string::npos) {
            consumerThreads = getValueFromLine(line, "c");
        } else if (line.find("q") != string::npos) {
            queueLength = getValueFromLine(line, "q");
        } 
    }
    configFile.close();
}