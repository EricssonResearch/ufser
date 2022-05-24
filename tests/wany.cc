#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h" //https://raw.githubusercontent.com/onqtam/doctest/master/doctest/doctest.h
#include <wany.h>
#include "tools.h"
#include <list>

using namespace std::string_view_literals;

auto const uf1 = uf::any("alef");
auto const uf1_str = R"(wv{type: [chunk{len: 1, buf: "s", mode: "writable"}], val: [chunk{len: 8, buf: "\x00\x00\x00\x04alef", mode: "writable"}]})";
auto const uf1_str2 = R"(wv{type: [chunk{len: 1, buf: "s", mode: "non-writable"}], val: [chunk{len: 8, buf: "\x00\x00\x00\x04alef", mode: "non-writable"}]})";

template <typename T> std::string print(const T& o) { return o.print(); }

TYPE_TO_STRING(uf::sview);
TYPE_TO_STRING(uf::gsview);
TYPE_TO_STRING(uf::tsview);

TEST_CASE_TEMPLATE("wv internals: sview", sview, uf::sview, uf::gsview, uf::tsview)
{
    sview a("aaa");
    REQUIRE(bool(a));
    REQUIRE(a->as_view() == "aaa");
    if constexpr (sview::has_refcount) REQUIRE(a.get_refcount() == 1);
    sview b(a);
    REQUIRE(bool(a));
    REQUIRE(a->as_view() == "aaa");
    if constexpr (sview::has_refcount) REQUIRE(a.get_refcount() == 2);
    REQUIRE(bool(b));
    REQUIRE(b->as_view() == "aaa");
    if constexpr (sview::has_refcount) REQUIRE(b.get_refcount() == 2);
    sview c(std::move(b));
    REQUIRE(bool(a));
    REQUIRE(a->as_view() == "aaa");
    if constexpr (sview::has_refcount) REQUIRE(a.get_refcount() == 2);
    REQUIRE(!bool(b));
    REQUIRE(bool(c));
    REQUIRE(c->as_view() == "aaa");
    if constexpr (sview::has_refcount) REQUIRE(c.get_refcount() == 2);
    b = std::move(c);
    REQUIRE(bool(a));
    REQUIRE(a->as_view() == "aaa");
    if constexpr (sview::has_refcount) REQUIRE(a.get_refcount() == 2);
    REQUIRE(!bool(c));
    REQUIRE(bool(b));
    REQUIRE(b->as_view() == "aaa");
    if constexpr (sview::has_refcount) REQUIRE(b.get_refcount() == 2);
    a = std::move(b);
    REQUIRE(bool(a));
    REQUIRE(a->as_view() == "aaa");
    if constexpr (sview::has_refcount) REQUIRE(a.get_refcount() == 1);
    REQUIRE(!bool(c));
    REQUIRE(!bool(b));
}

using pchunk = uf::impl::chunk<true, std::allocator>::ptr;
using gpchunk = uf::impl::chunk<false, uf::impl::GMonoAllocator>::ptr;
using tpchunk = uf::impl::chunk<false, uf::impl::TMonoAllocator>::ptr;
TYPE_TO_STRING(pchunk);
TYPE_TO_STRING(gpchunk);
TYPE_TO_STRING(tpchunk);

TEST_CASE_TEMPLATE("wv internals: chunk", chunk_ptr, pchunk, gpchunk, tpchunk)
{
    chunk_ptr a("aaa");
    REQUIRE(bool(a));
    REQUIRE(a->as_view() == "aaa");
    if constexpr (chunk_ptr::has_refcount) REQUIRE(a.get_refcount() == 1);
    chunk_ptr b(a);
    REQUIRE(bool(a));
    REQUIRE(a->as_view() == "aaa");
    if constexpr (chunk_ptr::has_refcount) REQUIRE(a.get_refcount() == 2);
    REQUIRE(bool(b));
    REQUIRE(b->as_view() == "aaa");
    if constexpr (chunk_ptr::has_refcount) REQUIRE(b.get_refcount() == 2);
    chunk_ptr c(std::move(b));
    REQUIRE(bool(a));
    REQUIRE(a->as_view() == "aaa");
    if constexpr (chunk_ptr::has_refcount) REQUIRE(a.get_refcount() == 2);
    REQUIRE(!bool(b));
    REQUIRE(bool(c));
    REQUIRE(c->as_view() == "aaa");
    if constexpr (chunk_ptr::has_refcount) REQUIRE(c.get_refcount() == 2);
    b = std::move(c);
    REQUIRE(bool(a));
    REQUIRE(a->as_view() == "aaa");
    if constexpr (chunk_ptr::has_refcount) REQUIRE(a.get_refcount() == 2);
    REQUIRE(!bool(c));
    REQUIRE(bool(b));
    REQUIRE(b->as_view() == "aaa");
    if constexpr (chunk_ptr::has_refcount) REQUIRE(b.get_refcount() == 2);
    a = std::move(b);
    REQUIRE(bool(a));
    REQUIRE(a->as_view() == "aaa");
    if constexpr (chunk_ptr::has_refcount) REQUIRE(a.get_refcount() == 1);
    REQUIRE(!bool(c));
    REQUIRE(!bool(b));
}

TYPE_TO_STRING(uf::wview);
TYPE_TO_STRING(uf::gwview);
TYPE_TO_STRING(uf::twview);


TEST_CASE_TEMPLATE("wv read consecutiveness", wview, uf::wview, uf::gwview, uf::twview) {
    auto const orig = uf::serialize(uf::any(uf::from_text, "<li>[14]"));
    wview const root({"a", false}, {std::string_view(orig), false});
    CHECK(root.get_consecutive_value());
    auto w = root[0];
    CHECK(w.get_consecutive_value());
    CHECK(root.get_consecutive_value());
}

TEST_CASE_TEMPLATE("wview", wview, uf::wview, uf::gwview, uf::twview) {
    CHECK_THROWS(wview("jozsi", "pista"));
    wview wv(uf1.type(), uf1.value());
    CHECK(std::string(wv) == uf1_str);
    wview wv2({ uf1.type(),false }, { uf1.value(), false });
    CHECK(std::string(wv2) == uf1_str2);

    auto len = wv.flatten_size();
    CHECK(len == 4 + 4);
    char buf[len];
    wv.flatten_to(buf);
    CHECK(!strncmp(buf, "\x00\x00\x00\x04""alef", len));
}

TEST_CASE_TEMPLATE("wview create", wview, uf::wview, uf::gwview, uf::twview) {
    CHECK(wview(true).template get_as<bool>() == true);
    CHECK(wview("", "").as_any().as_view().is_void());
}

