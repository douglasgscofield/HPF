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
#include "tinyxml2.h"  //  for reading/parsing xml
#include "tixml2ex.h"  //  this also includes tinyxml2.h, but it's already loaded
using namespace std;
using namespace tinyxml2;

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


// TODO   cannot currently handle more than one groupID, and...
// TODO   cannot currently detect if there is more than one groupID in use, and...
// TODO   does not currently detect if the channel info and data groupIDs match.  Address in reverse order.
// DONE   does not currently detect if there are multiple channelinfo blocks.


class HPFFile
{
    ////
    //// HPFFile opens a binary file it HPF format and reads/converts the chunks of data
    ////

    public:

        const string cnm = "";//"HPFFile";
        unsigned char debug = 1;  // if > 0, print lots of info

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
        string  xmldata; // used by header, channelinfo, eventdefinition, ...
        typedef struct Time {
            string s_time;
            long y, m, d, h, n, s, x;
            double frac_s;  // fractional seconds
            Time(const string& t = "")
                : s_time(t)
            {
                interpret(s_time);
            };
            string out() {
                stringstream ss;
                ss << setw(4) << y
                    << "." << setw(2) << m
                    << "." << setw(2) << d
                    << " " << setw(2) << h
                    << "." << setw(2) << n
                    << "." << setw(2) << s
                    << "." << x
                    << "  " << frac_s
                    << "  " << s_time;
                return ss.str();
            };
            void interpret(const string& t)
            {
                s_time = t;
                if (t.length() == 0 || atol(t.c_str()) == 0) {
                    y = m = d = h = n = s = x = 0;
                    frac_s = 0.0;
                } else {
                    y = atol(t.substr(0, 4).c_str());
                    m = atol(t.substr(5, 2).c_str());
                    d = atol(t.substr(8, 2).c_str());
                    h = atol(t.substr(11, 2).c_str());
                    n = atol(t.substr(14, 2).c_str());
                    s = atol(t.substr(17, 2).c_str());
                    x = atol(t.substr(20).c_str());
                    frac_s = atof(t.substr(17).c_str());
                }
            }
        } Time;

        // header
        int32_t creatorid; string creatorid_s;
        int64_t fileversion;
        int64_t indexchunkoffset;
        string  recdate; // RecordingDate from XML
        
        // channelinfo
        int32_t numberofchannels;
        typedef struct ChannelInfo {
            string Name;
            string Unit;
            string ChannelType;
            string AssignedTimeChannelIndex;
            string DataType;
            string DataIndex;
            string StartTime;
            string TimeIncrement;
            string RangeMin;
            string RangeMax;
            string DataScale;
            string DataOffset;
            string SensorScale;
            string SensorOffset;
            string PerChannelSampleRate;
            string PhysicalChannelNumber;
            string UsesSensorValues;
            string ThermocoupleType;
            string TemperatureUnit;
            string UseThermocoupleValues;
            double interpret_raw_as_volts(const string& rawdata) {
                double r = atof(rawdata.c_str());
                double dats = atof(DataScale.c_str());
                double dato = atof(DataOffset.c_str());
                return (r * dats + dato);
            }
        } ChannelInfo;
        vector<ChannelInfo> channelinfo;

        // data
        int64_t datastartindex;
        int32_t channeldatacount;
        typedef struct ChannelDescriptor {
            int32_t offset;
            int32_t length;
        } ChannelDescriptor;
        ChannelDescriptor* channeldescriptor;
        int32_t* data;

        // eventdefinition
        int32_t definitioncount;

