// Test host del protocollo Bluetooth (niente hardware):
//   c++ -std=c++17 -I src test/upload_proto_test.cpp -o /tmp/upt && /tmp/upt
#include <cassert>
#include <string>
#include <cstdio>

#include "UploadProto.h"

using namespace upload;

int main() {
  const char* s = "123456789";
  assert(crc32(0, reinterpret_cast<const uint8_t*>(s), 9) == 0xCBF43926u);
  // A pezzi come arrivano dal Bluetooth.
  uint32_t c = crc32(0, reinterpret_cast<const uint8_t*>(s), 4);
  assert(crc32(c, reinterpret_cast<const uint8_t*>(s + 4), 5) == 0xCBF43926u);
  assert(crc32(0, nullptr, 0) == 0);

  assert(validPath("01_lenschart.bmp"));
  assert(validPath("03_filters/screen.ini"));
  assert(validPath("03_filters/fxs2.p.bmp"));
  assert(!validPath(""));
  assert(!validPath("/abs.bmp"));
  assert(!validPath("../x.bmp"));
  assert(!validPath("a/../b"));
  assert(!validPath("a//b"));
  assert(!validPath("dir/"));
  assert(!validPath("._x.bmp"));
  assert(!validPath("spazio x.bmp"));
  assert(!validPath("città.bmp"));

  assert(parse("HELLO").cmd == Cmd::Hello);
  assert(parse("BEGIN").cmd == Cmd::Begin);
  assert(parse("COMMIT").cmd == Cmd::Commit);
  assert(parse("ABORT").cmd == Cmd::Abort);
  Command f = parse("FILE 52334 cbf43926 03_filters/nd0.bmp");
  assert(f.cmd == Cmd::File && f.size == 52334 && f.crc == 0xCBF43926u);
  assert(std::string(f.path) == "03_filters/nd0.bmp");
  assert(parse("FILE 0 0 vuoto.ini").cmd == Cmd::File);
  assert(parse("FILE 10 zz a.bmp").cmd == Cmd::Unknown);
  assert(parse("FILE 10 1a ../a.bmp").cmd == Cmd::Unknown);
  assert(parse("FILE 99999999 1a a.bmp").cmd == Cmd::Unknown);
  assert(parse("FILE x 1a a.bmp").cmd == Cmd::Unknown);
  assert(parse("hello").cmd == Cmd::Unknown);

  Command g = parse("GET project.json");
  assert(g.cmd == Cmd::Get && std::string(g.path) == "project.json");
  assert(parse("GET ../segreto").cmd == Cmd::Unknown);
  assert(parse("GET ").cmd == Cmd::Unknown);
  Command a = parse("ACK 4096");
  assert(a.cmd == Cmd::Ack && a.size == 4096);
  assert(parse("ACK").cmd == Cmd::Unknown);
  assert(parse("ACK 12x").cmd == Cmd::Unknown);
  assert(parse("ACK 99999999").cmd == Cmd::Unknown);
  puts("upload_proto_test: ok");
}