TEST_CASE_TEMPLATE("wv any", wview, uf::wview, uf::gwview, uf::twview) {
    auto data = uf::serialize(uf1);
    wview w{"a", data}, w0;
    CHECK(w.typechar() == 'a');
    CHECK(w.size() == 1);
    CHECK_NOTHROW(w0 = w[0]);
    CHECK(w0.typechar() == 's');
    CHECK(w0.type() == "s"sv);
    CHECK(w0.value().as_view()== "\x00\x00\x00\x04""alef"sv);
    CHECK(std::string(w0) == uf1_str);
    CHECK(w.as_any().print() == "<a><s>\"alef\""sv);
    CHECK_THROWS_AS(w[1], std::out_of_range);
    CHECK_THROWS_AS(w[-1], std::out_of_range);

    //set the child
    CHECK_NOTHROW(w0.set(wview{"srt"}));
    CHECK(w0.as_string() == "srt");
    CHECK(w.as_any().print() == "<a><s>\"srt\""sv);
    CHECK_NOTHROW(w0.set(wview{"longer"}));
    CHECK(w0.as_string() == "longer");
    CHECK(w.as_any().print() == "<a><s>\"longer\""sv);
    CHECK_NOTHROW(w0.set(wview{13}));
    CHECK(w0.template get_as<int>() == 13);
    CHECK(w.as_any().print() == "<a><i>13"sv);
    CHECK_NOTHROW(w0.set_void());
    CHECK(w.as_any().print() == "<a><>"sv);
    CHECK(w0.as_any().print() == "<>"sv);
    CHECK_NOTHROW(w0.set(14.5));
    CHECK(w0.template get_as<double>() == 14.5);
    CHECK(w.as_any().print() == "<a><d>14.5"sv);
    
    // Now set the parent
    CHECK_NOTHROW(w.set(19));
    CHECK(w0.template get_as<double>() == 14.5);
    CHECK(w.as_any().print() == "<i>19"sv);
    CHECK_NOTHROW(w0.set(wview{ 13 })); //check if we are disowned...
    CHECK(w0.template get_as<int>() == 13);
    CHECK(w.as_any().print() == "<i>19"sv);
    
    //destroy parent
    auto ow = std::optional<wview>(std::in_place, "a", data); 
    CHECK_NOTHROW(w0 = (*ow)[0]);
    CHECK(w0.typechar() == 's');
    CHECK(w0.type() == "s"sv);
    CHECK(w0.value().as_view() == "\x00\x00\x00\x04""alef"sv);
    CHECK(std::string(w0) == uf1_str);
    CHECK_NOTHROW(ow.reset());
    CHECK(w0.as_any().print() == "<s>\"alef\"");

    //start with empty any and set
    CHECK_NOTHROW(w.set("a", "\x0\x0\x0\x0\x0\x0\x0\x0"sv));
    CHECK(w.as_any().print() == "<a><>"sv);
    CHECK(w[0].as_any().print() == "<>"sv);
    CHECK_NOTHROW(w[0].set(5));
    CHECK(w.as_any().print() == "<a><i>5"sv);
    CHECK(w[0].as_any().print() == "<i>5"sv);
}

TEST_CASE_TEMPLATE("wv any from split-chunk any", wview, uf::wview, uf::gwview, uf::twview) {
    wview w(uf::any{}), x(uf::any{}), x0 = x[0];
    x0.set(int32_t{17});
    w.set(x);
    w[0];
}

TEST_CASE_TEMPLATE("wv list", wview, uf::wview, uf::gwview, uf::twview) {
    std::vector<std::string> v{"alef", "bet"};
    wview w{v}, w0, w1;
    CHECK(w.type().as_view() == "ls");
    CHECK(w.typechar() == 'l');
    CHECK(w.size() == 2);
    CHECK(w.as_any().print() == "<ls>[\"alef\",\"bet\"]");
    CHECK_NOTHROW(w1 = w[1]);
    CHECK(w1.typechar() == 's');
    CHECK(w1.as_string() == v[1]);
    CHECK(w.as_any().print() == "<ls>[\"alef\",\"bet\"]");
    CHECK_NOTHROW(w0 = w[0]);
    CHECK(w0.typechar() == 's');
    CHECK(w0.as_string() == v[0]);
    CHECK(w.as_any().print() == "<ls>[\"alef\",\"bet\"]");

    CHECK_NOTHROW(w[0].set("alpha"));
    CHECK(w.as_any().print() == "<ls>[\"alpha\",\"bet\"]");
    CHECK_THROWS_AS(w[0].set(42), uf::type_mismatch_error);
    CHECK(w0.as_string() == "alpha");
    CHECK(w1.as_string() == "bet");
    CHECK(w.as_any().print() == "<ls>[\"alpha\",\"bet\"]");
    CHECK_NOTHROW(w[1].set("beta"));
    CHECK_THROWS_AS(w[2].set("gamma"), std::out_of_range);
    CHECK(w.as_any().print() == "<ls>[\"alpha\",\"beta\"]");
    CHECK(w0.as_any().print() == "<s>\"alpha\"");
    CHECK(w1.as_any().print() == "<s>\"beta\"");

    //set parent
    CHECK_NOTHROW(w.set(5));
    CHECK(w.as_any().print() == "<i>5");
    CHECK(w0.as_any().print() == "<s>\"alpha\"");
    CHECK(w1.as_any().print() == "<s>\"beta\"");
    CHECK_NOTHROW(w0.set(1.1));
    CHECK(w.as_any().print() == "<i>5");
    CHECK(w0.as_any().print() == "<d>1.1");
    CHECK(w1.as_any().print() == "<s>\"beta\"");
    CHECK_NOTHROW(w1.set(true));
    CHECK(w.as_any().print() == "<i>5");
    CHECK(w0.as_any().print() == "<d>1.1");
    CHECK(w1.as_any().print() == "<b>true");
    CHECK_THROWS_AS(w[0], uf::type_mismatch_error);

    //destroy parent
    auto ow = std::optional<wview>(std::in_place, v);
    CHECK_NOTHROW(w0 = (*ow)[0]);
    CHECK(ow->as_any().print() == "<ls>[\"alef\",\"bet\"]");
    CHECK(w0.as_any().print() == "<s>\"alef\"");
    CHECK_NOTHROW(ow.reset());
    CHECK(w0.as_any().print() == "<s>\"alef\"");
    if constexpr (wview::has_refcount) {
        //Destroying a parent wview::ptr may not actually destroy the vwiew 
        //(if others hold a ref to it or if we use a monotonic allocator)
        CHECK_NOTHROW(w0.set(1.1));
        CHECK(w0.as_any().print() == "<d>1.1");
    }
}

