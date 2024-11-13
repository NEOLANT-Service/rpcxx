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

#include "rpcxx/rpcxx.hpp"
#include "json_samples.hpp"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

template<> struct doctest::StringMaker<rpcxx::Json> {
    static String convert(const rpcxx::Json& value) {
        return value.View().Dump().c_str();
    }
};
template<> struct doctest::StringMaker<rpcxx::JsonView> {
    static String convert(const rpcxx::JsonView& value) {
        return value.Dump().c_str();
    }
};

using namespace rpcxx;
using namespace describe;

TEST_CASE("json")
{
    SUBCASE("deep recursion") {
        constexpr size_t size = 500'000;
        string sample(size * 2, '[');
        std::fill(sample.begin() + size, sample.end(), ']');
        CHECK_THROWS((void)Json::Parse(sample));
    }
    SUBCASE("view basic") {
        JsonView json{5};
        CHECK_EQ(json.Get<int>(), 5);
        CHECK(DeepEqual(0, 0));
        json = "a";
        CHECK_EQ(json.Get<std::string>(), "a");
        CHECK_EQ(json.Get<string_view>(), "a");
    }
    SUBCASE("resize") {
        auto empty = MutableJson{t_array};
        auto json = empty.Copy();
        json.GetArray().resize(10);
    }
    SUBCASE("asign") {
        MutableJson wow;
        wow["a"] = string_view{"3123"};
        wow["a"] = 312311;
        wow["b"]["c"] = 3123;
        CHECK(wow["b"]["c"].GetInt() == 3123);
    }
    SUBCASE("conversion") {
        DefaultArena alloc;
        string_view raw = R"({"key": 123, "hello": "world", "arr": [true, "2", 3]})";
        auto orig = ParseJson(raw, alloc);
        auto persistent = orig.Get<Json>();
        auto back = JsonView::From(persistent, alloc);
        CHECK(DeepEqual(orig, back));
    }
}

TEST_CASE("algos")
{
    SUBCASE("basic") {
        DefaultArena alloc;
        auto json = Json::Parse(R"({"key": 123, "hello": "world", "arr": [true, "2", 3], "z": "w"})");
        auto flat = Flatten(json.View(), alloc);
        MutableJson back;
        MutableJson merged = MutableJson(json.View());
        Unflatten(back, flat);
        CHECK(DeepEqual(flat["/arr/0"], JsonView(true)));
        CHECK(DeepEqual(flat["/arr/1"], string_view{"2"}));
        CHECK(DeepEqual(flat["/arr/2"], 3));
        CHECK(DeepEqual(back.View(alloc), json.View()));

        auto patch = Json::Parse(R"({"lol": "kek"})");
        MergePatch(merged, patch.View());
        CHECK(merged.View(alloc)["lol"].GetString() == "kek");
        CHECK(patch.View()["lol"].GetString() == "kek");

        patch = Json::Parse(R"({"lol": [1, 2, 3]})");
        MergePatch(merged, patch.View());
        CHECK(merged.View(alloc)["lol"].Size() == 3);

        patch = Json::Parse(R"({"lol1": [1, 2, 3]})");
        MergePatch(merged, patch.View());
        patch = Json::Parse(R"({"lol": null})");
        MergePatch(merged, patch.View());
        CHECK(!merged.View(alloc).Find("lol"));

        CHECK(merged.View(alloc)["arr"].Size());
        patch = Json::Parse(R"({"arr": []})");
        MergePatch(merged, patch.View());
        CHECK(merged.View(alloc)["arr"].Size() == 0);
    }
}

