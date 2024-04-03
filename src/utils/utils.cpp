#include <chrono>
#include <cstring>
#include <ctime>
#include <regex>
#include <time.h>
#include <stdio.h>

#include <hlsparser/hlsparse.h>
#include <utils/utils.h>
#include "plist.h"

using namespace std::chrono;

const uint32_t EPOCH = 2208988800ULL;        // January 1970, in NTP seconds.
const double NTP_SCALE_FRAC = 4294967296ULL; // NTP fractional unit.

uint64_t get_ntp_timestamp() {
  uint64_t seconds = 0;
  uint64_t fraction = 0;

  milliseconds ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch());

  seconds = ms.count() / 1000 + EPOCH;
  fraction = (uint64_t)((ms.count() % 1000) * NTP_SCALE_FRAC) / 1000;

  return (seconds << 32) | fraction;
}

uint64_t normalize_ntp_to_ms(uint64_t ntp) {
  uint64_t milliseconds = (ntp >> 32) * 1000;
  uint32_t fraction = (uint32_t)((ntp & 0x0ffffffff) * 1000.f / NTP_SCALE_FRAC);
  return (milliseconds + fraction);
}

const char *gmt_time_string() {
  static char date_buf[64];
  memset(date_buf, 0, 64);

  std::time_t now = std::time(0);
  if (std::strftime(date_buf, 64, "%c GMT", std::gmtime(&now)))
    return date_buf;
  else
    return 0;
}

std::string generate_mac_address() {
  uint64_t ts = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

  static char buffer[32];
  memset(buffer, 0, 32);
  std::sprintf(buffer,
               "%02X:%02X:%02X:%02X:%02X:%02X",
               (uint8_t)((ts >> 0) & 0xff),
               (uint8_t)((ts >> 8) & 0xff),
               (uint8_t)((ts >> 16) & 0xff),
               (uint8_t)((ts >> 24) & 0xff),
               (uint8_t)((ts >> 32) & 0xff),
               (uint8_t)((ts >> 40) & 0xff));
  return buffer;
}

std::string string_replace(const std::string &str, const std::string &pattern, const std::string &with) {
  std::regex p(pattern);
  return std::regex_replace(str, p, with);
}

std::string generate_file_name() {
  time_t now = time(0);
  std::tm *local_now = localtime(&now);
  std::ostringstream oss;
  // clang-format off
  oss << local_now->tm_year + 1900 << "-"
    << local_now->tm_mon + 1 << "-"
    << local_now->tm_mday << "-"
    << local_now->tm_hour << "-"
    << local_now->tm_min << "-"
    << local_now->tm_sec;
  // clang-format on
  return oss.str();
}

int compare_string_no_case(const char *str1, const char *str2) {
#if defined(WIN32) || defined(MS_VER_)
  return _strcmpi(str1, str2);
#else
  return strcasecmp(str1, str2);
#endif
}

bool get_youtube_url(const char *data, uint32_t length, std::string &url) {
  static std::regex pattern("#YT-EXT-CONDENSED-URL:BASE-URI=\"(.*)\",PARAMS=");
  std::cmatch groups;

  if (std::regex_search(data, groups, pattern)) {
    if (groups.size() > 1) {
      url = groups.str(1);
      return true;
    }
  }

  return false;
}

std::string get_best_quality_stream_uri(const char *data, uint32_t length) {
  HLSCode r = hlsparse_global_init();
  master_t master_playlist;
  r = hlsparse_master_init(&master_playlist);
  r = hlsparse_master(data, length, &master_playlist);
  stream_inf_list_t *best_quality_stream = 0;
  stream_inf_list_t *stream_inf = &master_playlist.stream_infs;
  return master_playlist.media.data->uri;
  while (stream_inf && stream_inf->data) {
    if (!best_quality_stream) {
      best_quality_stream = stream_inf;
    } else if (stream_inf->data->bandwidth > best_quality_stream->data->bandwidth) {
      best_quality_stream = stream_inf;
    }
    stream_inf = stream_inf->next;
  }
  if (best_quality_stream) {
    return best_quality_stream->data->uri;
  }

  return std::string();
}

static int parse_dmap_header(const unsigned char *metadata, char *tag, int *len) {
    const unsigned char *header = metadata;

    bool istag = true;
    for (int i = 0; i < 4; i++) {
        tag[i] =  (char) *header;
	if (!isalpha(tag[i])) {
            istag = false;
        }
        header++;
    }

    *len = 0;
    for (int i = 0; i < 4; i++) {
        *len <<= 8;
        *len += (int) *header;
        header++;
    }
    if (!istag || *len < 0) {
        return 1;
    }
    return 0;
}

