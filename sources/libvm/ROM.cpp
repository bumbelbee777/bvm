#include <Libvm.h>
#include <fstream>
#include <vector>
#include <iostream>

using namespace std;
using namespace Libvm;

namespace Libvm {
    namespace ROM {
        vector<uint8_t> Read(const char* FileName) {
            ifstream file(FileName, ios::binary);
            if(file.good()) {
                streampos FileSize;
                file.seekg(0, ios::end);
                FileSize = file.tellg();
                file.seekg(0, ios::beg);
                vector<uint8_t> FileData(FileSize);
                file.read((char*) &FileData[0], FileSize);
                return FileData;
            } else {
                cerr << "couldn't read ROM file " << FileName << ", exiting..." << endl;
                throw 1;
            }
        }
    }
}