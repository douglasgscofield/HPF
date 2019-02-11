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
    // I may not need this... turns out I can do
    //     fileversion = *(reinterpret_cast<int64_t*>(&u.buffer32[5]));
    // to get an int64_t from a 32-bit aligned boundary no problem.
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
        static const size_t buffersz = 1024 * 1024; // 1024KB
        enum {
            chunkid_header          = 0x1000,
            chunkid_channelinfo     = 0x2000,
            chunkid_data            = 0x3000,
            chunkid_eventdefinition = 0x4000,
            chunkid_eventdata       = 0x5000
        };
        static const size_t int64_count = buffersz / sizeof(int64_t);  // number of 64-bit atoms
        static const size_t int32_count = buffersz / sizeof(int32_t);  // number of 32-bit atoms
        static const size_t int16_count = buffersz / sizeof(int16_t);  // number of 16-bit atoms
        static const size_t int8_count  = buffersz / sizeof(int8_t);   // number of 8-bit atoms

        // common
        char*   chunkbaseaddress;
        int64_t chunkid; string chunkid_s;
        int64_t chunksize;
        int32_t groupid; // used by channelinfo and data
        string xmldata; // used by header, channelinfo, eventdefinition, ...

        // header
        int32_t creatorid; string creatorid_s;
        int64_t fileversion;
        int64_t indexchunkoffset;
        
        // channelinfo
        int32_t numberofchannels;

        // data
        int64_t datastartindex;
        int32_t channeldatacount;
        typedef struct _ChannelDescriptor {
            int32_t offset;
            int32_t length;
        } ChannelDescriptor;
        ChannelDescriptor* channeldescriptor;
        int32_t* data;

        

        // eventdefinition
        int32_t definitioncount;

        // eventdata
        int32_t eventcount;
        typedef struct _Event {
            int32_t eventclass;
            int32_t id;
            int32_t channelindex;
            int64_t eventstartindex;
            int64_t eventendindex;
            int32_t idata1;
            int32_t idata2;
            double  ddata1;
            double  ddata2;
            double  ddata3;
            double  ddata4;
        } Event;
        Event* event;


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
            // read the first two int64 words
            // using word[1], determine buffer size
            // reposition to prior to the first two words, and read word[1] bytes into the buffer
            // interpret_chunk()
            int64_t twowords[2];
            streampos here = file.tellg();
            file.read(reinterpret_cast<char*>(&twowords[0]), 16);  // read the first two words
            if (debug) {
                cerr << "HPFFile::read_chunk:  here=" << here << " first two 64-bit words: twowords[0]=chunkid=" << i2h(twowords[0]) << " twowords[1]=chunksize=" << i2h(twowords[1]) << endl;
                cerr << "HPFFile::read_chunk:  repositioning to " << here << " and reading " << i2h(twowords[1]) << " " << twowords[1] << " bytes" << endl;
            }
            file.seekg(here);
            sz = twowords[1];
            if (sz > buffersz) {
                cerr << "HPFFile::read_chunk: buffer size " << i2h(buffersz) << " is too small for chunk size " << i2h(sz) << endl;
                exit(1);
            }
            chunkbaseaddress = reinterpret_cast<char*>(&u.buffer64[0]);
            file.read(chunkbaseaddress, sz);
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
                cerr << "HPFFile::interpret_chunk:           chunkbaseaddress   char*    : " << i2hp(chunkbaseaddress) << endl;
                cerr << "HPFFile::interpret_chunk:           chunkid            int64_t  : " << chunkid << " " << i2hp(chunkid) << " " << chunkid_s << endl;
                cerr << "HPFFile::interpret_chunk:           chunksize          int64_t  : " << chunksize << " " << i2hp(chunksize) << endl;
            }
            switch(chunkid) {
                case chunkid_header:
                    interpret_header(); break;
                case chunkid_channelinfo:
                    interpret_channelinfo(); break;
                case chunkid_data:
                    interpret_data(); break;
                case chunkid_eventdefinition:
                    interpret_eventdefinition(); break;
                case chunkid_eventdata:
                    interpret_eventdata(); break;
            }
            if (debug)
                cerr << endl;
        }
        void interpret_header() {
            creatorid = u.buffer32[4];
            creatorid_s = interpret_creatorid(creatorid);
            fileversion = *(reinterpret_cast<int64_t*>(&u.buffer32[5]));
            indexchunkoffset = *(reinterpret_cast<int64_t*>(&u.buffer32[7]));
            xmldata.assign(reinterpret_cast<const char*>(&u.buffer32[9]));
            if (debug) {
                cerr << "HPFFile::interpret_header:          creatorid          int32_t  : " << i2hp(creatorid) << " FourCC '" << creatorid_s << "'" << endl;
                cerr << "HPFFile::interpret_header:          fileversion        int64_t  : " << i2hp(fileversion) << endl;
                cerr << "HPFFile::interpret_header:          indexchunkoffset   int64_t  : " << i2hp(indexchunkoffset) << endl;
                cerr << "HPFFile::interpret_header:          xmldata            char[]   : " << xmldata << endl;
            }
        }
        void interpret_channelinfo() {
            groupid = u.buffer32[4];
            numberofchannels = u.buffer32[5];
            xmldata.assign(reinterpret_cast<const char*>(&u.buffer32[6]));
            if (debug) {
                cerr << "HPFFile::interpret_channelinfo:     groupid            int32_t  : " << groupid << " " << i2h(groupid) << endl;
                cerr << "HPFFile::interpret_channelinfo:     numberofchannels   int32_t  : " << numberofchannels << " " << i2h(numberofchannels) << endl;
                cerr << "HPFFile::interpret_channelinfo:     xmldata            char[]   : " << xmldata << endl;
            }
        }
        void interpret_data() {
            groupid = u.buffer32[4];
            datastartindex = *(reinterpret_cast<int64_t*>(&u.buffer32[5]));
            channeldatacount = u.buffer32[7];
            channeldescriptor = new ChannelDescriptor[channeldatacount];
            for (auto i = 0; i < channeldatacount; ++i) {
                channeldescriptor[i].offset = u.buffer32[8 + (2*i)];
                channeldescriptor[i].length = u.buffer32[9 + (2*i)];
            }
            int32_t* data = &u.buffer32[8 + (2*channeldatacount)];
            if (debug) {
                cerr << "HPFFile::interpret_data:            groupid            int32_t  : " << i2h(groupid) << " " << groupid << endl;
                cerr << "HPFFile::interpret_data:            datastartindex     int64_t  : " << i2h(datastartindex) << " " << datastartindex << endl;
                cerr << "HPFFile::interpret_data:            channeldatacount   int32_t  : " << i2h(channeldatacount) << " " << channeldatacount << endl;
                cerr << "HPFFile::interpret_data:            ChannelDescriptor* channeldescriptor[]  : " << endl;
                for (auto i = 0; i < channeldatacount; ++i)
                    cerr << "HPFFile::interpret_data:            " << i << " offset=" << i2hp(channeldescriptor[i].offset) << " length=" << i2hp(channeldescriptor[i].length) << endl;
                uint64_t offsetfromchunkbase = reinterpret_cast<char*>(data) - chunkbaseaddress;
                cerr << "HPFFile::interpret_data:            int32_t*           data[]   : " << i2hp(data) << " offsetfromchunkbase=" << i2h(offsetfromchunkbase) << " " << offsetfromchunkbase << endl;
            }
            delete[] channeldescriptor;
        }

        void interpret_eventdefinition() {
            definitioncount = u.buffer32[4];
            xmldata.assign(reinterpret_cast<const char*>(&u.buffer32[5]));
            if (debug) {
                cerr << "HPFFile::interpret_eventdefinition: definitioncount    int32_t  : " << i2h(definitioncount) << " " << definitioncount << endl;
                cerr << "HPFFile::interpret_eventdefinition: xmldata            char[]   : " << xmldata << endl;
            }
        }
        void interpret_eventdata() {
            eventcount = u.buffer64[2];
            event = new Event[eventcount];
            if (debug) {
                cerr << "HPFFile::interpret_eventdata:       eventcount         int64_t  : " << eventcount << " " << i2hp(eventcount) << endl;
                cerr << "HPFFile::interpret_eventdata:       Event*             event[]  : " << i2hp(event) << endl;
            }
            delete[] event;
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
                case chunkid_eventdata:
                    s = "eventdata"; break;
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
    h.read_chunk();
    h.read_chunk();
    h.read_chunk();
    h.read_chunk();
    h.read_chunk();
    h.read_chunk();
    h.read_chunk();
}