TEST_CASE_TEMPLATE("wv map", wview, uf::wview, uf::gwview, uf::twview) {
    std::map<int, std::string> v{ {42, "alef"}, {242, "bet"} };
    wview w{ v }, w0, w1;
    CHECK(w.type().as_view() == "mis");
    CHECK(w.typechar() == 'm');
    CHECK(w.size() == 2);
    CHECK(w.as_any().print() == "<mis>{42:\"alef\",242:\"bet\"}");
    CHECK_NOTHROW(w1 = w[1]);
    CHECK(w1.typechar() == 't');
    CHECK(w1.template get_as<std::pair<int, std::string>>().first == (++v.begin())->first);
    CHECK(w1.template get_as<std::pair<int, std::string>>().second== (++v.begin())->second);
    CHECK(w1.as_any().print() == "<t2is>(242,\"bet\")");
    CHECK_NOTHROW(w0 = w[0]);
    CHECK(w0.template get_as<std::pair<int, std::string>>().first == (v.begin())->first);
    CHECK(w0.template get_as<std::pair<int, std::string>>().second == (v.begin())->second);
    CHECK(w0.as_any().print() == "<t2is>(42,\"alef\")");

    wview wt{ std::pair(1000, "milla") };
    CHECK(wt.as_any().print() == "<t2is>(1000,\"milla\")");

    //set 
    CHECK_NOTHROW(w1.set(std::pair(1000, "milla")));
    CHECK(w.as_any().print() == "<mis>{42:\"alef\",1000:\"milla\"}");
    CHECK(w0.as_any().print() == "<t2is>(42,\"alef\")");
    CHECK(w1.as_any().print() == "<t2is>(1000,\"milla\")");
    CHECK_THROWS_AS(w1.set(std::pair(1, 2)), uf::type_mismatch_error);
    CHECK(w0[0].set(1));
    CHECK(w.as_any().print() == "<mis>{1:\"alef\",1000:\"milla\"}");
    CHECK(w0.as_any().print() == "<t2is>(1,\"alef\")");
    CHECK(w1.as_any().print() == "<t2is>(1000,\"milla\")");
    CHECK_THROWS_AS(w0[1].set(1), uf::type_mismatch_error);

    //set parent
    CHECK_NOTHROW(w.set(std::vector{ 1000, 1001 }));
    CHECK(w.as_any().print() == "<li>[1000,1001]");
    CHECK(w0.as_any().print() == "<t2is>(1,\"alef\")");
    CHECK(w1.as_any().print() == "<t2is>(1000,\"milla\")");
    CHECK_NOTHROW(w1.set(std::pair(10, 2)));
    CHECK(w.as_any().print() == "<li>[1000,1001]");
    CHECK(w0.as_any().print() == "<t2is>(1,\"alef\")");
    CHECK(w1.as_any().print() == "<t2ii>(10,2)");

    //destroy parent
    auto ow = std::optional<wview>(std::in_place, v);
    CHECK_NOTHROW(w0 = (*ow)[0]);
    CHECK_NOTHROW(w1 = (*ow)[1]);
    CHECK(ow->as_any().print() == "<mis>{42:\"alef\",242:\"bet\"}");
    CHECK(w0.as_any().print() == "<t2is>(42,\"alef\")");
    CHECK(w1.as_any().print() == "<t2is>(242,\"bet\")");
    CHECK_NOTHROW(ow.reset());
    CHECK(w0.as_any().print() == "<t2is>(42,\"alef\")");
    CHECK(w1.as_any().print() == "<t2is>(242,\"bet\")");
    if constexpr (wview::has_refcount) {
        //Destroying a parent wview::ptr may not actually destroy the vwiew 
        //(if others hold a ref to it or if we use a monotonic allocator)
        CHECK_NOTHROW(w1.set(std::pair(1, 2)));
        CHECK(w0.as_any().print() == "<t2is>(42,\"alef\")");
        CHECK(w1.as_any().print() == "<t2ii>(1,2)");
    }
}

TEST_CASE_TEMPLATE("wv list of any", wview, uf::wview, uf::gwview, uf::twview) {
    uf::any x1(int(13));
    uf::any x2(double(13.4));
    uf::any x3("arglebargle");
    std::vector<uf::any> v{x1, x2, x3};
    wview w{v}, w1, w10;
    CHECK(w.type().as_view() == "la");
    CHECK(w.size() == 3);
    CHECK(w.as_any().print() == "<la>[<i>13,<d>13.4,<s>\"arglebargle\"]");
    CHECK_NOTHROW(w1 = w[1]);
    CHECK(w.as_any().print() == "<la>[<i>13,<d>13.4,<s>\"arglebargle\"]");
    CHECK(w1.as_any().print() == "<a><d>13.4");
    CHECK_NOTHROW(w10 = w1[0]);
    CHECK(w.as_any().print() == "<la>[<i>13,<d>13.4,<s>\"arglebargle\"]");
    CHECK(w1.as_any().print() == "<a><d>13.4");
    CHECK(w10.as_any().print() == "<d>13.4");
    std::string w10_first(w10);
    CHECK_NOTHROW(w1.set(uf::any("jozsi")));
    CHECK(w.as_any().print() == "<la>[<i>13,<s>\"jozsi\",<s>\"arglebargle\"]");
    CHECK(w1.as_any().print() == "<a><s>\"jozsi\"");
    CHECK(w10.as_any().print() == "<d>13.4");
    //TODO: self-assign
}

TEST_CASE_TEMPLATE("wv tuple", wview, uf::wview, uf::gwview, uf::twview) {
    std::tuple v{ "alef", "bet" };
    wview w{ v }, w0, w1;
    CHECK(w.type().as_view() == "t2ss");
    CHECK(w.typechar() == 't');
    CHECK(w.size() == 2);
    CHECK(w.as_any().print() == "<t2ss>(\"alef\",\"bet\")");
    CHECK_NOTHROW(w1 = w[1]);
    CHECK(w1.typechar() == 's');
    CHECK(w1.as_string() == std::get<1>(v));
    CHECK_NOTHROW(w0 = w[0]);
    CHECK(w0.typechar() == 's');
    CHECK(w0.as_string() == std::get<0>(v));

    CHECK_NOTHROW(w[0].set("alpha"));
    CHECK(w.as_any().print() == "<t2ss>(\"alpha\",\"bet\")");
    CHECK(w0.as_any().print() == "<s>\"alpha\"");
    CHECK(w1.as_any().print() == "<s>\"bet\"");
    CHECK_NOTHROW(w[0].set(42));
    CHECK(w.as_any().print() == "<t2is>(42,\"bet\")");
    CHECK(w0.as_any().print() == "<i>42");
    CHECK(w1.as_any().print() == "<s>\"bet\"");
    CHECK_NOTHROW(w[1].set("beta"));
    CHECK(w.as_any().print() == "<t2is>(42,\"beta\")");
    CHECK(w0.as_any().print() == "<i>42");
    CHECK(w1.as_any().print() == "<s>\"beta\"");
    CHECK_THROWS_AS(w[2].set("gamma"), std::out_of_range);
    CHECK_THROWS_WITH_AS(w[1].set("",""), "Cannot set element of <t2i*s> to <void>.", uf::type_mismatch_error);

    //set parent
    CHECK_NOTHROW(w.set(5));
    CHECK(w.as_any().print() == "<i>5");
    CHECK(w0.as_any().print() == "<i>42");
    CHECK(w1.as_any().print() == "<s>\"beta\"");
    CHECK_NOTHROW(w0.set(1.1));
    CHECK(w.as_any().print() == "<i>5");
    CHECK(w0.as_any().print() == "<d>1.1");
    CHECK(w1.as_any().print() == "<s>\"beta\"");
    CHECK_NOTHROW(w1.set(true));
    CHECK(w.as_any().print() == "<i>5");
    CHECK(w0.as_any().print() == "<d>1.1");
    CHECK(w1.as_any().print() == "<b>true");
    CHECK_THROWS_AS(w[0], uf::type_mismatch_error);

    //destroy parent
    auto ow = std::optional<wview>(std::in_place, v);
    CHECK_NOTHROW(w0 = (*ow)[0]);
    CHECK(ow->as_any().print() == "<t2ss>(\"alef\",\"bet\")");
    CHECK(w0.as_any().print() == "<s>\"alef\"");
    CHECK_NOTHROW(ow.reset());
    CHECK(w0.as_any().print() == "<s>\"alef\"");
    CHECK_NOTHROW(w0.set(1.1));
    CHECK(w0.as_any().print() == "<d>1.1");
}

