// Link stub: Txt.cpp references JpegToBmpConverter::jpegFileToBmpStream for
// cover generation, which is irrelevant to encoding tests (JPEGDEC stays out
// of the host build).
#include <JpegToBmpConverter.h>

bool JpegToBmpConverter::jpegFileToBmpStream(HalFile&, Print&, bool) { return false; }
