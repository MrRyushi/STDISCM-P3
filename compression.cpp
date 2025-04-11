#include <cstdlib>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

bool compressVideo(const std::string& inputFilePath) {
    // Create the output folder if it doesn't exist.
    fs::path outputFolder("compressed_videos");
    if (!fs::exists(outputFolder)) {
        fs::create_directory(outputFolder);
    }
    
    // Extract the filename from the input path.
    fs::path inputPath(inputFilePath);
    fs::path outputFilePath = outputFolder / inputPath.filename();
    
    // Build the FFmpeg command.
    // Example command: compress with H.264 codec using CRF 23.
    std::string command = "ffmpeg -y -i \"" + inputFilePath +
                          "\" -c:v libx264 -crf 23 \"" + outputFilePath.string() + "\"";
    
    std::cout << "Running command: " << command << std::endl;
    int ret = std::system(command.c_str());
    if (ret != 0) {
        std::cerr << "Compression failed with error code: " << ret << std::endl;
        return false;
    }
    return true;
}

// Using a fixed CRF (23) prioritizes constant perceived quality rather than a specific file size. 
// This method is simpler and often preferred when quality consistency is more important than achieving a predictable file size. 
// However, a drawback is that different videos can end up with sizes that vary based on their complexity e.g 22-second compressed video might be larger than its 22-second original counterpart.