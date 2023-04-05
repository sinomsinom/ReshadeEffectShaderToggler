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
#pragma once

#include "stdafx.h"
#include <vector>
#include <fstream>
#include <string>
#include <filesystem>


// Globally defined structures, defines, & types
//////////////////////////////////////////////////////////////////////////////////

// AUTOCREATE_SECTIONS
// When set, this define will cause SetValue() to create a new section, if
// the requested section does not allready exist.
constexpr auto AUTOCREATE_SECTIONS = (1L<<1);

// AUOTCREATE_KEYS
// When set, this define causes SetValue() to create a new key, if the
// requested key does not allready exist.
constexpr auto AUTOCREATE_KEYS = (1L<<2);

// MAX_BUFFER_LEN
// Used simply as a max size of some internal buffers. Determines the maximum
// length of a line that will be read from or written to the file or the
// report output.
constexpr auto MAX_BUFFER_LEN = 512;


// eDebugLevel
// Used by our Report function to classify levels of reporting and severity
// of report.
enum e_DebugLevel
{
    // detailed programmatic informational messages used as an aid in
    // troubleshooting problems by programmers
    E_DEBUG = 0,
    // brief informative messages to use as an aid in troubleshooting
    // problems by production support and programmers
    E_INFO,
    // messages intended to notify help desk, production support and
    // programmers of possible issues with respect to the running application
    E_WARN,
    // messages that detail a programmatic error, these are typically
    // messages intended for help desk, production support, programmers and
    // occasionally users
    E_ERROR,
    // severe messages that are programmatic violations that will usually
    // result in application failure. These messages are intended for help
    // desk, production support, programmers and possibly users
    E_FATAL,
    // notice that all processing should be stopped immediately after the
    // log is written.
    E_CRITICAL
};



// CommentIndicators
// This constant contains the characters that we check for to determine if a 
// line is a comment or not. Note that the first character in this constant is
// the one used when writing comments to disk (if the comment does not allready
// contain an indicator)
const std::string CommentIndicators = ";#";

// EqualIndicators
// This constant contains the characters that we check against to determine if
// a line contains an assignment ( key = value )
// Note that changing these from their defaults ("=:") WILL affect the
// ability of CDataFile to read/write to .ini files.  Also, note that the
// first character in this constant is the one that is used when writing the
// values to the file. (EqualIndicators[0])
constexpr std::string EqualIndicators = "=:";

// WhiteSpace
// This constant contains the characters that the Trim() function removes from
// the head and tail of std::strings.
constexpr std::string WhiteSpace = " \t\n\r";

// st_key
// This structure stores the definition of a key. A key is a named identifier
// that is associated with a value. It may or may not have a comment.  All comments
// must PRECEDE the key on the line in the config file.
struct t_Key
{
    std::string		szKey;
    std::string		szValue;
    std::string		szComment;

    t_Key(std::string_view key, std::string_view value, std::string_view comment) :
        szKey(key),
        szValue(value),
        szComment(comment) 
    {};

    t_Key() :
        szKey(""),
        szValue(""),
        szComment("")
    {};

};

// st_section
// This structure stores the definition of a section. A section contains any number
// of keys (see st_keys), and may or may not have a comment. Like keys, all
// comments must precede the section.
struct t_Section
{
    std::string		    szName;
    std::string		    szComment;
    std::vector<t_Key>	Keys;

    t_Section(std::string_view name, std::string_view comment, std::vector<t_Key>&& keys = {}) :
        szName(name),
        szComment(comment),
        Keys(std::move(keys)) 
    {};

    t_Section() :
        szName(""),
        szComment(""),
        Keys()
    {}

};

/// General Purpose Utility Functions ///////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////
template<class... Args>
void Report(e_DebugLevel DebugLevel, const std::format_string<Args...> fmt, Args&&... args) 
{
    std::string szMsg = std::format(fmt, std::forward<Args>(args)...);

    switch (DebugLevel) {
    case e_DebugLevel::E_DEBUG:
        szMsg = "<debug> " + szMsg;
        break;
    case e_DebugLevel::E_INFO:
        szMsg = "<info> " + szMsg;
        break;
    case e_DebugLevel::E_WARN:
        szMsg = "<warn> " + szMsg;
        break;
    case e_DebugLevel::E_ERROR:
        szMsg = "<error> " + szMsg;
        break;
    case e_DebugLevel::E_FATAL:
        szMsg = "<fatal> " + szMsg;
        break;
    case e_DebugLevel::E_CRITICAL:
        szMsg = "<critical> " + szMsg;
        break;
    }

    std::cout << szMsg << '\n';
}
std::string         GetNextWord(std::string& CommandLine);
bool                CompareNoCase(std::string_view lhs, std::string_view rhs);
std::string_view    Trim(std::string_view szStr);
template<class... Args>
size_t              WriteLn(std::fstream& stream, const std::format_string<Args...> fmt, Args&&... args)
{
    std::string buf = std::format(fmt, std::forward<Args>(args)...);

    if (buf.back() != '\n')
        buf.push_back('\n');

    stream.write(buf.data(), buf.size());
    return buf.size();
}


/// Class Definitions ///////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////


// CDataFile
class CDataFile
{
    // Methods
public:
    // Constructors & Destructors
    /////////////////////////////////////////////////////////////////
    CDataFile(bool saveOnClose);
    CDataFile(const std::filesystem::path& filePath, bool saveOnClose);
    virtual		~CDataFile();

