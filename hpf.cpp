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

uint64_t ww2W( uint32_t w1, uint32_t w2 )
{
    uint64_t ans = static_cast<uint64_t>(w1) << sizeof(w1)*8;
    ans |= w2;
    return ans;
}


class HPFFile {
    // HPFFile opens a binary file, considering it HPF format, and reads/converts chunk data
    public:
        bool debug = true;  // if true, print lots of info
        // 64kb aligned on atom boundaries
        static const size_t chunksz = 64 * 1024; // 64KB
        enum {
            chunkid_header          = 0x1000,
            chunkid_channelinfo     = 0x2000,
            chunkid_data            = 0x3000,
            chunkid_eventdefinition = 0x4000
        };
        static const size_t int64_count = chunksz / sizeof(int64_t);  // number of 64-bit atoms
        static const size_t int32_count = chunksz / sizeof(int32_t);  // number of 32-bit atoms
        static const size_t int16_count = chunksz / sizeof(int16_t);  // number of 16-bit atoms
        static const size_t int8_count  = chunksz / sizeof(int8_t);   // number of 8-bit atoms

        // common
        int64_t chunkid; string chunkid_s;
        int64_t chunksize;
        string xmldata; // used by header, channelinfo, ...

        // header
        int32_t creatorid; string creatorid_s;
        int64_t fileversion;
        int64_t indexchunkoffset;
        
        // channelinfo
        int32_t groupid;
        int32_t numberofchannels;

    private:
        ifstream file;
        const string filename;
        size_t pos;
        size_t cursz;
        //char* buffer;
        union {
            int64_t buffer64 [int64_count];
            int32_t buffer32 [int32_count];
            int16_t buffer16 [int16_count];
            int8_t  buffer8  [int8_count];
        } u;
    public:

        HPFFile(const string& fn)
            : filename(fn), pos(0)
        {
            file.open(filename, ios::in | ios::binary);
            if (debug)
                dump();
        }
        ~HPFFile() {
            file.close();
        }

        void read_chunk(size_t sz = chunksz) {
            // next step:
            //    read the first two int64s (chunkid, chunksize)
            //    using chunksize, update buffer sizes if necessary
            //    read a further chunksize-(2*sizeof(int64_t)) bytes into the buffer
            //    set cursz to be chunksize
            //    the int64_count, etc should probably not be const now, and instead set with each chunk read
            file.read(reinterpret_cast<char*>(&u.buffer64[0]), sz);
            pos = file.tellg();
            cursz = sz;
            if (debug)
                file_status();
            interpret_chunk();
        }

        void interpret_chunk() {
            chunkid = u.buffer64[0];
            chunkid_s = interpret_chunkid(chunkid);
            chunksize = u.buffer64[1];
            if (debug) {
                cerr << "HPFFile::interpret_chunk:       chunkid            int64_t  : " << i2hp(chunkid) << " " << chunkid_s << endl;
                cerr << "HPFFile::interpret_chunk:       chunksize          int64_t  : " << i2hp(chunksize) << endl;
            }
            switch(chunkid) {
                case chunkid_header:
                    interpret_header(); break;
                case chunkid_channelinfo:
                    interpret_channelinfo(); break;
            }
            if (debug)
                cerr << endl;
        }
        void interpret_header() {
            creatorid = u.buffer32[4];
            creatorid_s = interpret_creatorid(creatorid);
            fileversion = ww2W(u.buffer32[6], u.buffer32[5]);
            indexchunkoffset = ww2W(u.buffer32[8], u.buffer32[7]);
            xmldata.assign(reinterpret_cast<const char*>(&u.buffer32[9]));
            if (debug) {
                cerr << "HPFFile::interpret_header:      creatorid          int32_t  : " << i2hp(creatorid) << " FourCC '" << creatorid_s << "'" << endl;
                cerr << "HPFFile::interpret_header:      fileversion        int64_t  : " << i2hp(fileversion) << endl;
                cerr << "HPFFile::interpret_header:      indexchunkoffset   int64_t  : " << i2hp(indexchunkoffset) << endl;
                cerr << "HPFFile::interpret_header:      xmldata            char[]   : " << xmldata << endl;
            }
        }
        void interpret_channelinfo() {
            groupid = u.buffer32[4];
            numberofchannels = u.buffer32[5];
            xmldata.assign(reinterpret_cast<const char*>(&u.buffer32[6]));
            if (debug) {
                cerr << "HPFFile::interpret_channelinfo: groupid            int32_t  : " << groupid << " " << i2hp(groupid) << endl;
                cerr << "HPFFile::interpret_channelinfo: numberofchannels   int32_t  : " << numberofchannels << " " << i2hp(numberofchannels) << endl;
                cerr << "HPFFile::interpret_channelinfo: xmldata            char[]   : " << xmldata << endl;
            }
        }

        string interpret_chunkid(const int64_t& id) {
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

        string interpret_creatorid(const int32_t& id) {  // this is a FourCC string: 'datx'
            string s;
            const int8_t * p = reinterpret_cast<const int8_t * const>(&id);
            s += p[0]; s += p[1]; s += p[2]; s += p[3];
            return s;
        }


        bool file_status(const bool verbose = false) {
            streampos beg, end, here;
            auto o = file.is_open();
            if (verbose) {
                cerr << "HPFFile::file_status(verbose): " << filename << " is " << (o ? "" : "not ") << "open" << endl;
            }
            if (o) {
                here = file.tellg();
                auto here_chunks = static_cast<double>(here) / chunksz;  // cast so division is double to catch fractional chunks
                file.seekg(0, ios::beg);
                beg = file.tellg();
                file.seekg(0, ios::end);
                end = file.tellg();
                file.seekg(here);
                if (verbose)
                    cerr << "HPFFile::file_status(verbose): " << filename << " size is " << (end - beg) << " bytes" << endl;
                cerr << "HPFFile::file_status: " << filename << " curpos=" << here << " " << i2h(here) << " (" << here_chunks << " 64KB chunks from beg)" << " cursz[size of last chunk read]=" << i2h(cursz) << endl;
            }
            return o;
        }
        void dump() {
            cerr << "HPFFile::dump: chunksz=" << chunksz << " sizeof(int64_t)=" << sizeof(int64_t) << " int64_count=" << int64_count << " filename='" << filename << "' pos=" << pos << endl;
            file_status(true);
        }
};

int main ()
{
    HPFFile h("t.hpf");
    if (! h.file_status())
        exit(1);
    h.read_chunk();
    h.read_chunk();
    h.read_chunk();
    h.read_chunk();
}


