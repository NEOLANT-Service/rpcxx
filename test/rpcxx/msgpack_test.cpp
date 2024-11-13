// This file is a part of RPCXX project

/*
Copyright 2024 "NEOLANT Service", "NEOLANT Kalinigrad", Alexey Doronin, Anastasia Lugovets, Dmitriy Dyakonov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// Часть тест кейсов взята из nlohmann/json
// Lohmann, N. (2023). JSON for Modern C++ (Version 3.11.3) [Computer software]. https://github.com/nlohmann

#include "json_samples.hpp"
#include <iomanip>
#include <sstream>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace rpcxx;

#define SECTION(x) SUBCASE(x)

using json = Json;

json operator""_json(const char* s, size_t sz) {
    return json::Parse({s, sz});
}

static bool operator==(Json const& l, Json const& r) {
    return DeepEqual(l.View(), r.View());
}

static bool operator==(Json const& l, JsonView const& r) {
    return DeepEqual(l.View(), r);
}

template<> struct doctest::StringMaker<std::vector<uint8_t>> {
    static String convert(const std::vector<uint8_t>& value) {
        string res = "[";
        for (auto i: value) {
            res += "," + std::to_string(i);
        }
        res += "]";
        return res.c_str();
    }
};

std::vector<uint8_t> DumpAsVec(JsonView j) {
    membuff::StringOut<std::vector<uint8_t>> buff;
    DumpMsgPackInto(buff, j);
    return buff.Consume();
}

std::vector<uint8_t> DumpAsVec(Json j) {
    return DumpAsVec(j.View());
}

Json JsonFromVec(const std::vector<uint8_t>& bytes, bool = false, bool = false) {
    DefaultArena ctx;
    auto res = Json{ParseMsgPackInPlace(bytes.data(), bytes.size(), ctx).result};
    return res;
}

using doctest::Approx;

TEST_CASE("small fixstr") {
    DefaultArena ctx;
    JsonView sample("aboba");
    auto ser = DumpMsgPack(sample);
    auto back = ParseMsgPackInPlace(ser, ctx).result;
    CHECK(sample == back);
}
TEST_CASE("depth limit") {
    DefaultArena ctx;
    std::vector<uint8_t> sample(250, 0x91);
    sample.push_back(0xc0);
    CHECK_THROWS((void)ParseMsgPackInPlace(sample.data(), sample.size(), ctx, {30}));
    CHECK_NOTHROW((void)ParseMsgPackInPlace(sample.data(), sample.size(), ctx, {251}));
}
TEST_CASE("roundtrip") {
    DefaultArena ctx;
    auto do_one = [&](auto sample, size_t len) {
        auto json = ParseMsgPackInPlace(sample, len, ctx);
        auto serialized = DumpMsgPack(json);
        auto back = ParseMsgPackInPlace(serialized, ctx);
        CHECK(DeepEqual(json, back));
    };
    GIVEN("rpc sample") {
        do_one(MsgPackRPC, sizeof(MsgPackRPC));
    }
    GIVEN("books sample") {
        do_one(MsgPackBooks, sizeof(MsgPackBooks));
    }
}
TEST_CASE("parse") {
    GIVEN("rpc sample") {
        DefaultArena ctx;
        auto json = ParseMsgPackInPlace(MsgPackRPC, sizeof(MsgPackRPC), ctx).result;
        CHECK_EQ(json["params"][0].Get<int>(), 3);
    }
    GIVEN("books sample") {
        DefaultArena ctx;
        auto json = ParseMsgPackInPlace(MsgPackBooks, sizeof(MsgPackBooks), ctx).result;
        CHECK(json["glossary"][0]["title"].Get<string_view>() == "example glossary");
        CHECK(json["glossary"][2]
                    ["GlossDiv"]
                    ["GlossList"]
                    ["GlossEntry"]
                    ["GlossDef"]
                    ["GlossSeeAlso"]
                    [1].GetString() == "XML");
    }
}
TEST_CASE("big containers") {
    auto bigarr = MutableJson(t_array);
    DefaultArena ctx;
    bigarr.GetArray().resize(200);
    auto bigarrView = bigarr.View(ctx);
    auto pack = DumpMsgPack(bigarrView);
    auto back = ParseMsgPackInPlace(pack, ctx).result;
    CHECK(DeepEqual(back, bigarrView));
    auto bigobj = MutableJson(t_object);
    for (auto i = 100; i < 200; ++i) {
        bigobj[std::to_string(i)] = bigarr.Copy();
    }
    auto source = bigobj.View(ctx);
    pack = DumpMsgPack(source);
    back = ParseMsgPackInPlace(pack, ctx).result;
    CHECK(back == source);
}
TEST_CASE("null")
{
    json const j;
    std::vector<uint8_t> const expected = {0xc0};
    const auto result = DumpAsVec(j);
    CHECK(result == expected);

    // roundtrip
    CHECK(JsonFromVec(result) == j);
}

TEST_CASE("boolean")
{
    SECTION("true")
    {
        json const j {JsonView(true)};
        std::vector<uint8_t> const expected = {0xc3};
        const auto result = DumpAsVec(j);
        CHECK(result == expected);

        // roundtrip
        CHECK(JsonFromVec(result) == j);
    }

    SECTION("false")
    {
        json const j {JsonView(false)};
        std::vector<uint8_t> const expected = {0xc2};
        const auto result = DumpAsVec(j);
        CHECK(result == expected);

        // roundtrip
        CHECK(JsonFromVec(result) == j);
    }
}

TEST_CASE("number")
{
    SECTION("signed")
    {
        SECTION("-32..-1 (negative fixnum)")
        {
            for (auto i = -32; i <= -1; ++i)
            {
                CAPTURE(i);

                // create JSON value with integer number
                json const j {i};

                // check type
                CHECK(j.View().Is(t_signed | jv::t_unsigned));

                // create expected byte vector
                std::vector<uint8_t> const expected
                    {
                        static_cast<uint8_t>(i)
                    };

                // compare result + size
                const auto result = DumpAsVec(j);
                CHECK(result == expected);
                CHECK(result.size() == 1);

                // check individual bytes
                CHECK(static_cast<int8_t>(result[0]) == i);

                // roundtrip
                CHECK(JsonFromVec(result) == j);
            }
        }

        SECTION("0..127 (positive fixnum)")
        {
            for (size_t i = 0; i <= 127; ++i)
            {
                CAPTURE(i);

                // create JSON value with integer number
                json j {i};

                // check type
                j.View().AssertType(t_signed | t_unsigned);

                // create expected byte vector
                std::vector<uint8_t> const expected{static_cast<uint8_t>(i)};

                // compare result + size
                const auto result = DumpAsVec(j);
                CHECK(result == expected);
                CHECK(result.size() == 1);

                // check individual bytes
                CHECK(result[0] == i);

                // roundtrip
                CHECK(JsonFromVec(result) == j);
            }
        }

        SECTION("128..255 (int 8)")
        {
            for (size_t i = 128; i <= 255; ++i)
            {
                CAPTURE(i);

                // create JSON value with integer number
                json j {i};

                // check type
                j.View().AssertType(t_signed | t_unsigned);

                // create expected byte vector
                std::vector<uint8_t> const expected
                    {
                        0xcc,
                        static_cast<uint8_t>(i),
                    };

                // compare result + size
                const auto result = DumpAsVec(j);
                CHECK(result == expected);
                CHECK(result.size() == 2);

                // check individual bytes
                CHECK(result[0] == 0xcc);
                auto const restored = static_cast<uint8_t>(result[1]);
                CHECK(restored == i);

                // roundtrip
                CHECK(JsonFromVec(result) == j);
            }
        }

        SECTION("256..65535 (int 16)")
        {
            for (size_t i = 256; i <= 65535; ++i)
            {
                CAPTURE(i);

                // create JSON value with integer number
                json j {i};

                // check type
                j.View().AssertType(t_signed | t_unsigned);

                // create expected byte vector
                std::vector<uint8_t> const expected
                    {
                        0xcd,
                        static_cast<uint8_t>((i >> 8) & 0xff),
                        static_cast<uint8_t>(i & 0xff),
                    };

                // compare result + size
                const auto result = DumpAsVec(j);
                CHECK(result == expected);
                CHECK(result.size() == 3);

                // check individual bytes
                CHECK(result[0] == 0xcd);
                auto const restored = static_cast<uint16_t>(static_cast<uint8_t>(result[1]) * 256 + static_cast<uint8_t>(result[2]));
                CHECK(restored == i);

                // roundtrip
                CHECK(JsonFromVec(result) == j);
            }
        }

        SECTION("65536..4294967295 (int 32)")
        {
            for (uint32_t i :
                    {
                        65536u, 77777u, 1048576u, 4294967295u
                    })
            {
                CAPTURE(i);

                // create JSON value with integer number
                json j {i};

                // check type
                j.View().AssertType(t_signed | t_unsigned);

                // create expected byte vector
                std::vector<uint8_t> const expected
                    {
                        0xce,
                        static_cast<uint8_t>((i >> 24) & 0xff),
                        static_cast<uint8_t>((i >> 16) & 0xff),
                        static_cast<uint8_t>((i >> 8) & 0xff),
                        static_cast<uint8_t>(i & 0xff),
                    };

                // compare result + size
                const auto result = DumpAsVec(j);
                CHECK(result == expected);
                CHECK(result.size() == 5);

                // check individual bytes
                CHECK(result[0] == 0xce);
                uint32_t const restored = (static_cast<uint32_t>(result[1]) << 030) +
                                            (static_cast<uint32_t>(result[2]) << 020) +
                                            (static_cast<uint32_t>(result[3]) << 010) +
                                            static_cast<uint32_t>(result[4]);
                CHECK(restored == i);

                // roundtrip
                CHECK(JsonFromVec(result) == j);
            }
        }

        SECTION("4294967296..9223372036854775807 (int 64)")
        {
            for (uint64_t i :
                    {
                        4294967296LU, 9223372036854775807LU
                    })
            {
                CAPTURE(i);

                // create JSON value with integer number
                json j {i};

                // check type
                j.View().AssertType(t_signed | t_unsigned);

                // create expected byte vector
                std::vector<uint8_t> const expected
                    {
                        0xcf,
                        static_cast<uint8_t>((i >> 070) & 0xff),
                        static_cast<uint8_t>((i >> 060) & 0xff),
                        static_cast<uint8_t>((i >> 050) & 0xff),
                        static_cast<uint8_t>((i >> 040) & 0xff),
                        static_cast<uint8_t>((i >> 030) & 0xff),
                        static_cast<uint8_t>((i >> 020) & 0xff),
                        static_cast<uint8_t>((i >> 010) & 0xff),
                        static_cast<uint8_t>(i & 0xff),
                    };

                // compare result + size
                const auto result = DumpAsVec(j);
                CHECK(result == expected);
                CHECK(result.size() == 9);

                // check individual bytes
                CHECK(result[0] == 0xcf);
                uint64_t const restored = (static_cast<uint64_t>(result[1]) << 070) +
                                            (static_cast<uint64_t>(result[2]) << 060) +
                                            (static_cast<uint64_t>(result[3]) << 050) +
                                            (static_cast<uint64_t>(result[4]) << 040) +
                                            (static_cast<uint64_t>(result[5]) << 030) +
                                            (static_cast<uint64_t>(result[6]) << 020) +
                                            (static_cast<uint64_t>(result[7]) << 010) +
                                            static_cast<uint64_t>(result[8]);
                CHECK(restored == i);

                // roundtrip
                CHECK(JsonFromVec(result) == j);
            }
        }

        SECTION("-128..-33 (int 8)")
        {
            for (auto i = -128; i <= -33; ++i)
            {
                CAPTURE(i);

                // create JSON value with integer number
                json const j {i};

                // check type
                CHECK(j.View().Is(t_signed | t_unsigned));

                // create expected byte vector
                std::vector<uint8_t> const expected
                    {
                        0xd0,
                        static_cast<uint8_t>(i),
                    };

                // compare result + size
                const auto result = DumpAsVec(j);
                CHECK(result == expected);
                CHECK(result.size() == 2);

                // check individual bytes
                CHECK(result[0] == 0xd0);
                CHECK(static_cast<int8_t>(result[1]) == i);

                // roundtrip
                CHECK(JsonFromVec(result) == j);
            }
        }

        SECTION("-9263 (int 16)")
        {
            json const j {-9263};
            std::vector<uint8_t> const expected = {0xd1, 0xdb, 0xd1};

            const auto result = DumpAsVec(j);
            CHECK(result == expected);

            auto const restored = static_cast<int16_t>((result[1] << 8) + result[2]);
            CHECK(restored == -9263);

            // roundtrip
            CHECK(JsonFromVec(result) == j);
        }

        SECTION("-32768..-129 (int 16)")
        {
            for (int16_t i = -32768; i <= static_cast<std::int16_t>(-129); ++i)
            {
                CAPTURE(i);

                // create JSON value with integer number
                json const j {i};

                // check type
                CHECK(j.View().Is(t_unsigned | t_signed));

                // create expected byte vector
                std::vector<uint8_t> const expected
                    {
                        0xd1,
                        static_cast<uint8_t>((i >> 8) & 0xff),
                        static_cast<uint8_t>(i & 0xff),
                    };

                // compare result + size
                const auto result = DumpAsVec(j);
                CHECK(result == expected);
                CHECK(result.size() == 3);

                // check individual bytes
                CHECK(result[0] == 0xd1);
                auto const restored = static_cast<int16_t>((result[1] << 8) + result[2]);
                CHECK(restored == i);

                // roundtrip
                CHECK(JsonFromVec(result) == j);
            }
        }

        SECTION("-32769..-2147483648")
        {
            std::vector<int32_t> const numbers
                {
                    -32769,
                    -65536,
                    -77777,
                    -1048576,
                    -2147483648LL,
                };
            for (auto i : numbers)
            {
                CAPTURE(i);

                // create JSON value with integer number
                json const j {i};

                // check type
                CHECK(j->Is(jv::t_any_integer));

                // create expected byte vector
                std::vector<uint8_t> const expected
                    {
                        0xd2,
                        static_cast<uint8_t>((i >> 24) & 0xff),
                        static_cast<uint8_t>((i >> 16) & 0xff),
                        static_cast<uint8_t>((i >> 8) & 0xff),
                        static_cast<uint8_t>(i & 0xff),
                    };

                // compare result + size
                const auto result = DumpAsVec(j);
                CHECK(result == expected);
                CHECK(result.size() == 5);

                // check individual bytes
                CHECK(result[0] == 0xd2);
                uint32_t const restored = (static_cast<uint32_t>(result[1]) << 030) +
                                            (static_cast<uint32_t>(result[2]) << 020) +
                                            (static_cast<uint32_t>(result[3]) << 010) +
                                            static_cast<uint32_t>(result[4]);
                CHECK(static_cast<std::int32_t>(restored) == i);

                // roundtrip
                CHECK(JsonFromVec(result) == j);
            }
        }

        SECTION("-9223372036854775808..-2147483649 (int 64)")
        {
            std::vector<int64_t> const numbers
                {
                    (std::numeric_limits<int64_t>::min)(),
                    -2147483649LL,
                };
            for (auto i : numbers)
            {
                CAPTURE(i);

                // create JSON value with unsigned integer number
                json const j {i};

                // check type
                CHECK(j.View().Is(t_any_integer));

                // create expected byte vector
                std::vector<uint8_t> const expected
                    {
                        0xd3,
                        static_cast<uint8_t>((i >> 070) & 0xff),
                        static_cast<uint8_t>((i >> 060) & 0xff),
                        static_cast<uint8_t>((i >> 050) & 0xff),
                        static_cast<uint8_t>((i >> 040) & 0xff),
                        static_cast<uint8_t>((i >> 030) & 0xff),
                        static_cast<uint8_t>((i >> 020) & 0xff),
                        static_cast<uint8_t>((i >> 010) & 0xff),
                        static_cast<uint8_t>(i & 0xff),
                    };

                // compare result + size
                const auto result = DumpAsVec(j);
                CHECK(result == expected);
                CHECK(result.size() == 9);

                // check individual bytes
                CHECK(result[0] == 0xd3);
                int64_t const restored = (static_cast<int64_t>(result[1]) << 070) +
                                            (static_cast<int64_t>(result[2]) << 060) +
                                            (static_cast<int64_t>(result[3]) << 050) +
                                            (static_cast<int64_t>(result[4]) << 040) +
                                            (static_cast<int64_t>(result[5]) << 030) +
                                            (static_cast<int64_t>(result[6]) << 020) +
                                            (static_cast<int64_t>(result[7]) << 010) +
                                            static_cast<int64_t>(result[8]);
                CHECK(restored == i);

                // roundtrip
                CHECK(JsonFromVec(result) == j);
            }
        }
    }

    SECTION("unsigned")
    {
        SECTION("0..127 (positive fixnum)")
        {
            for (size_t i = 0; i <= 127; ++i)
            {
                CAPTURE(i);

                // create JSON value with unsigned integer number
                json const j {i};

                // check type
                CHECK(j->Is(t_unsigned));

                // create expected byte vector
                std::vector<uint8_t> const expected{static_cast<uint8_t>(i)};

                // compare result + size
                const auto result = DumpAsVec(j);
                CHECK(result == expected);
                CHECK(result.size() == 1);

                // check individual bytes
                CHECK(result[0] == i);

                // roundtrip
                CHECK(JsonFromVec(result) == j);
            }
        }

        SECTION("128..255 (uint 8)")
        {
            for (size_t i = 128; i <= 255; ++i)
            {
                CAPTURE(i);

                // create JSON value with unsigned integer number
                json const j {i};

                // check type
                CHECK(j->Is(t_unsigned));

                // create expected byte vector
                std::vector<uint8_t> const expected
                    {
                        0xcc,
                        static_cast<uint8_t>(i),
                    };

                // compare result + size
                const auto result = DumpAsVec(j);
                CHECK(result == expected);
                CHECK(result.size() == 2);

                // check individual bytes
                CHECK(result[0] == 0xcc);
                auto const restored = static_cast<uint8_t>(result[1]);
                CHECK(restored == i);

                // roundtrip
                CHECK(JsonFromVec(result) == j);
            }
        }

        SECTION("256..65535 (uint 16)")
        {
            for (size_t i = 256; i <= 65535; ++i)
            {
                CAPTURE(i);

                // create JSON value with unsigned integer number
                json const j {i};

                // check type
                CHECK(j->Is(t_unsigned));

                // create expected byte vector
                std::vector<uint8_t> const expected
                    {
                        0xcd,
                        static_cast<uint8_t>((i >> 8) & 0xff),
                        static_cast<uint8_t>(i & 0xff),
                    };

                // compare result + size
                const auto result = DumpAsVec(j);
                CHECK(result == expected);
                CHECK(result.size() == 3);

                // check individual bytes
                CHECK(result[0] == 0xcd);
                auto const restored = static_cast<uint16_t>(static_cast<uint8_t>(result[1]) * 256 + static_cast<uint8_t>(result[2]));
                CHECK(restored == i);

                // roundtrip
                CHECK(JsonFromVec(result) == j);
            }
        }

        SECTION("65536..4294967295 (uint 32)")
        {
            for (const uint32_t i :
                    {
                        65536u, 77777u, 1048576u, 4294967295u
                    })
            {
                CAPTURE(i);

                // create JSON value with unsigned integer number
                json const j {i};

                // check type
                CHECK(j->Is(t_unsigned));

                // create expected byte vector
                std::vector<uint8_t> const expected
                    {
                        0xce,
                        static_cast<uint8_t>((i >> 24) & 0xff),
                        static_cast<uint8_t>((i >> 16) & 0xff),
                        static_cast<uint8_t>((i >> 8) & 0xff),
                        static_cast<uint8_t>(i & 0xff),
                    };

                // compare result + size
                const auto result = DumpAsVec(j);
                CHECK(result == expected);
                CHECK(result.size() == 5);

                // check individual bytes
                CHECK(result[0] == 0xce);
                uint32_t const restored = (static_cast<uint32_t>(result[1]) << 030) +
                                            (static_cast<uint32_t>(result[2]) << 020) +
                                            (static_cast<uint32_t>(result[3]) << 010) +
                                            static_cast<uint32_t>(result[4]);
                CHECK(restored == i);

                // roundtrip
                CHECK(JsonFromVec(result) == j);
            }
        }

        SECTION("4294967296..18446744073709551615 (uint 64)")
        {
            for (const uint64_t i :
                    {
                        4294967296LU, 18446744073709551615LU
                    })
            {
                CAPTURE(i);

                // create JSON value with unsigned integer number
                json const j {i};

                // check type
                CHECK(j->Is(t_unsigned));

                // create expected byte vector
                std::vector<uint8_t> const expected
                    {
                        0xcf,
                        static_cast<uint8_t>((i >> 070) & 0xff),
                        static_cast<uint8_t>((i >> 060) & 0xff),
                        static_cast<uint8_t>((i >> 050) & 0xff),
                        static_cast<uint8_t>((i >> 040) & 0xff),
                        static_cast<uint8_t>((i >> 030) & 0xff),
                        static_cast<uint8_t>((i >> 020) & 0xff),
                        static_cast<uint8_t>((i >> 010) & 0xff),
                        static_cast<uint8_t>(i & 0xff),
                    };

                // compare result + size
                const auto result = DumpAsVec(j);
                CHECK(result == expected);
                CHECK(result.size() == 9);

                // check individual bytes
                CHECK(result[0] == 0xcf);
                uint64_t const restored = (static_cast<uint64_t>(result[1]) << 070) +
                                            (static_cast<uint64_t>(result[2]) << 060) +
                                            (static_cast<uint64_t>(result[3]) << 050) +
                                            (static_cast<uint64_t>(result[4]) << 040) +
                                            (static_cast<uint64_t>(result[5]) << 030) +
                                            (static_cast<uint64_t>(result[6]) << 020) +
                                            (static_cast<uint64_t>(result[7]) << 010) +
                                            static_cast<uint64_t>(result[8]);
                CHECK(restored == i);

                // roundtrip
                CHECK(JsonFromVec(result) == j);
            }
        }
    }

    SECTION("float")
    {
        SECTION("3.1415925")
        {
            double const v = 3.1415925;
            json const j {v};
            std::vector<uint8_t> const expected =
                {
                    0xcb, 0x40, 0x09, 0x21, 0xfb, 0x3f, 0xa6, 0xde, 0xfc
                };
            const auto result = DumpAsVec(j);
            CHECK(result == expected);

            // roundtrip
            CHECK(JsonFromVec(result) == j);
            CHECK(JsonFromVec(result) == v);
        }

        SECTION("1.0")
        {
            double const v = 1.0;
            json const j {v};
            std::vector<uint8_t> const expected = {
                0xcb,
                0x3f, 0xf0, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00
            };
            const auto result = DumpAsVec(j);
            CHECK(result == expected);

            // roundtrip
            CHECK(JsonFromVec(result) == j);
            CHECK(JsonFromVec(result) == v);
        }

        SECTION("128.128")
        {
            double const v = 128.1280059814453125;
            json const j {v};
            std::vector<uint8_t> const expected = {
                0xcb, 0x40, 0x60, 0x04, 0x18, 0xa0,
                0x00, 0x00, 0x00
            };
            const auto result = DumpAsVec(j);
            CHECK(result == expected);

            // roundtrip
            CHECK(JsonFromVec(result) == j);
            CHECK(JsonFromVec(result) == v);
        }
    }
}

TEST_CASE("string")
{
    SECTION("N = 0..31")
    {
        // explicitly enumerate the first byte for all 32 strings
        const std::vector<uint8_t> first_bytes =
            {
                0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8,
                0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1,
                0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
                0xbb, 0xbc, 0xbd, 0xbe, 0xbf
            };

        for (size_t N = 0; N < first_bytes.size(); ++N)
        {
            CAPTURE(N);

            // create JSON value with string containing of N * 'x'
            const auto s = std::string(N, 'x');
            json const j {JsonView(s)};

            // create expected byte vector
            std::vector<uint8_t> expected;
            expected.push_back(first_bytes[N]);
            for (size_t i = 0; i < N; ++i)
            {
                expected.push_back('x');
            }

            // check first byte
            CHECK((first_bytes[N] & 0x1f) == N);

            // compare result + size
            const auto result = DumpAsVec(j);
            CHECK(result == expected);
            CHECK(result.size() == N + 1);
            // check that no null byte is appended
            if (N > 0)
            {
                CHECK(result.back() != '\x00');
            }

            // roundtrip
            CHECK(JsonFromVec(result) == j);
        }
    }

    SECTION("N = 32..255")
    {
        for (size_t N = 32; N <= 255; ++N)
        {
            CAPTURE(N);

            // create JSON value with string containing of N * 'x'
            const auto s = std::string(N, 'x');
            json const j {JsonView(s)};

            // create expected byte vector
            std::vector<uint8_t> expected;
            expected.push_back(0xd9);
            expected.push_back(static_cast<uint8_t>(N));
            for (size_t i = 0; i < N; ++i)
            {
                expected.push_back('x');
            }

            // compare result + size
            const auto result = DumpAsVec(j);
            CHECK(result == expected);
            CHECK(result.size() == N + 2);
            // check that no null byte is appended
            CHECK(result.back() != '\x00');

            // roundtrip
            CHECK(JsonFromVec(result) == j);
        }
    }

    SECTION("N = 256..65535")
    {
        for (size_t N :
                {
                    256u, 999u, 1025u, 3333u, 2048u, 65535u
                })
        {
            CAPTURE(N);

            // create JSON value with string containing of N * 'x'
            const auto s = std::string(N, 'x');
            json const j {JsonView(s)};

            // create expected byte vector (hack: create string first)
            std::vector<uint8_t> expected(N, 'x');
            // reverse order of commands, because we insert at begin()
            expected.insert(expected.begin(), static_cast<uint8_t>(N & 0xff));
            expected.insert(expected.begin(), static_cast<uint8_t>((N >> 8) & 0xff));
            expected.insert(expected.begin(), 0xda);

            // compare result + size
            const auto result = DumpAsVec(j);
            CHECK(result == expected);
            CHECK(result.size() == N + 3);
            // check that no null byte is appended
            CHECK(result.back() != '\x00');

            // roundtrip
            CHECK(JsonFromVec(result) == j);
        }
    }

    SECTION("N = 65536..4294967295")
    {
        for (size_t N :
                {
                    65536u, 77777u, 1048576u
                })
        {
            CAPTURE(N);

            // create JSON value with string containing of N * 'x'
            const auto s = std::string(N, 'x');
            json const j = json{JsonView(s)};

            // create expected byte vector (hack: create string first)
            std::vector<uint8_t> expected(N, 'x');
            // reverse order of commands, because we insert at begin()
            expected.insert(expected.begin(), static_cast<uint8_t>(N & 0xff));
            expected.insert(expected.begin(), static_cast<uint8_t>((N >> 8) & 0xff));
            expected.insert(expected.begin(), static_cast<uint8_t>((N >> 16) & 0xff));
            expected.insert(expected.begin(), static_cast<uint8_t>((N >> 24) & 0xff));
            expected.insert(expected.begin(), 0xdb);

            // compare result + size
            const auto result = DumpAsVec(j);
            CHECK(result == expected);
            CHECK(result.size() == N + 5);
            // check that no null byte is appended
            CHECK(result.back() != '\x00');

            // roundtrip
            CHECK(JsonFromVec(result) == j);
        }
    }
}

TEST_CASE("array")
{
    SECTION("empty")
    {
        json const j = json{EmptyArray()};
        std::vector<uint8_t> const expected = {0x90};
        const auto result = DumpAsVec(j);
        CHECK(result == expected);

        // roundtrip
        CHECK(JsonFromVec(result) == j);
    }

    SECTION("[null]")
    {
        json const j = json::Parse("[null]");
        std::vector<uint8_t> const expected = {0x91, 0xc0};
        const auto result = DumpAsVec(j);
        CHECK(result == expected);

        // roundtrip
        CHECK(JsonFromVec(result) == j);
    }

    SECTION("[1,2,3,4,5]")
    {
        json const j = json::Parse("[1,2,3,4,5]");
        std::vector<uint8_t> const expected = {0x95, 0x01, 0x02, 0x03, 0x04, 0x05};
        const auto result = DumpAsVec(j);
        CHECK(result == expected);

        // roundtrip
        CHECK(JsonFromVec(result) == j);
    }

    SECTION("[[[[]]]]")
    {
        json const j = json::Parse("[[[[]]]]");
        std::vector<uint8_t> const expected = {0x91, 0x91, 0x91, 0x90};
        const auto result = DumpAsVec(j);
        CHECK(result == expected);

        // roundtrip
        CHECK(JsonFromVec(result) == j);
    }

    SECTION("array 16")
    {
        DefaultArena alloc;
        auto j = MutableJson(t_array);
        for (auto i = 0; i < 16; ++i) {
            j.GetArray().push_back(MutableJson(JsonView{nullptr}));
        }
        std::vector<uint8_t> expected(j.GetArray().size() + 3, 0xc0); // all null
        expected[0] = 0xdc; // array 16
        expected[1] = 0x00; // size (0x0010), byte 0
        expected[2] = 0x10; // size (0x0010), byte 1
        const auto result = DumpAsVec(j.View(alloc));
        CHECK(result == expected);

        // roundtrip
        CHECK(JsonFromVec(result) == j.View(alloc));
    }

    SECTION("array 32")
    {
        DefaultArena alloc;
        auto j = MutableJson(t_array);
        for (auto i = 0; i < 65536; ++i) {
            j.GetArray().push_back(MutableJson(JsonView{nullptr}));
        }
        std::vector<uint8_t> expected(j.GetArray().size() + 5, 0xc0); // all null
        expected[0] = 0xdd; // array 32
        expected[1] = 0x00; // size (0x00100000), byte 0
        expected[2] = 0x01; // size (0x00100000), byte 1
        expected[3] = 0x00; // size (0x00100000), byte 2
        expected[4] = 0x00; // size (0x00100000), byte 3
        const auto result = DumpAsVec(j.View(alloc));
        //CHECK(result == expected);

        CHECK(result.size() == expected.size());
        for (size_t i = 0; i < expected.size(); ++i)
        {
            CAPTURE(i);
            CHECK(result[i] == expected[i]);
        }

        // roundtrip
        CHECK(JsonFromVec(result) == j.View(alloc));
    }
}

TEST_CASE("object")
{
    SECTION("empty")
    {
        auto j = json::Parse("{}");
        std::vector<uint8_t> const expected = {0x80};
        const auto result = DumpAsVec(j);
        CHECK(result == expected);

        // roundtrip
        CHECK(JsonFromVec(result) == j);
    }

    SECTION("{\"\":null}")
    {
        json const j = json::Parse("{\"\":null}");
        std::vector<uint8_t> const expected = {0x81, 0xa0, 0xc0};
        const auto result = DumpAsVec(j);
        CHECK(result == expected);

        // roundtrip
        CHECK(JsonFromVec(result) == j);
    }

    SECTION("{\"a\": {\"b\": {\"c\": {}}}}")
    {
        json const j = json::Parse(R"({"a": {"b": {"c": {}}}})");
        std::vector<uint8_t> const expected =
            {
                0x81, 0xa1, 0x61, 0x81, 0xa1, 0x62, 0x81, 0xa1, 0x63, 0x80
            };
        const auto result = DumpAsVec(j);
        CHECK(result == expected);

        // roundtrip
        CHECK(JsonFromVec(result) == j);
    }

    SECTION("map 16")
    {
        json const j = R"({"00": null, "01": null, "02": null, "03": null,
                        "04": null, "05": null, "06": null, "07": null,
                        "08": null, "09": null, "10": null, "11": null,
                        "12": null, "13": null, "14": null, "15": null})"_json;

        const auto result = DumpAsVec(j);

        // Checking against an expected vector byte by byte is
        // difficult, because no assumption on the order of key/value
        // pairs are made. We therefore only check the prefix (type and
        // size) and the overall size. The rest is then handled in the
        // roundtrip check.
        CHECK(result.size() == 67); // 1 type, 2 size, 16*4 content
        CHECK(result[0] == 0xde); // map 16
        CHECK(result[1] == 0x00); // byte 0 of size (0x0010)
        CHECK(result[2] == 0x10); // byte 1 of size (0x0010)

        // roundtrip
        CHECK(JsonFromVec(result) == j);
    }

    SECTION("map 32")
    {
        DefaultArena alloc;
        auto j = MutableJson(t_object);
        for (auto i = 0; i < 65536; ++i)
        {
            // format i to a fixed width of 5
            // each entry will need 7 bytes: 6 for fixstr, 1 for null
            std::stringstream ss;
            ss << std::setw(5) << std::setfill('0') << i;
            j[ss.str()] = MutableJson(t_null);
        }
        auto source = j.View(alloc);
        CHECK(source.HasFlag(f_sorted));
        const auto result = DumpAsVec(source);

        // Checking against an expected vector byte by byte is
        // difficult, because no assumption on the order of key/value
        // pairs are made. We therefore only check the prefix (type and
        // size) and the overall size. The rest is then handled in the
        // roundtrip check.
        CHECK(result.size() == 458757); // 1 type, 4 size, 65536*7 content
        CHECK(result[0] == 0xdf); // map 32
        CHECK(result[1] == 0x00); // byte 0 of size (0x00010000)
        CHECK(result[2] == 0x01); // byte 1 of size (0x00010000)
        CHECK(result[3] == 0x00); // byte 2 of size (0x00010000)
        CHECK(result[4] == 0x00); // byte 3 of size (0x00010000)

        // roundtrip
        auto back = JsonFromVec(result);
        CHECK(back->HasFlag(f_sorted));
        CHECK(back == source);
    }
}

TEST_CASE("binary")
{
    SECTION("N = 0..255")
    {
        for (std::size_t N = 0; N <= 0xFF; ++N)
        {
            CAPTURE(N);

            // create JSON value with byte array containing of N * 'x'
            const auto s = std::vector<uint8_t>(N, 'x');
            json j = json(JsonView::Binary(string_view(reinterpret_cast<const char*>(s.data()), s.size())));

            // create expected byte vector
            std::vector<std::uint8_t> expected;
            expected.push_back(static_cast<std::uint8_t>(0xC4));
            expected.push_back(static_cast<std::uint8_t>(N));
            for (size_t i = 0; i < N; ++i)
            {
                expected.push_back(0x78);
            }

            // compare result + size
            const auto result = DumpAsVec(j);
            CHECK(result == expected);
            CHECK(result.size() == N + 2);
            // check that no null byte is appended
            if (N > 0)
            {
                CHECK(result.back() != '\x00');
            }

            // roundtrip
            CHECK(JsonFromVec(result) == j);
        }
    }

    SECTION("N = 256..65535")
    {
        for (std::size_t N :
                {
                    256u, 999u, 1025u, 3333u, 2048u, 65535u
                })
        {
            CAPTURE(N);

            // create JSON value with string containing of N * 'x'
            const auto s = std::vector<std::uint8_t>(N, 'x');
            json j = json(JsonView::Binary(string_view(reinterpret_cast<const char*>(s.data()), s.size())));

            // create expected byte vector (hack: create string first)
            std::vector<std::uint8_t> expected(N, 'x');
            // reverse order of commands, because we insert at begin()
            expected.insert(expected.begin(), static_cast<std::uint8_t>(N & 0xff));
            expected.insert(expected.begin(), static_cast<std::uint8_t>((N >> 8) & 0xff));
            expected.insert(expected.begin(), 0xC5);

            // compare result + size
            const auto result = DumpAsVec(j);
            CHECK(result == expected);
            CHECK(result.size() == N + 3);
            // check that no null byte is appended
            CHECK(result.back() != '\x00');

            // roundtrip
            CHECK(JsonFromVec(result) == j);
        }
    }

    SECTION("N = 65536..4294967295")
    {
        for (std::size_t N :
                {
                    65536u, 77777u, 1048576u
                })
        {
            CAPTURE(N);

            // create JSON value with string containing of N * 'x'
            const auto s = std::vector<std::uint8_t>(N, 'x');
            json j = json(JsonView::Binary(string_view(reinterpret_cast<const char*>(s.data()), s.size())));

            // create expected byte vector (hack: create string first)
            std::vector<uint8_t> expected(N, 'x');
            // reverse order of commands, because we insert at begin()
            expected.insert(expected.begin(), static_cast<std::uint8_t>(N & 0xff));
            expected.insert(expected.begin(), static_cast<std::uint8_t>((N >> 8) & 0xff));
            expected.insert(expected.begin(), static_cast<std::uint8_t>((N >> 16) & 0xff));
            expected.insert(expected.begin(), static_cast<std::uint8_t>((N >> 24) & 0xff));
            expected.insert(expected.begin(), 0xC6);

            // compare result + size
            const auto result = DumpAsVec(j);
            CHECK(result == expected);
            CHECK(result.size() == N + 5);
            // check that no null byte is appended
            CHECK(result.back() != '\x00');

            // roundtrip
            CHECK(JsonFromVec(result) == j);
        }
    }
}

TEST_CASE("from float32")
{
    auto given = std::vector<uint8_t>({0xca, 0x41, 0xc8, 0x00, 0x01});
    json const j = JsonFromVec(given);
    CHECK(j->Get<double>() == Approx(25.0000019073486));
}
