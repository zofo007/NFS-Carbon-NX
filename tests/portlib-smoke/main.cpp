#include <expat.h>
#include <zlib.h>
#include <zstd.h>
#include <cstring>

int main() {
  auto parser = XML_ParserCreate(nullptr);
  if (!parser) return 1;
  const auto parsed = XML_Parse(parser, "<carbon/>", 9, XML_TRUE);
  XML_ParserFree(parser);
  if (parsed != XML_STATUS_OK) return 2;
  const unsigned char source[] = "Carbon target compression round trip";
  unsigned char compressed[256], restored[256];
  uLongf packed = sizeof(compressed), unpacked = sizeof(restored);
  if (compress(compressed, &packed, source, sizeof(source)) != Z_OK) return 3;
  if (uncompress(restored, &unpacked, compressed, packed) != Z_OK ||
      unpacked != sizeof(source) || std::memcmp(source, restored, sizeof(source))) return 4;
  auto size = ZSTD_compress(compressed, sizeof(compressed), source, sizeof(source), 1);
  if (ZSTD_isError(size)) return 5;
  auto restoredSize = ZSTD_decompress(restored, sizeof(restored), compressed, size);
  if (ZSTD_isError(restoredSize) || restoredSize != sizeof(source) ||
      std::memcmp(source, restored, sizeof(source))) return 6;
  return 0;
}
