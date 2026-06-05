// Link stubs: Epub.cpp references the cover/thumbnail image converters, which
// are irrelevant to the parsing tests (keeps JPEGDEC/PNGdec out of the host
// build).
#include <JpegToBmpConverter.h>
#include <PngToBmpConverter.h>

bool JpegToBmpConverter::jpegFileToBmpStream(HalFile&, Print&, bool) { return false; }
bool JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(HalFile&, Print&, int, int) { return false; }
bool PngToBmpConverter::pngFileToBmpStream(HalFile&, Print&, bool) { return false; }
bool PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(HalFile&, Print&, int, int) { return false; }
