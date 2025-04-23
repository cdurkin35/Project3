#pragma once

#include <cstdint>
#include <string>
#include <vector>

// WAD file header
struct Header {
    char magic[4];
    uint32_t count;
    uint32_t offset;
};

// Descriptor
struct Descriptor {
    uint32_t offset;
    uint32_t length;
    char name[8];
};

// Directory node
struct Node {
    std::string name; // File/Directory name
    bool isDir; // Directory indicator
    uint32_t offset; // Lump offset in bytes
    uint32_t length; // Lump size

    // Tree structure
    Node* parent; // Parent node
    std::vector<Node*> children; // Child nodes

    size_t descIndex; // Index of matching descriptor
};

class Wad
{
public:
    ~Wad(); // Destructor
    static Wad* loadWad(const std::string& path); // Dynamically create a WAD object
    std::string getMagic(); // Get magic data
    bool isContent(const std::string& path); // Checks if path represents data
    bool isDirectory(const std::string& path); // Checks if path represents a directory
    int getSize(const std::string& path); // Returns size of content if content

    // Copy lump data into buffer, returns bytes copied
    int getContents(const std::string& path, char* buffer, int length, int offset = 0);
    // Fill vector with immediate children of directory, returns count
    int getDirectory(const std::string& path, std::vector<std::string>* directory);

    void createDirectory(const std::string& path); // Create a new namespace directory at path
    void createFile(const std::string& path); // Create an empty lump (file) at path

    // Write buffer to lump, returns bytes written
    int writeToFile(const std::string& path, const char* buffer, int length, int offset = 0);

private:
    Header header; // File header
    std::vector<char> fileData; // Raw data from file
    std::vector<Descriptor> descriptors; // Hold descriptors in order

    Node* resolve(const std::string& path); // Convert path to a Node pointer

    Node* root; // Pointer to root directory node
};
