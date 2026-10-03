// yacoin-cpuminer: getwork round-trip tests (plan §7.2, task T-03). MIT licence.
//
// tests/data/getwork_samples.json holds real `getwork` replies from the
// low-difficulty test chain (tests/testchain.sh capture), together with the
// raw_block_header_hex and header fields the node logged for the same call.
#include <cstring>
#include <fstream>

#include "testing.h"

#include "json.hpp"
#include "util/hex.h"
#include "work/getwork.h"
#include "work/target.h"

using namespace yac;
using json = nlohmann::json;

static json load(const char* name)
{
    std::ifstream f(std::string(YAC_TEST_DATA_DIR) + "/" + name);
    if (!f) throw std::runtime_error(std::string("cannot open test data ") + name);
    return json::parse(f);
}

// The node's FormatHashBuffers_64bit_nTime (yacoin src/miner.cpp): pad the
// 84-byte header like SHA-256 and byte-reverse every 32-bit word.
static std::string node_format_data(const std::vector<uint8_t>& header84)
{
    uint8_t buf[128] = {};
    std::memcpy(buf, header84.data(), 84);
    buf[84] = 0x80;
    unsigned bits = 84 * 8;
    buf[127] = bits & 0xff;
    buf[126] = (bits >> 8) & 0xff;
    reverse_words(buf, sizeof buf);
    return to_hex(buf, sizeof buf);
}

TEST(decode_matches_node_log)
{
    json d = load("getwork_samples.json");
    CHECK(d["samples"].size() >= 3);
    for (auto& s : d["samples"]) {
        Work w = decode_getwork(s["data"], s["target"]);
        // Exact header bytes the node serialized for this call.
        CHECK_EQ(to_hex(w.header_bytes(), kHeaderSize), s["raw_block_header_hex"].get<std::string>());
        auto& log = s["log"];
        CHECK_EQ(w.header.version, log["nVersion"].get<uint32_t>());
        CHECK_EQ(w.header.prev_hex(), log["hashPrevBlock"].get<std::string>());
        CHECK_EQ(w.header.prev_hex(), s["bestblockhash"].get<std::string>());
        CHECK_EQ(w.header.merkle_hex(), log["hashMerkleRoot"].get<std::string>());
        CHECK_EQ(w.header.time, log["nTime"].get<int64_t>());
        CHECK_EQ(w.header.bits, log["nBits"].get<uint32_t>());
        CHECK_EQ(w.header.nonce, log["nNonce"].get<uint32_t>());
        // target is raw little-endian, not word-reversed (F6)
        CHECK_EQ(to_hex_reversed(w.target.data(), 32), log["target_BE"].get<std::string>());
        CHECK(w.target == target_from_compact(w.header.bits));
    }
}

TEST(encode_round_trip_is_exact)
{
    json d = load("getwork_samples.json");
    for (auto& s : d["samples"]) {
        Work w = decode_getwork(s["data"], s["target"]);
        // Nonce 0 is what the node sent: re-encoding gives the identical string.
        CHECK_EQ(encode_getwork_submit(w, w.header.nonce), s["data"].get<std::string>());

        // Another nonce changes only word 20 (bytes 80..83 of data).
        const uint32_t nonce = 0x12345678;
        std::string enc = encode_getwork_submit(w, nonce);
        std::string orig = s["data"];
        CHECK_EQ(enc.size(), orig.size());
        CHECK_EQ(enc.substr(0, 160), orig.substr(0, 160));
        CHECK_EQ(enc.substr(168), orig.substr(168));
        CHECK_EQ(enc.substr(160, 8), std::string("12345678"));  // word-reversed LE nonce

        // What the node does with it (src/rpc/mining.cpp getwork submit):
        // reverse the words back and read the packed struct block_header.
        auto bytes = from_hex(enc);
        reverse_words(bytes.data(), bytes.size());
        BlockHeader back = BlockHeader::parse(bytes.data());
        CHECK_EQ(back.nonce, nonce);
        CHECK_EQ(back.time, w.header.time);  // timestamp sent back unchanged (F10)
        CHECK(back.merkle_root == w.header.merkle_root);  // finds the saved block (F7)
    }
}

TEST(time_words_17_18_are_reversed_in_place)
{
    json d = load("getwork_samples.json");
    auto& s = d["samples"][0];
    Work w = decode_getwork(s["data"], s["target"]);
    auto raw = from_hex(s["data"].get<std::string>());
    uint64_t t = static_cast<uint64_t>(w.header.time);
    // word 17 = low 32 bits of nTime, big-endian; word 18 = high 32 bits (not swapped)
    uint32_t w17 = (uint32_t)raw[68] << 24 | (uint32_t)raw[69] << 16 | (uint32_t)raw[70] << 8 | raw[71];
    uint32_t w18 = (uint32_t)raw[72] << 24 | (uint32_t)raw[73] << 16 | (uint32_t)raw[74] << 8 | raw[75];
    CHECK_EQ(w17, static_cast<uint32_t>(t));
    CHECK_EQ(w18, static_cast<uint32_t>(t >> 32));
}

TEST(time_above_32_bits_round_trips)
{
    // Real samples have a zero high time word; check word 18's byte order with
    // a synthetic time >= 2^32 (low word in word 17, high word in word 18).
    json d = load("getwork_samples.json");
    Work w0 = decode_getwork(d["samples"][0]["data"], d["samples"][0]["target"]);
    BlockHeader h = w0.header;
    h.time = 0x0102030405060708LL;
    auto bytes = h.serialize();
    std::vector<uint8_t> hv(bytes.begin(), bytes.end());
    std::string data = node_format_data(hv);
    CHECK_EQ(data.substr(17 * 8, 16), std::string("0506070801020304"));
    Work w = decode_getwork(data, d["samples"][0]["target"]);
    CHECK_EQ(w.header.time, 0x0102030405060708LL);
    auto back = from_hex(encode_getwork_submit(w, 99));
    reverse_words(back.data(), back.size());
    CHECK_EQ(BlockHeader::parse(back.data()).time, 0x0102030405060708LL);
}

TEST(mainnet_headers_through_node_format)
{
    // A mainnet header put through (our copy of) the node's formatting decodes
    // back exactly. A consistency check over real field values; the real-node
    // proof of the format is decode_matches_node_log above.
    json d = load("mainnet_headers.json");
    for (auto& v : d["vectors"]) {
        auto header = from_hex(v["header_hex"].get<std::string>());
        std::string data = node_format_data(header);
        Work w = decode_getwork(data, std::string(64, '0'));
        CHECK_EQ(to_hex(w.header_bytes(), kHeaderSize), v["header_hex"].get<std::string>());
        CHECK_EQ(encode_getwork_submit(w, w.header.nonce), data);
    }
}

TEST(rejects_old_versions_and_bad_data)
{
    json d = load("getwork_samples.json");
    std::string data = d["samples"][0]["data"];
    std::string target = d["samples"][0]["target"];
    std::string v6 = "00000006" + data.substr(8);
    CHECK_THROWS(decode_getwork(v6, target));
    bool unsupported = false;
    try {
        decode_getwork(v6, target);
    } catch (const UnsupportedWork&) {
        unsupported = true;
    }
    CHECK(unsupported);
    std::string badpad = data;
    badpad[170] = '1';  // inside the zero padding
    CHECK_THROWS(decode_getwork(badpad, target));
    CHECK_THROWS(decode_getwork(data.substr(2), target));
    CHECK_THROWS(decode_getwork(data, target.substr(2)));
}

TEST_MAIN()
