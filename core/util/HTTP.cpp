#include "HTTP.h"
#include <boost/algorithm/string.hpp>
#include <iostream>
#include <regex>
#include <fstream>
#include <boost/filesystem.hpp>
#include <cryptopp/sha.h>
#include "Log.h"
#include "base64.h"

namespace i2p {
namespace util {
namespace http {

// see https://en.wikipedia.org/wiki/WebSocket
static const char * websocket_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  
std::string WebsocketKeyHash(const std::string& key)
{
  uint8_t digest[20];
  char wshash[128];
  std::memset(wshash, 0, sizeof(wshash));
  std::stringstream ss;
  ss << key;
  ss << websocket_guid;
  auto str = ss.str();
  CryptoPP::SHA1().CalculateDigest(digest, (const uint8_t*)str.c_str(), str.size());
  auto encoded = i2p::util::ByteStreamToBase64(digest, sizeof(digest), wshash, sizeof(wshash));
  std::stringstream s;
  for(std::size_t idx = 0; idx < encoded; idx++)
  {
    char c = wshash[idx];
    switch(c)
    {
    case '-':
      s << '+';
      break;
    case '~':
      s << '/';
      break;
    default:
      s << c;
    }
  }
  return s.str();
}

std::vector<WebsocketFrame> CreateWebsocketFrames(const std::string & data, uint8_t chunksize)
{
  auto itr = data.begin();
  std::vector<WebsocketFrame> frames;
  std::size_t datasize = data.size();
  auto chunks = datasize / chunksize;
  std::size_t counter = datasize;
  WebsocketFrame initial;
  if (chunks && datasize > chunksize) {
    initial.push_back(0x01);
    initial.push_back(chunksize);
    initial.insert(initial.end(), itr, itr+chunksize);
    itr += chunksize;
    counter -= chunksize;
  }
  else // send it in 1 frame
  {
    initial.push_back(0x81);
    initial.push_back(uint8_t(datasize));
    initial.insert(initial.end(), itr, data.end());
  }
  frames.push_back(initial);

  while(chunks)
  {
    WebsocketFrame frame;
    if (counter <= chunksize)
    {
      // last packet
      frame.push_back(0x80);
      frame.push_back(uint8_t(counter));
      frame.insert(frame.end(), itr, data.end());
    }
    else
    {
      frame.push_back(0x00);
      frame.push_back(chunksize);
      frame.insert(frame.end(), itr, itr + chunksize);
    }
    counter -= chunksize;
    itr += chunksize;
    chunks--;
    frames.push_back(frame);
  }
  return frames;
}
  
void Request::parseRequestLine(const std::string& line)
{
    std::stringstream ss(line);
    ss >> method;
    ss >> uri;
}

void Request::parseHeaderLine(const std::string& line)
{
    const std::size_t pos = line.find_first_of(':');
    headers[boost::trim_copy(line.substr(0, pos))] = boost::trim_copy(line.substr(pos + 1));
}

void Request::parseHeader(std::stringstream& ss)
{
    std::string line;
    while(std::getline(ss, line) && !boost::trim_copy(line).empty())
        parseHeaderLine(line);

    has_header = boost::trim_copy(line).empty();
    if(!has_header)
        header_part = line;
    else
        header_part = "";
}

void Request::setIsComplete()
{
    auto it = headers.find("Content-Length");
    if(it == headers.end()) {
        // If Content-Length is not set, assume there is no more content 
        // TODO: Support chunked transfer, or explictly reject it
        is_complete = true;
        return;
    }
    const std::size_t length = std::stoi(it->second);
    is_complete = content.size() >= length;
}

Request::Request(const std::string& data)
{
    if(!data.empty())
        has_data = true;

    std::stringstream ss(data);

    std::string line;
    std::getline(ss, line);

    // Assume the request line is always passed in one go
    parseRequestLine(line);

    parseHeader(ss);

    if(has_header && ss) {
        const std::string current = ss.str();
        content = current.substr(ss.tellg());
    }

    if(has_header)
        setIsComplete();
}

std::string Request::getMethod() const
{
    return method;
}

std::string Request::getUri() const
{
    return uri;
}

std::string Request::getHost() const
{
    return host;
}

int Request::getPort() const
{
    return port;
}

std::string Request::getHeader(const std::string& name) const
{
    return headers.at(name);
}

std::string Request::getContent() const
{
    return content;
}

bool Request::hasData() const
{
    return has_data; 
}

bool Request::isComplete() const
{
    return is_complete;
}

void Request::clear()
{
    has_data = false;
    has_header = false;
    is_complete = false;
}

void Request::update(const std::string& data)
{
    std::stringstream ss(header_part + data);
    if(!has_header)
        parseHeader(ss);

    if(has_header && ss) {
        const std::string current = ss.str();
        content += current.substr(ss.tellg());
    }

    if(has_header)
        setIsComplete();
}

Response::Response(int status, const std::string& content)
    : status(status), content(content), headers()
{

}

void Response::setHeader(const std::string& name, const std::string& value)
{
    headers[name] = value;
}

std::string Response::toString() const
{
    std::stringstream ss;
    ss << "HTTP/1.1 " << status << ' ' << getStatusMessage() << "\r\n";
    for(auto& pair : headers)
        ss << pair.first << ": " << pair.second << "\r\n";
    ss << "\r\n" << content; 
    return ss.str();
}

std::string Response::getStatusMessage() const
{
    switch(status) {
        case 101:
            return "Switching Protocols";
        case 105:
            return "Name Not Resolved";
        case 200:
            return "OK";
        case 400:
            return "Bad Request";
        case 404:
            return "Not Found";
        case 408:
            return "Request Timeout";
        case 500:
            return "Internal Server Error";
        case 502:
            return "Not Implemented";
        case 504:
            return "Gateway Timeout";
        default:
            return std::string();
    }
}

void Response::setContentLength()
{
    setHeader("Content-Length", std::to_string(content.size()));
}

std::string preprocessContent(const std::string& content, const std::string& path)
{
    const boost::filesystem::path directory(path); // Given path is assumed to be clean

    static const std::regex re(
        "<\\!\\-\\-\\s*#include\\s+virtual\\s*\\=\\s*\"([^\"]*)\"\\s*\\-\\->"
    );

    boost::system::error_code e;

    std::string result;

    std::smatch match;
    auto it = content.begin();
    while(std::regex_search(it, content.end(), match, re)) {
        const auto last = it;
        std::advance(it, match.position());
        result.append(last, it);
        std::advance(it, match.length());

        // Read the contents of the included file
        std::ifstream ifs(
            boost::filesystem::canonical(directory / std::string(match[1]), e).string(),
            std::ios_base::in | std::ios_base::binary
        );
        if(e || !ifs)
            continue;

        std::string data;
        ifs.seekg(0, ifs.end);
        data.resize(ifs.tellg());
        ifs.seekg(0, ifs.beg);
        ifs.read(&data[0], data.size());
        
        result += data; 
    }

    // Append all of the remaining content
    result.append(it, content.end());

    return result;
}

std::string getMimeType(const std::string& filename)
{
    const std::string ext = filename.substr(filename.find_last_of("."));
    if(ext == ".css")
        return "text/css";
    else if(ext == ".js")
        return "text/javascript";
    else if(ext == ".html" || ext == ".htm")
        return "text/html";
    else
        return "application/octet-stream";
}

}
}
}
