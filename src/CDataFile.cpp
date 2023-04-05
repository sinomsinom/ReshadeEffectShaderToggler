//
// CDataFile Class Implementation
//
// The purpose of this class is to provide a simple, full featured means to
// store persistent data to a text file.  It uses a simple key/value paradigm
// to achieve this.  The class can read/write to standard Windows .ini files,
// and yet does not rely on any windows specific calls.  It should work as
// well in a linux environment (with some minor adjustments) as it does in
// a Windows one.
//
// Written July, 2002 by Gary McNickle <gary#sunstorm.net>
// If you use this class in your application, credit would be appreciated.
//

//
// CDataFile
// The purpose of this class is to provide the means to easily store key/value
// pairs in a config file, seperated by independant sections. Sections may not
// have duplicate keys, although two or more sections can have the same key.
// Simple support for comments is included. Each key, and each section may have
// it's own multiline comment.
//
// An example might look like this;
//
// [UserSettings]
// Name=Joe User
// Date of Birth=12/25/01
//
// ;
// ; Settings unique to this server
// ;
// [ServerSettings]
// Port=1200
// IP_Address=127.0.0.1
// MachineName=ADMIN
//
#include "stdafx.h"
#include <vector>
#include <string>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <fstream>
#include <float.h>
#include <ranges>
#include <optional>
#include <format>

#ifdef WIN32
#include <windows.h>
#endif

#include "CDataFile.h"

// Compatibility Defines ////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
#ifdef WIN32
#define snprintf  _snprintf
#define vsnprintf _vsnprintf
#endif


// CDataFile
// Our default contstructor.  If it can load the file, it will do so and populate
// the section list with the values from the file.
CDataFile::CDataFile(const std::filesystem::path& filePath, bool saveOnClose):
    m_bDirty(false),
    m_filePath(filePath),
    m_Flags((AUTOCREATE_SECTIONS | AUTOCREATE_KEYS)),
    m_saveOnClose(saveOnClose)
{
    m_Sections.emplace_back(t_Section());

    Load(m_filePath);
}

CDataFile::CDataFile(bool saveOnClose)
{
    Clear();
    m_Flags = (AUTOCREATE_SECTIONS | AUTOCREATE_KEYS);
    m_Sections.emplace_back(t_Section());
}

// ~CDataFile
// Saves the file if any values have changed since the last save.
CDataFile::~CDataFile()
{
    if (m_bDirty && m_saveOnClose)
        Save();
}

// Clear
// Resets the member variables to their defaults
void CDataFile::Clear()
{
    m_bDirty = false;
    m_filePath = "";
    m_Sections.clear();
}

// SetFileName
// Set's the m_szFileName member variable. For use when creating the CDataFile
// object by hand (-vs- loading it from a file
void CDataFile::SetFileName(const std::filesystem::path& filePath)
{
    const std::string filePathStr = filePath.string();
    const std::string m_filePathStr = m_filePath.string();
    if (!m_filePath.empty() && !CompareNoCase(filePathStr, m_filePathStr))
    {
        m_bDirty = true;

        Report(E_WARN, "[CDataFile::SetFileName] The filename has changed from <{}> to <{}>.",
            m_filePathStr, filePathStr);
    }

    m_filePath = filePath;
}

