// WODLE-PORT addition (not upstream): cached chapter list for a Txt book.
#pragma once

#include <vector>

#include "Txt.h"
#include "TxtChapterScanner.h"

namespace TxtChapters {

// Load the chapter list from <cachePath>/chapters.bin, or scan the (already
// UTF-8) book content once and write the cache. Returns empty when the book
// has no recognizable headings. txt must be load()ed.
std::vector<TxtChapter> loadOrScan(const Txt& txt);

}  // namespace TxtChapters
