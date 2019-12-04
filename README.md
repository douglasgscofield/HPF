HPF
===

Convert HPF data from QuickDAQ monitoring machine to TXT or CSV.
The tool provided by QuickDAQ does not work with files that are 'too large'.
The output format should be identical to QuickDAQ's CSV, at the least.
Also, it will provide the ability to subsample from the records.
The large HPF files that motivate this project were recorded at 1 khz, when 1 hz was desired.
This tool handles this downsampling as well.
It is not a general tool for handling input records, but could be turned into that with some more work, and direct experience with QuickDAQ's instrument.

This is also an exercise on handling binary fines and parsing data in XML format in C++.
Anyway, that's my excuse for clumsiness in the code :-)



HPF file format
---------------

For documentation of generic HPF format, I'm using a spec provided as a [downloadable downument](https://forums.ni.com/ni/attachments/ni/170/813238/1/high_performance_file_format_spec%5B1%5D.pdf) from a [National Instruments, Inc. LabView 8.5 forum](https://forums.ni.com/t5/LabVIEW/Reading-hpf-file-LabVIEW-8-5/td-p/2757308) that is copyright (c) 2007 Data Translation, Inc.
It is working so far, though I'm sure to run into some QuickDAQ-specific issues soon.
For one thing, it seems not all chunks in a QuickDAQ HPF file are 64KB.
I haven't been able to find similar QuickDAQ docs which provide similar information.

As I learn them, I'll include QuickDAQ specifics here.

All six chunk types defined in the document are now read and partially interpreted.
The document also names a trigger chunk, but provides no definition.
The test file does not contain index chunks that I have found so far, and I have not yet found an eventdata chunk, but perhaps that is part of the data chunk.
In the list below, chunk types are marked with tags that indicate special data that remains to be further interpreted.

* header (xml for RecordingDate)
* channelinfo (xml for ChannelInformationData)
* data (structured data)
* eventdefinition (xml for EventDefinitionData)
* eventdata (structured data)
* index (structured data indexing data into chunk positions)


XML
---

Much of the data within the HPF file is held in [XML][XML]-format records.
In order to parse this easily, I use [TinyXML2][TinyXML2] and the [TinyXML2-ex][TinyXML2-ex] extensions.
These are included as submodules of this repository, and the compilation includes `-I` options which reflect this.
It is best handled this way since `#include "tixml2ex.h"` does a `#include "tinyxml2.h"` of its own.


[XML]: https://en.wikipedia.org/wiki/XML
[TinyXML2]: https://github.com/leethomason/tinyxml2
[TinyXML2-ex]: https://github.com/stanthomas/tinyxml2-ex