    // File handling methods
    /////////////////////////////////////////////////////////////////
    bool		Load(const std::filesystem::path& filePath);
    bool		Save();

    // Data handling methods
    /////////////////////////////////////////////////////////////////

    // GetValue: Our default access method. Returns the raw std::string value
    // Note that this returns keys specific to the given section only.
    [[nodiscard]] std::optional<std::string>	GetValue(std::string_view szKey, std::string_view szSection = "");
    // GetString: Returns the value as a string
    [[nodiscard]] std::optional<std::string>	GetString(std::string_view szKey, std::string_view szSection = "");
    // GetFloat: Returns the value as a float
    [[nodiscard]] std::optional<float>	        GetFloat(std::string_view szKey, std::string_view szSection = "");
    // GetInt: Returns the value as an int
    [[nodiscard]] std::optional<int32_t>	    GetInt(std::string_view szKey, std::string_view szSection = "");
    // GetUInt: Returns the value as an int
    [[nodiscard]] std::optional<uint32_t>	    GetUInt(std::string_view szKey, std::string_view szSection = "");
    // GetBool: Returns the value as a bool
    [[nodiscard]] std::optional<bool>		    GetBool(std::string_view szKey, std::string_view szSection = "");

    // SetValue: Sets the value of a given key. Will create the
    // key if it is not found and AUTOCREATE_KEYS is active.
    bool		SetValue(std::string_view szKey, std::string_view szValue,
        std::string_view szComment = "", std::string_view szSection = "");

    // SetFloat: Sets the value of a given key. Will create the
    // key if it is not found and AUTOCREATE_KEYS is active.
    bool		SetFloat(std::string_view szKey, float fValue,
        std::string_view szComment = "", std::string_view szSection = "");

    // SetInt: Sets the value of a given key. Will create the
    // key if it is not found and AUTOCREATE_KEYS is active.
    bool		SetInt(std::string_view szKey, int32_t nValue,
        std::string_view szComment = "", std::string_view szSection = "");

    // SetUInt: Sets the value of a given key. Will create the
    // key if it is not found and AUTOCREATE_KEYS is active.
    bool		SetUInt(std::string_view szKey, uint32_t nValue,
        std::string_view szComment = "", std::string_view szSection = "");


    // SetBool: Sets the value of a given key. Will create the
    // key if it is not found and AUTOCREATE_KEYS is active.
    bool		SetBool(std::string_view szKey, bool bValue,
        std::string_view szComment = "", std::string_view szSection = "");

    // Sets the comment for a given key.
    bool		SetKeyComment(std::string_view szKey, std::string_view szComment, std::string_view szSection = "");

    // Sets the comment for a given section
    bool		SetSectionComment(std::string_view szSection, std::string_view szComment);

    // DeleteKey: Deletes a given key from a specific section
    bool		DeleteKey(std::string_view szKey, std::string_view szFromSection = "");

    // DeleteSection: Deletes a given section.
    bool		DeleteSection(std::string_view szSection);

    // Key/Section handling methods
    /////////////////////////////////////////////////////////////////

    // CreateKey: Creates a new key in the requested section. The
    // Section will be created if it does not exist and the 
    // AUTOCREATE_SECTIONS bit is set.
    bool CreateKey(std::string_view szKey, std::string_view szValue,
        std::string_view szComment = "", std::string_view szSection = "");
    // CreateSection: Creates the new section if it does not allready
    // exist. Section is created with no keys.
    bool CreateSection(std::string_view szSection, std::string_view szComment = "");
    // CreateSection: Creates the new section if it does not allready
    // exist, and copies the keys passed into it into the new section.
    bool CreateSection(std::string_view szSection, std::string_view szComment, std::vector<t_Key> Keys);

    // Utility Methods
    /////////////////////////////////////////////////////////////////
    // SectionCount: Returns the number of valid sections in the database.
    [[nodiscard]] int SectionCount();
    // KeyCount: Returns the total number of keys, across all sections.
    [[nodiscard]] int KeyCount();
    // Clear: Initializes the member variables to their default states
    void Clear();
    // SetFileName: For use when creating the object by hand
    // initializes the file name so that it can be later saved.
    void SetFileName(const std::filesystem::path& filePath);
    // CommentStr
    // Parses a string into a proper comment token/comment.
    [[nodiscard]] std::string CommentStr(std::string_view szComment);
    //
    void SetSaveOnClose(bool saveOnClose) { m_saveOnClose = saveOnClose; };


protected:
    // Note: I've tried to insulate the end user from the internal
    // data structures as much as possible. This is by design. Doing
    // so has caused some performance issues (multiple calls to a
    // GetSection() function that would otherwise not be necessary,etc).
    // But, I believe that doing so will provide a safer, more stable
    // environment. You'll notice that nothing returns a reference,
    // to modify the data values, you have to call member functions.
    // think carefully before changing this.

    // GetKey: Returns the requested key (if found) from the requested
    // Section. Returns nullptr otherwise.
    [[nodiscard]] t_Key* GetKey(std::string_view szKey, std::string_view szSection);
    // GetSection: Returns the requested section (if found), nullptr otherwise.
    [[nodiscard]] t_Section* GetSection(std::string_view szSection);


    // Data
public:
    long		m_Flags;		// Our settings flags.

protected:
    std::vector<t_Section>	m_Sections;	   // Our list of sections
    std::filesystem::path	m_filePath;	   // The filename to write to
    bool		            m_bDirty;	   // Tracks whether or not data has changed.
    bool                    m_saveOnClose; // Whether or not the file should be saved on close.
};


