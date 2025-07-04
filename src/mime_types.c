#include <stdio.h>
#include <string.h>

// Mapping of extensions to MIME types (extend as needed)
static const char extensions_to_mime_types[][2][256] = {
    {".aac", "audio/aac"},
    {".abw", "application/x-abiword"},
    {".apng", "image/apng"},
    {".arc", "application/x-freearc"},
    {".avi", "x-msvideo"},
    {".avif", "image/avif"},
    {".azw", "application/vnd.amazon.ebook"},
    {".bash", "application/x-sh"},
    {".sh", "application/x-sh"},
    {".bin", "application/octet-stream"},
    {".bmp", "image/bmp"},
    {".bz", "application/x-bzip"},
    {".bz2", "application/x-bzip2"},
    {".c", "text/x-c"},
    {".cda", "application/x-cdf"},
    {".class", "application/java-vm"},
    {".cpp", "text/x-c++"},
    {".csh", "application/x-csh"},
    {".css", "text/css"},
    {".csv", "text/csv"},
    {".doc", "application/msword"},
    {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {".eot", "application/vnd.ms-fontobject"},
    {".epub", "application/epub+zip"},
    {".erb", "text/x-ruby"},
    {".go", "text/x-go"},
    {".gif", "image/gif"},
    {".gz", "application/gzip"},
    {".h", "text/x-c"},
    {".hpp", "text/x-c++"},
    {".htm", "text/html"},
    {".html", "text/html"},
    {".ico", "image/vnd.microsoft.icon"},
    {".ics", "text/calendar"},
    {".java", "text/x-java-source"},
    {".jar", "application/java-archive"},
    {".jpeg", "image/jpeg"},
    {".jpg", "image/jpeg"},
    {".js", "text/javascript"},
    {".json", "application/json"},
    {".jsonld", "application/ld+json"},
    {".kdbx", "application/x-keepass"},
    {".kt", "text/x-kotlin"},
    {".kts", "text/x-kotlin"},
    {".md", "text/markdown"},
    {".mid", "audio/midi"},
    {".midi", "audio/x-midi"},
    {".mjs", "text/javascript"},
    {".mp3", "audio/mpeg"},
    {".mp4", "video/mp4"},
    {".mpeg", "video/mpeg"},
    {".mpkg", "application/vnd.apple.installer+xml"},
    {".odp", "application/vnd.oasis.opendocument.presentation"},
    {".ods", "application/vnd.oasis.opendocument.spreadsheet"},
    {".odt", "application/vnd.oasis.opendocument.text"},
    {".oga", "audio/ogg"},
    {".ogv", "video/ogg"},
    {".ogx", "application/ogg"},
    {".opus", "audio/ogg"},
    {".otf", "font/otf"},
    {".png", "image/png"},
    {".pdf", "application/pdf"},
    {".php", "application/x-httpd-php"},
    {".phtml", "application/x-httpd-php"},
    {".ppt", "application/vnd.ms-powerpoint"},
    {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    {".py", "text/x-python"},
    {".pyc", "application/x-python-bytecode"},
    {".pyo", "application/x-python-bytecode"},
    {".rar", "application/vnd.rar"},
    {".rb", "text/x-ruby"},
    {".rs", "text/x-rust"},
    {".rtf", "application/rtf"},
    {".sh", "application/x-sh"},
    {".svg", "image/svg+xml"},
    {".swift", "text/x-swift"},
    {".tar", "application/x-tar"},
    {".tif", "image/tiff"},
    {".tiff", "image/tiff"},
    {".ts", "video/mp2t"},
    {".ttf", "font/ttf"},
    {".txt", "text/plain"},
    {".vsd", "application/vnd.visio"},
    {".wav", "audio/wav"},
    {".weba", "audio/webm"},
    {".webm", "video/webm"},
    {".webmanifest", "application/manifest+json"},
    {".webp", "image/webp"},
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
    {".xhtml", "application/xhtml+xml"},
    {".xls", "application/vnd.ms-excel"},
    {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {".xml", "application/xml"},
    {".xsl", "application/xml"},
    {".xul", "application/vnd.mozilla.xul+xml"},
    {".zip", "application/zip"},
    {".3gp", "video/3gpp"},
    {".3g2", "video/3gpp2"},
    {".7z", "application/x-7z-compressed"},
    {"", ""} // Last key must be an empty string
};

// Returns a file's MIME type based on its extensions
const char *get_MIME_type(const char *filename)
{
    const char *ext = strrchr(filename, '.');

    // Cycle through the list until the key is an empty string
    for (int i = 0; extensions_to_mime_types[i][0][0] != '\0'; i++){

        // If the key matches with the extension, return the value (MIME type)
        if (strcmp(ext, extensions_to_mime_types[i][0]) == 0){
            return extensions_to_mime_types[i][1];
        }   
    }
    printf("Unknown file extension detected: %s\n", ext);
    return "application/octet-stream"; // Fallback for unknown types
}