        // eventdata
        int32_t eventcount;
        typedef struct Event {
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
        typedef struct Index {
            int64_t datastartindex;
            int64_t perchanneldatalengthinsamples;
            int64_t chunkid;
            int64_t groupid;
            int64_t fileoffset;
        } Index;
        vector<Index> index;


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
                    interpret_chunk_header(); break;
                case chunkid_channelinfo:
                    interpret_chunk_channelinfo(); break;
                case chunkid_data:
                    interpret_chunk_data(); break;
                case chunkid_eventdefinition:
                    interpret_chunk_eventdefinition(); break;
                case chunkid_eventdata:
                    interpret_chunk_eventdata(); break;
                case chunkid_index:
                    interpret_chunk_index(); break;
                default:
                    cerr << p << "unknown chunkid " << i2h(chunkid) << endl;
                    exit(1);
            }
            if (debug)
                cerr << endl;
        }

        void interpret_chunk_header()
        {
            static const string p = pfx(cnm + "::" + "interpret_chunk_header");
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
            // handle the XML
            static const char* rootname = "RecordingDate";
            auto doc = tinyxml2::load_document(xmldata);
            auto root = doc->FirstChildElement();
            if (! root) { cerr << p << "*** Root of XML not found" << endl; exit(1); }
            if (strcmp(root->Name(), rootname)) {
                cerr << p << "*** <" << rootname << "> not found in doc, instead found " << root->Name() << endl;
                exit(1);
            }
            recdate.assign(text(root));
            Time rectime(recdate);
            //rectime.interpret(recdate);
            if (debug) {
                cerr << p << "XML is unpacked. Name:recdate:rectime :" << endl;
                cerr << p << root->Name() << ":" << recdate << ":" << rectime.out() << endl;
            }
        }

        void interpret_chunk_channelinfo()
        {
            static const string p = pfx(cnm + "::" + "interpret_chunk_channelinfo");
            groupid = u.buffer32[4];
            numberofchannels = u.buffer32[5];
            xmldata.assign(reinterpret_cast<const char*>(&u.buffer32[6]));
            if (debug) {
                cerr << p << "groupid            int32_t  : " << groupid << " " << i2h(groupid) << endl;
                cerr << p << "numberofchannels   int32_t  : " << numberofchannels << " " << i2h(numberofchannels) << endl;
                cerr << p << "xmldata            char[]   : " << xmldata.substr(0, 200) << " ..." << endl;
            }

            // handle the XML
            static const char* rootname = "ChannelInformationData";
            auto doc = tinyxml2::load_document(xmldata);
            auto root = doc->FirstChildElement();
            if (! root) { cerr << p << "*** Root of XML not found" << endl; exit(1); }
            if (strcmp(root->Name(), rootname)) {
                cerr << p << "*** <" << rootname << "> not found in doc, instead found " << root->Name() << endl;
                exit(1);
            }
            if (channelinfo.size()) {
                cerr << p << "*** channelinfo already allocated, has size " << channelinfo.size() << endl;
                exit(1);
            }
            channelinfo.resize(numberofchannels);
            for (auto chinfo : root)
            {
                auto i = atol(text(chinfo->FirstChildElement("DataIndex")).c_str());
                for_each (cbegin(chinfo), cend(chinfo),
                        [this, i](auto c) {
                        if      (! strcmp(c->Name(), "Name"))                     { channelinfo[i].Name.assign(text(c)); }
                        else if (! strcmp(c->Name(), "Unit"))                     { channelinfo[i].Unit.assign(text(c)); }
                        else if (! strcmp(c->Name(), "ChannelType"))              { channelinfo[i].ChannelType.assign(text(c)); }
                        else if (! strcmp(c->Name(), "AssignedTimeChannelIndex")) { channelinfo[i].AssignedTimeChannelIndex.assign(text(c)); }
                        else if (! strcmp(c->Name(), "DataType"))                 { channelinfo[i].DataType.assign(text(c)); }
                        else if (! strcmp(c->Name(), "DataIndex"))                { channelinfo[i].DataIndex.assign(text(c)); }
                        else if (! strcmp(c->Name(), "StartTime"))                { channelinfo[i].StartTime.assign(text(c)); }
                        else if (! strcmp(c->Name(), "TimeIncrement"))            { channelinfo[i].TimeIncrement.assign(text(c)); }
                        else if (! strcmp(c->Name(), "RangeMin"))                 { channelinfo[i].RangeMin.assign(text(c)); }
                        else if (! strcmp(c->Name(), "RangeMax"))                 { channelinfo[i].RangeMax.assign(text(c)); }
                        else if (! strcmp(c->Name(), "DataScale"))                { channelinfo[i].DataScale.assign(text(c)); }
                        else if (! strcmp(c->Name(), "DataOffset"))               { channelinfo[i].DataOffset.assign(text(c)); }
                        else if (! strcmp(c->Name(), "SensorScale"))              { channelinfo[i].SensorScale.assign(text(c)); }
                        else if (! strcmp(c->Name(), "SensorOffset"))             { channelinfo[i].SensorOffset.assign(text(c)); }
                        else if (! strcmp(c->Name(), "PerChannelSampleRate"))     { channelinfo[i].PerChannelSampleRate.assign(text(c)); }
                        else if (! strcmp(c->Name(), "PhysicalChannelNumber"))    { channelinfo[i].PhysicalChannelNumber.assign(text(c)); }
                        else if (! strcmp(c->Name(), "UsesSensorValues"))         { channelinfo[i].UsesSensorValues.assign(text(c)); }
                        else if (! strcmp(c->Name(), "ThermocoupleType"))         { channelinfo[i].ThermocoupleType.assign(text(c)); }
                        else if (! strcmp(c->Name(), "TemperatureUnit"))          { channelinfo[i].TemperatureUnit.assign(text(c)); }
                        else if (! strcmp(c->Name(), "UseThermocoupleValues"))    { channelinfo[i].UseThermocoupleValues.assign(text(c)); }
                        else { cerr << "*** Unknown child of ChannelInformationData: " << c->Name() << endl; exit(1); }
                        });
            }

            if (debug) {
                cerr << p << rootname << " name : " << root->Name() << endl;
                if (debug >= 2) {
                    for (auto i = numberofchannels - numberofchannels; i < numberofchannels; ++i) {
                        cerr << p << std::setw(3) << std::right << i << " Name " << channelinfo[i].Name << endl;
                        cerr << p << std::setw(3) << std::right << i << " Unit " << channelinfo[i].Unit << endl;
                        cerr << p << std::setw(3) << std::right << i << " ChannelType " << channelinfo[i].ChannelType << endl;
                        cerr << p << std::setw(3) << std::right << i << " AssignedTimeChannelIndex " << channelinfo[i].AssignedTimeChannelIndex << endl;
                        cerr << p << std::setw(3) << std::right << i << " DataType " << channelinfo[i].DataType << endl;
                        cerr << p << std::setw(3) << std::right << i << " DataIndex " << channelinfo[i].DataIndex << endl;
                        cerr << p << std::setw(3) << std::right << i << " StartTime " << channelinfo[i].StartTime << endl;
                        cerr << p << std::setw(3) << std::right << i << " TimeIncrement " << channelinfo[i].TimeIncrement << endl;
                        cerr << p << std::setw(3) << std::right << i << " RangeMin " << channelinfo[i].RangeMin << endl;
                        cerr << p << std::setw(3) << std::right << i << " RangeMax " << channelinfo[i].RangeMax << endl;
                        cerr << p << std::setw(3) << std::right << i << " DataScale " << channelinfo[i].DataScale << endl;
                        cerr << p << std::setw(3) << std::right << i << " DataOffset " << channelinfo[i].DataOffset << endl;
                        cerr << p << std::setw(3) << std::right << i << " SensorScale " << channelinfo[i].SensorScale << endl;
                        cerr << p << std::setw(3) << std::right << i << " SensorOffset " << channelinfo[i].SensorOffset << endl;
                        cerr << p << std::setw(3) << std::right << i << " PerChannelSampleRate " << channelinfo[i].PerChannelSampleRate << endl;
                        cerr << p << std::setw(3) << std::right << i << " PhysicalChannelNumber " << channelinfo[i].PhysicalChannelNumber << endl;
                        cerr << p << std::setw(3) << std::right << i << " UsesSensorValues " << channelinfo[i].UsesSensorValues << endl;
                        cerr << p << std::setw(3) << std::right << i << " ThermocoupleType " << channelinfo[i].ThermocoupleType << endl;
                        cerr << p << std::setw(3) << std::right << i << " TemperatureUnit " << channelinfo[i].TemperatureUnit << endl;
                        cerr << p << std::setw(3) << std::right << i << " UseThermocoupleValues " << channelinfo[i].UseThermocoupleValues << endl;
                    }
                }
                // print channel names
                cerr << p << "XML is unpacked. Channel Name:DataType :" << endl;
                cerr << p; for (auto c : channelinfo) cerr << c.Name << ":" << c.DataType << ", "; cerr << endl;
            }
        }

        void interpret_chunk_data()
        {
            static const string p = pfx(cnm + "::" + "interpret_chunk_data");
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

        void interpret_chunk_eventdefinition()
        {
            static const string p = pfx(cnm + "::" + "interpret_chunk_eventdefinition");
            definitioncount = u.buffer32[4];
            xmldata.assign(reinterpret_cast<const char*>(&u.buffer32[5]));
            if (debug) {
                cerr << p << "definitioncount    int32_t  : " << i2h(definitioncount) << " " << definitioncount << endl;
                cerr << p << "xmldata            char[]   : " << xmldata << endl;
            }
        }

        void interpret_chunk_eventdata()
        {
            static const string p = pfx(cnm + "::" + "interpret_chunk_eventdata");
            eventcount = u.buffer64[2];
            event = new Event[eventcount];
            if (debug) {
                cerr << p << "eventcount         int64_t  : " << eventcount << " " << i2hp(eventcount) << endl;
                cerr << p << "Event*             event[]  : " << i2hp(event) << endl;
            }
            delete[] event;
        }

        void interpret_chunk_index()
        {
            static const string p = pfx(cnm + "::" + "interpret_chunk_index");
            indexcount = u.buffer64[2];
            if (index.size()) {
                cerr << p << "*** index already allocated, has size " << index.size() << endl;
                exit(1);
            }
            index.resize(indexcount);
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
    h.read_chunk();
    h.read_chunk();
    h.read_chunk();
    h.read_chunk();
    h.read_chunk();
    //h.read_chunk();
}


