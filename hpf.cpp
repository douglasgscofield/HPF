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
#include <cctype>
#include <limits>
#include <algorithm>
#include <vector>
#include "tinyxml2.h"  //  for reading/parsing xml
#include "tixml2ex.h"  //  this also includes tinyxml2.h, but it's already loaded
using namespace std;
using namespace tinyxml2;

typedef std::numeric_limits<double> dbl;

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

string ToLower(const string& s)
{
    string t = s;
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);
    return t;
}



// TODO   cannot currently handle more than one groupID, and...
// TODO   cannot currently detect if there is more than one groupID in use, and...
// TODO   does not currently detect if the channel info and data groupIDs match.  Address in reverse order.
// DONE   does not currently detect if there are multiple channelinfo blocks.


class HPFFile
{
    ////
    //// HPFFile opens a binary file in HPF format and reads/converts the chunks of data
    ////

    public:

        const string  cnm               = "HPFFile";
        unsigned char debug             = 0;     // if > 0, print lots of info to cerr
        bool          do_downsample     = true;
        int16_t       downsample_count  = 1000;  // only output every downsample_count-th reading
        bool          table             = true;  // if true, print data table to cout
        streampos     filebeg;                   // beginning of the file opened, set by the constructor
        streampos     fileend;                   // end of the file opened, set by the constructor
        streampos     filesize;                  // size of the file opened, set by the constructor
        int64_t       data_lines        = 0;     // number of data lines
        int64_t       table_data_lines  = 0;     // number of data lines in the table
        bool          include_data_line = false; // prefix output lines with data line?
#define DEFAULT_SEP "\t"

        ////
        //// buffer
        ////
        static const size_t defaultchunksz = 64 * 1024; // 64KB chunks are the default with HPF files
        static const size_t buffersz       = 1024 * 1024; // 1024KB is the largest chunk size we allow for now

        static const size_t int64_count    = buffersz / sizeof(int64_t);  // number of 64-bit atoms
        static const size_t int32_count    = buffersz / sizeof(int32_t);  // number of 32-bit atoms
        static const size_t int16_count    = buffersz / sizeof(int16_t);  // number of 16-bit atoms
        static const size_t int8_count     = buffersz / sizeof(int8_t);   // number of 8-bit atoms

    private:

        ifstream      file;
        const string  filename;
        size_t        pos;
        streampos     curchunkfilepos;
        size_t        curchunksz;

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
        streampos  chunkfilepos;
        int64_t    chunkid;
        string     chunkid_s;
        int64_t    chunksize;
        int32_t    groupid; // 'set' by definition in channelinfo, ref'd by data
        string     xmldata; // used by header, channelinfo, eventdefinition, ...
        typedef struct Time {
            string s_time;
            long y, m, d, h, n, s, x;
            double frac_s;  // fractional seconds
            Time(const string& t = "")
            {
                interpret(t);
            };
            string out() const {
                stringstream ss;
                ss.fill('0');
                ss << setw(4) << y
                    << "-" << setw(2) << m
                    << "-" << setw(2) << d
                    << "|" << setw(2) << h
                    << "." << setw(2) << n
                    << "." << setw(2) << s
                    << "." << x;
                    //<< "." << x
                    //<< "  " << frac_s
                    //<< "  " << s_time;
                return ss.str();
            };
            void interpret(const string& t)
            {
                //cerr << "Time::interpret(" << t << ")" << endl;
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
                //cerr << "Time::out()" << out();
            }
            friend ostream& operator<<(std::ostream& os, const Time& t);
        } Time;

        typedef struct DataType {
            string  s_datatype;
            string  str;
            int32_t size_bytes;
            bool    is_signed;
            bool    is_fp;
            DataType(const string& t = "")
                : s_datatype(t), str(t), size_bytes(0), is_signed(false), is_fp(false)
            {
                interpret(t);
            };
            string out() const {
                stringstream ss;
                ss << str;
                //ss << s_datatype << std::boolalpha
                //    << ":" << size_bytes
                //    << ":" << is_signed;
                return ss.str();
            };
            void interpret(const string& t)
            {
                // cerr << "DataType::interpret(" << t << ")" << endl;
                s_datatype = t;
                str = ToLower(s_datatype);
                if (str == "int16") {
                    size_bytes = 2;
                    is_signed = true;
                } else if (str == "uint16") {
                    size_bytes = 2;
                    is_signed = false;
                } else if (str == "int32") {
                    size_bytes = 4;
                    is_signed = true;
                } else if (str == "float") {
                    size_bytes = 4;
                    is_signed = is_fp = true;
                } else if (str == "double") {
                    size_bytes = 8;
                    is_signed = is_fp = true;
                } else { cerr << "DataType::interpret(): datatype unknown: " << t << endl; exit(1); }
                if (str != "int16") cerr << "DataType not implemented: " << out();
                // cerr << "DataType::out()" << out();
            }
            friend ostream& operator<<(std::ostream& os, const DataType& t);
        } DataType;