template <typename T, typename wview>
bool check_same(const wview wv, const T& t) { return wv.template get_as<T>() == t; }

void remove_typestring(std::string& s)
{
    if (size_t pos = s.find_first_of('>'); pos != std::string::npos)
        s.erase(0, pos + 1);
}

using complex = std::map<int, std::vector<std::pair<double, std::string>>>;
template <typename wview>
auto test_str(const complex & v, const wview &w, std::string_view msg)
{
    std::string s;
    for (unsigned i = 0; i < v.size(); i++) {
//std::cout<<msg<<",p:"<<p.as_any().print()<<std::endl;
//std::cout<<msg<<",p["<<i<<"]:"<<p[i].print()<<std::endl;
//std::cout<<msg<<",p["<<i<<"]:"<<p[i].as_any().print()<<std::endl;
        auto& ii = *std::next(v.begin(), i);
        std::vector<std::string> ss;
        for (unsigned j = 0; j < ii.second.size(); j++) {
//std::cout<<msg<<",p["<<i<<"][1]:"<<p[i][1].print()<<std::endl;
//std::cout<<msg<<",p["<<i<<"][1]["<<j<<"]:"<<p[i][1][j].print()<<std::endl;
//std::cout<<msg<<",p["<<i<<"][1]["<<j<<"]:"<<p[i][1][j].as_any().print()<<std::endl;
            auto& jj = ii.second.at(j);
            std::vector<std::string> sss;
            //push concrete values 
            sss.push_back(uf::any(std::get<0>(jj)).print());
            CHECK_MESSAGE(sss.back() == w[i][1][j][0].as_any().print(), uf::concat(msg,",i=",i,",j=",j));
            remove_typestring(sss.back());
            sss.push_back(uf::any(std::get<1>(jj)).print());
            CHECK_MESSAGE(sss.back() == w[i][1][j][1].as_any().print(), uf::concat(msg, ",i=", i, ",j=", j));
            remove_typestring(sss.back());
            std::string jo = Join(sss, ",");
            ss.push_back(uf::concat('<', uf::serialize_type<decltype(jj)>(), ">(", jo, ')'));
            CHECK_MESSAGE(ss.back() == w[i][1][j].as_any().print(), uf::concat(msg, ",i=", i, ",j=", j));
            remove_typestring(ss.back());
        }
        std::string jo = Join(ss, ",");
        std::string t2 = uf::concat('<', uf::serialize_type<decltype(ii.second)>(), ">[", jo, ']');
        CHECK_MESSAGE(t2 == w[i][1].as_any().print(), uf::concat(msg, ",i=", i));
        remove_typestring(t2);
        if (s.length()) s.push_back(',');
        s += uf::concat(i, ':', t2);
    }
    s = uf::concat('<', uf::serialize_type<complex>(), ">{", s, '}');
    CHECK_MESSAGE(s == w.as_any().print(), std::string(msg));
}

TEST_CASE_TEMPLATE("wv multi-layer", wview, uf::wview, uf::gwview, uf::twview) {
    complex v =
    { {0, {{1.2, "aa"}, {2.3, "bb"}}}, {1, {{9.2, "cc"}, {6.7, "dd"}}} };

    wview w(v);
    test_str(v, w, "basic");

    CHECK_NOTHROW(w[0][1][0][0].set(0.12));
    v[0][0].first = 0.12;
    test_str(v, w, "1st change");
    CHECK_NOTHROW(w[0][1][1][0].set(0.23));
    v[0][1].first = 0.23;
    test_str(v, w, "2nd change");
    CHECK_NOTHROW(w[0][1][0][1].set("cc"));
    v[0][0].second = "cc";
    test_str(v, w, "3rd change");
    CHECK_THROWS_AS(w[0][1][0][1].set(1), uf::type_mismatch_error);

    auto w01 = w[0][1], w11 = w[1][1], w111 = w11[1];
    CHECK(w111.as_any().print() == "<t2ds>(6.7,\"dd\")");
    w11.set(w01); //w111 is now disowned
    auto v1 = v[1];
    v[1] = v[0];
    test_str(v, w, "large set");
    w111.set(std::pair(6.8, "dd+"));
    CHECK(w111.as_any().print() == "<t2ds>(6.8,\"dd+\")");
    w111.set(std::pair(6.9, "ddd+"));
    CHECK(w111.as_any().print() == "<t2ds>(6.9,\"ddd+\")");
    test_str(v, w, "disowned children set"); //unchanged p
    w11[0][1].set("new!");
    v[1][0].second = "new!";
    test_str(v, w, "owned child set");
}

TEST_CASE_TEMPLATE("wv self ref", wview, uf::wview, uf::gwview, uf::twview) {
    //Here we test if we assign an object to its parent or to its child.
    std::pair<std::pair<std::pair<int, std::string>, double>, std::optional<int>> v = { {{5, "aa"}, 3.4}, 42 };
    wview w(v);
    CHECK(w.as_any().print() == "<t2t2t2isdoi>(((5,\"aa\"),3.4),42)");
    REQUIRE_NOTHROW(w[0][0][0].set(w[0][0]));
    CHECK(w.as_any().print() == "<t2t2t2t2issdoi>((((5,\"aa\"),\"aa\"),3.4),42)");
    REQUIRE_NOTHROW(w[0].set(w[0][0][0]));
    CHECK(w.as_any().print() == "<t2t2isoi>((5,\"aa\"),42)");
}

TEST_CASE_TEMPLATE("wv optional", wview, uf::wview, uf::gwview, uf::twview) {
    std::pair<std::string, std::optional<std::string>> v("aa", {});
    wview w(v);
    CHECK(w.as_any().print() == "<t2sos>(\"aa\",)");
    //REQUIRE_NOTHROW(p[1][0].set("bb"));
    //CHECK(p.as_any().print() == "<t2sos>(\"aa\",\"bb\")");
    //CHECK_THROWS_AS(p[1][0].set(5), uf::type_mismatch_error);
    //TODO: how to insert/delete an optional?

    v.second = "bb";
    w.set(v);
    CHECK(w.as_any().print() == "<t2sos>(\"aa\",\"bb\")");
    REQUIRE_NOTHROW(w[1][0].set("cc"));
    CHECK(w.as_any().print() == "<t2sos>(\"aa\",\"cc\")");
    CHECK_THROWS_AS(w[1][0].set(5), uf::type_mismatch_error);
}

