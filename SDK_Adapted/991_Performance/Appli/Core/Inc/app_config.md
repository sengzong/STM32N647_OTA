# Input queue configuration

The performance variant retains three 320x320 RGB inputs and the enlarged queue
capacity from the first optimization. The input arrays are now in internal BSS;
the linker and build checks enforce available internal RAM and stack margin.
All translation units are rebuilt using this same header.