// Load
// Attempts to load in the text file. If successful it will populate the 
// Section list with the key/value pairs found in the file. Note that comments
// are saved so that they can be rewritten to the file later.
bool CDataFile::Load(const std::filesystem::path& filePath)
{
    // We dont want to create a new file here.  If it doesn't exist, just
    // return false and report the failure.
    std::fstream File(filePath, std::ios::in);

    if (File.is_open())
    {
        bool bDone = false;
        bool bAutoKey = (m_Flags & AUTOCREATE_KEYS) == AUTOCREATE_KEYS;
        bool bAutoSec = (m_Flags & AUTOCREATE_SECTIONS) == AUTOCREATE_SECTIONS;

        std::string szLine;
        std::string szComment;
        char buffer[MAX_BUFFER_LEN];
        t_Section* pSection = GetSection("");

        // These need to be set, we'll restore the original values later.
        m_Flags |= AUTOCREATE_KEYS;
        m_Flags |= AUTOCREATE_SECTIONS;

        while (!bDone)
        {
            memset(buffer, 0, MAX_BUFFER_LEN);
            File.getline(buffer, MAX_BUFFER_LEN);

            szLine = buffer;
            szLine = Trim(szLine);

            bDone = (File.eof() || File.bad() || File.fail());

            if (szLine.find_first_of(CommentIndicators) == 0)
            {
                szComment += "\n";
                szComment += szLine;
            }
            else
                if (szLine.find_first_of('[') == 0) // new section
                {
                    szLine.erase(0, 1);
                    szLine.erase(szLine.find_last_of(']'), 1);

                    CreateSection(szLine, szComment);
                    pSection = GetSection(szLine);
                    szComment = "";
                }
                else
                    if (szLine.size() > 0) // we have a key, add this key/value pair
                    {
                        std::string szKey = GetNextWord(szLine);
                        std::string szValue = szLine;

                        if (szKey.size() > 0 && szValue.size() > 0)
                        {
                            SetValue(szKey, szValue, szComment, pSection->szName);
                            szComment = "";
                        }
                    }
        }

        // Restore the original flag values.
        if (!bAutoKey)
            m_Flags &= ~AUTOCREATE_KEYS;

        if (!bAutoSec)
            m_Flags &= ~AUTOCREATE_SECTIONS;
    }
    else
    {
        Report(E_INFO, "[CDataFile::Load] Unable to open file. Does it exist?");
        return false;
    }

    File.close();

    return true;
}


// Save
// Attempts to save the Section list and keys to the file. Note that if Load
// was never called (the CDataFile object was created manually), then you
// must set the m_szFileName variable before calling save.
bool CDataFile::Save()
{
    if (KeyCount() == 0 && SectionCount() == 0)
    {
        // no point in saving
        Report(E_INFO, "[CDataFile::Save] Nothing to save.");
        return false;
    }

    if (m_filePath.empty())
    {
        Report(E_ERROR, "[CDataFile::Save] No filename has been set.");
        return false;
    }

    std::fstream File(m_filePath, std::ios::out | std::ios::trunc);

    if (File.is_open())
    {
        for (const auto& Section : m_Sections)
        {
            bool isComment = !Section.szComment.empty();
            bool isSection = !Section.szName.empty();

            if (isComment)
            {
                WriteLn(File, "\n{}", CommentStr(Section.szComment));
                //File << std::format("\n{}\n", CommentStr(Section.szComment));
            }

            if (isSection)
            {
                WriteLn(File, "{}[{}]",
                    isComment ? "" : "\n",
                    Section.szName);
                //File << std::format("{}[{}]\n", isComment ? "" : "\n", Section.szName);
            }

            for (const auto& [key,value,comment] : Section.Keys)
            {
                if (!key.empty() && !value.empty())
                {
                    bool isComment = !comment.empty();
                    WriteLn(File, "{}{}{}{}{}{}",
                        isComment ? "\n" : "",
                        CommentStr(comment),
                        isComment ? "\n" : "",
                        key,
                        EqualIndicators[0],
                        value);
                }
            }
        }

    }
    else
    {
        Report(E_ERROR, "[CDataFile::Save] Unable to save file.");
        return false;
    }

    m_bDirty = false;

    return true;
}

// SetKeyComment
// Set the comment of a given key. Returns true if the key is not found.
bool CDataFile::SetKeyComment(std::string_view szKey, std::string_view szComment, std::string_view szSection)
{
    t_Section* pSection = GetSection(szSection);

    if (pSection == nullptr)
        return false;

    for (auto& key : pSection->Keys)
    {
        if (CompareNoCase(key.szKey, szKey))
        {
            key.szComment = szComment;
            m_bDirty = true;
            return true;
        }
    }

    return false;

}


// SetSectionComment
// Set the comment for a given section. Returns false if the section
// was not found.
bool CDataFile::SetSectionComment(std::string_view szSection, std::string_view szComment)
{

    for (auto& section :m_Sections)
    {
        if (CompareNoCase(section.szName, szSection))
        {
            section.szComment = szComment;
            m_bDirty = true;
            return true;
        }
    }

    return false;
}