TEST_CASE_TEMPLATE("wv expected", wview, uf::wview, uf::gwview, uf::twview) {
    std::pair<std::string, uf::expected<std::string>> v("aa", "bb");
    wview w(v);
    CHECK(w.as_any().print() == "<t2sxs>(\"aa\",\"bb\")");
    REQUIRE_NOTHROW(w[1][0].set("bbbb"));
    CHECK(w.as_any().print() == "<t2sxs>(\"aa\",\"bbbb\")");
    CHECK_THROWS_AS(w[1][0].set(5), uf::type_mismatch_error);
    REQUIRE_NOTHROW(w[1][0].set(uf::error_value("type", "message", uf::any("params"))));
    CHECK(w.as_any().print() == "<t2sxs>(\"aa\",err(\"type\",\"message\",<s>\"params\"))");
    REQUIRE_NOTHROW(w[1][0].set("bbbaaa"));
    CHECK(w.as_any().print() == "<t2sxs>(\"aa\",\"bbbaaa\")");

    uf::expected<void> X;
    w.set(X);
    CHECK(w.as_any().print() == "<X>");
    CHECK(w[0].as_any().as_view().is_void());
    REQUIRE_NOTHROW(w[0].set(uf::error_value("a","b","c")));
    CHECK(w.as_any().print() == "<X>err(\"a\",\"b\",<s>\"c\")");
    CHECK(w[0].as_any().print() == "<e>err(\"a\",\"b\",<s>\"c\")");
    CHECK(w[0][1].as_any().print()=="<s>\"b\"");
    REQUIRE_THROWS_AS(w[0].set(4), uf::type_mismatch_error);
    REQUIRE_NOTHROW(w[0].set_void());
    CHECK(w.as_any().print() == "<X>");
    CHECK(w[0].as_any().as_view().is_void());
}

TEST_CASE_TEMPLATE("wv error", wview, uf::wview, uf::gwview, uf::twview) {
    uf::error_value e("a", "b", uf::any(5));
    wview w(e), w0, w1, w2;
    CHECK(w.as_any().print() == "<e>err(\"a\",\"b\",<i>5)");
    CHECK(w[0].as_any().print() == "<s>\"a\"");
    CHECK(w[1].as_any().print() == "<s>\"b\"");
    CHECK(w[2].as_any().print() == "<a><i>5");
    REQUIRE_THROWS_AS(w[3], std::out_of_range);
    REQUIRE_THROWS_AS(w[0].set(4), uf::type_mismatch_error);
    REQUIRE_THROWS_AS(w[1].set(4), uf::type_mismatch_error);
    REQUIRE_THROWS_AS(w[2].set(4), uf::type_mismatch_error);
    REQUIRE_NOTHROW(w0 = w[0]);
    REQUIRE_NOTHROW(w1 = w[1]);
    REQUIRE_NOTHROW(w2 = w[2]);
    REQUIRE_NOTHROW(w[0].set("aa"));
    CHECK(w.as_any().print() == "<e>err(\"aa\",\"b\",<i>5)");
    CHECK(w[0].as_any().print() == "<s>\"aa\"");
    CHECK(w[1].as_any().print() == "<s>\"b\"");
    CHECK(w[2].as_any().print() == "<a><i>5");
    REQUIRE_NOTHROW(w[2][0].set("any"));
    CHECK(w.as_any().print() == "<e>err(\"aa\",\"b\",<s>\"any\")");
    CHECK(w[0].as_any().print() == "<s>\"aa\"");
    CHECK(w[1].as_any().print() == "<s>\"b\"");
    CHECK(w[2].as_any().print() == "<a><s>\"any\"");
    CHECK(w0.as_any().print() == "<s>\"aa\"");
    CHECK(w1.as_any().print() == "<s>\"b\"");
    CHECK(w2.as_any().print() == "<a><s>\"any\"");
    w.set_void();
    CHECK(w0.as_any().print() == "<s>\"aa\"");
    CHECK(w1.as_any().print() == "<s>\"b\"");
    CHECK(w2.as_any().print() == "<a><s>\"any\"");
}

template <typename wview>
void random_delete(wview w, std::string_view msg) {
    std::vector<int> indices(w.size());
    iota(indices.begin(), indices.end(), 0);
    std::random_shuffle(indices.begin(), indices.end());
    std::string message = uf::concat("random delete (", msg, "): ", Join(indices, ","));
    const int total = indices.size();
    while (indices.size()) {
        int i = indices.front();
        CHECK_NOTHROW_MESSAGE(w.erase(i), uf::concat(message, ". erase #", total-indices.size()));
        indices.erase(indices.begin());
        for (int& ii : indices)
            if (ii > i) --ii;
    }
    CHECK_MESSAGE(w.size() == 0, message);
}

TEST_CASE_TEMPLATE("wv insert/delete list", wview, uf::wview, uf::gwview, uf::twview) {
    std::vector<int> v = { 1,2,3,4 };
    wview w(v), wf = w[0];
    CHECK_THROWS_AS(w.erase(4), std::out_of_range);
    CHECK(w.as_any().print() == "<li>[1,2,3,4]");
    CHECK(w.size() == 4);
    CHECK_NOTHROW(w.erase(0)); //erase from front
    CHECK(w.as_any().print() == "<li>[2,3,4]");
    CHECK(wf.as_any().print() == "<i>1");
    CHECK_NOTHROW(wf.set('a'));
    CHECK(wf.as_any().print() == "<c>'a'");
    CHECK(w.as_any().print() == "<li>[2,3,4]");
    CHECK_NOTHROW(w.erase(1)); //erase from middle
    CHECK(w.as_any().print() == "<li>[2,4]");
    CHECK_NOTHROW(w.erase(w[1])); //erase from end
    CHECK(w.as_any().print() == "<li>[2]");
    CHECK_NOTHROW(w.erase(0)); //erase to empty
    CHECK(w.as_any().print() == "<li>[]");
    CHECK(w.size() == 0);
    CHECK_THROWS_AS(w.erase(0), std::out_of_range);
    //inserts
    CHECK_THROWS_AS(w.insert_after(2, wview()), std::out_of_range);
    CHECK_THROWS_AS(w.insert_after(-1, wview(1.1)), uf::type_mismatch_error);
    CHECK_NOTHROW(w.insert_after(-1, wview(5)));
    CHECK(w.as_any().print() == "<li>[5]");
    CHECK_NOTHROW(w.insert_after(-1, wview(3)));
    CHECK(w.as_any().print() == "<li>[3,5]");
    CHECK_NOTHROW(w.insert_after(0, wview(4)));
    CHECK(w.as_any().print() == "<li>[3,4,5]");
    CHECK_NOTHROW(w.insert_after(2, wview(6)));
    CHECK(w.as_any().print() == "<li>[3,4,5,6]");
    wview i(42);
    CHECK_NOTHROW(w.insert_after(2, i));
    CHECK(w.as_any().print() == "<li>[3,4,5,42,6]");
    CHECK_NOTHROW(i.set(43));
    CHECK(w.as_any().print() == "<li>[3,4,5,42,6]");
    CHECK_THROWS_AS(w.insert_after(5, i), std::out_of_range);
    CHECK_THROWS_AS(w.insert_after(4, wview{ false }), uf::type_mismatch_error);
    //delete again
    random_delete(w, "list");
}

