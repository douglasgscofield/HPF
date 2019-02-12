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

// handy int-to-hex printer padded to var nibble width, from https://stackoverflow.com/questions/5100718/integer-to-hex-string-in-c
template< typename T >
std::string i2hp( T i )
{
  std::stringstream stream;
  stream << "0x" << std::setfill ('0') << std::setw(sizeof(T)*2) << std::hex << i;
  return stream.str();
}

// mod for unpadded int-to-hex
template< typename T >
std::string i2h( T i )
{
  std::stringstream stream;
  stream << "0x" << std::hex << i;
  return stream.str();
}




class HPFFile
{
    ////
    //// HPFFile opens a binary file, considering it HPF format, and reads/converts chunk data
    ////

    public:

        const string cnm = "HPFFile";
        bool debug = true;  // if true, print lots of info

        ////
        //// buffer
        ////
        static const size_t chunksz = 64 * 1024; // 64KB chunks are the default with HPF files
        static const size_t buffersz = 1024 * 1024; // 1024KB is the largest chunk size we allow for now

        static const size_t int64_count = buffersz / sizeof(int64_t);  // number of 64-bit atoms
        static const size_t int32_count = buffersz / sizeof(int32_t);  // number of 32-bit atoms
        static const size_t int16_count = buffersz / sizeof(int16_t);  // number of 16-bit atoms
        static const size_t int8_count  = buffersz / sizeof(int8_t);   // number of 8-bit atoms

    private:

        ifstream file;
        const string filename;
        size_t pos;
        size_t cursz;
        union {  // union to allow access to the buffer containing buffersz bytes at whichever atom size we wish
            int64_t buffer64 [int64_count];
            int32_t buffer32 [int32_count];
            int16_t buffer16 [int16_count];
            int8_t  buffer8  [int8_count];
        } u;

        string pfx(const string& p, const int w = 36)  // standardised prefix for debug output lines
        {
            stringstream s;
            s.width(w);
            s << std::left << (p + ":");
            return s.str();
        }


    public:

        ////
        //// chunk contents, for all chunk types
        ////
        enum {
            chunkid_header          = 0x1000,
            chunkid_channelinfo     = 0x2000,
            chunkid_data            = 0x3000,
            chunkid_eventdefinition = 0x4000,
            chunkid_eventdata       = 0x5000,
            chunkid_index           = 0x6000
        };

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

        // index
        int64_t indexcount;
        typedef struct _Index {
            int64_t datastartindex;
            int64_t perchanneldatalengthinsamples;
            int64_t chunkid;
            int64_t groupid;
            int64_t fileoffset;
        } Index;
        Index* index;


    public:

        HPFFile(const string& fn)
            : filename(fn), pos(0)
        {
            file.open(filename, ios::in | ios::binary);
            if (debug)
                dump();
        }
        ~HPFFile()
        {
            file.close();
        }