// SetValue
// Given a key, a value and a section, this function will attempt to locate the
// Key within the given section, and if it finds it, change the keys value to
// the new value. If it does not locate the key, it will create a new key with
// the proper value and place it in the section requested.
bool CDataFile::SetValue(std::string_view szKey, std::string_view szValue, std::string_view szComment, std::string_view szSection)
{
    t_Key* pKey = GetKey(szKey, szSection);
    t_Section* pSection = GetSection(szSection);

    if (pSection == nullptr)
    {
        if (!(m_Flags & AUTOCREATE_SECTIONS) || !CreateSection(szSection, ""))
            return false;

        pSection = GetSection(szSection);
    }

    // Sanity check...
    if (pSection == nullptr)
        return false;

    // if the key does not exist in that section, and the value passed 
    // is not "" then add the new key.
    if (pKey == nullptr && !szValue.empty() && (m_Flags & AUTOCREATE_KEYS))
    {
        m_bDirty = true;

        pSection->Keys.emplace_back(szKey, szValue, szComment);

        return true;
    }

    if (pKey != nullptr)
    {
        pKey->szValue = szValue;
        pKey->szComment = szComment;

        m_bDirty = true;

        return true;
    }

    return false;
}

// SetFloat
// Passes the given float to SetValue as a std::string
bool CDataFile::SetFloat(std::string_view szKey, float fValue, std::string_view szComment, std::string_view szSection)
{
    return SetValue(szKey, std::format("{}", fValue), szComment, szSection);
}

// SetInt
// Passes the given int to SetValue as a std::string
bool CDataFile::SetInt(std::string_view szKey, int32_t nValue, std::string_view szComment, std::string_view szSection)
{
    return SetValue(szKey, std::format("{:d}", nValue), szComment, szSection);
}


// SetUInt
// Passes the given int to SetValue as a std::string
bool CDataFile::SetUInt(std::string_view szKey, uint32_t nValue, std::string_view szComment, std::string_view szSection)
{
    return SetValue(szKey, std::format("{:u}",nValue), szComment, szSection);
}


// SetBool
// Passes the given bool to SetValue as a std::string
bool CDataFile::SetBool(std::string_view szKey, bool bValue, std::string_view szComment, std::string_view szSection)
{
    return SetValue(szKey, bValue ? "True" : "False", szComment, szSection);
}

// GetValue
// Returns the key value as a std::string object. A return value of
// "" indicates that the key could not be found.
[[nodiscard]] std::optional<std::string> CDataFile::GetValue(std::string_view szKey, std::string_view szSection)
{
    t_Key* pKey = GetKey(szKey, szSection);

    if (pKey == nullptr) return nullopt;

    return pKey->szValue;
}

// Getstd::string
// Returns the key value as a std::string object. A return value of
// "" indicates that the key could not be found.
[[nodiscard]] std::optional<std::string> CDataFile::GetString(std::string_view szKey, std::string_view szSection)
{
    return GetValue(szKey, szSection);
}

// GetFloat
// Returns the key value as a float type. Returns FLT_MIN if the key is
// not found.
[[nodiscard]] std::optional<float> CDataFile::GetFloat(std::string_view szKey, std::string_view szSection)
{
    return GetValue(szKey, szSection)
        .transform([](const std::string& str) {return static_cast<float>(stof(str)); });
}

// GetInt
// Returns the key value as an integer type. Returns INT_MIN if the key is
// not found.
[[nodiscard]] std::optional<int32_t> CDataFile::GetInt(std::string_view szKey, std::string_view szSection)
{
    return GetValue(szKey, szSection)
        .transform([](const std::string& str) {return static_cast<int32_t>(std::stoi(str)); });
}

// GetUInt
// Returns the key value as an integer type. Returns UINT_MAX if the key is
// not found.
[[nodiscard]] std::optional<uint32_t> CDataFile::GetUInt(std::string_view szKey, std::string_view szSection)
{
    return GetValue(szKey, szSection)
        .transform([](const std::string& str) { return static_cast<uint32_t>(std::stoul(str)); });
}

// GetBool
// Returns the key value as a bool type. Returns nullopt if the key is
// not found.
[[nodiscard]] std::optional<bool> CDataFile::GetBool(std::string_view szKey, std::string_view szSection)
{
    const auto valueOpt = GetValue(szKey, szSection);
    if (!valueOpt.has_value()) return std::nullopt;

    const auto& value = valueOpt.value();

    if (CompareNoCase(value, "true")
        || CompareNoCase(value, "yes")
        || value.find("1") == 0)
        return true;
    if (value.find("0") == 0
        || CompareNoCase(value, "false") == 0
        || CompareNoCase(value, "no") == 0)
        return false;

    return std::nullopt;
}