TEST_CASE_TEMPLATE("wv insert/delete list 2", wview, uf::wview, uf::gwview, uf::twview) {
    auto from_dal = "\x00\x00\x00\x02li\x00\x00\x00\x10\x00\x00\x00\x03\x00\x00\x00\x0d\x00\x00\x00\x0e\x00\x00\x00\x0f"sv;
    wview w({"li"sv, false}, {std::string_view{from_dal.data() + 10, 16}, false});
    CHECK(w.as_any().print() == "<li>[13,14,15]");
    w.erase(1);
    CHECK(w.size() == 2);
    wview a({"a", false}, {from_dal, false});
    w = a[0];
    CHECK(w.as_any().print() == "<li>[13,14,15]");
    CHECK(w.size() == 3);
    w.erase(1);
    CHECK(w.size() == 2);
    CHECK(w[1].as_any().print() == "<i>15");
    CHECK(w.as_any().print() == "<li>[13,15]");
}

TEST_CASE_TEMPLATE("wv insert/delete map", wview, uf::wview, uf::gwview, uf::twview) {
    std::map<int, std::string> m = { {1,"a"},{2,"b"},{3,"c"},{4,"d"} };
    wview w(m), wf = w[0];
    CHECK_THROWS_AS(w.erase(4), std::out_of_range);
    CHECK_THROWS_AS(w.erase(w), std::invalid_argument);
    CHECK(w.size() == 4);
    CHECK(w.as_any().print() == "<mis>{1:\"a\",2:\"b\",3:\"c\",4:\"d\"}");
    CHECK_NOTHROW(w.erase(0)); //erase from front
    CHECK(w.as_any().print() == "<mis>{2:\"b\",3:\"c\",4:\"d\"}");
    CHECK(wf.as_any().print() == "<t2is>(1,\"a\")");
    CHECK_NOTHROW(wf.set('a'));
    CHECK(wf.as_any().print() == "<c>'a'");
    CHECK(w.as_any().print() == "<mis>{2:\"b\",3:\"c\",4:\"d\"}");
    CHECK_NOTHROW(w.erase(w[1])); //erase from middle
    CHECK(w.as_any().print() == "<mis>{2:\"b\",4:\"d\"}");
    CHECK_NOTHROW(w.erase(1)); //erase from end
    CHECK(w.as_any().print() == "<mis>{2:\"b\"}");
    CHECK_NOTHROW(w.erase(0)); //erase to empty
    CHECK(w.as_any().print() == "<mis>{}");
    CHECK(w.size() == 0);
    CHECK_THROWS_AS(w.erase(0), std::out_of_range);
    //inserts
    CHECK_THROWS_AS(w.insert_after(-1, wview(1.1)), uf::type_mismatch_error);
    CHECK_THROWS_AS(w.insert_after(0, wview(std::pair(5, "5.5"))), std::out_of_range);
    CHECK_NOTHROW(w.insert_after(-1, wview(std::pair(5, "5.5"))));
    CHECK(w.as_any().print() == "<mis>{5:\"5.5\"}");
    CHECK_NOTHROW(w.insert_after(-1, wview(std::pair(3, "3.3"))));
    CHECK(w.as_any().print() == "<mis>{3:\"3.3\",5:\"5.5\"}");
    CHECK_NOTHROW(w.insert_after(0, wview(std::pair(4, "4.4"))));
    CHECK(w.as_any().print() == "<mis>{3:\"3.3\",4:\"4.4\",5:\"5.5\"}");
    CHECK_NOTHROW(w.insert_after(2, wview(std::pair(6, "6.6"))));
    CHECK(w.as_any().print() == "<mis>{3:\"3.3\",4:\"4.4\",5:\"5.5\",6:\"6.6\"}");
    wview i(std::pair(42, "42.42"));
    CHECK_NOTHROW(w.insert_after(2, i));
    CHECK(w.as_any().print() == "<mis>{3:\"3.3\",4:\"4.4\",5:\"5.5\",42:\"42.42\",6:\"6.6\"}");
    CHECK_NOTHROW(i.set(43));
    CHECK(w.as_any().print() == "<mis>{3:\"3.3\",4:\"4.4\",5:\"5.5\",42:\"42.42\",6:\"6.6\"}");
    CHECK_THROWS_AS(w.insert_after(5, i), std::out_of_range);
    CHECK_THROWS_AS(w.insert_after(4, wview{ false }), uf::type_mismatch_error);
    //delete again
    random_delete(w, "map");
}

TEST_CASE_TEMPLATE("wv insert/delete optional", wview, uf::wview, uf::gwview, uf::twview) {
    std::optional<int> o = 1;
    wview wo(o), wf = wo[0];
    CHECK(wo.as_any().print() == "<oi>1");
    CHECK_THROWS_AS(wo.erase(1), std::out_of_range);
    CHECK_NOTHROW(wo.erase(0)); 
    CHECK(wo.as_any().print() == "<oi>");
    CHECK(wf.as_any().print() == "<i>1");
    CHECK_NOTHROW(wf.set('a'));
    CHECK(wf.as_any().print() == "<c>'a'");
    CHECK(wo.as_any().print() == "<oi>");
    CHECK_THROWS_AS(wo.erase(0), std::out_of_range);
    //inserts
    CHECK_THROWS_AS(wo.insert_after(0, wview(42)), std::out_of_range);
    CHECK_THROWS_AS(wo.insert_after(-1, wview(1.1)), uf::type_mismatch_error);
    CHECK_NOTHROW(wo.insert_after(-1, wview(42)));
    CHECK(wo.as_any().print() == "<oi>42");
    //erase again
    random_delete(wo, "optional");
}

TEST_CASE_TEMPLATE("wv insert/delete tuple", wview, uf::wview, uf::gwview, uf::twview) {
    std::array<int, 4> a = { 1,2,3,4 };
    wview w(a), wf = w[0];
    CHECK_THROWS_AS(w.erase(4), std::out_of_range);
    CHECK(w.as_any().print() == "<t4iiii>(1,2,3,4)");
    CHECK(w.size() == 4);
    CHECK_NOTHROW(w.erase(0)); //erase from front
    CHECK(w.as_any().print() == "<t3iii>(2,3,4)");
    CHECK(wf.as_any().print() == "<i>1");
    CHECK_NOTHROW(wf.set('a'));
    CHECK(wf.as_any().print() == "<c>'a'");
    CHECK(w.as_any().print() == "<t3iii>(2,3,4)");
    CHECK_NOTHROW(w.erase(w[1])); //erase from middle
    CHECK(w.as_any().print() == "<t2ii>(2,4)");
    auto w0 = w[0];
    CHECK_THROWS_AS(w.erase(0), uf::type_mismatch_error); //erase to single element is not allowed
    CHECK_THROWS_AS(w.erase(1), uf::type_mismatch_error); //erase to single element is not allowed
    //inserts
    CHECK_THROWS_AS(w.insert_after(2, wview()), std::out_of_range);
    CHECK_NOTHROW(w.insert_after(-1, wview("aaa"))); //insert front
    CHECK(w.as_any().print() == "<t3sii>(\"aaa\",2,4)");
    CHECK_NOTHROW(w.insert_after(2, wview(std::vector<bool>{true,false}))); //insert back
    CHECK(w.as_any().print() == "<t4siilb>(\"aaa\",2,4,[true,false])");
    CHECK_NOTHROW(w.insert_after(3, wview(std::optional<bool>{false}))); //insert back again
    CHECK(w.as_any().print() == "<t5siilbob>(\"aaa\",2,4,[true,false],false)");
    CHECK_NOTHROW(w.insert_after(0, wview(42.42))); //insert back again
    CHECK(w.as_any().print() == "<t6sdiilbob>(\"aaa\",42.42,2,4,[true,false],false)");
    //delete again
    CHECK_NOTHROW(w.erase(0));
    CHECK(w.as_any().print() == "<t5diilbob>(42.42,2,4,[true,false],false)");
    CHECK_NOTHROW(w.erase(1));
    CHECK(w.as_any().print() == "<t4dilbob>(42.42,4,[true,false],false)");
    CHECK_NOTHROW(w.erase(3));
    CHECK(w.as_any().print() == "<t3dilb>(42.42,4,[true,false])");
}

