#include "Wad.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stack>

/* Helper functions */

// For use with namespace markers
bool endsWith(const std::string& str, const std::string& suffix)
{
    return str.size() >= suffix.size()
        && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Normalize path
std::string norm(const std::string& p)
{
    if (p.empty()) return p;                  // Leave empty
    std::string s = p;
    while (s.size() > 1 && s.back() == '/')   // STrip trailing slashes
        s.pop_back();
    return s;
}

// Wad implementation

// Destructor
Wad::~Wad()
{
    if (!root)
        return;

    std::vector<Node*> stack;
    stack.push_back(root);

    while (!stack.empty()) {
        Node* n = stack.back();
        stack.pop_back();

        for (Node* ch : n->children)
            stack.push_back(ch);

        delete n;
    }
}

Wad* Wad::loadWad(const std::string& path)
{
    // Construct new default WAD
    Wad* wad = new Wad();

    // Open file
    std::ifstream file(path, std::ios::binary | std::ios::in | std::ios::ate);

    // Make sure file opens correctly
    if (!file)
        return nullptr;

    // Get size of file and resize vector
    size_t fsize = file.tellg();
    wad->fileData.resize(fsize);

    // Go to beginning of file and read to vector
    file.seekg(0);
    file.read(wad->fileData.data(), fsize);

    // Copy header information
    memcpy(&wad->header, wad->fileData.data(), sizeof(Header));

    // Copy descriptors from file into vector
    wad->descriptors.resize(wad->header.count);
    char* descStart = wad->fileData.data() + wad->header.offset;
    memcpy(wad->descriptors.data(), descStart, wad->header.count * sizeof(Descriptor));

    // Build directory tree
    wad->root = new Node { "/", true, 0, 0, nullptr, {}, 0 };
    std::stack<Node*> dirStack;
    dirStack.push(wad->root);

    int mapCounter = 0;

    for (size_t i = 0; i < wad->descriptors.size(); ++i) {
        Descriptor& d = wad->descriptors[i];
        std::string name(d.name, strnlen(d.name, 8));

        // Deal with Map Marker
        if (name.size() == 4 && name[0] == 'E' && std::isdigit(name[1]) && name[2] == 'M'
            && std::isdigit(name[3])) {
            // Add new directory to directory tree
            Node* mapDir = new Node { name, true, d.offset, d.length };
            mapDir->parent = dirStack.top();
            mapDir->descIndex = i;
            dirStack.top()->children.push_back(mapDir);
            // Make most recent directory
            dirStack.push(mapDir);
            mapCounter = 10;
            continue;
        }

        // Deal with Namespace Markers
        // _START
        if (endsWith(name, "_START")) {
            // Get namespace name
            std::string new_name = name.substr(0, name.size() - 6);
            // Add new directory to directory tree
            Node* namespaceDir = new Node { new_name, true, d.offset, d.length };
            namespaceDir->parent = dirStack.top();
            namespaceDir->descIndex = i;
            dirStack.top()->children.push_back(namespaceDir);
            // Make most recent directory
            dirStack.push(namespaceDir);
            continue;
        }

        // _END
        if (endsWith(name, "_END")) {
            // Remove directory from stack
            dirStack.pop();
            continue;
        }

        // Lumps
        Node* fileNode = new Node { name, false, d.offset, d.length };
        fileNode->parent = dirStack.top();
        fileNode->descIndex = i;
        dirStack.top()->children.push_back(fileNode);

        // Check if still in Map Marker
        if (mapCounter > 0 && --mapCounter == 0)
            dirStack.pop();
    }

    return wad;
}

std::string Wad::getMagic() { return std::string(header.magic, 4); }

bool Wad::isContent(const std::string& path)
{
    std::string clean = norm(path);
    if (clean.empty()) return false;

    Node* n = resolve(clean);
    return n && !n->isDir;
}

bool Wad::isDirectory(const std::string& path)
{
    std::string clean = norm(path);
    if (clean.empty()) return false;

    Node* node = resolve(clean);
    return node && node->isDir;
}

int Wad::getSize(const std::string& path)
{
    std::string clean = norm(path);
    if (clean.empty()) return false;

    Node* node = resolve(clean);
    return (node && !node->isDir) ? (node->length) : -1;
}

int Wad::getContents(const std::string& path, char* buffer, int length, int offset)
{
    std::string clean = norm(path);
    if (clean.empty()) return false;

    // Get node from path
    Node* node = resolve(clean);
    if (!node || node->isDir || !buffer || length <= 0)
        return -1;

    if (offset >= node->length)
	return 0;

    // Calculate size/location of contents that is being retrieved
    size_t available = node->length - offset;
    size_t nbytes = (length < available) ? length : available;
    char* lumpStart = fileData.data() + node->offset + offset;

    // Copy contents to buffer
    memcpy(buffer, lumpStart, nbytes);

    return nbytes;
}

int Wad::getDirectory(const std::string& path, std::vector<std::string>* directory)
{
    if (!directory)
	return -1;
    // Clear vector for safety
    directory->clear();
    // Get node from path

    std::string clean = norm(path);
    if (clean.empty()) return -1;

    Node* node = resolve(clean);
    if (!node || !(node->isDir))
        return -1;

    // Add nodes children to directory vector
    for (Node* child : node->children)
        directory->push_back(child->name);

    return directory->size();
}

void Wad::createDirectory(const std::string& path)
{
    std::string cleaned = norm(path);
    if (cleaned.empty() || cleaned == "/") return;

    // Split the path up
    int slash = cleaned.find_last_of('/');
    std::string parentPath = (slash == 0) ? "/" : cleaned.substr(0, slash);
    std::string dirName = cleaned.substr(slash + 1);

    // Check if valid directory name
    if (dirName.empty() || dirName.size() > 2)
        return;

    std::string cleanParent = norm(parentPath);
    if (cleanParent.empty()) return;

    // Get parent node
    Node* parent = resolve(cleanParent);

    // Make sure parent is not a Map Marker
    if (parent->name.size() == 4 && parent->name[0] == 'E' && std::isdigit(parent->name[1])
        && parent->name[2] == 'M' && std::isdigit(parent->name[3]))
        return;

    // Check if directory already exists
    for (Node* c : parent->children)
        if (c->name == dirName)
            return;

    // Calculate where to insert descriptors
    size_t insertPos;
    if (parent == root) {
        insertPos = descriptors.size();
    } else {
        // Find <PARENT>_END
        std::string endTag = parent->name + "_END";
        insertPos = parent->descIndex + 1;
        while (insertPos < descriptors.size()) {
            std::string name(descriptors[insertPos].name, strnlen(descriptors[insertPos].name, 8));
            if (name == endTag)
                break;
            ++insertPos;
        }
        // Check to make sure <PARENT>_END exists
        if (insertPos == descriptors.size())
            return;
    }

    // Create new descriptors
    Descriptor startDesc { 0, 0 };
    Descriptor endDesc { 0, 0 };

    // Add names to descriptors
    std::string startTag = dirName + "_START";
    std::string endTag = dirName + "_END";

    memset(startDesc.name, 0, 8);
    memset(endDesc.name, 0, 8);

    strncpy(startDesc.name, (dirName + "_START").c_str(), 8);
    strncpy(endDesc.name, (dirName + "_END").c_str(), 8);

    // Insert descriptors into vector
    descriptors.insert(descriptors.begin() + insertPos, { startDesc, endDesc });
    // Update header
    header.count += 2;

    // Adjust descIndex for every Node after insertPos
    std::vector<Node*> stack { root };
    while (!stack.empty()) {
        Node* n = stack.back();
        stack.pop_back();
        if (n->descIndex >= insertPos)
            n->descIndex += 2;
        for (Node* ch : n->children)
            stack.push_back(ch);
    }

    // Create node and add it to directory tree
    Node* dir = new Node { dirName, true, 0, 0, parent, {}, insertPos };
    // Keep children in descriptor order
    auto& vec = parent->children;
    auto pos = vec.begin();
    while (pos != vec.end() && (*pos)->descIndex < dir->descIndex)
        ++pos;
    vec.insert(pos, dir);

    // Add into fileData
    std::vector<char> raw(32);
    memcpy(raw.data(), &startDesc, sizeof(Descriptor));
    memcpy(raw.data() + 16, &endDesc, sizeof(Descriptor));
    // Byte position in the descriptor table
    size_t bytePos = header.offset + insertPos * sizeof(Descriptor);
    // Splice the 32 bytes into the file image
    fileData.insert(fileData.begin() + bytePos, raw.begin(), raw.end());

    // Update header lump count inside the file image
    std::memcpy(fileData.data() + 4, &header.count, sizeof(uint32_t));

    return;
}

void Wad::createFile(const std::string& path)
{
    // Split the path up
    std::string cleaned = norm(path);
    if (cleaned.empty() || cleaned == "/")
	return;

    int slash = cleaned.find_last_of('/');
    std::string parentPath = (slash == 0) ? "/" : cleaned.substr(0, slash);
    std::string fileName = cleaned.substr(slash + 1);

    // Check if valid directory name
    if (fileName.empty() || fileName.size() > 8)
        return;

    // Check if fileName is a Map Marker
    if (fileName.size() == 4 && fileName[0] == 'E' && std::isdigit(fileName[1]) &&
	fileName[2] == 'M' && std::isdigit(fileName[3]))
    return;

    // Get parent node
    Node* parent = resolve(parentPath);
    if (!parent || !parent->isDir)
        return;

    // Make sure parent is not a Map Marker
    if (parentPath.size() == 4 && parentPath[0] == 'E' && std::isdigit(parentPath[1])
        && parentPath[2] == 'M' && std::isdigit(parentPath[3]))
        return;

    // Check if file already exists
    for (Node* c : parent->children)
        if (c->name == fileName)
            return;

    // Calculate where to insert descriptors
    size_t insertPos;
    if (parent == root) {
        insertPos = descriptors.size();
    } else {
        std::string endTag = parent->name + "_END";
        insertPos = parent->descIndex + 1;
        while (insertPos < descriptors.size()) {
            std::string name(descriptors[insertPos].name, strnlen(descriptors[insertPos].name, 8));
            if (name == endTag)
                break;
            ++insertPos;
        }
        if (insertPos == descriptors.size())
            return;
    }

    // Build new lump descriptor
    Descriptor fileDesc { 0, 0 };
    memset(fileDesc.name, 0, 8);
    strncpy(fileDesc.name, fileName.c_str(), 8);

    // Insert into descriptor vector and fix header count
    descriptors.insert(descriptors.begin() + insertPos, fileDesc);
    header.count += 1;

    // Adjust descIndex
    std::vector<Node*> stack { root };
    while (!stack.empty()) {
        Node* n = stack.back();
        stack.pop_back();
        if (n->descIndex >= insertPos)
            n->descIndex += 1;
        for (Node* ch : n->children)
            stack.push_back(ch);
    }

    // Create node and add to directory tree
    Node* fileNode = new Node { fileName, false, 0, 0, parent, {}, insertPos };
    // Keep children in order
    auto& vec = parent->children;
    auto it = vec.begin();
    while (it != vec.end() && (*it)->descIndex < fileNode->descIndex)
        ++it;
    vec.insert(it, fileNode);

    // Add into fileData
    std::vector<char> raw(16);
    std::memcpy(raw.data(), &fileDesc, sizeof(Descriptor));

    // Byte position in the descriptor table
    size_t bytePos = header.offset + insertPos * sizeof(Descriptor);
    // Add data to fileData
    fileData.insert(fileData.begin() + bytePos, raw.begin(), raw.end());

    // Write updated lump count into header
    std::memcpy(fileData.data() + 4, &header.count, sizeof(uint32_t));
}

int Wad::writeToFile(const std::string& path, const char* buffer, int length, int offset)
{
    // Check validity
    if (!buffer || length <= 0 || offset < 0)
        return -1;

    Node* node = resolve(path);
    // Make sure it's a lump
    if (!node || node->isDir)
        return -1;

    // Lump has to be empty
    if (node->length != 0)
        return -1;

    // Calculate lump size and create lump data
    uint32_t lumpSize = offset + length;
    std::vector<char> lumpData(lumpSize, 0);
    memcpy(lumpData.data() + offset, buffer, length);

    // Insert lump before descriptor list
    size_t insertPos = header.offset;
    fileData.insert(fileData.begin() + insertPos, lumpData.begin(), lumpData.end());

    // Update header
    header.offset += lumpSize;
    memcpy(fileData.data() + 8, &header.offset, sizeof(uint32_t));

    // Update descriptor
    Descriptor& d = descriptors[node->descIndex];
    d.offset = static_cast<uint32_t>(insertPos);
    d.length = lumpSize;
    node->offset = d.offset;
    node->length = d.length;
    size_t descByte = header.offset + node->descIndex * sizeof(Descriptor);
    std::memcpy(fileData.data() + descByte, &d, sizeof(Descriptor));

    return length;
}

Node* Wad::resolve(const std::string& path)
{
    // Path validity check
    if (path.empty() || path[0] != '/')
        return nullptr;

    // Check if root
    if (path == "/")
        return root;

    Node* cur = root;
    std::size_t pos = 1; // Skip leading '/'
    while (pos < path.size()) {
        std::size_t next = path.find('/', pos);
        std::string part = path.substr(pos, next - pos);
        if (part.empty()) {
            pos = (next == std::string::npos) ? path.size() : next + 1;
            continue;
        }

        // Search in child vector
        bool found = false;
        for (Node* ch : cur->children) {
            if (ch->name == part) {
                cur = ch;
                found = true;
                break;
            }
        }
        if (!found)
            return nullptr;

        // Advance to next component
        if (next == std::string::npos)
            break; // Last token
        pos = next + 1;
    }

    return cur;
}
