// Read .HPF file produced by QuickDAQ.  It has its own coverter to TXT or CSV,
// but a large (>1GB) file recorded at too high a sampling frequency (1khz; 1hz
// was desired) cannot be opened by the included tool.
//
// Also an exercise in dealing with binary-format files in C++

#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <vector>
using namespace std;

// handy int-to-hex printer, from https://stackoverflow.com/questions/5100718/integer-to-hex-string-in-c
template< typename T >
std::string i2hp( T i )
{
  std::stringstream stream;
  stream << "0x" 
         << std::setfill ('0') << std::setw(sizeof(T)*2) 
         << std::hex << i;
  return stream.str();
}

// my mod
template< typename T >
std::string i2h( T i )
{
  std::stringstream stream;
  stream << "0x" << std::hex << i;
  return stream.str();
}

class HPFFile {
    // HPFFile opens a binary file, considering it HPF format, and reads/converts chunk data
    public:
        // 64kb aligned on atom boundaries
        static const size_t chunksz = 64 * 1024; // 64KB
        enum {
            chunkid_header          = 0x1000,
            chunkid_channelinfo     = 0x2000,
            chunkid_data            = 0x3000,
            chunkid_eventdefinition = 0x4000
        };
        typedef int64_t int64;
        typedef int32_t int32;
        typedef int8_t  int8;
        static const size_t int64_count = chunksz / sizeof(int64);  // number of 64-bit atoms
        static const size_t int32_count = chunksz / sizeof(int32);  // number of 32-bit atoms
        static const size_t int8_count   = chunksz / sizeof(int8);  // number of 8-bit atoms

        int64 chunkid; string chunkid_s;
        int64 chunksize;
        int32 creatorid; string creatorid_s;
        int64 fileversion;
        int64 indexchunkoffset;

    private:
        ifstream file;
        const string filename;
        size_t pos;
        //char* buffer;
        union {
            int64 buffer64[int64_count];
            int32 buffer32[int32_count];
            int8 buffer8[int8_count];
        } u;
    public:

        HPFFile(const string& fn)
            : filename(fn), pos(0)
        {
            file.open(filename, ios::in | ios::binary);
            dump();
        }
        ~HPFFile() {
            file.close();
        }

        void read_chunk() {
            file.read(reinterpret_cast<char*>(&u.buffer64[0]), chunksz);
            pos = file.tellg();
            file_status();
            interpret_chunk();
        }

        void interpret_chunk() {
            chunkid = u.buffer64[0];
            chunkid_s = interpret_chunkid(chunkid);
            chunksize = u.buffer64[1];
            cout << "HPFFile::interpret_chunk: chunkid     Int64 : " << i2hp(chunkid) << " " << chunkid_s << endl;
            cout << "HPFFile::interpret_chunk: chunksize   Int64 : " << i2hp(chunksize) << endl;
            switch(chunkid) {
                case chunkid_header:
                    interpret_header(); break;
            }
            cout << endl;
        }
        void interpret_header() {
            creatorid = u.buffer32[4];
            creatorid_s = interpret_creatorid(creatorid);
            fileversion = u.buffer32[6] << sizeof(int32) | u.buffer32[5];
            indexchunkoffset = u.buffer32[8] << sizeof(32) | u.buffer32[7];
            cout << "HPFFile::interpret_header: creatorid        Int32 : " << i2hp(creatorid) << " FourCC '" << creatorid_s << "'" << endl;
            cout << "HPFFile::interpret_header: fileversion      Int64 : " << i2hp(fileversion) << endl;
            cout << "HPFFile::interpret_header: indexchunkoffset Int64 : " << i2hp(indexchunkoffset) << endl;
        }
        string interpret_chunkid(const int64& id) {
            string s;
            switch(id) {
                case chunkid_header:
                    s = "header"; break;
                case chunkid_channelinfo:
                    s = "channelinfo"; break;
                case chunkid_data:
                    s = "data"; break;
                case chunkid_eventdefinition:
                    s = "eventdefinition"; break;
                default:
                    s = "UNKNOWN_" + i2h(id); break;
            }
            return s;
        }

        string interpret_creatorid(const int32& id) {  // this is a FourCC string: 'datx'
            string s;
            const int8_t * p = reinterpret_cast<const int8_t * const>(&id);
            s += p[0]; s += p[1]; s += p[2]; s += p[3];
            return s;
        }


        void file_status(const bool verbose = false) {
            streampos beg, end, here;
            if (verbose) {
                cerr << "HPFFile::file_status(verbose): " << filename << " is " << (file.is_open() ? "" : "not ") << "open" << endl;
                cerr << "HPFFile::file_status(verbose): chunksz is " << chunksz << endl;
            }
            here = file.tellg();
            auto here_chunks = static_cast<double>(here) / chunksz;  // cast so division is double to catch fractional chunks
            file.seekg(0, ios::beg);
            beg = file.tellg();
            file.seekg(0, ios::end);
            end = file.tellg();
            file.seekg(here);
            if (verbose)
                cerr << "HPFFile::file_status(verbose): " << filename << " size is " << (end - beg) << " bytes" << endl;
            cerr << "HPFFile::file_status: " << filename << " curpos " << here << " " << i2h(here) << " is " << here_chunks << " 64KB chunks from beg" << endl;
        }
        void dump() {
            cerr << "HPFFile::dump: chunksz=" << chunksz << " sizeof(int64)=" << sizeof(int64) << " int64_count=" << int64_count << " filename='" << filename << "' pos=" << pos << endl;
            file_status(true);
        }
};

int main ()
{
    HPFFile h("t.hpf");
    h.read_chunk();
    h.read_chunk();
    h.read_chunk();
    h.read_chunk();
}