TEST_CASE_TEMPLATE("wv insert/delete errors", wview, uf::wview, uf::gwview, uf::twview) {
    CHECK_THROWS_AS(wview(true).erase(0), uf::type_mismatch_error);
    CHECK_THROWS_AS(wview(1).erase(0), uf::type_mismatch_error);
    CHECK_THROWS_AS(wview(1.1).erase(0), uf::type_mismatch_error);
    CHECK_THROWS_AS(wview("a").erase(0), uf::type_mismatch_error);
    CHECK_THROWS_AS(wview('c').erase(0), uf::type_mismatch_error);
    CHECK_THROWS_AS(wview(uf::any{}).erase(0), uf::type_mismatch_error);
    CHECK_THROWS_AS(wview(uf::error_value{}).erase(0), uf::type_mismatch_error);
    CHECK_THROWS_AS(wview(uf::expected<void>{}).erase(0), uf::type_mismatch_error);
    CHECK_THROWS_AS(wview(uf::expected<int>{}).erase(0), uf::type_mismatch_error);

    CHECK_THROWS_AS(wview(true).insert_after(0,wview()), uf::type_mismatch_error);
    CHECK_THROWS_AS(wview(1).insert_after(0,wview()), uf::type_mismatch_error);
    CHECK_THROWS_AS(wview(1.1).insert_after(0,wview()), uf::type_mismatch_error);
    CHECK_THROWS_AS(wview("a").insert_after(0,wview()), uf::type_mismatch_error);
    CHECK_THROWS_AS(wview('c').insert_after(0,wview()), uf::type_mismatch_error);
    CHECK_THROWS_AS(wview(uf::any{}).insert_after(0,wview()), uf::type_mismatch_error);
    CHECK_THROWS_AS(wview(uf::error_value{}).insert_after(0,wview()), uf::type_mismatch_error);
    CHECK_THROWS_AS(wview(uf::expected<void>{}).insert_after(0,wview()), uf::type_mismatch_error);
    CHECK_THROWS_AS(wview(uf::expected<int>{}).insert_after(0,wview()), uf::type_mismatch_error);

    //detect type change for tuple
    std::list<std::array<int, 3>> v = { {1,2,3} };
    wview w(v);
    CHECK_THROWS_AS(w[0].insert_after(-1, wview("aa")), uf::type_mismatch_error);
    CHECK_THROWS_AS(w[0].insert_after(-1, wview(1)), uf::type_mismatch_error);
    CHECK_THROWS_AS(w[0][0].set("aa"), uf::type_mismatch_error);
}

TEST_CASE_TEMPLATE("wv create_tuple_from", wview, uf::wview, uf::gwview, uf::twview)
{
    wview i(1), d(5.6), s("aaa"), t;
    std::vector<wview> v{ i, {}, d, s, i, wview{std::monostate{}}, d, s };
    CHECK(i.as_any().print() == "<i>1");
    CHECK(d.as_any().print() == "<d>5.6");
    CHECK(s.as_any().print() == "<s>\"aaa\"");
    REQUIRE_NOTHROW(t = wview::create_tuple_from(v));
    CHECK(t.as_any().print() == "<t6idsids>(1,5.6,\"aaa\",1,5.6,\"aaa\")");
    CHECK(i.as_any().print() == "<i>1"); //check parents unchanged
    CHECK(d.as_any().print() == "<d>5.6");
    CHECK(s.as_any().print() == "<s>\"aaa\"");
    REQUIRE_NOTHROW(t[2].set(42)); //type modifying change
    CHECK(t.as_any().print() == "<t6idiids>(1,5.6,42,1,5.6,\"aaa\")");
    CHECK(i.as_any().print() == "<i>1"); //check parents unchanged after write
    CHECK(d.as_any().print() == "<d>5.6");
    CHECK(s.as_any().print() == "<s>\"aaa\"");
}

TEST_CASE_TEMPLATE("wv create_error", wview, uf::wview, uf::gwview, uf::twview)
{
    uf::error_value ei("a", "b", 42);
    uf::error_value ev("a", "b");
    wview wei(ei), cei = wview::create_error("a", "b", wview(42));
    wview wev(ev), cev = wview::create_error("a", "b");
    CHECK(uf::serialize_print(ei) == "err(\"a\",\"b\",<i>42)");
    CHECK(uf::serialize_print(ev) == "err(\"a\",\"b\",<>)");
    CHECK(wei.as_any().print() == "<e>err(\"a\",\"b\",<i>42)");
    CHECK(wev.as_any().print() == "<e>err(\"a\",\"b\",<>)");
    CHECK(cei.as_any().print() == "<e>err(\"a\",\"b\",<i>42)");
    CHECK(cev.as_any().print() == "<e>err(\"a\",\"b\",<>)");
    CHECK(cei.as_any().as_view() == wei.as_any().as_view());
    CHECK(cev.as_any().as_view() == wev.as_any().as_view());
}

TEST_CASE_TEMPLATE("wv create_optional_expected", wview, uf::wview, uf::gwview, uf::twview)
{
    CHECK(!wview::create_optional_from(wview(std::monostate{})));
    wview oi = wview::create_optional_from(wview(42));
    CHECK(oi.type() == "oi");
    CHECK(oi.as_any().print() == "<oi>42");
    CHECK(oi.size() == 1);
    CHECK(oi[0].as_any().print() == "<i>42");
    CHECK_NOTHROW(oi.erase(0));
    CHECK(oi.size() == 0);

    wview X = wview::create_expected_from(wview(std::monostate{}));
    CHECK(X.type() == "X");
    CHECK(X.as_any().print() == "<X>");
    CHECK(X.size() == 1);
    CHECK(X[0].as_any().as_view().is_void());

    wview ei = wview::create_expected_from(wview(42));
    CHECK(ei.type() == "xi");
    CHECK(ei.as_any().print() == "<xi>42");
    CHECK(ei.size() == 1);
    CHECK(ei[0].as_any().print() == "<i>42");

    wview cei = wview::create_error("a", "b", wview(42));
    wview ee = wview::create_expected_from_error(cei, "s");
    CHECK(ee.type() == "xs");
    CHECK(ee.as_any().print() == "<xs>err(\"a\",\"b\",<i>42)");
    CHECK(ee.size() == 1);
    CHECK(ee[0].as_any().print() == "<e>err(\"a\",\"b\",<i>42)");
}

