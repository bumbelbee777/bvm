#include <Bvm.h>
#include <fstream>
#include <vector>
#include <iostream>

using namespace std;
using namespace Bvm;

namespace Bvm {
    namespace ROM {
        vector<uint64_t> Read(const char* FileName) {
            ifstream file(FileName, ios::binary);
            if(file.good()) {
                streampos FileSize;
                file.seekg(0, ios::end);
                FileSize = file.tellg();
                file.seekg(0, ios::beg);
                vector<uint64_t> FileData(FileSize);
                file.read((char*) &FileData[0], FileSize);
                return FileData;
            } else {
                cerr << "Couldn't read ROM file " << FileName << ", exiting..." << endl;
                throw 1;
            }
        }
    }
}