#include <Libvm.h>
#include <fstream>
#include <vector>
#include <iostream>

using namespace std;
using namespace libvm;

namespace libvm {
    namespace ROM {
        vector<uint8_t> Read(const char* FileName) {
            streampos fileSize;
            ifstream file(FileName, ios::binary);
            file.seekg(0, ios::end);
            FileSize = file.tellg();
            file.seekg(0, ios::beg);
            vector<uint8_t> FileData(FileSize);
            file.read((char*) &FileData[0], FileSize);
            return FileData;
        }
    }
}