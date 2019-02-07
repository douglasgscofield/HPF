HPF
===

Convert HPF data from QuickDAQ monitoring machine to TXT or CSV.
Too-large files cannot be converted with the tool provided with QuickDAQ.
The output format should be identical to QuickDAQ's CSV, at the least.
Also, it will provide the ability to subsample from the records.
The large HPF files that motivate this project were recorded at 1khz, when 1hz was desired.
This tool will do this downsampling as well.

This is also an exercise on handling binary fines and parsing data in XML format in C++.
Anyway, that's my justification for clumsiness in the code :-)



HPF file format
---------------

For documentation of generic HPF format, I'm using a spec provided as a [downloadable downument](https://forums.ni.com/ni/attachments/ni/170/813238/1/high_performance_file_format_spec%5B1%5D.pdf) from a [National Instruments, Inc. LabView 8.5 forum](https://forums.ni.com/t5/LabVIEW/Reading-hpf-file-LabVIEW-8-5/td-p/2757308) that is copyright (c) 2007 Data Translation, Inc.
It is working so far, though I'm sure to run into some QuickDAQ-specific issues soon; for one thing, it seems not all chunks in a QuickDAQ HPF file are 64KB.
I haven't been able to find similar QuickDAQ docs which provide similar information.

As I learn them, I'll include QuickDAQ specifics here.



XML
---

Much of the data within the HPF file is held in [XML][XML]-format records.
In order to parse this easily, I use [TinyXML2][TinyXML2] and the [TinyXML2-ex][TinyXML2-ex] extensions.
These are included as submodules of this repository, and the compilation (will) include `-I` options which reflect this.
This is best since `#include "tixml2ex.h"` does a `#include "tinyxml2.h"` of its own.


[XML]: https://en.wikipedia.org/wiki/XML
[TinyXML2]: https://github.com/leethomason/tinyxml2
[TinyXML2-ex]: https://github.com/stanthomas/tinyxml2-ex