        // header
        int32_t creatorid; string creatorid_s;
        int64_t fileversion;
        int64_t indexchunkoffset;
        string  recdate; // RecordingDate from XML
        Time    rectime; // RecordingDate from XML, interpreted as Time

        // channelinfo
        int32_t numberofchannels;
        typedef struct ChannelInfo {
            int32_t  _index;
            string   Name;
            string   Unit;
            string   ChannelType;
            int32_t  AssignedTimeChannelIndex;
            string   DataType;
            int32_t  DataIndex;
            Time     StartTime;
            double   TimeIncrement;
            int16_t  RangeMin;
            int16_t  RangeMax;
            double   DataScale;
            double   DataOffset;
            double   SensorScale;
            double   SensorOffset;
            double   PerChannelSampleRate;
            int32_t  PhysicalChannelNumber;
            bool     UsesSensorValues;
            string   ThermocoupleType;
            string   TemperatureUnit;
            bool     UseThermocoupleValues;
            //
            double interpret_as_volts(int16_t p) const {
                return static_cast<double>(p) * DataScale + DataOffset;
            }
        } ChannelInfo;
        vector<ChannelInfo> channelinfo;

        // data block
        typedef struct ChannelDescriptor {
            int32_t _index;      // not defined as part of the ChannelDescriptor structure
            int32_t offset;
            int32_t length;
            string  _datatype;   // type of each piece of data
            int32_t _atom_size;  // size of each piece of data
            int32_t _num_atoms;  // number of pieces of data
        } ChannelDescriptor;
        // index block filled in as we see each data chunk
        typedef struct DataChunkIndex {
            streampos fileoffset_chunk;
            streampos fileoffset_data;
            int64_t startindex;
            int64_t endindex;
            int32_t groupID;
        } DataChunkIndex;
        // each data chunk has its own ChannelDescriptor block, so allocate in interpret_data_chunk()
        // int64_t datastartindex;
        // int32_t channeldatacount;
        // ChannelDescriptor* channeldescriptor;
        // int32_t* data;
        
        // The actual data
        typedef struct ChannelData {
            int32_t         _index;
            vector<int16_t> data;
        } ChannelData;
        vector<ChannelData> channeldata;
        

        // eventdefinition block
        int32_t definitioncount;
        typedef struct EventDefinition {
            int32_t  eventdef_index;
            string   Name;
            string   Description;
            int32_t  Class;
            int32_t  ID;
            string   Type;
            bool     UsesIData1;
            bool     UsesIData2;
            bool     UsesDData1;
            bool     UsesDData2;
            bool     UsesDData3;
            bool     UsesDData4;
            string   DescriptionIData1;
            string   DescriptionIData2;
            string   DescriptionDData1;
            string   DescriptionDData2;
            string   DescriptionDData3;
            string   DescriptionDData4;
            string   Parameter1;
            string   Parameter2;
            string   Tolerance;
            bool     UsesParameter1;
            bool     UsesParameter2;
            bool     UsesTolerance;
            string   DescriptionParameter1;
            string   DescriptionParameter2;
            string   DescriptionTolerance;
        } EventDefinition;
        vector<EventDefinition> eventdefinition;

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
        // int64_t indexcount;  now allocated in interpret_chunk_index()
        int64_t index_entries = 0;
        typedef struct Index {
            int64_t _index;
            HPFFile* parent;
            int64_t datastartindex;
            int64_t perchanneldatalengthinsamples;
            int64_t chunkid;
            int64_t groupid;
            int64_t fileoffset;
            Index(HPFFile* p, int64_t dsi, int64_t pcdlis, int64_t cid, int64_t gid, int64_t fo)
                : parent(p), datastartindex(dsi), perchanneldatalengthinsamples(pcdlis),
                  chunkid(cid), groupid(gid), fileoffset(fo)
            {
                _index = parent->index_entries++;
            }
            string out() const
            {
                stringstream ss;
                ss << std::setw(6) << std::right << _index
                    << " datastartindex=" << i2h(datastartindex)
                    << " perchanneldatalengthinsamples=" << i2h(perchanneldatalengthinsamples)
                    << " chunkid=" << i2h(chunkid)
                    << " groupid=" << i2h(groupid)
                    << " fileoffset=" << i2h(fileoffset);
                return ss.str();
            }
        } Index;
        vector<Index> index;


    public:

