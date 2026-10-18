// Parses every given .wmv header with the production ASF parser and prints the movie parameters.
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "src/video_wmv3.h"

int main(int argc, char** argv) {
  int bad = 0;
  for (int i = 1; i < argc; ++i) {
    std::ifstream f(argv[i], std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    carbon::video::MovieInfo m;
    if (!carbon::video::ParseAsfHeader(d.data(), d.size() < 65536 ? d.size() : 65536, m)) {
      std::printf("FAIL %s\n", argv[i]);
      ++bad;
      continue;
    }
    carbon::video::Wmv3Decoder dec;
    const bool open = dec.Open(m);
    std::printf("%s %dx%d seq=%02x%02x%02x%02x bframes=%d open=%d\n", argv[i], m.width, m.height,
                m.sequence[0], m.sequence[1], m.sequence[2], m.sequence[3],
                carbon::video::MaxBFrames(m), open);
    bad += !open;
  }
  return bad;
}