TEST_CASE("parse json") {
    SUBCASE("fuzz victims") {
        std::vector samples = {
            "1e1",
        };
        for (auto& s: samples) {
            auto j = Json::Parse(s);
            auto b = Json::Parse(j->Dump());
            CHECK(DeepEqual(j.View(), b.View()));
        }
    }
    SUBCASE("alloc") {
        DefaultArena alloc;
        ArenaString str("123", alloc);
        str.Append(str);
        str.Append(str);
        str.Append(str);
        str.Append(str);
        ArenaVector<ArenaString> strs(alloc);
        ArenaVector<int> vec(alloc);
        for (auto i = 0; i < 10; ++i) {
            strs.push_back(str);
            vec.push_back(i);
        }
        vec.push_back(2);
        CHECK(vec[9] == 9);
        CHECK(strs[0] == str);
        CHECK(strs[9] == str);
    }
    GIVEN("basic") {
        DefaultArena alloc;
        ParseSettings opts;
        string_view raw = R"({
            "key": 123,
            "hello": "world",
            "arr": [
                true,
                "2",
                3,
                "false",
                false,
                {},
                [{}],
                "abrobrababor"
            ]
        })";
        auto run = [&]{
            auto json = ParseJson(raw, alloc, opts);
            auto key = json.At("key");
            CHECK_EQ(key.Get<int>(), 123);
            auto hello = json.At("hello");
            CHECK_EQ(hello.GetString(), "world");
            auto arr = json.At("arr");
            CHECK_EQ(arr.At(0).Get<bool>(), true);
            CHECK_EQ(arr.At(1).Get<string_view>(), "2");
            CHECK_EQ(arr.At(2).Get<int>(), 3);
            CHECK_EQ(arr.At(3).GetString(), "false");
            CHECK_EQ(arr.At(4).Get<bool>(), false);
        };
        run();
        opts.sorted = true;
        run();
    }
    GIVEN("escaped") {
        DefaultArena alloc;
        string_view raw = R"({"key": 123, "he\"llo": "wo\"rld", "arr": [true, "2", 3]})";
        auto mut = ParseJson(raw, alloc);
        CHECK(mut.At("he\"llo").GetString() == "wo\"rld");
    }
    GIVEN("non terminated") {
        DefaultArena alloc;
        char sample[] = {R"({"key": 123, "hello": "world"}123)"};
        CHECK_THROWS((void)ParseJson(sample, alloc));
        CHECK_NOTHROW((void)ParseJson(string_view{sample, sizeof(sample) - 4}, alloc));
    }
    GIVEN("books") {
        DefaultArena alloc;
        auto json = ParseJson(BooksSample, alloc);
        auto nested = json
            ["glossary"][1]
            ["GlossDiv"]
            ["GlossList"]
            ["GlossEntry"]
            ["GlossDef"]
            ["GlossSeeAlso"][0];
        CHECK_EQ(nested.GetString(), "GML");
    }
    GIVEN("big") {
        DefaultArena alloc;
        auto res = ParseJson(BigSample, alloc);
        CHECK(res.At("big1").Is(jv::t_array));
    }
    GIVEN("empties") {
        DefaultArena alloc;
        auto sample = R"({"array":[], "object": {}})";
        auto empty = ParseJson(sample, alloc);
        CHECK(empty["array"].Is(t_array));
        CHECK_EQ(empty["array"].Size(), 0);
        CHECK(empty["object"].Is(jv::t_object));
        CHECK_EQ(empty["object"].Size(), 0);
    }
    SUBCASE("dump") {
        DefaultArena alloc;
        GIVEN("rpc sample") {
            auto json = ParseJson(RPCSample, alloc);
            auto serialized = DumpJson(json);
            auto back = ParseJson(serialized, alloc);
            CHECK(DeepEqual(json, back));
        }
        GIVEN("books sample") {
            auto json = ParseJson(BooksSample, alloc);
            auto serialized = DumpJson(json);
            auto back = ParseJson(serialized, alloc);
            CHECK(DeepEqual(json, back));
        }
    }
}

enum Lol {
    kek, chebureck
};
DESCRIBE(Lol, kek, chebureck)

struct Nested {
    int a;
    std::string b;
    Lol en = kek;
};
DESCRIBE(Nested, &_::a, &_::b, &_::en)

namespace test {

struct Data {
    int a;
    uint32_t b;
    Nested nested;
    void Validate(JsonView json);

    int lol(int b);
};

DESCRIBE(test::Data, &_::a, &_::b, &_::nested, &_::lol)

TEST_CASE("json describe") {
    CHECK(describe::Get<test::Data>().fields_count == 3);
    CHECK(describe::Get<test::Data>().methods_count == 1);
    CHECK(describe::Get<test::Data>().index_of<&Data::a>() == 0);
    CHECK(describe::Get<test::Data>().index_of<&Data::b>() == 1);
    CHECK(describe::Get<test::Data>().index_of(Field<&Data::a>{}) == 0);
    CHECK(describe::Get<test::Data>().index_of(Field<&Data::b>{}) == 1);
}

}


struct Derived : test::Data {};

void test::Data::Validate(JsonView)
{
    if (b > 2000) {
        throw "b is too big!";
    }
}

