
#include <string>
#include <string_view>


// Escape special characters to allow any arbitrary string to be expressed in JSON.
std::string EscapeJsonString(std::string_view const& input);

// Undo the JSON string escaping to recover the original string.
std::string UnescapeJsonString(std::string_view const& input);

void ReplaceNewlinesIn(std::string& string, std::string_view replacement);

void ReplaceIn(std::string& string, std::string_view placeholder, std::string_view replacement);