        HPFFile(const string& fn)
            : filename(fn), pos(0)
        {
            file.open(filename, ios::in | ios::binary);
            streampos here;
            here = file.tellg();
            file.seekg(0, ios::beg);
            filebeg = file.tellg();
            file.seekg(0, ios::end);
            fileend = file.tellg();
            filesize = fileend - filebeg;
            file.seekg(here);
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
        bool read_chunk()
        {
            static const string p = pfx(cnm + "::" + "read_chunk", 25);
            if (file_status() == false)
                return false;
            // read the first two int64 words
            // using word[1], determine buffer size
            // reposition to prior to the first two words, and read word[1] bytes into the buffer
            // interpret_chunk()
            int64_t twowords[2];
            streampos here = file.tellg();
            file.read(reinterpret_cast<char*>(&twowords[0]), 16);  // read the first two words
            if (! file) {
                if (debug)
                    cerr << p << "could only read " << file.gcount() << " bytes, closing file" << endl;
                file.close();
                return false;
            }
            if (debug >= 2) {
                cerr << p << "here=" << here << " " << i2h(here)
                    << " first two 64-bit words: twowords[0]=chunkid=" << i2hp(twowords[0])
                    << " twowords[1]=chunksize=" << i2hp(twowords[1])
                    << endl;
                cerr << p << "repositioning to " << here << " and reading " << i2h(twowords[1]) << " " << twowords[1] << " bytes" << endl;
            }
            file.seekg(here);
            curchunksz = twowords[1];
            if (curchunksz > buffersz) {
                cerr << p << "buffer size " << i2h(buffersz) << " is too small for chunk size " << i2h(curchunksz) << endl;
                exit(1);
            }
            curchunkfilepos = here;
            file.read(reinterpret_cast<char*>(&u.buffer64[0]), curchunksz);  // read into the buffer
            pos = file.tellg();
            if (debug)
                file_status();
            interpret_chunk();
            return true;
        }

        void interpret_chunk()
        {
            static const string p = pfx(cnm + "::" + "interpret_chunk");
            chunkid = u.buffer64[0];
            chunkid_s = interpret_chunkid(chunkid);
            if (u.buffer64[1] != curchunksz) { cerr << p << "error interpreting curchunksz" << endl; exit(1); }
            if (debug) {
                cerr << p << "curchunkfilepos    streampos: " << curchunkfilepos << " " << i2h(curchunkfilepos)
                    << " chunkid : " << chunkid << " " << i2h(chunkid) << " " << chunkid_s 
                    << " curchunksz : " << curchunksz << " " << i2h(curchunksz)
                    << endl;
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
            rectime.interpret(recdate);
            if (debug) {
                cerr << p << "XML is unpacked. Name:recdate:rectime :" << endl;
                cerr << p << root->Name() << ":" << recdate << ":" << rectime << endl;
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
            else if (strcmp(root->Name(), rootname)) {
                cerr << p << "*** <" << rootname << "> not found in doc, instead found " << root->Name() << endl;
                exit(1);
            }
            if (channelinfo.size()) {
                cerr << p << "*** channelinfo already allocated, has size " << channelinfo.size() << endl;
                exit(1);
            }
            channelinfo.resize(numberofchannels);
            channeldata.resize(numberofchannels);
            auto i = 0 * numberofchannels;
            for (auto chinfo : root)
            {
                for_each (cbegin(chinfo), cend(chinfo),
                        [this, i](auto x) {  // note the lambda
                        channelinfo[i]._index = i;
                        channeldata[i]._index = i;
                        if (debug >= 3) {
                            cerr << p << "x->Name() = " << x->Name() 
                                 << "  text(x) = " << text(x)
                                 << endl;
                        }
#define MATCH_SET_STRING_VAR(_a_,_b_,_v_)          (! strcmp(_a_->Name(), #_v_)) { _b_._v_.assign(text(_a_)); }
#define MATCH_SET_CONVERT_VAR(_a_,_b_,_v_,_cfunc_) (! strcmp(_a_->Name(), #_v_)) { _b_._v_ = _cfunc_(text(_a_));  }
#define MATCH_SET_ASSIGN_VAR(_a_,_b_,_v_,_method_) (! strcmp(_a_->Name(), #_v_)) { _b_._v_._method_(text(_a_));  }
                        if      MATCH_SET_STRING_VAR  ( x, channelinfo[i], Name)
                        else if MATCH_SET_STRING_VAR  ( x, channelinfo[i], Unit)
                        else if MATCH_SET_STRING_VAR  ( x, channelinfo[i], ChannelType)
                        else if MATCH_SET_CONVERT_VAR ( x, channelinfo[i], AssignedTimeChannelIndex, interpret_int32)
                        else if MATCH_SET_CONVERT_VAR ( x, channelinfo[i], DataType,                 interpret_datatype)
                        else if MATCH_SET_CONVERT_VAR ( x, channelinfo[i], DataIndex,                interpret_int32)
                        else if MATCH_SET_CONVERT_VAR ( x, channelinfo[i], StartTime,                interpret_time)
                        else if MATCH_SET_CONVERT_VAR ( x, channelinfo[i], TimeIncrement,            interpret_double)
                        else if MATCH_SET_CONVERT_VAR ( x, channelinfo[i], RangeMin,                 interpret_int16)
                        else if MATCH_SET_CONVERT_VAR ( x, channelinfo[i], RangeMax,                 interpret_int16)
                        else if MATCH_SET_CONVERT_VAR ( x, channelinfo[i], DataScale,                interpret_double)
                        else if MATCH_SET_CONVERT_VAR ( x, channelinfo[i], DataOffset,               interpret_double)
                        else if MATCH_SET_CONVERT_VAR ( x, channelinfo[i], SensorScale,              interpret_double)
                        else if MATCH_SET_CONVERT_VAR ( x, channelinfo[i], SensorOffset,             interpret_double)
                        else if MATCH_SET_CONVERT_VAR ( x, channelinfo[i], PerChannelSampleRate,     interpret_double)
                        else if MATCH_SET_CONVERT_VAR ( x, channelinfo[i], PhysicalChannelNumber,    interpret_int32)
                        else if MATCH_SET_CONVERT_VAR ( x, channelinfo[i], UsesSensorValues,         interpret_bool)
                        else if MATCH_SET_STRING_VAR  ( x, channelinfo[i], ThermocoupleType)
                        else if MATCH_SET_STRING_VAR  ( x, channelinfo[i], TemperatureUnit)
                        else if MATCH_SET_CONVERT_VAR ( x, channelinfo[i], UseThermocoupleValues,    interpret_bool)
                        else { cerr << "*** Unknown child of ChannelInformation: " << x->Name() << endl; exit(1); }
                        });
                ++i;
            }

            if (debug) {
                cerr << p << rootname << " name : " << root->Name() << endl;
                cerr << std::boolalpha;
                if (debug >= 2) {
                    for (auto c : channelinfo) {
#define PRINT_VAR(_c_,_v_) " " #_v_ "=" << _c_._v_
                        cerr << p << std::setw(3) << std::right << c._index
                            << PRINT_VAR(c, Name)
                            << PRINT_VAR(c, Unit)
                            << PRINT_VAR(c, ChannelType)
                            << PRINT_VAR(c, AssignedTimeChannelIndex)
                            << PRINT_VAR(c, DataType)
                            << PRINT_VAR(c, DataIndex)
                            << PRINT_VAR(c, StartTime)
                            << PRINT_VAR(c, TimeIncrement)
                            << PRINT_VAR(c, RangeMin)
                            << PRINT_VAR(c, RangeMax)
                            << PRINT_VAR(c, DataScale)
                            << PRINT_VAR(c, DataOffset)
                            << PRINT_VAR(c, SensorScale)
                            << PRINT_VAR(c, SensorOffset)
                            << PRINT_VAR(c, PerChannelSampleRate)
                            << PRINT_VAR(c, PhysicalChannelNumber)
                            << PRINT_VAR(c, UsesSensorValues)
                            << PRINT_VAR(c, ThermocoupleType)
                            << PRINT_VAR(c, TemperatureUnit)
                            << PRINT_VAR(c, UseThermocoupleValues)
                            << endl;
                    }
                }
                // print channel names
                cerr << p << "XML is unpacked. Channel Name:DataType :" << endl;
                cerr << p;
                for (auto c : channelinfo)
                    cerr << c.Name << ":" << c.DataType << ", ";
                cerr << endl;
            }
        }

        void interpret_chunk_data()
        {
            static const string p = pfx(cnm + "::" + "interpret_chunk_data");
            if (u.buffer32[4] != groupid) {
                cerr << p << "*** groupid as recorded in data chunk " << u.buffer32[4] << " does not match groupid as recorded in channelinfo " << groupid << endl;
                exit(1);
            }
            int64_t datastartindex = *(reinterpret_cast<int64_t*>(&u.buffer32[5]));
            int32_t channeldatacount = u.buffer32[7];
            vector<ChannelDescriptor> channeldescriptor(channeldatacount);
            for (auto i = 0; i < channeldatacount; ++i) {
                channeldescriptor[i]._index = i;
                channeldescriptor[i].offset = u.buffer32[8 + (2*i)];
                channeldescriptor[i].length = u.buffer32[9 + (2*i)];
                DataType datatype(channelinfo[i].DataType);
                channeldescriptor[i]._datatype = datatype.str;
                channeldescriptor[i]._atom_size = datatype.size_bytes;
                channeldescriptor[i]._num_atoms = channeldescriptor[i].length / datatype.size_bytes;
            }
            if (debug >= 2) {
                cerr << p << "groupid            int32_t  : " << i2h(groupid) << " " << groupid << endl;
                cerr << p << "datastartindex     int64_t  : " << i2h(datastartindex) << " " << datastartindex << endl;
                cerr << p << "channeldatacount   int32_t  : " << i2h(channeldatacount) << " " << channeldatacount << endl;
                cerr << p << "vector<ChannelDescriptor> channeldescriptor[]  : " << endl;
            }
            for (auto c : channeldescriptor) {
                int16_t *dptr = &u.buffer16[c.offset / 2];
                channeldata[c._index].data.resize(c._num_atoms);
                for (auto j = 0; j < c._num_atoms; ++j) {
                    channeldata[c._index].data[j] =*dptr++;
                    // channeldata[c._index].data.push_back(*dptr++);
                }
                if (debug >= 3)
                    cerr << p << "channel" << std::setw(3) << std::right << c._index << " data @"
                        << " offset=" << i2hp(c.offset)
                        << " length=" << i2hp(c.length)
                        << " _datatype=" << c._datatype
                        << " _atom_size=" << c._atom_size
                        << " _num_atoms=" << c._num_atoms
                        << endl;
            }
            if (debug >= 3) {
                for (auto i = 0; i < channeldatacount; ++i) {
                    cerr << "channeldata[ " << std::setw(2) << std::right << i << "]"
                        << " " << channeldescriptor[i]._datatype
                        // << " _datatype=" << channeldescriptor[i]._datatype
                        // << " _atom_size=" << channeldescriptor[i]._atom_size
                        << ".data[" << datastartindex << "- +" << channeldescriptor[i]._num_atoms << "]"
                        // << " _num_atoms=" << channeldescriptor[i]._num_atoms
                        << "  ";
                    for (auto j = 0; j < channeldescriptor[i]._num_atoms; ++j) {
                        cerr << channeldata[i].data[j] << " ";
                        if (j >= 10) { cerr << "..."; break; }
                    }
                    cerr << endl;
                }
            }
            // Output the lines of data we read
            cout << table_from_data_csv(channeldescriptor);
            // Clear channeldata[].data
            for (auto c : channeldata) {
                c.data.clear();
            }
        }

        void interpret_chunk_eventdefinition()
        {
            static const string p = pfx(cnm + "::" + "interpret_chunk_eventdefinition");
            definitioncount = u.buffer32[4];
            xmldata.assign(reinterpret_cast<const char*>(&u.buffer32[5]));
            if (debug) {
                cerr << p << "definitioncount    int32_t  : " << definitioncount << " " << i2h(definitioncount) << endl;
                cerr << p << "xmldata            char[]   : " << xmldata.substr(0, 200) << " ..." << endl;
            }
            //
            // handle the XML
            static const char* rootname = "EventDefinitionData";
            auto doc = tinyxml2::load_document(xmldata);
            auto root = doc->FirstChildElement();
            if (! root) { cerr << p << "*** Root of XML not found" << endl; exit(1); }
            else if (strcmp(root->Name(), rootname)) {
                cerr << p << "*** <" << rootname << "> not found in doc, instead found " << root->Name() << endl;
                exit(1);
            }
            if (eventdefinition.size()) {
                cerr << p << "*** eventdefinition already allocated, has size " << eventdefinition.size() << endl;
                exit(1);
            }
            eventdefinition.resize(definitioncount);
            auto i = 0 * definitioncount;
            for (auto evdef : root)
            {
                for_each (cbegin(evdef), cend(evdef),
                        [this, i](auto x) {  // note the lambda
                        eventdefinition[i].eventdef_index = i;
                        if      MATCH_SET_STRING_VAR ( x, eventdefinition[i], Name)
                        else if MATCH_SET_STRING_VAR ( x, eventdefinition[i], Description)
                        else if MATCH_SET_CONVERT_VAR( x, eventdefinition[i], Class,          interpret_event_class)
                        else if MATCH_SET_CONVERT_VAR( x, eventdefinition[i], ID,             interpret_event_id)
                        else if MATCH_SET_CONVERT_VAR( x, eventdefinition[i], Type,           interpret_event_type)
                        else if MATCH_SET_CONVERT_VAR( x, eventdefinition[i], UsesIData1,     interpret_bool)
                        else if MATCH_SET_CONVERT_VAR( x, eventdefinition[i], UsesIData2,     interpret_bool)
                        else if MATCH_SET_CONVERT_VAR( x, eventdefinition[i], UsesDData1,     interpret_bool)
                        else if MATCH_SET_CONVERT_VAR( x, eventdefinition[i], UsesDData2,     interpret_bool)
                        else if MATCH_SET_CONVERT_VAR( x, eventdefinition[i], UsesDData3,     interpret_bool)
                        else if MATCH_SET_CONVERT_VAR( x, eventdefinition[i], UsesDData4,     interpret_bool)
                        else if MATCH_SET_STRING_VAR ( x, eventdefinition[i], DescriptionIData1)
                        else if MATCH_SET_STRING_VAR ( x, eventdefinition[i], DescriptionIData2)
                        else if MATCH_SET_STRING_VAR ( x, eventdefinition[i], DescriptionDData1)
                        else if MATCH_SET_STRING_VAR ( x, eventdefinition[i], DescriptionDData2)
                        else if MATCH_SET_STRING_VAR ( x, eventdefinition[i], DescriptionDData3)
                        else if MATCH_SET_STRING_VAR ( x, eventdefinition[i], DescriptionDData4)
                        else if MATCH_SET_STRING_VAR ( x, eventdefinition[i], Parameter1)
                        else if MATCH_SET_STRING_VAR ( x, eventdefinition[i], Parameter2)
                        else if MATCH_SET_STRING_VAR ( x, eventdefinition[i], Tolerance)
                        else if MATCH_SET_CONVERT_VAR( x, eventdefinition[i], UsesParameter1, interpret_bool)
                        else if MATCH_SET_CONVERT_VAR( x, eventdefinition[i], UsesParameter2, interpret_bool)
                        else if MATCH_SET_CONVERT_VAR( x, eventdefinition[i], UsesTolerance,  interpret_bool)
                        else if MATCH_SET_STRING_VAR ( x, eventdefinition[i], DescriptionParameter1)
                        else if MATCH_SET_STRING_VAR ( x, eventdefinition[i], DescriptionParameter2)
                        else if MATCH_SET_STRING_VAR ( x, eventdefinition[i], DescriptionTolerance)
                        else { cerr << "*** Unknown child of EventDefinition: " << x->Name() << endl; exit(1); }
                        });
                ++i;
            }
            if (i != definitioncount) { cerr << "*** observed eventdefs " << i << " does not match definitioncount " << definitioncount << endl; exit(1); }

            if (debug) {
                cerr << p << rootname << " name : " << root->Name() << endl;
                cerr << std::boolalpha;
                if (debug >= 3) {
                    for (auto c : eventdefinition) {
                        cerr << p << std::setw(3) << std::right << c.eventdef_index
                            << PRINT_VAR(c, Name)
                            << PRINT_VAR(c, Description)
                            << PRINT_VAR(c, Class)
                            << PRINT_VAR(c, ID)
                            << PRINT_VAR(c, Type);
                            // << endl;
#define PRINT_EVENTDEF_VAR2(_c_,_v_) \
                        if (! _c_.Uses ## _v_) { \
                            /* if (debug >= 3) cerr << p << std::setw(3) << std::right << _c_.eventdef_index << " " #_v_ " notused" << endl;*/ \
                            if (debug >= 3) cerr << " " #_v_ " notused" << endl; \
                        } else { \
                            /* cerr << p << std::setw(3) << std::right << _c_.eventdef_index */ \
                            cerr << PRINT_VAR(_c_,Uses##_v_) \
                                << PRINT_VAR(_c_,Description##_v_); \
                                /* << PRINT_VAR(_c_,Description##_v_) */ \
                                /* << endl; */ \
                        }
#define PRINT_EVENTDEF_VAR3(_c_,_v_) \
                        if (! _c_.Uses ## _v_) { \
                            /* if (debug >= 3) cerr << p << std::setw(3) << std::right << _c_.eventdef_index << " " #_v_ " notused" << endl; */ \
                            if (debug >= 3) cerr << " " #_v_ " notused" << endl; \
                        } else { \
                            /* cerr << p << std::setw(3) << std::right << _c_.eventdef_index */  \
                            cerr << PRINT_VAR(_c_,_v_) \
                                << PRINT_VAR(_c_,Uses##_v_) \
                                << PRINT_VAR(_c_,Description##_v_); \
                                /* << PRINT_VAR(_c_,Description##_v_) */ \
                                /* << endl; */ \
                        }
                        PRINT_EVENTDEF_VAR2(c, IData1);
                        PRINT_EVENTDEF_VAR2(c, IData2);
                        PRINT_EVENTDEF_VAR2(c, DData1);
                        PRINT_EVENTDEF_VAR2(c, DData2);
                        PRINT_EVENTDEF_VAR2(c, DData3);
                        PRINT_EVENTDEF_VAR2(c, DData4);
                        PRINT_EVENTDEF_VAR3(c, Parameter1);
                        PRINT_EVENTDEF_VAR3(c, Parameter2);
                        PRINT_EVENTDEF_VAR3(c, Tolerance);
                        cerr << endl;
                    }
                }
                // print channel names
                cerr << p << "XML is unpacked. " << definitioncount << " event definitions: eventdef_index:Class:ID:Type :" << endl;
                cerr << p;
                for (auto c : eventdefinition)
                    cerr << c.eventdef_index << ":" << c.Class << ":" << c.ID << ":" << c.Type << ", ";
                cerr << endl;
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
            int64_t indexcount = u.buffer64[2];  // the number of index entries in this chunk
            for (auto i = 0; i < indexcount; ++i) {
                index.emplace_back( this,
                                    u.buffer64[3 + (5*i)],  // constructs a new Index element
                                    u.buffer64[4 + (5*i)],
                                    u.buffer64[5 + (5*i)],
                                    u.buffer64[6 + (5*i)],
                                    u.buffer64[7 + (5*i)] );
            }
            if (debug) {
                cerr << p << "indexcount         int64_t  : " << i2h(indexcount) << " " << indexcount << endl;
                cerr << p << "there are a total of " << index.size() << " index entries now" << endl;
                cerr << p << "index_entries=" << index_entries << "  index.size()=" << index.size() << endl;
                cerr << p << "vector<Index>        index  : " << endl;
                for (auto c : index) {
                    cerr << c.out() << endl;
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
                case chunkid_header:          s = "header"; break;
                case chunkid_channelinfo:     s = "channelinfo"; break;
                case chunkid_data:            s = "data"; break;
                case chunkid_eventdefinition: s = "eventdefinition"; break;
                case chunkid_eventdata:       s = "eventdata"; break;
                case chunkid_index:           s = "index"; break;
                default:                      s = "UNKNOWN_" + i2h(id); break;
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

        bool interpret_bool(const string& s)
        {  // turn True and False into the corresponding bool value
            if (s == "True")        return true;
            else if (s == "False")  return false;
            else { cerr << "interpret_bool: unknown argument " << s << endl; exit(1); }
        }

        int32_t interpret_event_class(const string& s)
        {  // just one class, 0x0001 Data Translation Event
            if (atol(s.c_str()) == 1) return 0x0001;
            else { cerr << "interpret_event_class: unknown event class " << s << endl; exit(1); }
        }

        int32_t interpret_event_id(const string& s)
        {  // multiple per class
            if (auto id = atol(s.c_str())) return id;
            else { cerr << "interpret_event_id: event id is 0 or uninterpretable: " << s << endl; exit(1); }
        }

        int32_t interpret_int32(const string& s)
        {
            auto i = atol(s.c_str());
            // cerr << "interpret_int32: i=" << i << endl;
            return static_cast<int32_t>(i);
        }

        int16_t interpret_int16(const string& s)
        {
            auto i = atol(s.c_str());
            // cerr << "interpret_int16: i=" << i << endl;
            return static_cast<int16_t>(i);
        }

        double interpret_double(const string& s)
        {
            double d = atof(s.c_str());
            // cerr << "interpret_double: d=" << d << endl;
            return static_cast<double>(d);
        }

        string interpret_channel_type(const string& s)
        {  // three channel types, we should only ever see one
            string t = ToLower(s);
            if      (t == "randomdatachannel")   return "RandomDataChannel";
            // else if (t == "calculatedtimechannel")  return "CalculatedTimeChannel";
            // else if (t == "monotonicdatachannel")   return "MonotonicDataChannel";
            else { cerr << "interpret_channel_type: channel type unknown: " << s << endl; exit(1); }
        }

        string interpret_event_type(const string& s)
        {  // two event types
            string t = ToLower(s);
            if      (t == "point")   return "Point";
            //else if (t == "ranged")  return "Ranged";
            else { cerr << "interpret_event_type: event type unknown: " << s << endl; exit(1); }
        }

        Time interpret_time(const string& s)
        {  // fully interpret Time
            return Time(s);
        }

        string interpret_datatype(const string& s)
        {  // just int16 for now
            string t = ToLower(s);
            if      (t == "int16")   return "Int16";
            else { cerr << "interpret_datatype: datatype unknown: " << s << endl; exit(1); }
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
            if (file) {
                if (debug) cerr << p << filename << " : file is 'true'" << endl;
            } else {
                if (debug) cerr << p << filename << " : file is 'false'" << endl;
            }
            if (verbose) {
                if (debug) cerr << pv << filename << " is " << (o ? "" : "not ") << "open" << endl;
            }
            if (o) {
                here = file.tellg();
                auto here_chunks = static_cast<double>(here) / defaultchunksz;  // cast so division is double to catch fractional chunks
                if (debug) {
                    cerr << p << filename
                        << " filesize=" << filesize << " " << i2h(filesize) << " bytes"
                        << " curpos=" << here << " " << i2h(here) << " (" << here_chunks << " 64KB chunks from beg)"
                        << " curchunkfilepos=" << i2h(curchunkfilepos)
                        << " curchunksz=" << i2h(curchunksz)
                        << endl;
                }
            }
            //return o && here != filesize;
            return o;
        }

        void dump()
        {
            static const string p = pfx(cnm + "::" + "dump", 20);
            cerr << p << "defaultchunksz=" << defaultchunksz
                << " sizeof(int64_t)=" << sizeof(int64_t)
                << " int64_count=" << int64_count
                << " filename=" << filename
                << " pos=" << pos
                << endl;
            file_status(true);
        }

        void summarise_data()
        {
            static const string p = pfx(cnm + "::" + "summarise_data", 20);
            cerr << p << "channeldata.size()=" << channeldata.size()
                << endl;
            for (auto c : channeldata) {
                cerr << p << "[" << std::setw(2) << std::right << c._index << "]";
                cerr << " data.size()=" << c.data.size();
                auto i = 0, j = 20;
                for (auto d : c.data) {
                    cerr << " " << d;
                    if (++i >= j) { cerr << " ..."; break; }
                }
                cerr << endl;
            }
        }

        string table_header_csv(bool minimal = false, const string sep = DEFAULT_SEP) const
        {
            stringstream ss;
            if (! minimal) {
                ss << "RecordingDate :" << sep << recdate << endl
                    << "FromSample(TimeOfDay):" << sep << "xx:xx:xx.xxx" << endl
                    << "ToSample(TimeOfDay):" << sep << "yy:yy:yy.yyy" << endl
                    << "" << sep << "" << endl
                    << "Channels Recorded " << sep << "" << numberofchannels << endl
                    << "PerChannelSamplingFreq :" << sep << "" << setprecision(15) << channelinfo[0].PerChannelSampleRate << endl;
                if (do_downsample) {
                    ss << "DownsampleCount :" << sep << "" << downsample_count << endl;
                }
                ss << "" << sep << "" << endl;
                // channelinfo
                ss << "ChannelName" << sep << "ChannelNumber" << sep << "Units" << sep << "DataType" << sep 
                    << "RangeMin" << sep << "RangeMax" << sep << "DataScale" << sep << "DataOffset" << sep
                    << "SensorScale" << sep << "SensorOffset" << endl;
                for (auto c : channelinfo) {
                    ss << c.Name
                        << sep << c.DataIndex
                        << sep << c.Unit
                        << sep << c.DataType
                        << sep << c.RangeMin
                        << sep << c.RangeMax
                        << sep << c.DataScale
                        << sep << c.DataOffset
                        << sep << c.SensorScale
                        << sep << c.SensorOffset
                        << endl;
                }
                ss << "" << sep << "" << endl;
                if (include_data_line)
                    ss << "data_line" << sep;
            }
            for (auto i = 0; i < numberofchannels; ++i) {
                ss << channelinfo[i].Name;
                if (i < numberofchannels - 1)
                    ss << sep;
            }
            ss << endl;
            return ss.str();
        }
        string table_from_data_csv(const vector<ChannelDescriptor>& cd,
                                   const string sep = DEFAULT_SEP)
            // output all data in channeldata[] using channeldescriptor[]
        {
            if (table_data_lines == 0) { // this is the first data, so drop the header first
                cout << table_header_csv(true);
            }
            stringstream ss;
            // fetch the number of items from the first channel descriptor
            auto n = cd[0]._num_atoms;

            for (auto i = 0; i < n; ++i) {
                ++data_lines;  // the line of data in the table (all lines)
                if (do_downsample && (data_lines - 1) % downsample_count) {
                    // if we are downsampling, only output lines where (data_lines - 1) mod downsample_count == 0
                    // so skip this line
                    continue;
                }
                table_data_lines++;  // the line of data in the table (all lines)
                if (include_data_line)
                    ss << (data_lines - 0) << sep;
                for (auto j = 0; j < numberofchannels; ++j) {  // across each column/channel
                    ss << setprecision(15) << channelinfo[j].interpret_as_volts(channeldata[j].data[i]);
                    if (j < numberofchannels - 1)
                        ss << sep;
                }
                ss << endl;
            }
            return ss.str();
        }
};

std::ostream& operator<<(std::ostream& os, const HPFFile::Time& t)     { return os << t.out(); }
std::ostream& operator<<(std::ostream& os, const HPFFile::DataType& t) { return os << t.out(); }



int 
main(int argc, char* argv[])
{
    string file;
    if (argc > 1)
        file.assign(argv[1]);
    else
        cerr << "*** Must provide filename as only argument:  " << argv[0] << " file.hpf" << endl;
    HPFFile h(file);
    if (! h.file_status())
        exit(1);
    while (h.read_chunk());
  
    if (0) {  // for debugging; dump the first several chunks
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
        h.read_chunk();
    }
    //h.read_chunk();

    // h.summarise_data();
    //cout << "," << endl;
    //cout << "," << endl;
}