// DeleteSection
// Delete a specific section. Returns false if the section cannot be 
// found or true when sucessfully deleted.
bool CDataFile::DeleteSection(std::string_view szSection)
{
    const auto pred = [&](const t_Section& section) {
        return CompareNoCase(section.szName, szSection);
    };
    auto numErased = std::erase_if(m_Sections, pred);

    return numErased > 0;
}

// DeleteKey
// Delete a specific key in a specific section. Returns false if the key
// cannot be found or true when sucessfully deleted.
bool CDataFile::DeleteKey(std::string_view szKey, std::string_view szFromSection)
{
    t_Section* pSection = GetSection(szFromSection);

    if (pSection == nullptr)
        return false;

    auto pred = [&](const t_Key& key) {return CompareNoCase(key.szKey, szKey);};
    auto numErased = std::erase_if(pSection->Keys,pred);


    return numErased > 0;
}

// CreateKey
// Given a key, a value and a section, this function will attempt to locate the
// Key within the given section, and if it finds it, change the keys value to
// the new value. If it does not locate the key, it will create a new key with
// the proper value and place it in the section requested.
bool CDataFile::CreateKey(std::string_view szKey, std::string_view szValue, std::string_view szComment, std::string_view szSection)
{
    bool bAutoKey = (m_Flags & AUTOCREATE_KEYS) == AUTOCREATE_KEYS;
    bool bReturn = false;

    m_Flags |= AUTOCREATE_KEYS;

    bReturn = SetValue(szKey, szValue, szComment, szSection);

    if (!bAutoKey)
        m_Flags &= ~AUTOCREATE_KEYS;

    return bReturn;
}


// CreateSection
// Given a section name, this function first checks to see if the given section
// allready exists in the list or not, if not, it creates the new section and
// assigns it the comment given in szComment.  The function returns true if
// sucessfully created, or false otherwise. 
bool CDataFile::CreateSection(std::string_view szSection, std::string_view szComment)
{
    t_Section* pSection = GetSection(szSection);

    if (pSection)
    {
        Report(E_INFO, "[CDataFile::CreateSection] Section <{}> allready exists. Aborting.", std::string(szSection));
        return false;
    }

    m_Sections.emplace_back(szSection, szComment);
    m_bDirty = true;

    return true;
}

// CreateSection
// Given a section name, this function first checks to see if the given section
// allready exists in the list or not, if not, it creates the new section and
// assigns it the comment given in szComment.  The function returns true if
// sucessfully created, or false otherwise. This version accpets a KeyList 
// and sets up the newly created Section with the keys in the list.
bool CDataFile::CreateSection(std::string_view szSection, std::string_view szComment, std::vector<t_Key> Keys)
{
    if (!CreateSection(szSection, szComment))
        return false;

    t_Section* pSection = GetSection(szSection);

    if (pSection == nullptr)
        return false;


    pSection->szName = szSection;
    for(const auto& key : Keys)
    {
        pSection->Keys.emplace_back(key.szComment, key.szKey, key.szValue);
    }

    m_Sections.push_back(*pSection);
    m_bDirty = true;

    return true;
}

// SectionCount
// Simply returns the number of sections in the list.
[[nodiscard]] int CDataFile::SectionCount()
{
    return static_cast<int>(m_Sections.size());
}

// KeyCount
// Returns the total number of keys contained within all the sections.
[[nodiscard]] int CDataFile::KeyCount()
{
    int nCounter = 0;

    for (const auto& section: m_Sections)
        nCounter += static_cast<int>(section.Keys.size());

    return nCounter;
}


// Protected Member Functions ///////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////

// GetKey
// Given a key and section name, looks up the key and if found, returns a
// pointer to that key, otherwise returns NULL.
t_Key* CDataFile::GetKey(std::string_view szKey, std::string_view szSection)
{
    t_Section* pSection = GetSection(szSection);

    // Since our default section has a name value of "" this should
    // always return a valid section, wether or not it has any keys in it is
    // another matter.
    if (pSection == nullptr)
        return nullptr;

    for(auto& key : pSection->Keys)
    {
        if (CompareNoCase(key.szKey, szKey))
            return &key;
    }

    return nullptr;
}

// GetSection
// Given a section name, locates that section in the list and returns a pointer
// to it. If the section was not found, returns NULL
t_Section* CDataFile::GetSection(std::string_view szSection)
{
    for (auto& section: m_Sections)
    {
        if (CompareNoCase(section.szName, szSection))
            return &section;
    }

    return nullptr;
}


