/* WODLE-PORT: Arduino String shim backed by std::string.
 * Covers the API surface CrossPoint actually uses; extend as the compiler
 * demands rather than chasing full Arduino fidelity. */
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

class String
{
public:
    String() = default;
    String(const char *s) : _s(s ? s : "") {}
    String(const char *s, size_t n) : _s(s ? std::string(s, n) : std::string()) {}
    /* explicit: upstream Arduino String has no std::string ctor; an implicit
     * one makes calls like hasJpgExtension(std::string) ambiguous between the
     * string_view and const String& overloads. */
    explicit String(const std::string &s) : _s(s) {}
    explicit String(std::string &&s) : _s(std::move(s)) {}
    String(char c) : _s(1, c) {}
    explicit String(int v, int base = 10) { fromLong(v, base); }
    explicit String(unsigned int v, int base = 10) { fromULong(v, base); }
    explicit String(long v, int base = 10) { fromLong(v, base); }
    explicit String(unsigned long v, int base = 10) { fromULong(v, base); }
    explicit String(float v, int decimals = 2) { fromDouble(v, decimals); }
    explicit String(double v, int decimals = 2) { fromDouble(v, decimals); }

    /* --- capacity / access ------------------------------------------------ */
    size_t length() const { return _s.length(); }
    bool isEmpty() const { return _s.empty(); }
    const char *c_str() const { return _s.c_str(); }
    char charAt(size_t i) const { return i < _s.size() ? _s[i] : '\0'; }
    char operator[](size_t i) const { return charAt(i); }
    char &operator[](size_t i) { return _s[i]; }
    void reserve(size_t n) { _s.reserve(n); }
    const std::string &str() const { return _s; } /* port-only helper */

    /* --- concat ----------------------------------------------------------- */
    String &operator+=(const String &o) { _s += o._s; return *this; }
    String &operator+=(const char *o) { if (o) _s += o; return *this; }
    String &operator+=(char c) { _s += c; return *this; }
    String &operator+=(int v) { _s += String(v)._s; return *this; }
    String &operator+=(unsigned v) { _s += String(v)._s; return *this; }
    String &operator+=(long v) { _s += String(v)._s; return *this; }
    String &operator+=(unsigned long v) { _s += String(v)._s; return *this; }
    bool concat(const String &o) { _s += o._s; return true; }
    bool concat(const char *o) { if (o) _s += o; return true; }
    bool concat(char c) { _s += c; return true; }

    /* ArduinoJson's Writer<::String> appends via write() */
    size_t write(uint8_t c) { _s += (char)c; return 1; }
    size_t write(const uint8_t *buf, size_t n) { _s.append((const char *)buf, n); return n; }

    friend String operator+(String a, const String &b) { a += b; return a; }
    friend String operator+(String a, const char *b) { a += b; return a; }
    friend String operator+(const char *a, const String &b) { String r(a); r += b; return r; }
    friend String operator+(String a, char b) { a += b; return a; }

    /* --- compare ----------------------------------------------------------- */
    bool equals(const String &o) const { return _s == o._s; }
    bool equals(const char *o) const { return o && _s == o; }
    bool equalsIgnoreCase(const String &o) const
    {
        if (_s.size() != o._s.size()) return false;
        return strcasecmp(_s.c_str(), o._s.c_str()) == 0;
    }
    int compareTo(const String &o) const { return strcmp(_s.c_str(), o._s.c_str()); }
    bool operator==(const String &o) const { return _s == o._s; }
    bool operator==(const char *o) const { return equals(o); }
    bool operator!=(const String &o) const { return _s != o._s; }
    bool operator!=(const char *o) const { return !equals(o); }
    bool operator<(const String &o) const { return _s < o._s; }
    bool operator>(const String &o) const { return _s > o._s; }

    bool startsWith(const String &p) const { return _s.rfind(p._s, 0) == 0; }
    bool endsWith(const String &p) const
    {
        return _s.size() >= p._s.size() &&
               _s.compare(_s.size() - p._s.size(), p._s.size(), p._s) == 0;
    }

    /* --- search ------------------------------------------------------------ */
    int indexOf(char c, size_t from = 0) const
    {
        auto p = _s.find(c, from);
        return p == std::string::npos ? -1 : (int)p;
    }
    int indexOf(const String &sub, size_t from = 0) const
    {
        auto p = _s.find(sub._s, from);
        return p == std::string::npos ? -1 : (int)p;
    }
    int lastIndexOf(char c) const
    {
        auto p = _s.rfind(c);
        return p == std::string::npos ? -1 : (int)p;
    }
    int lastIndexOf(const String &sub) const
    {
        auto p = _s.rfind(sub._s);
        return p == std::string::npos ? -1 : (int)p;
    }

    /* --- slicing / mutation ------------------------------------------------ */
    String substring(size_t begin) const
    {
        return begin >= _s.size() ? String() : String(_s.substr(begin));
    }
    String substring(size_t begin, size_t end) const
    {
        if (begin >= _s.size() || end <= begin) return String();
        return String(_s.substr(begin, end - begin));
    }
    void remove(size_t index)
    {
        if (index < _s.size()) _s.erase(index);
    }
    void remove(size_t index, size_t count)
    {
        if (index < _s.size()) _s.erase(index, count);
    }
    void replace(const String &find, const String &repl)
    {
        if (find._s.empty()) return;
        size_t pos = 0;
        while ((pos = _s.find(find._s, pos)) != std::string::npos)
        {
            _s.replace(pos, find._s.size(), repl._s);
            pos += repl._s.size();
        }
    }
    void replace(char find, char repl)
    {
        for (auto &c : _s)
            if (c == find) c = repl;
    }
    void toLowerCase()
    {
        for (auto &c : _s)
            if (c >= 'A' && c <= 'Z') c += 32;
    }
    void toUpperCase()
    {
        for (auto &c : _s)
            if (c >= 'a' && c <= 'z') c -= 32;
    }
    void trim()
    {
        size_t b = _s.find_first_not_of(" \t\r\n");
        size_t e = _s.find_last_not_of(" \t\r\n");
        _s = (b == std::string::npos) ? std::string() : _s.substr(b, e - b + 1);
    }

    /* --- conversion --------------------------------------------------------- */
    long toInt() const { return strtol(_s.c_str(), nullptr, 10); }
    float toFloat() const { return strtof(_s.c_str(), nullptr); }
    double toDouble() const { return strtod(_s.c_str(), nullptr); }
    void toCharArray(char *buf, size_t size) const
    {
        if (!buf || !size) return;
        size_t n = _s.size() < size - 1 ? _s.size() : size - 1;
        memcpy(buf, _s.data(), n);
        buf[n] = '\0';
    }
    void getBytes(unsigned char *buf, size_t size) const { toCharArray((char *)buf, size); }

private:
    void fromLong(long v, int base)
    {
        char buf[34];
        if (base == 16) snprintf(buf, sizeof(buf), "%lx", v);
        else if (base == 2) { /* rare; fall back to decimal */ snprintf(buf, sizeof(buf), "%ld", v); }
        else snprintf(buf, sizeof(buf), "%ld", v);
        _s = buf;
    }
    void fromULong(unsigned long v, int base)
    {
        char buf[34];
        if (base == 16) snprintf(buf, sizeof(buf), "%lx", v);
        else snprintf(buf, sizeof(buf), "%lu", v);
        _s = buf;
    }
    void fromDouble(double v, int decimals)
    {
        char buf[48];
        snprintf(buf, sizeof(buf), "%.*f", decimals, v);
        _s = buf;
    }

    std::string _s;
};
