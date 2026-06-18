#include <a2a/models/message_part.hpp>
#include <json.hpp>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace a2a {

// Helper function to escape JSON string
static std::string escape_json_string(const std::string& input) {
    std::ostringstream oss;
    for (unsigned char c : input) {  // Use unsigned char to handle UTF-8 properly
        switch (c) {
            case '"':  oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (c < 0x20) {
                    // Control characters only (0x00-0x1F)
                    oss << "\\u" 
                        << std::hex << std::setw(4) << std::setfill('0') 
                        << static_cast<int>(c);
                } else {
                    // Normal characters including UTF-8 bytes (0x20-0xFF)
                    oss << static_cast<char>(c);
                }
        }
    }
    return oss.str();
}

// Base64 encoding helper (simplified)
static std::string base64_encode(const std::vector<uint8_t>& data) {
    static const char* base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    std::string ret;
    int i = 0;
    int j = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];
    size_t in_len = data.size();
    const uint8_t* bytes_to_encode = data.data();
    
    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for(i = 0; i < 4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    
    if (i) {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';
        
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        
        for (j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];
        
        while((i++ < 3))
            ret += '=';
    }
    
    return ret;
}

// TextPart implementation
std::string TextPart::to_json() const {
    std::ostringstream oss;
    oss << "{"
        << "\"kind\":\"text\","
        << "\"text\":\"" << escape_json_string(text_) << "\""
        << "}";
    return oss.str();
}

// FilePart implementation
std::string FilePart::to_json() const {
    std::ostringstream oss;
    oss << "{"
        << "\"kind\":\"file\","
        << "\"file\":{"
        << "\"filename\":\"" << filename_ << "\","
        << "\"mimeType\":\"" << mime_type_ << "\","
        << "\"data\":\"" << base64_encode(data_) << "\""
        << "}}";
    return oss.str();
}

// DataPart implementation
std::string DataPart::to_json() const {
    std::ostringstream oss;
    oss << "{"
        << "\"kind\":\"data\","
        << "\"data\":" << data_json_
        << "}";
    return oss.str();
}

// Part factory method
std::unique_ptr<Part> Part::from_json(const std::string& json) {
    const auto parsed = nlohmann::json::parse(json);
    const auto kind = parsed.value("kind", "");
    if (kind == "text") {
        return std::make_unique<TextPart>(parsed.value("text", ""));
    } else if (kind == "file") {
        const auto file = parsed.value("file", nlohmann::json::object());
        return std::make_unique<FilePart>(file.value("filename", "file.dat"),
            file.value("mimeType", "application/octet-stream"), std::vector<uint8_t>());
    } else if (kind == "data") {
        return std::make_unique<DataPart>(parsed.value("data", nlohmann::json::object()).dump());
    }
    return nullptr;
}

} // namespace a2a
