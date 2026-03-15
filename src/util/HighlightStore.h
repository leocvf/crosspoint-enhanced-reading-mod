// --- HIGHLIGHT MODE ---
#pragma once

#include <Epub/Page.h>
#include <HalStorage.h>

#include <memory>
#include <string>
#include <vector>

namespace HighlightStore {

// Directory where highlights are saved on SD card
constexpr const char* HIGHLIGHT_DIR = "/highlights";

struct SavedHighlight {
  int spineIndex;
  int page;  // 0-indexed chapter page
  std::string text;
};

/**
 * Ensure the /highlights/ directory exists on SD card.
 * Returns true on success.
 */
bool ensureDir();

/**
 * Save a highlight to the SD card.
 *
 * @param title      Book title
 * @param author     Book author
 * @param spineIndex Current spine index (chapter number)
 * @param chapterName Chapter/section name (from TOC)
 * @param currentPage Current page within chapter
 * @param totalPages  Total pages in chapter
 * @param progressPercent Book-wide progress percentage (0-100)
 * @param highlightedText The extracted text of the highlight
 * @return true on success
 */
bool saveHighlight(const std::string& title, const std::string& author, int spineIndex,
                   const std::string& chapterName, int currentPage, int totalPages,
                   float progressPercent, const std::string& highlightedText);

/**
 * Find the precise bounds of a saved highlight on a page, by matching
 * the first and last words of the highlight text against the page lines.
 * Returns start/end line indices AND char offsets within those lines,
 * suitable for trimmed bar rendering identical to the live SELECT renderer.
 * outEndChar is one-past the last character of the last word (-1 = full line).
 * Returns false if no match found.
 */
bool findHighlightBounds(const Page& page, const std::string& text,
                         int& outStartLine, int& outStartChar,
                         int& outEndLine, int& outEndChar);

/**
 * Load highlights for a specific page of a book.
 * Reads all .txt files in /highlights/ and returns those matching title+spineIndex+page.
 */
std::vector<SavedHighlight> loadHighlightsForPage(const std::string& title, int spineIndex, int page);

/**
 * Count the number of text lines (PageLine elements) on a page.
 */
int countTextLines(const Page& page);

/**
 * Get the concatenated text of a single text line (all words joined with spaces).
 */
std::string getLineText(const Page& page, int textLineIndex);

/**
 * Get the Y position and height of a text line for rendering overlays.
 * Returns false if the line index is out of range.
 */
bool getLineGeometry(const Page& page, int fontId, int textLineIndex,
                     int16_t& outY, int16_t& outHeight);

/**
 * Get the leftmost X position and rightmost extent of a text line.
 * Returns false if the line index is out of range.
 */
bool getLineXExtent(const Page& page, int fontId, int textLineIndex,
                    int16_t& outXStart, int16_t& outXEnd);

/**
 * Extract highlighted text from page elements given a selection range.
 * Character offsets are applied to the concatenated word text of each line.
 * endCharOffset of -1 means "to end of line".
 *
 * @param page          The current Page
 * @param startLine     First selected text line index
 * @param startChar     Character offset into the first line's text
 * @param endLine       Last selected text line index
 * @param endChar       Character offset into the last line's text (-1 = end)
 * @return The extracted text with lines joined by spaces, trimmed to sentence boundaries
 */
std::string extractText(const Page& page, int startLine, int startChar, int endLine, int endChar);

/**
 * Find the character offset of the first sentence start on a line.
 * Looks for the beginning of the line or the first char after a sentence-ending
 * punctuation mark (. ! ?) followed by whitespace.
 */
int findFirstSentenceStart(const std::string& lineText);

/**
 * Find the character offset of the last sentence end on a line.
 * Returns the offset just past the sentence-ending punctuation (. ! ?).
 * Returns -1 if no sentence ending is found.
 */
int findLastSentenceEnd(const std::string& lineText);

}  // namespace HighlightStore
// --- HIGHLIGHT MODE ---