TEST_CASE("deserialize")
{
    SUBCASE("basic") {
        auto sample = R"({
            "a":5, "b": 150,
            "nested": {"a": 123, "b": "avava", "en": "chebureck"}
        })";
        constexpr auto dataDesc = describe::Get<test::Data>();
        constexpr auto nestedDesc = describe::Get<Nested>();
        static_assert(dataDesc.name == "test::Data");
        static_assert(nestedDesc.name == "Nested");
        auto res = Json::Parse(sample).View().Get<test::Data>();
        CHECK_EQ(res.a, 5);
        CHECK_EQ(res.b, 150);
        CHECK_EQ(res.nested.a, 123);
        CHECK_EQ(res.nested.en, chebureck);
        CHECK_EQ(res.nested.b, "avava");
        auto sample_bad = R"({
            "a":5, "b": -15,
            "nested": {"a": 123, "b": 3}
        })";
        auto badJson = Json::Parse(sample_bad);
        CHECK_THROWS(badJson.View().Get<test::Data>());
    }
    SUBCASE("containers") {
        auto sample_map = R"({"a":5, "b": -15, "c": -15})";
        auto sample_vec = R"([54, -15])";
        using int_map = std::map<std::string, int>;
        using int_vec = std::vector<int>;
        static_assert(is_assoc_container_v<int_map>);
        static_assert(!is_index_container_v<int_map>);
        static_assert(!is_assoc_container_v<int_vec>);
        static_assert(is_index_container_v<int_vec>);
        auto map = Json::Parse(sample_map).View().Get<int_map>();
        CHECK_EQ(map["a"], 5);
        CHECK_EQ(map["b"], -15);
        auto vec = Json::Parse(sample_vec).View().Get<int_vec>();
        CHECK_EQ(vec.at(0), 54);
        CHECK_EQ(vec.at(1), -15);
    }
}

TEST_CASE("describe") {
    SUBCASE("dump struct") {
        DefaultArena alloc;
        auto lol = DumpStruct<test::Data>(alloc);
        CHECK(lol["a"].GetString() == "int32");
        CHECK(lol["b"].GetString() == "uint32");
        CHECK(lol["nested"]["a"].GetString() == "int32");
        CHECK(lol["nested"]["b"].GetString() == "string");
        CHECK(lol["nested"]["en"].GetString() == "Lol");
    }
    SUBCASE("methods") {
        auto desc = describe::Get<test::Data>();
        desc.for_each_field([](auto f){
            if (f.name == "lol") {
                CHECK(f.is_method);
            }
        });
    }
}

TEST_CASE("exceptions")
{
    auto sample = R"({
        "a":5, "b": 150,
        "nested": {"a": 123, "b": 3},
        "arr": [1.0, -2, 300],
        "empty": [],
        "empty_obj": {}
    })";
    auto raw = Json::Parse(sample);
    auto json = raw.View();

    CHECK_THROWS_AS(json.At("c"), KeyError);
    CHECK_THROWS_AS(json["empty_obj"].At("3"), KeyError);
    CHECK_THROWS_AS(json["arr"][2].Get<int8_t>(), IntRangeError);
    CHECK_THROWS_AS(json["arr"][1].Get<uint8_t>(), IntRangeError);
    CHECK_NOTHROW(json["arr"][2].Get<int16_t>());
    CHECK_NOTHROW(json["arr"][2].Get<double>());
    CHECK_NOTHROW(json["arr"][1].Get<int8_t>());

    CHECK_NOTHROW(json["arr"][1]);
    CHECK_THROWS_AS(json["arr"][3], IndexError);
    CHECK_THROWS_AS(json["empty"][0], IndexError);

    CHECK_THROWS_AS(json["arr"][0].Get<int>(), TypeMissmatch);
    CHECK_THROWS_AS(json["arr"][2].Size(), TypeMissmatch);
    CHECK_THROWS_AS(json["nested"][0], TypeMissmatch);
    CHECK_THROWS_AS(json.Get<test::Data>(), TypeMissmatch);
    CHECK_THROWS_AS(json["nested"].Get<Nested>(), TypeMissmatch);
}


TEST_CASE("serialize")
{
    SUBCASE("basic") {
        DefaultArena ctx;
        test::Data data{1, 2, {3, "123"}};
        auto res = JsonView::From(data, ctx);
        CHECK_EQ(res["a"].Get<int>(), 1);
        CHECK_EQ(res["b"].Get<int>(), 2);
        CHECK_EQ(res["nested"]["a"].Get<int>(), 3);
        CHECK_EQ(res["nested"]["b"].Get<string_view>(), "123");
        CHECK_EQ(res["nested"]["en"].Get<string_view>(), "kek");
    }
    SUBCASE("containers") {
        DefaultArena ctx;
        test::Data data{1, 2, {3, "123"}};
        std::vector<test::Data> vec{data, data, data};
        auto ser_vec = JsonView::From(vec, ctx);
        auto test_one = [](JsonView j){
            CHECK_EQ(j["a"].Get<int>(), 1);
            CHECK_EQ(j["b"].Get<int>(), 2);
            CHECK_EQ(j["nested"]["a"].Get<int>(), 3);
            CHECK_EQ(j["nested"]["b"].Get<string_view>(), "123");
        };
        test_one(ser_vec[0]);
        test_one(ser_vec[1]);
        test_one(ser_vec[2]);
        std::map<std::string, test::Data> map {
            {"lol", data},
            {"kek", data},
            {"cheburek", data}
        };
        auto ser_map = JsonView::From(map, ctx);
        test_one(ser_map["lol"]);
        test_one(ser_map["kek"]);
        test_one(ser_map["cheburek"]);
    }
}

