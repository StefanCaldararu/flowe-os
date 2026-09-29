#include "NoteLayout.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <new>

#include "../Fonts.h"

namespace notes {
namespace {

constexpr int kMarginX = 20;
constexpr int kIndent = 24;       // per list level
constexpr int kQuoteStep = 14;    // per quote depth
constexpr int kBlankGap = 10;     // an empty source line
constexpr int kHeadingGap = 10;   // above a heading
constexpr int kRuleH = 20;        // --- block height

bool isBlank(const char* s) {
  for (; *s; s++)
    if (*s != ' ' && *s != '\t') return false;
  return true;
}

const char* skipWs(const char* s) {
  while (*s == ' ' || *s == '\t') s++;
  return s;
}

bool startsWith(const char* s, const char* p) { return strncmp(s, p, strlen(p)) == 0; }

// A lone * or _ that opens (text follows) or closes (text precedes)
// emphasis. Not snake_case's underscore, and not a spaced " * ".
bool isItalicMarker(const char* src, const char* p) {
  const bool wordBefore = p > src && isalnum(static_cast<uint8_t>(p[-1]));
  const bool wordAfter = isalnum(static_cast<uint8_t>(p[1]));
  if (*p == '_' && wordBefore && wordAfter) return false;
  const bool opens = p[1] && p[1] != ' ';
  const bool closes = p > src && p[-1] != ' ';
  return opens || closes;
}

bool isFence(const char* s) {
  s = skipWs(s);
  return startsWith(s, "```") || startsWith(s, "~~~");
}

// "---", "***", "___" (3+ of one char, spaces allowed).
bool isRule(const char* s) {
  s = skipWs(s);
  const char c = *s;
  if (c != '-' && c != '*' && c != '_') return false;
  int n = 0;
  for (; *s; s++) {
    if (*s == c) n++;
    else if (*s != ' ' && *s != '\t') return false;
  }
  return n >= 3;
}

// Table separator row "| --- | :-: |".
bool isTableRule(const char* s) {
  s = skipWs(s);
  if (*s != '|') return false;
  bool dash = false;
  for (; *s; s++) {
    if (*s == '-') dash = true;
    else if (*s != '|' && *s != ':' && *s != ' ' && *s != '\t') return false;
  }
  return dash;
}

// Decode one UTF-8 codepoint at s (n bytes left); returns its byte length.
int utf8At(const char* s, const size_t n, uint32_t& cp) {
  const uint8_t c = static_cast<uint8_t>(s[0]);
  int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
  if (static_cast<size_t>(len) > n) len = 1;
  cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
  for (int i = 1; i < len; i++) cp = (cp << 6) | (static_cast<uint8_t>(s[i]) & 0x3F);
  return len;
}

// ASCII stand-ins for common typography outside the UI fonts' ranges
// (U+0020..U+04FF). nullptr = drop the character (emoji, symbols).
const char* asciiFor(const uint32_t cp) {
  switch (cp) {
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: return "-";
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return "'";
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: return "\"";
    case 0x2026: return "...";
    case 0x2022: case 0x2023: case 0x25CF: case 0x25E6: return "*";
    case 0x2192: case 0x27F6: return "->";
    case 0x2190: case 0x27F5: return "<-";
    case 0x21D2: return "=>";
    case 0x2194: return "<->";
    case 0x2713: case 0x2714: case 0x2705: return "[x]";
    case 0x2260: return "!=";
    case 0x2264: return "<=";
    case 0x2265: return ">=";
    case 0x00A0: case 0x2002: case 0x2003: case 0x2009: case 0x202F: return " ";
    default: return nullptr;
  }
}

}  // namespace

NoteLayout::~NoteLayout() {
  delete[] _line;
  delete[] _disp;
  delete[] _attr;
}

bool NoteLayout::init() {
  if (!_line) _line = new (std::nothrow) char[kLineCap];
  if (!_disp) _disp = new (std::nothrow) char[kLineCap];
  if (!_attr) _attr = new (std::nothrow) uint8_t[kLineCap];
  return _line && _disp && _attr;
}

bool NoteLayout::linkTarget(LineReader& rd, const int i, char* out, const size_t n) const {
  if (i < 0 || i >= _pageLinks || n == 0) return false;
  const size_t want = _pageTargets[i].len < n - 1 ? _pageTargets[i].len : n - 1;
  const int got = rd.readAt(_pageTargets[i].offset, out, want);
  out[got > 0 ? got : 0] = 0;
  return got == static_cast<int>(want);
}

void NoteLayout::putText(const char* s, const size_t n, const uint8_t sty, const uint8_t lnk) {
  const uint8_t attr = static_cast<uint8_t>(sty | (lnk << kLinkShift));
  for (size_t i = 0; i < n;) {
    uint32_t cp;
    const int len = utf8At(s + i, n - i, cp);
    const char* out = s + i;
    size_t outLen = static_cast<size_t>(len);
    if (cp == '\t') {
      out = " ";
      outLen = 1;
    } else if (cp >= 0x500 || cp == 0xA0) {
      out = asciiFor(cp);
      outLen = out ? strlen(out) : 0;
    }
    if (static_cast<size_t>(_len) + outLen < kLineCap) {
      for (size_t k = 0; k < outLen; k++) {
        _disp[_len] = out[k];
        _attr[_len] = attr;
        _len++;
      }
    }
    i += static_cast<size_t>(len);
  }
}

// Record a link whose target is `len` bytes at `target` inside _line.
void NoteLayout::addLocalLink(const char* target, const size_t len, uint8_t& lnk) {
  lnk = 0;
  constexpr int kCap = sizeof(_local) / sizeof(_local[0]);
  if (_localCount >= kCap || len == 0 || target < _line || target + len > _line + kLineCap) return;
  _local[_localCount] = {static_cast<uint16_t>(target - _line), static_cast<uint16_t>(len)};
  lnk = static_cast<uint8_t>(++_localCount);
}

void NoteLayout::inlineToDisplay(const char* src, const uint8_t baseSty, const bool verbatim) {
  _len = 0;
  _localCount = 0;
  if (verbatim) {
    putText(src, strlen(src), baseSty, 0);
    _disp[_len] = 0;
    return;
  }
  bool bold = false;
  const char* p = src;
  auto prevIsWordChar = [&](const char* at) { return at > src && isalnum(static_cast<uint8_t>(at[-1])); };
  while (*p) {
    const uint8_t sty = static_cast<uint8_t>(baseSty | (bold ? kBold : 0));
    if (*p == '\\' && p[1] && ispunct(static_cast<uint8_t>(p[1]))) {  // escaped marker
      putText(p + 1, 1, sty, 0);
      p += 2;
    } else if (startsWith(p, "%%")) {  // %% comment %% (to the end when unclosed)
      const char* end = strstr(p + 2, "%%");
      p = end ? end + 2 : p + strlen(p);
    } else if (startsWith(p, "**") || (startsWith(p, "__") && !prevIsWordChar(p))) {
      bold = !bold;
      p += 2;
    } else if (startsWith(p, "~~") || startsWith(p, "==")) {
      p += 2;  // strikethrough / highlight: text only
    } else if ((*p == '*' || *p == '_') && isItalicMarker(src, p)) {
      p += 1;  // italic marker: the UI fonts have no italic face
    } else if (*p == '`') {
      const char* end = strchr(p + 1, '`');
      if (!end) {
        putText(p, 1, sty, 0);
        p++;
      } else {
        putText(p + 1, static_cast<size_t>(end - p - 1), static_cast<uint8_t>(sty | kCode), 0);
        p = end + 1;
      }
    } else if (startsWith(p, "[[") || startsWith(p, "![[")) {
      const char* open = p + (*p == '!' ? 3 : 2);
      const char* close = strstr(open, "]]");
      if (!close) {
        putText(p, 1, sty, 0);
        p++;
        continue;
      }
      const char* bar = static_cast<const char*>(memchr(open, '|', static_cast<size_t>(close - open)));
      const char* tEnd = bar ? bar : close;
      uint8_t lnk;
      addLocalLink(open, static_cast<size_t>(tEnd - open), lnk);
      if (bar) {
        putText(bar + 1, static_cast<size_t>(close - bar - 1), sty, lnk);
      } else {
        // Shown as typed, "Note#Heading" as "Note > Heading".
        for (const char* q = open; q < close; q++) {
          if (*q == '#') putText(" > ", 3, sty, lnk);
          else if (*q != '^') putText(q, 1, sty, lnk);
        }
      }
      p = close + 2;
    } else if (*p == '[' || (*p == '!' && p[1] == '[')) {
      // [text](target). External URLs show their text; vault paths link.
      const char* open = p + (*p == '!' ? 2 : 1);
      const char* close = strstr(open, "](");
      const char* end = close ? strchr(close + 2, ')') : nullptr;
      if (!close || !end || memchr(open, ']', static_cast<size_t>(close - open))) {
        putText(p, 1, sty, 0);
        p++;
        continue;
      }
      const char* target = close + 2;
      uint8_t lnk = 0;
      const size_t tLen = static_cast<size_t>(end - target);
      char probe[16] = {0};
      memcpy(probe, target, tLen < sizeof(probe) - 1 ? tLen : sizeof(probe) - 1);
      const bool external = strstr(probe, "://") || startsWith(probe, "mailto:");
      if (!external) addLocalLink(target, tLen, lnk);
      putText(open, static_cast<size_t>(close - open), sty, lnk);
      p = end + 1;
    } else {
      uint32_t cp;
      const int len = utf8At(p, strlen(p), cp);
      putText(p, static_cast<size_t>(len), sty, 0);
      p += len;
    }
  }
  _disp[_len] = 0;
}

// Text of _disp[a, b) measured in place (a NUL is parked at b and put back).
int NoteLayout::measure(Gfx& gfx, const int a, const int b) {
  int w = 0;
  for (int i = a; i < b;) {
    const uint8_t bold = _attr[i] & kBold;
    int j = i;
    while (j < b && (_attr[j] & kBold) == bold) j++;
    const char saved = _disp[j];
    _disp[j] = 0;
    w += gfx.textWidth(bold ? kFontBold : kFontRegular, _disp + i);
    _disp[j] = saved;
    i = j;
  }
  return w;
}

// Greedy word wrap of _disp into _brk (start index of each visual line).
// `anywhere`: break between any two characters (code, tables).
int NoteLayout::wrap(Gfx& gfx, const int width, const bool anywhere) {
  constexpr int kMaxLines = sizeof(_brk) / sizeof(_brk[0]);
  int n = 0;
  _brk[n++] = 0;
  const int spaceW = gfx.textWidth(kFontRegular, " ");
  int lineW = 0;
  int i = 0;
  while (i < _len && n < kMaxLines) {
    if (_disp[i] == ' ' && !anywhere) {
      if (i == _brk[n - 1] && n > 1) _brk[n - 1] = static_cast<uint16_t>(i + 1);  // no leading space
      else lineW += spaceW;
      i++;
      continue;
    }
    int j = i;
    if (anywhere) {
      uint32_t cp;
      j += utf8At(_disp + i, static_cast<size_t>(_len - i), cp);
    } else {
      while (j < _len && _disp[j] != ' ') j++;
    }
    const int w = measure(gfx, i, j);
    if (lineW + w <= width) {
      lineW += w;
      i = j;
      continue;
    }
    if (lineW > 0) {  // the word starts a new line
      _brk[n++] = static_cast<uint16_t>(i);
      lineW = 0;
      continue;
    }
    // Wider than a whole line on its own: hard-break at codepoints.
    int k = i;
    while (k < j) {
      uint32_t cp;
      const int step = utf8At(_disp + k, static_cast<size_t>(j - k), cp);
      if (k > i && measure(gfx, i, k + step) > width) break;
      k += step;
    }
    _brk[n++] = static_cast<uint16_t>(k);
    lineW = 0;
    i = k;
  }
  if (n > 1 && _brk[n - 1] >= _len) n--;  // no empty trailing line
  return n;
}

void NoteLayout::drawLine(Gfx& gfx, const int a, const int b, const int x0, const int y, const int selLink) {
  const int lineH = gfx.lineHeight(kFontRegular);
  int x = x0;
  for (int i = a; i < b;) {
    const uint8_t attr = _attr[i];
    int j = i;
    while (j < b && _attr[j] == attr) j++;
    const XpFont& f = (attr & kBold) ? kFontBold : kFontRegular;
    const int w = measure(gfx, i, j);
    bool ink = true;
    if (const uint8_t lnk = linkOf(attr)) {
      // First sight of this link on the page gives it the next page id.
      int8_t& id = _localToPage[lnk];
      if (id < 0 && _pageLinks < kMaxPageLinks) {
        const LocalLink& L = _local[lnk - 1];
        _pageTargets[_pageLinks] = {_lineStart + L.off, L.len};
        id = static_cast<int8_t>(_pageLinks++);
      }
      int textEnd = j;  // underline the words, not a trailing space
      while (textEnd > i && _disp[textEnd - 1] == ' ') textEnd--;
      const int textW = measure(gfx, i, textEnd);
      if (id >= 0 && id == selLink) {
        gfx.fillRect(x - 2, y + 1, textW + 4, lineH - 1, true);
        ink = false;
      } else {
        gfx.fillRect(x, y + f.ascender + 3, textW, 2, true);
      }
    }
    if (attr & kCode) {  // inline code: a dotted underline
      for (int dx = 0; dx < w; dx += 3) gfx.drawPixel(x + dx, y + f.ascender + 4, true);
    }
    const char saved = _disp[j];
    _disp[j] = 0;
    gfx.drawText(f, x, y, _disp + i, ink);
    _disp[j] = saved;
    x += w;
    i = j;
  }
}

bool NoteLayout::page(Gfx& gfx, LineReader& rd, const PageStart& from, const int top, const int bottom,
                      const bool draw, const int selLink, PageStart* next) {
  _pageLinks = 0;
  if (!_line || !_disp || !_attr) return false;
  const int lineH = gfx.lineHeight(kFontRegular);
  const int right = gfx.width() - kMarginX;
  uint8_t inCode = from.inCode, inFront = from.inFront;
  uint16_t skip = from.skip;
  int y = top;
  bool placed = false;
  if (!rd.seek(from.offset)) return false;
  uint32_t lineStart;
  while (rd.next(_line, kLineCap, lineStart)) {
    _lineStart = lineStart;
    const PageStart here{lineStart, 0, inCode, inFront};
    auto breakHere = [&](const uint16_t vi) {
      if (next) *next = PageStart{here.offset, vi, here.inCode, here.inFront};
      return true;
    };
    // --- frontmatter and code fences ---------------------------------------
    if (lineStart == 0 && startsWith(_line, "\xEF\xBB\xBF"))  // UTF-8 BOM
    {
      memmove(_line, _line + 3, strlen(_line + 3) + 1);
      _lineStart += 3;
    }
    if (lineStart == 0 && !strcmp(_line, "---")) {
      inFront = 1;
      continue;
    }
    if (inFront) {
      if (!strcmp(_line, "---") || !strcmp(_line, "...")) inFront = 0;
      continue;
    }
    if (isFence(_line)) {
      inCode = !inCode;
      continue;
    }

    // --- block structure ---------------------------------------------------
    int x = kMarginX;          // text start
    int quoteDepth = 0;
    const char* s = _line;
    bool verbatim = inCode, anywhere = inCode;
    uint8_t baseSty = 0;
    int headingLevel = 0;
    enum class Marker : uint8_t { None, Bullet, Number, Task, TaskDone } marker = Marker::None;
    char number[12] = {0};
    int markerX = 0, listLevel = 0;
    bool calloutTitle = false;

    if (!inCode) {
      // Quote / callout prefixes: "> > text".
      for (const char* q = skipWs(s); *q == '>'; q = skipWs(q)) {
        quoteDepth++;
        q++;
        if (*q == ' ') q++;
        s = q;
      }
      if (quoteDepth && startsWith(s, "[!")) {
        const char* close = strchr(s, ']');
        if (close) {
          // "[!note]- Title" -> "Title", "[!tip]" -> "Tip".
          const char* title = close + 1;
          if (*title == '-' || *title == '+') title++;
          title = skipWs(title);
          if (*title) {
            s = title;
          } else {
            const size_t n = static_cast<size_t>(close - s - 2);
            snprintf(number, sizeof(number), "%.*s", static_cast<int>(n < 11 ? n : 11), s + 2);
            if (number[0] >= 'a' && number[0] <= 'z') number[0] = static_cast<char>(number[0] - 32);
            s = number;
          }
          calloutTitle = true;
          baseSty = kBold;
        }
      }
      x += quoteDepth * kQuoteStep + (quoteDepth ? 6 : 0);

      if (isBlank(s)) {
        if (y > top) y += kBlankGap;
        skip = 0;
        continue;
      }
      if (!calloutTitle && isRule(s) && quoteDepth == 0) {
        if (y + kRuleH > bottom && placed) return breakHere(0);
        if (draw) gfx.fillRect(kMarginX, y + kRuleH / 2 - 1, right - kMarginX, 2, true);
        y += kRuleH;
        placed = true;
        continue;
      }
      if (isTableRule(s)) continue;
      const char* t = skipWs(s);
      if (!calloutTitle && *t == '#') {
        int h = 0;
        while (t[h] == '#') h++;
        if (h <= 6 && t[h] == ' ') {
          headingLevel = h;
          baseSty = kBold;
          s = t + h + 1;
        }
      }
      if (!headingLevel && !calloutTitle) {
        if (*t == '|') {
          anywhere = true;
          verbatim = true;
          s = t;
        } else {
          // List items. Indent: a tab or four spaces per level (two-space
          // indents count too when a note uses them).
          int spaces = 0, tabs = 0;
          for (const char* q = s; *q == ' ' || *q == '\t'; q++) (*q == '\t' ? tabs : spaces)++;
          listLevel = tabs + (spaces >= 4 ? spaces / 4 : spaces / 2);
          if (listLevel > 6) listLevel = 6;
          if ((*t == '-' || *t == '*' || *t == '+') && t[1] == ' ') {
            marker = Marker::Bullet;
            s = t + 2;
            if (s[0] == '[' && s[1] && s[2] == ']' && s[3] == ' ') {
              marker = s[1] == ' ' ? Marker::Task : Marker::TaskDone;
              s += 4;
            }
          } else if (isdigit(static_cast<uint8_t>(*t))) {
            int d = 0;
            while (isdigit(static_cast<uint8_t>(t[d])) && d < 9) d++;
            if ((t[d] == '.' || t[d] == ')') && t[d + 1] == ' ') {
              marker = Marker::Number;
              snprintf(number, sizeof(number), "%.*s.", d, t);
              s = t + d + 2;
            }
          }
          if (marker != Marker::None) {
            markerX = x + listLevel * kIndent;
            x = markerX + (marker == Marker::Number ? gfx.textWidth(kFontRegular, number) + 8 : kIndent);
          } else {
            s = t;  // a plain paragraph line: no leading whitespace
          }
        }
      }
    } else {
      x += 14;  // code block body
    }

    inlineToDisplay(s, baseSty, verbatim);
    for (int8_t& id : _localToPage) id = -1;
    _brk[0] = 0;
    const int nLines = _len ? wrap(gfx, right - x, anywhere) : 1;

    if (headingLevel && skip == 0 && y > top) y += kHeadingGap;
    const int ruleAfter = headingLevel == 1 ? 8 : headingLevel == 2 ? 6 : 0;
    for (int vi = skip; vi < nLines; vi++) {
      if (y + lineH > bottom && placed) return breakHere(static_cast<uint16_t>(vi));
      if (draw) {
        for (int d = 0; d < quoteDepth; d++) gfx.fillRect(kMarginX + d * kQuoteStep, y, 3, lineH, true);
        if (inCode) {  // code block: a dotted bar down the left edge
          for (int dy = 0; dy < lineH; dy += 2) gfx.drawPixel(kMarginX + 2, y + dy, true);
        }
        if (vi == 0) {
          const int midY = y + kFontRegular.ascender - 7;
          switch (marker) {
            case Marker::Bullet: {
              constexpr int d = 8;
              if (listLevel % 2 == 0) gfx.fillRoundedRect(markerX + 4, midY - d / 2 + 1, d, d, d / 2, true);
              else gfx.drawRoundedRect(markerX + 4, midY - d / 2 + 1, d, d, d / 2, 2, true);
              break;
            }
            case Marker::Number:
              gfx.drawText(kFontRegular, markerX, y, number);
              break;
            case Marker::Task:
            case Marker::TaskDone: {
              constexpr int b = 16;
              const int by = midY - b / 2 + 1;
              gfx.drawRect(markerX, by, b, b, 2, true);
              if (marker == Marker::TaskDone) {
                gfx.drawLine(markerX + 3, by + 8, markerX + 7, by + 12, 2, true);
                gfx.drawLine(markerX + 7, by + 12, markerX + 13, by + 3, 2, true);
              }
              break;
            }
            case Marker::None:
              break;
          }
        }
        const int a = _brk[vi];
        const int b = vi + 1 < nLines ? _brk[vi + 1] : _len;
        drawLine(gfx, a, b, x, y, selLink);
      }
      y += lineH;
      placed = true;
    }
    skip = 0;
    if (ruleAfter) {
      if (draw && y + ruleAfter <= bottom)
        gfx.fillRect(kMarginX, y + 1, right - kMarginX, headingLevel == 1 ? 3 : 1, true);
      y += ruleAfter;
    }
  }
  return false;
}

}  // namespace notes