void parse_metadata(const void *buffer, int buflen) {
    char dmap_tag[5] = {0x0};
    const unsigned char *metadata = (const  unsigned char *) buffer;
    int datalen;
    int count = 0;

    if (buflen < 8) {
      printf("invalid metadata, length %d < 8\n", buflen);
        return;
    } else if (parse_dmap_header(metadata, dmap_tag, &datalen)) {
      printf("invalid metadata, tag [%s]  datalen %d\n", dmap_tag, datalen);
        return;
    }
    metadata += 8;
    buflen -= 8;

    while (buflen >= 8) {
        count++;
        if (parse_dmap_header(metadata, dmap_tag, &datalen)) {
            printf("invalid DMAP header:  tag = [%s],  datalen = %d\n", dmap_tag, datalen);
            return;
        }
        metadata += 8;
        buflen -= 8;
        printf("item %d: tag: %s len: %d\n",count, dmap_tag, datalen);
        metadata += datalen;
        buflen -= datalen;
    }
    if (buflen != 0) {
      printf("%d bytes of metadata were not processed\n", buflen);
    }
}

void print_content(const xtxp_message &msg, const char * msg_type) {
  if (msg.content_length <= 0) return;
  int len = msg.content_length;
  if (!msg.content_type.compare(APPLICATION_BINARY_PLIST)) {  
      printf("--------------------begin %s plist ----------------------------------\n", msg_type);
      plist_print_xml(msg.content.data(), (uint32_t)msg.content.size());
      printf("--------------------end %s plist ------------------------------------\n", msg_type);
  } else   if (!msg.content_type.compare(APPLICATION_MPEGURL)) {
  
  } else   if (!msg.content_type.compare(APPLICATION_OCTET_STREAM)) {
    printf("----------------------begin %s octet_stream ---------------------------\n", msg_type);
    printf(" %2.2x ",msg.content[0]);
    for ( int i = 1 ; i < len ; i++) {
      if (i%16 == 0) printf("\n");
      if (i%8 == 0) printf(" ");
      printf("%2.2x ", msg.content[i]);
    }
    printf("\n");
    printf("----------------------end %s octet stream -----------------------------\n", msg_type);
  } else   if (!msg.content_type.compare(APPLICATION_DMAP_TAGGED)) {
    printf("----------------------begin %s tagged dmap data------------------------\n", msg_type);
    parse_metadata((const void *) &msg.content[0], len);
    printf("----------------------end %s tagged dmap data--------------------------\n", msg_type);
  } else   if (!msg.content_type.compare(TEXT_APPLE_PLIST_XML)) {
    printf("----------------------begin %s plist_xml-------------------------------\n", msg_type);
    std::string content(msg.content.begin(),msg.content.end());
    printf("%s",content.c_str());
    printf("----------------------end %s plist_xml---------------------------------\n", msg_type);
  } else   if (!msg.content_type.compare(TEXT_PARAMETERS)) {
    printf("----------------------begin %s text parameters ------------------------\n", msg_type);
    std::string content(msg.content.begin(),msg.content.end());
    printf("%s\n",content.c_str());
    printf("----------------------end %s text parameters --------------------------\n", msg_type);
    printf("\n");
  } else   if (!msg.content_type.compare(IMAGE_JPEG) || !msg.content_type.compare(IMAGE_PNG)) {
    printf("----------------------begin %s image data -----------------------------\n", msg_type);
    printf("image type %s, image size %d\n", msg.content_type, (int) msg.content_length);
    printf("----------------------end %s image data -------------------------------\n", msg_type);
  } else {
    printf("Unknown content_type %s\n", msg.content_type);
  }
}

void print_request(const request &req, const char *handler) {
  printf("\nnew request received: %s\n",handler);
  printf("    Request: %s %s %s\n",req.method.c_str(), req.uri.c_str(), req.scheme_version.c_str());
  printf("    Header:\n");
  for (std::map<std::string, std::string>::const_iterator it = req.headers.begin();
       it != req.headers.end(); ++it) {
    printf("        %s:%s\n",it->first.c_str(),it->second.c_str());
  }
  printf("    Body: (%s) length %d\n", req.content_type.c_str(), req.content_length);
  if (req.content_length > 0) {
    print_content(req, "request");
  }
  printf("\n");
}
  
void print_response(const response &res, const char *handler) {
  printf("*** response sent: %s\n",handler);
  printf("    Request: %s %d %s\n",res.scheme_version.c_str(), res.status_code, res.status_text.c_str());
  printf("    Header:\n");
  for (std::map<std::string, std::string>::const_iterator it = res.headers.begin();
       it != res.headers.end(); ++it) {
    printf("        %s:%s\n",it->first.c_str(),it->second.c_str());
  }
  printf("        Content-Type: %s\n",res.content_type.c_str());
  printf("        Content-Length: %d\n",res.content_length);
  printf("    Body: (%s) length %d\n", res.content_type.c_str(), res.content_length);
  if (res.content_length > 0) {
    print_content(res, "response");
  }
  printf("\n");
}
  