        ////
        //// public methods for reading and interpreting chunk contents
        ////
        void read_chunk(size_t sz = chunksz)
        {
            static const string p = pfx(cnm + "::" + "read_chunk", 25);
            // read the first two int64 words
            // using word[1], determine buffer size
            // reposition to prior to the first two words, and read word[1] bytes into the buffer
            // interpret_chunk()
            int64_t twowords[2];
            streampos here = file.tellg();
            file.read(reinterpret_cast<char*>(&twowords[0]), 16);  // read the first two words
            if (debug) {
                cerr << p << "here=" << here 
                    << " first two 64-bit words: twowords[0]=chunkid=" << i2hp(twowords[0]) 
                    << " twowords[1]=chunksize=" << i2hp(twowords[1]) 
                    << endl;
                cerr << p << "repositioning to " << here << " and reading " << i2h(twowords[1]) << " " << twowords[1] << " bytes" << endl;
            }
            file.seekg(here);
            sz = twowords[1];
            if (sz > buffersz) {
                cerr << p << "buffer size " << i2h(buffersz) << " is too small for chunk size " << i2h(sz) << endl;
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

        void interpret_chunk()
        {
            static const string p = pfx(cnm + "::" + "interpret_chunk");
            chunkid = u.buffer64[0];
            chunkid_s = interpret_chunkid(chunkid);
            chunksize = u.buffer64[1];
            if (debug) {
                cerr << p << "chunkbaseaddress   char*    : " << i2hp(chunkbaseaddress) << endl;
                cerr << p << "chunkid            int64_t  : " << chunkid << " " << i2hp(chunkid) << " " << chunkid_s << endl;
                cerr << p << "chunksize          int64_t  : " << chunksize << " " << i2hp(chunksize) << endl;
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
                case chunkid_index:
                    interpret_index(); break;
                default:
                    cerr << p << "unknown chunkid " << i2h(chunkid) << endl;
                    exit(1);
            }
            if (debug)
                cerr << endl;
        }

        void interpret_header()
        {
            static const string p = pfx(cnm + "::" + "interpret_header");
            creatorid = u.buffer32[4];
            creatorid_s = interpret_creatorid(creatorid);
            fileversion = *(reinterpret_cast<int64_t*>(&u.buffer32[5]));
            indexchunkoffset = *(reinterpret_cast<int64_t*>(&u.buffer32[7]));
            xmldata.assign(reinterpret_cast<const char*>(&u.buffer32[9]));
            if (debug) {
                cerr << p << "creatorid          int32_t  : " << i2hp(creatorid) << " FourCC '" << creatorid_s << "'" << endl;
                cerr << p << "fileversion        int64_t  : " << i2hp(fileversion) << endl;
                cerr << p << "indexchunkoffset   int64_t  : " << i2hp(indexchunkoffset) << endl;
                cerr << p << "xmldata            char[]   : " << xmldata << endl;
            }
        }

        void interpret_channelinfo()
        {
            static const string p = pfx(cnm + "::" + "interpret_channelinfo");
            groupid = u.buffer32[4];
            numberofchannels = u.buffer32[5];
            xmldata.assign(reinterpret_cast<const char*>(&u.buffer32[6]));
            if (debug) {
                cerr << p << "groupid            int32_t  : " << groupid << " " << i2h(groupid) << endl;
                cerr << p << "numberofchannels   int32_t  : " << numberofchannels << " " << i2h(numberofchannels) << endl;
                cerr << p << "xmldata            char[]   : " << xmldata << endl;
            }
        }

        void interpret_data()
        {
            static const string p = pfx(cnm + "::" + "interpret_data");
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
                cerr << p << "groupid            int32_t  : " << i2h(groupid) << " " << groupid << endl;
                cerr << p << "datastartindex     int64_t  : " << i2h(datastartindex) << " " << datastartindex << endl;
                cerr << p << "channeldatacount   int32_t  : " << i2h(channeldatacount) << " " << channeldatacount << endl;
                cerr << p << "ChannelDescriptor* channeldescriptor[]  : " << endl;
                for (auto i = 0; i < channeldatacount; ++i) {
                    cerr << p << std::setw(3) << std::right << i 
                        << " offset=" << i2hp(channeldescriptor[i].offset) 
                        << " length=" << i2hp(channeldescriptor[i].length) 
                        << endl;
                }
                uint64_t offsetfromchunkbase = reinterpret_cast<char*>(data) - chunkbaseaddress;
                cerr << p << "int32_t*           data[]   : " << i2hp(data) 
                    << " offsetfromchunkbase=" << i2h(offsetfromchunkbase) << " " << offsetfromchunkbase 
                    << endl;
            }
            delete[] channeldescriptor;
        }

        void interpret_eventdefinition()
        {
            static const string p = pfx(cnm + "::" + "interpret_eventdefinition");
            definitioncount = u.buffer32[4];
            xmldata.assign(reinterpret_cast<const char*>(&u.buffer32[5]));
            if (debug) {
                cerr << p << "definitioncount    int32_t  : " << i2h(definitioncount) << " " << definitioncount << endl;
                cerr << p << "xmldata            char[]   : " << xmldata << endl;
            }
        }

        void interpret_eventdata()
        {
            static const string p = pfx(cnm + "::" + "interpret_eventdata");
            eventcount = u.buffer64[2];
            event = new Event[eventcount];
            if (debug) {
                cerr << p << "eventcount         int64_t  : " << eventcount << " " << i2hp(eventcount) << endl;
                cerr << p << "Event*             event[]  : " << i2hp(event) << endl;
            }
            delete[] event;
        }

        void interpret_index()
        {
            static const string p = pfx(cnm + "::" + "interpret_index");
            indexcount = u.buffer64[2];
            index = new Index[indexcount];
            for (auto i = 0; i < indexcount; ++i) {
                index[i].datastartindex                = u.buffer64[3 + (2*i)];
                index[i].perchanneldatalengthinsamples = u.buffer64[4 + (2*i)];
                index[i].chunkid                       = u.buffer64[5 + (2*i)];
                index[i].groupid                       = u.buffer64[6 + (2*i)];
                index[i].fileoffset                    = u.buffer64[7 + (2*i)];
            }
            if (debug) {
                cerr << p << "indexcount         int64_t  : " << i2h(indexcount) << " " << indexcount << endl;
                cerr << p << "Index*             index[]  : " << endl;
                for (auto i = 0; i < indexcount; ++i) {
                    cerr << p << std::setw(10) << std::right << i 
                        << " datastartindex=" << i2h(index[i].datastartindex) 
                        << " perchanneldatalengthinsamples=" << i2h(index[i].perchanneldatalengthinsamples) 
                        << " chunkid=" << i2h(index[i].chunkid) 
                        << " groupid=" << i2h(index[i].groupid) 
                        << " fileoffset=" << i2h(index[i].fileoffset) 
                        << endl;
                }
            }
            delete[] index;
        }

    private:

        ////
        //// private methods that help the public methods
        ////
        string interpret_chunkid(const int64_t& id)
        {
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
                case chunkid_index:
                    s = "index"; break;
                default:
                    s = "UNKNOWN_" + i2h(id); break;
            }
            return s;
        }

        string interpret_creatorid(const int32_t& id)
        {  // this is a FourCC string: 'datx'
            string s;
            const int8_t * p = reinterpret_cast<const int8_t * const>(&id);
            s += p[0]; s += p[1]; s += p[2]; s += p[3];
            return s;
        }


    public:

        ////
        //// public methods for status and debugging
        ////
        bool file_status(const bool verbose = false)
        {
            static const string p = pfx(cnm + "::" + "file_status", 25);
            static const string pv = pfx(cnm + "::" + "file_status(verbose)", 30);
            streampos beg, end, here;
            auto o = file.is_open();
            if (verbose) {
                cerr << pv << filename << " is " << (o ? "" : "not ") << "open" << endl;
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
                    cerr << pv << filename << " size is " << (end - beg) << " bytes" << endl;
                cerr << p << filename 
                    << " curpos=" << here << " " << i2h(here) << " (" << here_chunks << " 64KB chunks from beg)" 
                    << " cursz[size of last chunk read]=" << i2h(cursz) 
                    << endl;
            }
            return o;
        }

        void dump()
        {
            static const string p = pfx(cnm + "::" + "dump", 20);
            cerr << p << "chunksz=" << chunksz 
                << " sizeof(int64_t)=" << sizeof(int64_t) 
                << " int64_count=" << int64_count 
                << " filename=" << filename 
                << " pos=" << pos 
                << endl;
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


