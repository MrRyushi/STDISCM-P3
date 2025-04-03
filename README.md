# STDISCM-P3

## Overview

STDISCM-P3 is a simulation of a media upload service that leverages concurrent programming, file I/O, queueing, and network communication. This project demonstrates the producer-consumer model where separate machines run the producer and consumer processes. The system simulates a media upload scenario by reading video files, uploading them via network sockets, and providing a GUI-based preview and display for the uploaded content.

## Project Specifications

## Components

# 1. Producer

- **Function**: Reads video files from a designated folder.

- **Operation**: Uploads videos to the consumer service via network sockets.

- **Concurrency**: Supports multiple threads/instances (each thread reads from a separate folder).

# 2. Consumer

- **Function**: Accepts simultaneous media uploads.

- **Operation**:
- Saves the uploaded videos to a single folder.

- Provides a browser-based GUI that displays the list of uploaded videos.

- Previews the first 10 seconds of each video when hovered over with the - mouse.

- Plays the full video when clicked.

- **Networking**: Runs on a different machine than the producer and communicates via network sockets.

##Input Parameters
`p`: Number of producer threads/instances.

`c`: Number of consumer threads.

`q`: Maximum queue length.

Note: This project implements a leaky bucket design; any additional videos beyond the queue capacity will be dropped.

## Output

- GUI Display: The consumer’s GUI shows:

- The list of uploaded videos.

- A preview of 10 seconds for each video when hovered over.

- The full video playback upon clicking a video.

## Bonus Features Implemented:

- Duplicate Detection: Prevents uploading duplicate videos.

- Video Compression: Compresses videos after the uploading process to optimize performance.

### For CLI:

1. Ensure you have **GCC** installed. You can check if GCC is installed by typing `g++ --version` in your terminal. If not, install it using:
   - **Linux (Ubuntu/Debian)**: `sudo apt install g++`
   - **macOS**: `brew install gcc`
   - **Windows**: Download from [MinGW](https://sourceforge.net/projects/mingw/).
2. **Navigate to the path/variant** where the source file "P2.cpp" is stored.

## Compilation & Running

# Producer

In the terminal, type:

- `g++ -std=c++11 -o producer producer.cpp -pthread -lws2_32`
- `./producer`

# Consumer

In the terminal, type:

- `java Gui.java`

### For Visual Studio Code (VS Code):

1. Open the project folder in VS Code.
2. Press Ctrl + F5 to build and run the programs respectively.

## Usage Flow

# 1. Start the consumer

# 2. Start the producer

#### Note: Ensure MinGW is installed and configured in your system PATH for Windows.

### Troubleshooting

- GCC not found: If you encounter issues with GCC, make sure it is correctly installed and available in your system's PATH.
- MinGW issues: If you're using MinGW on Windows and encounter errors, ensure MinGW is properly installed and added to the system PATH.