std::string CDataFile::CommentStr(std::string_view szComment)
{
    std::string szNewStr = "";

    szComment = Trim(szComment);

    if (szComment.empty())
        return std::string(szComment);

    if (szComment.find_first_of(CommentIndicators) != 0)
    {
        szNewStr = CommentIndicators[0];
        szNewStr += " ";
    }

    szNewStr += szComment;

    return szNewStr;
}



// Utility Functions ////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////

// GetNextWord
// Given a key +delimiter+ value std::string, pulls the key name from the std::string,
// deletes the delimiter and alters the original std::string to contain the
// remainder.  Returns the key
std::string GetNextWord(std::string& CommandLine)
{
    int nPos = static_cast<int>(CommandLine.find_first_of(EqualIndicators));
    std::string sWord = "";

    if (nPos > -1)
    {
        sWord = CommandLine.substr(0, nPos);
        CommandLine.erase(0, nPos + 1);
    }
    else
    {
        sWord = CommandLine;
        CommandLine = "";
    }

    sWord = Trim(sWord);
    return sWord;
}


// CompareNoCase
// Lowercase compare of two strings
bool CompareNoCase(std::string_view lhs, std::string_view rhs)
{
    auto to_lower{ std::ranges::views::transform([](char c) {return std::tolower(c); }) };
    return std::ranges::equal(lhs | to_lower, rhs | to_lower);
}

// Trim
// Trims whitespace from both sides of a std::string.
[[nodiscard]] std::string_view Trim(std::string_view szStr)
{
    constexpr std::string szTrimChars = WhiteSpace + EqualIndicators;

    // trim left
    size_t nPos = szStr.find_first_not_of(szTrimChars);

    if (nPos != std::string_view::npos)
        szStr.remove_prefix(nPos);

    // trim right and return
    size_t rPos = szStr.find_last_not_of(szTrimChars);

    if (rPos != std::string_view::npos)
        szStr.remove_suffix(szStr.size() - ++rPos);

    return szStr;
}


// WriteLn
// Writes the formatted output to the file stream, returning the number of
// bytes written.
//int WriteLn(std::fstream& stream, const char* fmt, ...)
//{
//    char buf[MAX_BUFFER_LEN];
//    int nLength;
//    std::string szMsg;
//
//    memset(buf, 0, MAX_BUFFER_LEN);
//    va_list args;
//
//    va_start(args, fmt);
//    nLength = _vsnprintf_s(buf, MAX_BUFFER_LEN, fmt, args);
//    va_end(args);
//
//
//    if (buf[nLength] != '\n' && buf[nLength] != '\r')
//        buf[nLength++] = '\n';
//
//
//    stream.write(buf, nLength);
//
//    return nLength;
//}

//// WriteLn
//// Writes the formatted output to the file stream, returning the number of
//// bytes written.
//int WriteLn(std::fstream& stream, auto&& fmt, auto&&... args)
//{
//    std::string buf = std::format(std::forward(fmt), std::forward<decltype(args)>(args)...);
//    if (buf.back() != '\n')
//        buf += '\n';
//
//    stream.write(buf.data(), buf.size());
//
//    return buf.size();
//}


//// Report
//// A simple reporting function. Outputs the report messages to stdout
//// This is a dumb'd down version of a simmilar function of mine, so if 
//// it looks like it should do more than it does, that's why...
//void Report(e_DebugLevel DebugLevel, const char* fmt, ...)
//{
//    char buf[MAX_BUFFER_LEN];
//    int nLength;
//    std::string szMsg;
//
//    va_list args;
//
//    memset(buf, 0, MAX_BUFFER_LEN);
//
//    va_start(args, fmt);
//    nLength = _vsnprintf_s(buf, MAX_BUFFER_LEN, fmt, args);
//    va_end(args);
//
//
//    if (buf[nLength] != '\n' && buf[nLength] != '\r')
//        buf[nLength++] = '\n';
//
//
//    switch (DebugLevel)
//    {
//    case E_DEBUG:
//        szMsg = "<debug> ";
//        break;
//    case E_INFO:
//        szMsg = "<info> ";
//        break;
//    case E_WARN:
//        szMsg = "<warn> ";
//        break;
//    case E_ERROR:
//        szMsg = "<error> ";
//        break;
//    case E_FATAL:
//        szMsg = "<fatal> ";
//        break;
//    case E_CRITICAL:
//        szMsg = "<critical> ";
//        break;
//    }
//
//
//    szMsg += buf;
//
//    printf(szMsg.c_str());
//
//}


