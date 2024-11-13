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
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"


using namespace rpcxx;
using namespace describe;

struct custom {};
struct more_custom : custom {
    constexpr bool operator()() {
        return true;
    }
};

template<typename T>
struct Victim : SkipMissing {
    T a, b, c;
};

template<typename T>
DESCRIBE(Victim<T>, &_::a, &_::b, &_::c)

using Current = Victim<int>;

constexpr auto vict = describe::Get<Current>();

namespace test_basic {

using SHOULD_HAVE = get_attrs_t<Current>;
using SHOULD_HAVE_ALSO = get_attrs_t<decltype(vict)>;
static_assert(std::is_same_v<SHOULD_HAVE, SHOULD_HAVE_ALSO>);

}

const auto a = vict.get<&Victim<int>::a>();
const auto b = vict.get<&Victim<int>::b>();
const auto c = vict.get<&Victim<int>::c>();

TEST_CASE("basic get") {
    CHECK(a.name == "a");
    CHECK(b.name == "b");
    CHECK(c.name == "c");
}

namespace idx {

const auto by_ind = vict.index_of(describe::by_index<1>(vict));
const auto mem_b = vict.index_of<&Victim<int>::b>();
const auto f_b = vict.index_of(b);
const auto name_b = vict.index_of("b");

TEST_CASE("idx get") {
    CHECK(by_ind == mem_b);
    CHECK(mem_b == f_b);
    CHECK(f_b == name_b);
}

}

template<auto i> struct NotEq : FieldValidator {
    static constexpr auto validate = [](auto val){
        if (val == i) {
            throw std::runtime_error("value is eq to: " + std::to_string(i));
        }
    };
};

template<typename T>
DESCRIBE_FIELD_ATTRS(Victim<T>, b, custom, NotEq<3>)
template<typename T>
DESCRIBE_FIELD_ATTRS(Victim<T>, c, more_custom, SkipMissing)

TEST_CASE("Attrs") {
    CHECK(describe::has_attr_v<SkipMissing, decltype(vict)>);
    CHECK(!describe::has_attr_v<SkipMissing, decltype(a)>);
    CHECK(describe::has_attr_v<custom, decltype(b)>);
    CHECK(describe::has_attr_v<NotEq<3>, decltype(b)>);
    CHECK(describe::has_attr_v<FieldValidator, decltype(b)>);
    CHECK(describe::has_attr_v<custom, decltype(c)>);
    CHECK(describe::extract_attr_t<custom, decltype(c)>()());
}

struct Inheritance : describe::Attrs<custom> {
    int c, d;
};
DESCRIBE(Inheritance, &_::c, &_::d)

constexpr auto chk = describe::Get<Inheritance>();
constexpr auto chkC = chk.get<&Inheritance::c>();
constexpr auto chkD = chk.get<&Inheritance::d>();

TEST_CASE("Inheritance") {
    CHECK(describe::has_attr_v<custom, decltype(chk)>);
    CHECK(!describe::has_attr_v<custom, decltype(chkC)>);
    CHECK(!describe::has_attr_v<custom, decltype(chkD)>);
}

struct Outside {};

DESCRIBE_ATTRS(Outside, custom)
static_assert(describe::has_attr_v<custom, Outside>);

enum Lol {
    kek, chebureck
};

enum class SecondLol {
    kek, chebureck
};

DESCRIBE(Lol, kek, chebureck)
DESCRIBE(SecondLol, _::kek, _::chebureck)

template<typename T> void testEnum() {
    const auto desc = describe::Get<Lol>();
    const auto dKek = desc.get<Lol::kek>();
    const auto dCheb = desc.get<Lol::chebureck>();
    CHECK(dKek.value == kek);
    CHECK(dKek.name == "kek");
    CHECK(dCheb.value == chebureck);
    CHECK(dCheb.name == "chebureck");
}

struct SelfValidate : ValidatedWith<SelfValidate> {
    int a;
    static void validate(SelfValidate&) {
        throw std::runtime_error("!");
    }
};

DESCRIBE(SelfValidate, &_::a)

static_assert(has_attr_v<ClassValidator, SelfValidate>);

TEST_CASE("describe atrrs") {
    testEnum<Lol>();
    testEnum<SecondLol>();
    Victim<int> v = {};
    a.get(v) = 1;
    CHECK(a.get(v) == 1);
    CHECK(b.get(v) == 0);
    CHECK_NOTHROW(Json::Parse(R"({"b": 2})")->GetTo(v));
    CHECK(b.get(v) == 2);
    CHECK(a.get(v) == 1);
    CHECK_THROWS(Json::Parse(R"({"b": 3})")->GetTo(v));
    SelfValidate self;
    CHECK_THROWS(Json::Parse(R"({"a": 3})")->GetTo(self));
}