TEST_CASE_TEMPLATE("wv swap_content_with", wview, uf::wview, uf::gwview, uf::twview)
{
    auto v = std::tuple(
        5, "aaa", 67.23,
        std::vector<int> {42,43,44},
        uf::any("any_string"),
        std::optional<int>(4242)
    );
    wview w{ v }, x{ "original" };
    CHECK(w.as_any().print() == "<t6isdliaoi>(5,\"aaa\",67.23,[42,43,44],<s>\"any_string\",4242)");
    CHECK(x.as_any().print() == "<s>\"original\"");
    CHECK_NOTHROW(w.swap_content_with(x));
    CHECK(x.as_any().print() == "<t6isdliaoi>(5,\"aaa\",67.23,[42,43,44],<s>\"any_string\",4242)");
    CHECK(w.as_any().print() == "<s>\"original\"");
    CHECK_NOTHROW(w.swap_content_with(x));
    CHECK(w.as_any().print() == "<t6isdliaoi>(5,\"aaa\",67.23,[42,43,44],<s>\"any_string\",4242)");
    CHECK(x.as_any().print() == "<s>\"original\"");
    CHECK_NOTHROW(w[0].swap_content_with(x));
    CHECK(w.as_any().print() == "<t6ssdliaoi>(\"original\",\"aaa\",67.23,[42,43,44],<s>\"any_string\",4242)");
    CHECK(x.as_any().print() == "<i>5");
    CHECK_NOTHROW(w[0].swap_content_with(x));
    CHECK(w.as_any().print() == "<t6isdliaoi>(5,\"aaa\",67.23,[42,43,44],<s>\"any_string\",4242)");
    CHECK(x.as_any().print() == "<s>\"original\"");
    CHECK_THROWS_AS(w[3][1].swap_content_with(x), uf::type_mismatch_error);
    CHECK(w.as_any().print() == "<t6isdliaoi>(5,\"aaa\",67.23,[42,43,44],<s>\"any_string\",4242)");
    CHECK(x.as_any().print() == "<s>\"original\"");
    CHECK_NOTHROW(w[3][0].swap_content_with(w[3][0]));
    CHECK(w.as_any().print() == "<t6isdliaoi>(5,\"aaa\",67.23,[42,43,44],<s>\"any_string\",4242)");
    CHECK(x.as_any().print() == "<s>\"original\"");
    CHECK_NOTHROW(w[3][0].swap_content_with(w[3][1]));
    CHECK(w.as_any().print() == "<t6isdliaoi>(5,\"aaa\",67.23,[43,42,44],<s>\"any_string\",4242)");
    CHECK(x.as_any().print() == "<s>\"original\"");
    CHECK_NOTHROW(w[0].swap_content_with(w[3][1]));
    CHECK(w.as_any().print() == "<t6isdliaoi>(42,\"aaa\",67.23,[43,5,44],<s>\"any_string\",4242)");
    CHECK(x.as_any().print() == "<s>\"original\"");
    //parent child
    CHECK_THROWS_AS(w.swap_content_with(w[2]), uf::api_error);
    CHECK_THROWS_AS(w.swap_content_with(w[3][2]), uf::api_error);
    CHECK_THROWS_AS(w[2].swap_content_with(w), uf::api_error);
    CHECK_THROWS_AS(w[3][2].swap_content_with(w), uf::api_error);
}

TEST_CASE("indexof") {
    uf::wview w{std::vector<std::string>{"alef", "bet", "gimel"}};
    CHECK(w[1].indexof() == 1);
}

TEST_CASE("multilevel erase")
{
    uf::any a(uf::from_text, "{\"a\":2,\"b\":4}");
    uf::wview amsi(a), msi=amsi[0];
    CHECK_NOTHROW(msi[0][1]); //parse 2 level deep
    CHECK_NOTHROW(msi.erase(1));
    CHECK_NOTHROW(msi[0]); //THis check()s

    std::tuple p { std::tuple(1,2,3), std::tuple(4,5,6), std::tuple(7,8,9) };
    uf::wview t3t3(p);
    CHECK_NOTHROW(t3t3[0][2]); //parse 2 level deep
    CHECK_NOTHROW(t3t3.erase(1));
    CHECK_NOTHROW(t3t3[0]); //THis check()s
}

TEST_CASE("set with C-style arrays")
{
    int i[2] = {1,2}, j[] = {1,2,3};
    const char txt[] = "Text", tyt[] = "Teyt";

    uf::wview vi{i}, vt{txt};
    CHECK(vi.as_any().print()=="<t2ii>(1,2)");
    CHECK(vt.as_any().print()=="<s>\"Text\"");
    REQUIRE_NOTHROW(vi.set(j));
    REQUIRE_NOTHROW(vt.set(tyt));
    CHECK(vi.as_any().print()=="<t3iii>(1,2,3)");
    CHECK(vt.as_any().print()=="<s>\"Teyt\"");
}

TEST_CASE("linear_search") {
    std::vector<std::tuple<int, double, bool>> lid = {{1,42.1,true}, {17,42.17,true}, {5,42.5,false}, {17,42.172,false}};
    uf::wview vlid{lid}, vi{17}, vid{std::pair(17,42.172)};
    std::pair<uf::wview, std::string> ret;
    //we find n==1
    CHECK_NOTHROW(ret = vlid.linear_search(vi, 1));
    CHECK_NOTHROW(vlid.check());
    CHECK(ret.second=="");
    CHECK(ret.first);
    CHECK(ret.first.as_any().as_view().print()=="<t3idb>(17,42.17,true)");

    //we find n==0
    CHECK_NOTHROW(ret = vlid.linear_search(vi, 0));
    CHECK_NOTHROW(vlid.check());
    CHECK(ret.second=="");
    CHECK(ret.first);
    CHECK(ret.first.as_any().as_view().print()=="<t3idb>(17,42.17,true)");

    //n==2, find second appearance of 17
    CHECK_NOTHROW(ret = vlid.linear_search(vid, 2));
    CHECK_NOTHROW(vlid.check());
    CHECK(ret.second=="");
    CHECK(ret.first);
    CHECK(ret.first.as_any().as_view().print()=="<t3idb>(17,42.172,false)");

    //n==1, find 
    CHECK_NOTHROW(ret = vlid.linear_search(vid, 1));
    CHECK_NOTHROW(vlid.check());
    CHECK(ret.second=="");
    CHECK(ret.first);
    CHECK(ret.first.as_any().as_view().print()=="<t3idb>(17,42.17,true)");

    //n==1, not find 
    CHECK_NOTHROW(ret = vlid.linear_search(uf::wview{3}, 1));
    CHECK_NOTHROW(vlid.check());
    CHECK(ret.second=="");
    CHECK(!ret.first);
}
