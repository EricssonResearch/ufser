#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h" //https://raw.githubusercontent.com/onqtam/doctest/master/doctest/doctest.h
#include <ufser.h>
#include <iostream>
#include <atomic>
#include <string_view>
#include <list>
#include <span>

using namespace std::string_view_literals;

/// I had to create this helper otherwise the executable was stuck on the TEST_CASE(...) line containing the create_from...() stuff
template<typename T>
auto serialize(T const &t) {
    return uf::any(t);
}

TEST_CASE("is_serializable/is_deserializable") {
    struct test
    {
        int a;
        int b;
        using auto_serialization = void;
    };

    //serializable via a tuple
    struct test_good : public test
    {
        auto tuple_for_serialization() noexcept { return std::tie(a, b); }
        auto tuple_for_serialization() const noexcept { return std::tie(a, b); }
    };

    //not serializable
    struct test_bad
    {
        test tuple_for_serialization() { return test(); }
    };

    REQUIRE(uf::impl::has_tuple_for_serialization_member_deser<test_bad>::value);
    REQUIRE(!uf::impl::has_tuple_for_serialization_member_ser<test_bad>::value);
    REQUIRE(uf::impl::has_tuple_for_serialization<true, test_bad>::value);
    REQUIRE(!uf::impl::has_tuple_for_serialization<false, test_bad>::value);
    REQUIRE(uf::impl::is_serializable_container<std::unordered_map<std::string, double>>::value);
    REQUIRE(uf::impl::is_serializable_f<std::array<double, 4>>());
    REQUIRE(uf::impl::is_serializable_primitive<std::string>::value);
    REQUIRE(uf::impl::is_serializable_primitive<std::remove_cvref_t<std::string>>::value);
    REQUIRE(uf::impl::is_serializable_f<std::string>());
    REQUIRE(uf::impl::is_serializable_f<const char[4]>());
    REQUIRE(uf::impl::is_pair<std::pair<std::string_view, std::string_view>>::value);
    REQUIRE(uf::impl::is_serializable_f<std::pair<std::string_view, std::string_view>>());
    REQUIRE(uf::is_serializable<std::string>::value);
    REQUIRE(uf::is_serializable<uf::any_view>::value);
    REQUIRE(uf::is_serializable<std::tuple<std::string, int>>::value);
    REQUIRE(!uf::is_serializable<test>::value);
    REQUIRE(!uf::is_serializable<test_bad>::value);
    REQUIRE(uf::is_deserializable<std::tuple<std::string&, int&, uf::any&, test_good&>>::value);
    REQUIRE(uf::is_deserializable<std::tuple<std::string&, int&, uf::any&, bool&, test_good&>>::value);
    REQUIRE(!uf::is_deserializable<std::tuple<std::string&, int&, uf::any&, bool&, test&>>::value);
    REQUIRE(!uf::is_deserializable<uf::any_view&>::value);
    REQUIRE(!uf::is_deserializable<std::tuple<std::string&, int&, uf::any_view&, bool&, test_good&>>::value);
    REQUIRE(!uf::is_serializable<std::vector<test>>::value);
    REQUIRE(uf::is_serializable<const char*>::value);
    REQUIRE(uf::impl::is_smart_ptr<std::unique_ptr<int>>::value);
    REQUIRE(uf::is_serializable<std::unique_ptr<int>>::value);
    REQUIRE(std::is_same<std::unique_ptr<int>::element_type, int>::value);
    REQUIRE(uf::is_serializable<int*>::value);
}

TEST_CASE("Create from POD struct")
{
    struct A
    {
        int i;
        auto tuple_for_serialization() const { return std::tie(i); }
        auto tuple_for_serialization() { return std::tie(i); }
    };
    A a{ 1 };
    static_assert(uf::is_serializable_v<A>);
    static_assert(uf::is_serializable_v<A&>);
    static_assert(uf::is_serializable_v<std::tuple<A&>>);
    auto ser = serialize(a);
    REQUIRE(ser.print() == "<i>1");
}

TEST_CASE("C-style arrays") {
    double i[] = {1.,2.};
    CHECK(uf::serialize_type(i)=="t2dd");
    CHECK(uf::deserialize_type(i)=="t2dd");
    CHECK(uf::serialize_type(i, uf::use_tags, "dummy")=="t2dd");
    CHECK(uf::deserialize_type(i, uf::use_tags, "dummy")=="t2dd");
    CHECK(uf::serialize(i).length()==16);
    uf::any a(i), b(i, uf::use_tags, "dummy"), c, d;
    c.assign(i);
    d.assign(i, uf::use_tags, "dummy");
    CHECK(a==b);
    CHECK(a==c);
    CHECK(a==d);
    CHECK(a.type()=="t2dd");
    CHECK(a.value().length()==16);
}
TEST_CASE("C-style strings") 
{
    CHECK(uf::is_deserializable_v<char[5]> == false);
    CHECK(uf::is_deserializable_view_v<char[5]> == false);
    const char i[] = "text";
    CHECK(uf::serialize_type(i)=="s");
    CHECK(uf::serialize_type(i, uf::use_tags, "dummy")=="s");
    CHECK(uf::serialize(i).length()==8);
    uf::any a(i), b(i, uf::use_tags, "dummy"), c, d;
    c.assign(i);
    d.assign(i, uf::use_tags, "dummy");
    CHECK(a==b);
    CHECK(a==c);
    CHECK(a==d);
    CHECK(a.type()=="s");
    CHECK(a.value().length()==8);
}


#define check(any, str, txt)\
{\
    REQUIRE_NOTHROW((any).get(str));\
    CHECK(uf::concat('<', uf::serialize_type(str), '>', uf::serialize_print(str))==(txt));\
}

TEST_CASE("Int, bool and double conversions") {
    struct A
    {
        bool b; char c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } a = { true, 'a', 42, 4242, 41.3 }, _a;
    struct A2
    {
        int b; int c; int64_t i; int32_t I; int d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } a2; //need all
    struct A3
    {
        double b; double c; int64_t i; int32_t I; int d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } a3; //need all - shall fail
    uf::any aa(a);
    check(aa, _a, "<t5bciId>(True,'a',42,4242,41.3)");
    check(aa, a2, "<t5iiIii>(1,97,42,4242,41)");
    REQUIRE_THROWS(aa.get(a3));
}

TEST_CASE("Expected conversions") {
    struct A
    {
        bool b; char c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } a = { true, 'a', 42, 4242, 41.3 };
    struct A3
    {
        double b; double c; int64_t i; int32_t I; int d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } a3; //need all - shall fail
    struct AX1
    {
        bool b; uf::expected<char> c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } ax1;

    struct AX2
    {
        bool b; uf::expected<std::string> c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } ax2;
    uf::any aa(a);
    //serialize a matching value into an expected
    check(aa, ax1, "<t5bxciId>(True,'a',42,4242,41.3)");
    //serialize a non-matching value into an expected
    REQUIRE_THROWS(aa.get(ax2));
    //serialize a matching expected into a value
    uf::any aax(ax1);
    check(aax, a, "<t5bciId>(True,'a',42,4242,41.3)");
    //then into a non-matching value
    REQUIRE_THROWS(aax.get(a3));
}

TEST_CASE("optional conversion") {
    //Convert empty optional to that of compatible type
    std::optional<int> oi;
    std::optional<double> od;
    uf::any aoi(oi);
    CHECK_NOTHROW(aoi.get(od, uf::allow_converting_double));
    CHECK(!od.has_value());
    CHECK_THROWS(aoi.get(od, uf::allow_converting_none));

    oi = 42;
    aoi.assign(oi);
    CHECK_NOTHROW(aoi.get(od, uf::allow_converting_double));
    CHECK(od.has_value());
    CHECK(od == 42);
    CHECK_THROWS(aoi.get(od, uf::allow_converting_none));
}

TEST_CASE("Serialize error to expected") {
    struct A
    {
        bool b; char c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } a;
    struct AX1
    {
        bool b; uf::expected<char> c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } ax1;
    struct AE1
    {
        bool b; uf::error_value c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } ae1 = { true, uf::error_value("test error", "msg"), 42, 4242, 41.3 };
    uf::any aae(ae1);
    check(aae, ax1, "<t5bxciId>(True,err(\"test error\",\"msg\",<>),42,4242,41.3)");
    //then into a non-expected value
    REQUIRE_THROWS(aae.get(a));
}

TEST_CASE("X decay") {
    struct A
    {
        bool b; char c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } a;
    struct AX0
    {
        bool b; uf::expected<void> X; char c;  int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, X, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, X, i, I, d); }
    } ax0 = { true,  {}, 'a',42, 4242, 41.3 };
    struct AE1
    {
        bool b; uf::error_value c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } ae1;
    uf::any aax0(ax0);
    check(aax0, a, "<t5bciId>(True,'a',42,4242,41.3)");
    //then into a non-expected value
    CHECK_THROWS(aax0.get(ae1));
}

TEST_CASE("List of expected") {
    struct A
    {
        bool b; char c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } a;
    struct ALX1
    {
        bool b; std::vector<uf::expected<int>> c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } alx1 = { true, {1, 2, 3}, 42, 4242, 41.3 };
    struct AL1
    {
        bool b; std::vector<int> c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } al1 = { true, {1, 2, 3}, 42, 4242, 41.3 };
    uf::any aal(al1);
    check(aal, alx1, "<t5blxiiId>(True,[1,2,3],42,4242,41.3)");
    //vice versa
    aal.assign(alx1);
    check(aal, al1, "<t5bliiId>(True,[1,2,3],42,4242,41.3)");
    //then into a non-expected value
    REQUIRE_THROWS(aal.get(a));
}

TEST_CASE("Any serialization") {
    struct A
    {
        bool b; char c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } a = { true, 'a', 42, 4242, 41.3 };
    struct ANY1
    {
        bool b; uf::any c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } any1 = { true, {}, 42, 4242, 41.3 };
    struct AL1
    {
        bool b; std::vector<int> c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } al1;
    uf::any aa(a);
    //c->a
    check(aa, any1, "<t5baiId>(True,<c>'a',42,4242,41.3)");
    //vice versa: a('a')->c
    uf::any aany1(any1);
    check(aany1, a, "<t5bciId>(True,'a',42,4242,41.3)");
    //vice versa plus numeric: a(1)->c
    any1.c.assign(32);
    aany1.assign(any1);
    check(aany1, a, "<t5bciId>(True,' ',42,4242,41.3)");
    //vice versa badly: a("str")->c
    any1.c.assign("str");
    aany1.assign(any1);
    REQUIRE_THROWS(aany1.get(a));
    //then into a non-matching a('a')->li
    REQUIRE_THROWS(aany1.get(al1));
}

TEST_CASE("la->lT conversion"){
    struct AL1
    {
        bool b; std::vector<int> c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } al1;
    struct ALA1
    {
        bool b; std::vector<uf::any> c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } ala1 = { true, {uf::any(1), uf::any(2), uf::any(3)}, 42, 4242, 41.3 };
    uf::any aala1(ala1);
    //good types a(i)->i
    check(aala1, al1, "<t5bliiId>(True,[1,2,3],42,4242,41.3)");
    //bad types a(s)->i
    ala1.c = std::vector<uf::any>{ uf::any(1), uf::any('c'), uf::any("f") };
    aala1.assign(ala1);
    REQUIRE_THROWS(aala1.get(al1));
}

 TEST_CASE("Map X decay") {
    struct AM1
    {
        bool b; std::map<std::string, uf::expected<void>> c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } am1 = { true, {{"alma", {}},{"korte", {}},{"cekla",{}}}, 42, 4242, 41.3 }, _am1;
    uf::any aam1(am1);
    check(aam1, _am1, "<t5bmsXiId>(True,{\"alma\":,\"cekla\":,\"korte\":},42,4242,41.3)");
    //test mTX, test
    struct AL2
    {
        bool b; std::list<std::string> c; int32_t i; int64_t I; double d;
        auto tuple_for_serialization() const { return std::tie(b, c, i, I, d); }
        auto tuple_for_serialization() { return std::tie(b, c, i, I, d); }
    } al2;
    check(aam1, al2, "<t5blsiId>(True,[\"alma\",\"cekla\",\"korte\"],42,4242,41.3)");
}

TEST_CASE("maT, maa, mTa -> some map conversion")
{
    std::map<uf::any, int> mai{{uf::any(uf::from_text, "\"alma\""), -1},
                               {uf::any(uf::from_text, "\"korte\""), 2},
                               {uf::any(uf::from_text, "\"cekla\""), 3}};
    std::map<std::string, int> msi;
    std::map<std::string_view, int> msvi;
    std::map<uf::any, double> mad;
    std::map<uf::any, uf::any> maa;
    std::map<int, uf::any> mia;
    uf::any am(mai);
    check(am, msi, "<msi>{\"alma\":-1,\"cekla\":3,\"korte\":2}");
    check(am, mad, "<mad>{<s>\"alma\":-1.,<s>\"cekla\":3.,<s>\"korte\":2.}");
    check(am, maa, "<maa>{<s>\"alma\":<i>-1,<s>\"cekla\":<i>3,<s>\"korte\":<i>2}");
    REQUIRE_THROWS(am.get(mia));
}

TEST_CASE("X->xT and vice versa")
{
    uf::expected<void> X;
    uf::expected<uf::any> xa = uf::any(5), xav;
    uf::expected<int> xi;
    uf::any aX(X), axa(xa), axav(xav), axi(xi);
    CHECK_FALSE(uf::cant_convert("a", "", uf::allow_converting_any));
    const auto r1 = uf::cant_convert("xa", "X", uf::allow_converting_any);
    INFO((r1 ? r1->what() : ""));
    CHECK_FALSE(r1);
    CHECK(uf::cant_convert("xa", "X", uf::allow_converting_none));
    CHECK(uf::cant_convert("xi", "X", uf::allow_converting_all));

    CHECK_THROWS(axa.get(X));    //xa(d) dont convert to X
    CHECK_NOTHROW(axav.get(X));  //xa(void) converts to X
    CHECK_THROWS(axi.get(X));    //xi dont convert to X
    CHECK_NOTHROW(axi.get(xav)); //xi does convert to xa
    CHECK_THROWS(aX.get(xi));    //X dont convert to xi
    CHECK_NOTHROW(aX.get(xa));   //X converts to Xa

    xa.set_error();
    xi.set_error();
    axa.assign(xa);
    axi.assign(xi);

    CHECK_NOTHROW(axa.get(X));
    CHECK_THROWS(axi.get(X));
}

TEST_CASE("string_deserializer")
{
    struct SD
    {
        std::string kkk;
        int i;
        auto tuple_for_serialization() const { return std::tie(kkk, i); }
        auto tuple_for_serialization() { return uf::tie(uf::string_deserializer([this](std::string_view s) {kkk = s; }), i); }
    } sd = { "aaa", 5 }, sd2;
    REQUIRE(uf::is_ser_deser_ok_v<SD>);
    uf::any a(sd);
    check(a, sd2, a.print());
}

template <size_t N> struct int_C_array
{
    int v[N];
    auto tuple_for_serialization() const { return uf::tie(std::span(v)); }
    auto tuple_for_serialization() { return uf::tie(uf::array_inserter(v, N)); }
};
TEST_CASE("span, array_inserter")
{
    int_C_array<3> a3{ 1 };
    int_C_array<2> a2;
    static_assert(uf::impl::is_serializable_container<std::span<const int>>::value);
    static_assert(uf::is_serializable_v<int_C_array<3>>);
    static_assert(uf::is_serializable_v<int_C_array<3>&>);
    static_assert(uf::is_serializable_v<std::tuple<int_C_array<3>&>>);
    uf::any ser(a3);
    REQUIRE(ser.print() == "<li>[1,0,0]");
    REQUIRE_THROWS_AS(ser.get(a2), uf::value_mismatch_error);
}

struct myexc : public std::runtime_error { using runtime_error::runtime_error; };

struct locking_struct
{
    mutable int count = 0;
    int data = 0;
    auto tuple_for_serialization() const { return std::tie(data); }
    auto tuple_for_serialization() { count++;  return std::tie(data); }
};

void before_serialization(const locking_struct &l) { l.count++; }
void after_serialization(const locking_struct &l, bool) noexcept { l.count--; }
void after_deserialization_simple(const locking_struct &l) { l.count--; }
void after_deserialization_error(const locking_struct &l) noexcept { l.count--; }


struct locking_struct2
{
    locking_struct a;
    auto tuple_for_serialization() const { return std::tie(a); }
    auto tuple_for_serialization() { return std::tie(a); }
};

struct locking_struct_throw_ser : public locking_struct
{
    using locking_struct::tuple_for_serialization;
    std::tuple<const locking_struct&> tuple_for_serialization() const { throw myexc("aaa"); }
};

struct locking_struct_throw_deser : public locking_struct
{
    using locking_struct::tuple_for_serialization;
    std::tuple<locking_struct&> tuple_for_serialization() { count++;  throw myexc("bbb"); }
};

struct locking_struct3
{
    locking_struct a;
    locking_struct_throw_ser b;
    locking_struct c;
    auto tuple_for_serialization() const { return std::tie(a, b, c); }
    auto tuple_for_serialization() { return std::tie(a, b, c); }
};


struct locking_struct4
{
    locking_struct a;
    locking_struct_throw_deser b;
    locking_struct c;
    auto tuple_for_serialization() const { return std::tie(a, b, c); }
    auto tuple_for_serialization() { return std::tie(a, b, c); }
};


TEST_CASE("before/after serialization")
{
    CHECK(uf::has_before_serialization_tag_v<locking_struct>);
    CHECK(!uf::has_before_serialization_tag_v<locking_struct2>);
    CHECK(uf::has_before_serialization_tag_v<locking_struct_throw_ser>);
    CHECK(uf::has_before_serialization_tag_v<locking_struct_throw_deser>);
    locking_struct2 l2;
    std::vector<locking_struct2> vl2;
    std::map<int, locking_struct3> mil3;
    static_assert(uf::impl::has_before_serialization_inside_v<decltype(l2)>);
    static_assert(uf::impl::has_before_serialization_inside_v<decltype(vl2)>);
    static_assert(uf::impl::has_before_serialization_inside_v<decltype(mil3)>);
    static_assert(uf::impl::has_after_serialization_inside_v<decltype(l2)>);
    static_assert(uf::impl::has_after_serialization_inside_v<decltype(vl2)>);
    static_assert(uf::impl::has_after_serialization_inside_v<decltype(mil3)>);
    static_assert(uf::impl::has_before_serialization_inside_v<locking_struct3>);
    static_assert(uf::impl::has_after_serialization_inside_v<locking_struct3>);
    static_assert(uf::impl::has_before_serialization_inside_v<locking_struct_throw_ser>);
    static_assert(uf::impl::has_after_serialization_inside_v<locking_struct_throw_ser>);
    vl2.emplace_back();
    uf::impl::call_before_serialization(&vl2);
    REQUIRE(vl2[0].a.count == 1);
    uf::impl::call_after_serialization(&vl2, true);
    REQUIRE(vl2[0].a.count == 0);
    static_assert(uf::impl::has_after_serialization_inside_v<locking_struct_throw_ser>);
    mil3[0];
    auto r = uf::impl::call_before_serialization(&mil3);
    CHECK(r.obj == &mil3[0].b);
    CHECK(bool(r.ex));
    CHECK_THROWS_WITH(std::rethrow_exception(r.ex), "aaa");
    CHECK(mil3[0].a.count == 1);
    REQUIRE(mil3[0].c.count == 0);
    mil3[0].a.count = 0;

    auto s = uf::serialize(vl2);
    REQUIRE(vl2[0].a.count == 0);
    CHECK_THROWS_WITH(uf::serialize(mil3), "aaa");
    REQUIRE(mil3[0].a.count == 0);
    CHECK(mil3[0].c.count == 0);
}

struct custom_des
{
    std::atomic_int i;
    auto tuple_for_serialization() const { return uf::tie(int(i)); }
    auto tuple_for_serialization() { return int(0); }
    void after_deserialization(int &&t) { i = t; }
};

TEST_CASE("before/after deserialization")
{
    static_assert(uf::is_serializable_v<locking_struct_throw_deser>);
    const locking_struct_throw_deser x;
    static_assert(uf::is_serializable_v<decltype(x.tuple_for_serialization())>);
    locking_struct4 l4;
    uf::any s(l4);
    REQUIRE(l4.a.count == 0);
    REQUIRE(l4.c.count == 0);
    CHECK(s.type() == "t3iii");
    CHECK(uf::deserialize_type(l4) == "t3iii");
    CHECK_THROWS_WITH(s.get(l4), "bbb");
    CHECK(l4.a.count == 0);
    CHECK(l4.c.count == 0);
    std::vector<locking_struct4> ll4;
    ll4.emplace_back();
    uf::any ls(ll4);
    CHECK_THROWS_WITH(ls.get(ll4), "bbb");
    CHECK(ll4.size() == 0); //list element thrown, not inserted

    static_assert(uf::impl::has_after_deserialization_member<custom_des>::value);
    static_assert(uf::has_after_deserialization_tag_v<custom_des>);
    custom_des c;
    //using uf::impl::after_deserialization;
    //after_deserialization(c, c.tuple_for_serialization());
    //uf::impl::after_deserialization(c, c.tuple_for_serialization());
    int i = 42;
    uf::any a(i);
    a.get(c);
    CHECK(int(c.i) == 42);
}

TEST_CASE("convert")
{
    uf::expected<int> ei = 3;
    uf::any aei(ei);
    CHECK(uf::any(uf::from_text, "<>3.0").print() == "<a><d>3.");
    CHECK(uf::any(uf::from_text, "<i>3.0").print()=="<a><i>3");
    CHECK(uf::any(ei).convert_to("d").print() == "<d>3.");
    CHECK(uf::any(ei).convert_to<double>().print() == "<d>3.");
    CHECK(uf::any(aei).convert_to("d").print() == "<d>3.");
    CHECK(uf::any(uf::from_text, "<a><xi>3.0").print() == "<a><a><xi>3");
    CHECK(uf::any(uf::from_text, "<xs>['h','e','l','l','o']").print() == "<a><xs>\"hello\"");
}

namespace check_equality_op
{
struct S {};
template<typename T, typename Arg> S operator== (const T&, const Arg&);

template<typename T, typename Arg = T>
struct has
{
    enum { value = !std::is_same<decltype(*(T*)(0) == *(Arg*)(0)), S>::value };
};
}

//Test viability of conversion with the given policy
template<bool should, bool should_with_content, typename T, typename U>
void TC(T t, U u, uf::serpolicy policy)
{
    uf::any at(t);
    INFO(uf::concat("T=<", uf::serialize_type<T>(), ">, U=<", uf::deserialize_type<U>(), ">, policy=", policy,
                    should ? " [should succeed]" : " [should fail]",
                    should_with_content ? " [should succeed with content]" : " [should fail with content]",
                    " t=", uf::serialize_print(t), " u=", uf::serialize_print(u)));
    //Test if we can deserialize 't' into 'u'
    if constexpr (should_with_content) CHECK_NOTHROW(at.get(u, policy));
    else CHECK_THROWS(at.get(u, policy));
    //Test if the type of T can be converted to the type of U (no serialized data available)
    if constexpr (should) CHECK_FALSE(uf::cant_convert(uf::serialize_type<T>(), uf::deserialize_type<U>(), policy));
    else CHECK(uf::cant_convert(uf::serialize_type<T>(), uf::deserialize_type<U>(), policy));
    //Test if the type of T can be converted to the type of U (serialized data available)
    if constexpr (should_with_content) CHECK_FALSE(uf::cant_convert(uf::serialize_type<T>(), uf::deserialize_type<U>(), policy, at.value()));
    else CHECK(uf::cant_convert(uf::serialize_type<T>(), uf::deserialize_type<U>(), policy, at.value()));
    //Test that if we convert t to U in serialized form we get the same as in the first step
    if constexpr (should_with_content && check_equality_op::has<U>::value)
        CHECK_MESSAGE(u == at.convert_to<U>(policy).template get_as<U>(uf::allow_converting_none),
                      uf::concat("u=", u, " other=", at.convert_to<U>(policy).template get_as<U>(uf::allow_converting_none)));
}

//Test the convertibility of conversion with the given policy only
template<bool should_with_content, typename T, typename U>
void TCO(T t, U u, uf::serpolicy policy = uf::allow_converting_none)
{
    TC<true, should_with_content>(t, u, policy);
    if (policy != uf::allow_converting_none) 
        for (uf::serpolicy p : {uf::allow_converting_any, uf::allow_converting_aux, uf::allow_converting_bool, 
                                uf::allow_converting_double, uf::allow_converting_expected, 
                                uf::allow_converting_ints, uf::allow_converting_ints_narrowing,
                                uf::allow_converting_none}) {
            //If p lacks some bit of policy we shall fail, else we shall succeed
            if ((p | policy) ^ p) TC<false, false>(t,u,p);
            else TC<true, should_with_content>(t,u,p);
        }
}

//Test the convertibility of conversion with the given policy only and vice versa
template<typename T, typename U>
void TCO2(T t, U u, uf::serpolicy policy = uf::allow_converting_none)
{
    TCO<true>(t, u, policy);
    TCO<true>(u, t, policy);
}

//Test the non-convertibility of conversion with any policy and vice versa
template<typename T, typename U>
void TCF2(T t, U u)
{
    TC<false, false>(t, u, uf::allow_converting_all);
    TC<false, false>(u, t, uf::allow_converting_all);
}

TEST_CASE("primitive conversions")
{
    TCO2(double(5), int(0), uf::allow_converting_double);
    TCO2(double(5), int(0), uf::allow_converting_double);
    TCO<true>(char(5), int64_t(0), uf::allow_converting_ints);
    TCO<true>(char(5), int32_t(0), uf::allow_converting_ints);
    TCO<true>(int32_t(5), int64_t(0), uf::allow_converting_ints);
    TCO<true>(int64_t(5), int32_t(0), uf::allow_converting_ints_narrowing);
    TCO<true>(int64_t(5), char(0), uf::allow_converting_ints_narrowing);
    TCO<true>(int32_t(5), char(0), uf::allow_converting_ints_narrowing);
    TCF2(double(5), bool());
    TCO2(char(1), bool(), uf::allow_converting_bool);
    TCO2(uint32_t(1), bool(), uf::allow_converting_bool);
    TCO2(int64_t(1), bool(), uf::allow_converting_bool);
}

TEST_CASE("container conversions") 
{
    TCF2(bool(), std::vector<uf::any>{});
    TCF2(int32_t(), std::vector<uf::any>{});
    TCF2(int64_t(), std::vector<uf::any>{});
    TCF2(double(), std::vector<uf::any>{});
    TCO<false>(uf::any().wrap(), std::vector<uf::any>{}, uf::allow_converting_any);
    //TCO<true>(std::vector<uf::any>{}, uf::any()); //This is not possible to check with TC get_as<any> will always suceeed, but cant_convert(la->a) will only work for allow_convert_any
    TC<true, false>(std::optional<uf::any>(), std::vector<uf::any>{}, uf::allow_converting_all);
    TCF2(uf::error_value(), std::vector<uf::any>{});
    TCF2(bool(), std::map<uf::any,uf::any>{});
    TCF2(int32_t(), std::map<uf::any,uf::any>{});
    TCF2(int64_t(), std::map<uf::any,uf::any>{});
    TCF2(double(), std::map<uf::any,uf::any>{});
    TCF2(uf::error_value(), std::map<uf::any,uf::any>{});
    TCO<false>(uf::any().wrap(), std::map<uf::any, uf::any>{}, uf::allow_converting_any);
    //TCO<true>(std::map<uf::any,uf::any>{}, uf::any()); //This is not possible to check with TC get_as<any> will always suceeed, but cant_convert(la->a) will only work for allow_convert_any
    TC<true, false>(std::optional<uf::any>(), std::map<uf::any, uf::any>{}, uf::allow_converting_all);
}

template <typename T> void Def(T def)
{
    uf::any a(uf::from_typestring, uf::serialize_type<T>());
    T t;
    CHECK_NOTHROW_MESSAGE(a.get(t), uf::serialize_type<T>());
    CHECK_MESSAGE(t == def, uf::serialize_type<T>());
}

TEST_CASE("default_value")
{
    Def(double(0));
    Def(int(0));
    Def(int64_t(0));
    Def(int16_t(0));
    Def(char(0));
    Def(false);
    Def(std::string());
    Def(std::vector<int>{});
    Def(std::map<int,int>{});
    Def(std::optional<double>{});
    Def(uf::any{});
    Def(uf::error_value{});
    struct test
    {
        int a;
        int b;
        auto tuple_for_serialization() noexcept { return std::tie(a, b); }
        auto tuple_for_serialization() const noexcept { return std::tie(a, b); }
        bool operator==(const test& t) const noexcept { return a == t.a && b == t.b; }
    };
    Def(test{ 0,0 });

    //uf::expected has no operator==
    uf::any a(uf::from_typestring, "xd");
    uf::expected<double> xd;
    CHECK_NOTHROW(a.get(xd));
    CHECK(xd.has_value());
    CHECK(*xd == 0.0);
}

TEST_CASE("get_as<void>")
{
    std::pair<uf::expected<void>, uf::expected<void>> t2xv;
    std::monostate m;
    uf::any At2xv(t2xv);
    auto AVt2xv = At2xv;
    CHECK_NOTHROW(At2xv.get_as<void>());
    CHECK_NOTHROW(At2xv.get_view_as<void>());
    CHECK_NOTHROW(m = At2xv.get_as< std::monostate>());
    CHECK_NOTHROW(m = At2xv.get_view_as< std::monostate>());
    CHECK_NOTHROW(At2xv.get_as<void>());
    CHECK_NOTHROW(At2xv.get_view_as<void>());
    CHECK_NOTHROW(m = At2xv.get_as< std::monostate>());
    CHECK_NOTHROW(m = At2xv.get_view_as< std::monostate>());
    t2xv.first.set_error("my_strange err", "", "");
    At2xv.assign(t2xv);
    AVt2xv = At2xv;
    CHECK_THROWS_AS(At2xv.get_as<void>(), uf::expected_with_error);
    CHECK_THROWS_AS(At2xv.get_view_as<void>(), uf::expected_with_error);
    CHECK_THROWS_AS(m = At2xv.get_as< std::monostate>(), uf::expected_with_error);
    CHECK_THROWS_AS(m = At2xv.get_view_as< std::monostate>(), uf::expected_with_error);
    CHECK_THROWS_AS(At2xv.get_as<void>(), uf::expected_with_error);
    CHECK_THROWS_AS(At2xv.get_view_as<void>(), uf::expected_with_error);
    CHECK_THROWS_AS(m = At2xv.get_as< std::monostate>(), uf::expected_with_error);
    CHECK_THROWS_AS(m = At2xv.get_view_as< std::monostate>(), uf::expected_with_error);
}

TEST_CASE("get<any>")
{
    auto aa = uf::any(1).wrap();
    uf::any a1, a2;
    CHECK_NOTHROW(aa.get(a1));
    CHECK_NOTHROW(a1.get(a2, uf::allow_converting_any));
    CHECK(a1.get_as<int>() == 1);
    CHECK(a2 == a1);
    REQUIRE_THROWS_AS(a1.get(a2, uf::serpolicy(uf::allow_converting_all & ~uf::allow_converting_any)), uf::type_mismatch_error);
    try {
        a1.get(a2, uf::serpolicy(uf::allow_converting_all & ~uf::allow_converting_any));
    } catch (const uf::type_mismatch_error & e) {
        CHECK(std::string(e.what()) == "Type mismatch when converting <i> to <a> (missing flag: convert:any)");
    }
}

using psli = std::pair<std::string, std::vector<int>>;
TEST_CASE_TEMPLATE("any::create_serialized", T, int, double, psli)
{
    T t = {};
    uf::any a;
    std::string s;
    std::map<int, std::string> m = { {0,"aaa"} };
    REQUIRE_NOTHROW(a.assign(std::tie(t, m)));
    REQUIRE_NOTHROW(s = uf::any::create_serialized(std::tie(t, m)));
    REQUIRE(uf::serialize(a) == s);
}

TEST_CASE("serializable variant") {
    std::variant<double, int, std::string> v("aaa");
    static_assert(uf::is_ser_deser_ok_v<std::variant<double, int, std::string>>);
    static_assert(uf::is_ser_deser_ok_v<std::variant<uf::any, uf::any>>);

    uf::any a;
    CHECK_NOTHROW(a.assign(v));
    CHECK(uf::serialize_print(v)=="(,,\"aaa\")");
    CHECK(a.print()=="<t3odoios>(,,\"aaa\")");

    const std::string ok =
        uf::serialize(std::optional<double>(42.42))+
        uf::serialize(std::optional<int>())+
        uf::serialize(std::optional<std::string>());
    const std::string nok1 =
        uf::serialize(std::optional<double>(42.42))+
        uf::serialize(std::optional<int>(42))+
        uf::serialize(std::optional<std::string>());
    const std::string nok2 =
        uf::serialize(std::optional<double>())+
        uf::serialize(std::optional<int>())+
        uf::serialize(std::optional<std::string>());
    CHECK_NOTHROW(uf::deserialize(ok, v));
    CHECK_THROWS_AS(uf::deserialize(nok1, v), uf::value_mismatch_error);
    CHECK_THROWS_AS(uf::deserialize(nok2, v), uf::value_mismatch_error);

    struct ser_only { int i; auto tuple_for_serialization() const noexcept { return std::tie(i); } };
    struct deser_only { int i; auto tuple_for_serialization() noexcept { return std::tie(i); } };

    std::variant<double, int, ser_only> v_ser(1);
    std::variant<double, int, deser_only> v_deser(1);
    std::string s;
    CHECK_NOTHROW(s = uf::serialize(v_ser));
    CHECK_NOTHROW(uf::deserialize(s, v_deser));
}

TEST_CASE("tuple_for_serialization with tags") {
    struct my_tag {};
    struct bad_tag {};
    struct di_tag {};
    struct void_tag {};
    static_assert(uf::impl::is_void_like<true, std::monostate, my_tag>::value);

    struct has_tag {
        int i = 0;
        int j = 1; 
        auto tuple_for_serialization() const noexcept { return std::tie(i); }
        auto tuple_for_serialization()       noexcept { return std::tie(i); }
        auto tuple_for_serialization(my_tag) const noexcept { return std::tie(j); }
        auto tuple_for_serialization(my_tag)       noexcept { return std::tie(j); }
        auto tuple_for_serialization(void_tag) const noexcept { return std::tie(); }
        auto tuple_for_serialization(void_tag)       noexcept { return std::monostate(); }
    };
    struct one_tag {
        int i = 0;
        int j = 1;
        auto tuple_for_serialization(my_tag) const noexcept { return std::tie(j); }
        auto tuple_for_serialization(my_tag)       noexcept { return std::tie(j); }
        using auto_serialization = void;
    };
    struct two_tag {
        int i = 0;
        int j = 1;
        auto tuple_for_serialization(my_tag) const noexcept { return std::tie(j); }
        auto tuple_for_serialization(my_tag)       noexcept { return std::tie(j); }
        auto tuple_for_serialization(di_tag) const noexcept { return std::tie(i); }
        auto tuple_for_serialization(di_tag)       noexcept { return std::tie(i); }
        using auto_serialization = void;
    };

    static_assert(uf::impl::has_tuple_for_serialization<false, has_tag>::value);
    static_assert(uf::impl::has_tuple_for_serialization<true, has_tag>::value);
    static_assert(uf::impl::has_tuple_for_serialization<false, has_tag, my_tag>::value);
    static_assert(uf::impl::has_tuple_for_serialization<true, has_tag, my_tag>::value);
    static_assert(uf::impl::has_tuple_for_serialization<false, has_tag, bad_tag>::value);
    static_assert(uf::impl::has_tuple_for_serialization<true, has_tag, bad_tag>::value);

    static_assert(!uf::impl::has_tuple_for_serialization<false, one_tag>::value);
    static_assert(!uf::impl::has_tuple_for_serialization<true, one_tag>::value);
    static_assert(uf::impl::has_tuple_for_serialization<false, one_tag, my_tag>::value);
    static_assert(uf::impl::has_tuple_for_serialization<true, one_tag, my_tag>::value);
    static_assert(!uf::impl::has_tuple_for_serialization<false, one_tag, bad_tag>::value);
    static_assert(!uf::impl::has_tuple_for_serialization<true, one_tag, bad_tag>::value);

    static_assert(std::is_same_v<typename uf::impl::select_tag<has_tag, uf::impl::has_tuple_for_serialization_ser, my_tag>::type, my_tag>);
    static_assert(uf::impl::has_trait_v<has_tag, uf::impl::has_tuple_for_serialization_ser, my_tag>);
    static_assert(uf::impl::has_trait_v<has_tag, uf::impl::has_tuple_for_serialization_ser, bad_tag>);
    static_assert(std::is_same_v<typename uf::impl::select_tag<one_tag, uf::impl::has_tuple_for_serialization_ser, my_tag>::type, my_tag>);
    static_assert(uf::impl::has_trait_v<one_tag, uf::impl::has_tuple_for_serialization_ser, my_tag>);
    static_assert(!uf::impl::has_trait_v<one_tag, uf::impl::has_tuple_for_serialization_ser, bad_tag>);

    static_assert(uf::impl::is_serializable_f<has_tag>());
    static_assert(uf::impl::is_serializable_f<has_tag, false, my_tag>());
    static_assert(uf::impl::is_serializable_f<has_tag, false, bad_tag>());
    static_assert(uf::impl::is_serializable_f<has_tag, false, my_tag, di_tag, bad_tag>());
    static_assert(!uf::impl::is_serializable_f<one_tag>());
    static_assert(uf::impl::is_serializable_f<one_tag, false, my_tag>());
    static_assert(!uf::impl::is_serializable_f<one_tag, false, bad_tag>());
    static_assert(uf::impl::is_serializable_f<one_tag, false, my_tag, di_tag, bad_tag>());
    static_assert(!uf::impl::is_serializable_f<two_tag>());
    static_assert(uf::impl::is_serializable_f<two_tag, false, my_tag>());
    static_assert(!uf::impl::is_serializable_f<two_tag, false, bad_tag>());
    static_assert(uf::impl::is_serializable_f<two_tag, false, my_tag, di_tag, bad_tag>());

    static_assert(uf::impl::is_deserializable_f<has_tag>());
    static_assert(uf::impl::is_deserializable_f<has_tag, false, false, my_tag>());
    static_assert(uf::impl::is_deserializable_f<has_tag, false, false, bad_tag>());
    static_assert(!uf::impl::is_deserializable_f<one_tag>());
    static_assert(uf::impl::is_deserializable_f<one_tag, false, false, my_tag>());
    static_assert(!uf::impl::is_deserializable_f<one_tag, false, false, bad_tag>());

    static_assert(uf::impl::is_deserializable_f<has_tag, true>());
    static_assert(uf::impl::is_deserializable_f<has_tag, true, false, my_tag>());
    static_assert(uf::impl::is_deserializable_f<has_tag, true, false, bad_tag>());
    static_assert(!uf::impl::is_deserializable_f<one_tag, true>());
    static_assert(uf::impl::is_deserializable_f<one_tag, true, false, my_tag>());
    static_assert(!uf::impl::is_deserializable_f<one_tag, true, false, bad_tag>());

    static_assert(uf::impl::is_void_like<true, std::tuple<>>::value);
    static_assert(uf::impl::is_void_like<true, std::tuple<std::monostate>>::value);
    static_assert(uf::impl::is_void_like<true, has_tag, void_tag>::value);
    static_assert(!uf::impl::is_void_like<true, has_tag>::value);

    struct int_tag {};
    struct str_tag {};
    struct two_types {
        int i = 0;
        std::string s;
        auto tuple_for_serialization(int_tag) const noexcept { return std::tie(i); }
        auto tuple_for_serialization(int_tag)       noexcept { return std::tie(i); }
        auto tuple_for_serialization(str_tag) const noexcept { return std::tie(s); }
        auto tuple_for_serialization(str_tag)       noexcept { return std::tie(s); }
        using auto_serialization = void;
    };
    static_assert(!uf::impl::has_tuple_for_serialization<true, two_types>::value);
    static_assert(!uf::impl::has_tuple_for_serialization<true, two_types, di_tag>::value);
    static_assert(uf::impl::has_tuple_for_serialization<true, two_types, int_tag>::value);
    static_assert(uf::impl::has_tuple_for_serialization<true, two_types, int_tag>::value);
    static_assert(uf::impl::has_tuple_for_serialization<true, two_types, str_tag>::value);
    static_assert(uf::impl::has_tuple_for_serialization<true, two_types, str_tag>::value);
    static_assert(uf::impl::has_tuple_for_serialization<true, two_types, int_tag, str_tag>::value);
    static_assert(uf::impl::has_tuple_for_serialization<true, two_types, int_tag, str_tag>::value);
    static_assert(uf::impl::has_tuple_for_serialization<true, two_types, str_tag, int_tag>::value);
    static_assert(uf::impl::has_tuple_for_serialization<true, two_types, str_tag, int_tag>::value);
    static_assert(uf::impl::has_tuple_for_serialization<true, two_types, int_tag, di_tag>::value);
    static_assert(uf::impl::has_tuple_for_serialization<true, two_types, di_tag, int_tag>::value);

    static_assert(!uf::impl::is_serializable_f<two_types>());
    static_assert(!uf::impl::is_deserializable_f<two_types>());
    static_assert(!uf::impl::is_deserializable_f<two_types, true>());
    static_assert(uf::impl::is_serializable_f<two_types, false, int_tag>());
    static_assert(uf::impl::is_deserializable_f<two_types, false, false, int_tag>());
    static_assert(uf::impl::is_deserializable_f<two_types, true, false, int_tag>());
    static_assert(uf::impl::is_serializable_f<two_types, false, str_tag>());
    static_assert(uf::impl::is_deserializable_f<two_types, false, false, str_tag>());
    static_assert(uf::impl::is_deserializable_f<two_types, true, false, str_tag>());
    static_assert(uf::impl::is_serializable_f<two_types, false, int_tag, str_tag>());
    static_assert(uf::impl::is_deserializable_f<two_types, false, false, int_tag, str_tag>());
    static_assert(uf::impl::is_deserializable_f<two_types, true, false, int_tag, str_tag>());

    static_assert(uf::impl::serialize_type_static<false, two_types>().c_str() == ""sv);
    static_assert(uf::impl::serialize_type_static<false, two_types, int_tag>().c_str() == "i"sv);
    static_assert(uf::impl::serialize_type_static<false, two_types, str_tag>().c_str() == "s"sv);
    static_assert(uf::impl::serialize_type_static<false, two_types, str_tag, int_tag>().c_str() == "s"sv);
    static_assert(uf::impl::serialize_type_static<false, two_types, int_tag, str_tag>().c_str() == "i"sv);

    two_types t2{42, "42"}, t3, t4, t5;
    uf::any ai(t2, uf::use_tags, int_tag{});
    uf::any as(t2, uf::use_tags, str_tag{});
    uf::any ad(42.42);
    CHECK(ai.print()=="<i>42");
    CHECK(as.print()=="<s>\"42\"");
    ai.get(t3, uf::allow_converting_all, uf::use_tags, int_tag{});
    t4 = as.get_as<two_types>(uf::allow_converting_all, uf::use_tags, str_tag{});
    t5 = ad.get_as<two_types>(uf::allow_converting_all, uf::use_tags, int_tag{});
    CHECK_THROWS_AS((void)ai.get_as<two_types>(uf::allow_converting_all, uf::use_tags, str_tag{}), uf::type_mismatch_error);
    CHECK_THROWS_AS((void)as.get_as<two_types>(uf::allow_converting_all, uf::use_tags, int_tag{}), uf::type_mismatch_error);
    CHECK_THROWS_AS((void)ad.get_as<two_types>(uf::allow_converting_all, uf::use_tags, str_tag{}), uf::type_mismatch_error);
    CHECK_THROWS_AS((void)ad.get_as<two_types>(uf::allow_converting_none, uf::use_tags, int_tag{}), uf::type_mismatch_error);
    CHECK(t3.i==42);
    CHECK(t3.s=="");
    CHECK(t4.i==0);
    CHECK(t4.s=="42");
}

TEST_CASE("more tag related") {
    struct S {
        using ptr = std::string *;
        mutable std::string messages;
        int i = 0;
        auto tuple_for_serialization(ptr p) const { messages.append(uf::concat("tuple_for_serialization(const):", *p, '|')); return std::tie(i); }
        void before_serialization(ptr p) const { messages.append(uf::concat("before_serialization:", *p, '|'));  }
        void after_serialization(bool success, ptr p) const noexcept { messages.append(uf::concat("after_serialization(",success?"true":"false","):", *p, '|')); }
        auto tuple_for_serialization(ptr p) { messages.append(uf::concat("tuple_for_serialization:", *p, '|')); return std::tie(i); }
        void after_deserialization_simple(ptr p) noexcept { messages.append(uf::concat("after_deserialization_simple:", *p, '|')); }
    };

    S RV1, RV2;    
    auto msg1 = std::make_unique<std::string>("msg1");
    uf::any a(RV1, uf::use_tags, std::move(msg1).get());
    CHECK(RV1.messages=="before_serialization:msg1|tuple_for_serialization(const):msg1|tuple_for_serialization(const):msg1|after_serialization(true):msg1|");
    a.get(RV2, uf::allow_converting_none, uf::use_tags, std::move(msg1).get());
    CHECK(RV2.messages=="tuple_for_serialization:msg1|after_deserialization_simple:msg1|");
}

TEST_CASE("multi-tag example") {
   struct S {
       struct as_string {};
       struct as_double {};
       double d=0;
       auto& tuple_for_serialization(as_double) const noexcept { return d; }
       auto& tuple_for_serialization(as_double)       noexcept { return d; }
       auto tuple_for_serialization(as_string) const { return std::tuple(std::to_string(d)); }
       auto tuple_for_serialization(as_string)       noexcept { return std::string(); }
       void after_deserialization(std::string &&s, as_string) noexcept { d = std::atof(s.c_str()); }
    } s;
   //CHECK(uf::serialize_type<S>()=="");       //error: no tags and no 'tuple_for_serialization() const' (without a tag)
   //CHECK(uf::serialize_type<S, int>()=="");  //error: no tags and no 'tuple_for_serialization() const' or 'tuple_for_serialization(int)' const 
   CHECK(uf::serialize_type<S, S::as_string>()=="s");   //good: yields "s"
   CHECK(uf::serialize_type<S, S::as_double>()=="d");   //good: yields "d"
   //CHECK(uf::serialize(s)=="");                                 //error: not serializable without tags
   CHECK(uf::print_escaped(uf::serialize(s, uf::use_tags, S::as_string()))=="%00%00%00%080.000000");      //good, returns a serialized string
   CHECK(uf::print_escaped(uf::serialize(s, uf::use_tags, S::as_string(), int()))=="%00%00%00%080.000000"); //good, returns a serialized string, the tag 'int' is unused
   CHECK(uf::print_escaped(uf::serialize(s, uf::use_tags, S::as_string(), S::as_double()))=="%00%00%00%080.000000"); //good, returns a serialized string, the tag 'S::as_double' has lower precedence
   //uf::any a1(s);               //error: cannot serialize 's' without tags
   uf::any a2(s, uf::use_tags, S::as_string());  //good, 'a2' now contains a string
   CHECK(a2.get_as<std::string>()=="0.000000");    //good, we can get it out.
   uf::any a3(3.14);            //a3 now contains a double
   //a3.get(s);                   //error: cannot get any value into 's' without a tag
  CHECK_THROWS_AS(a3.get(s, uf::allow_converting_all, uf::use_tags, S::as_string()), uf::type_mismatch_error);   //error: 's' with a tag 'S::as_string' expects a string. This throws a type_mismatch_error 'd'->'s'
  CHECK_NOTHROW(a3.get(s, uf::allow_converting_all, uf::use_tags, S::as_double()));   //good: 's' with a tag 'S::as_double' expects a double
  CHECK(s.d==3.14);
  CHECK(a3.get_as<S>(uf::allow_converting_all, uf::use_tags, S::as_double()).d==3.14); //good, too
  uf::any a4(42);              //a4 now contains a int
  //a4.get(s);                   //error: cannot get any value into 's' without a tag
  CHECK_THROWS_AS(a4.get(s, uf::allow_converting_all, uf::use_tags, S::as_string()), uf::type_mismatch_error);   //error: 's' with a tag 'S::as_string' expects a string. This throws a type_mismatch_error 'd'->'s'
  CHECK_NOTHROW(a4.get(s, uf::allow_converting_double, uf::use_tags, S::as_double()));//good: 's' with a tag 'S::as_double' expects a double and we can convert an int
  CHECK(s.d==42);
  CHECK_THROWS_AS(a4.get(s, uf::allow_converting_none, uf::use_tags, S::as_double()), uf::type_mismatch_error);  //error: 's' with a tag 'S::as_double' expects a double and we can NOT convert an int
}

TEST_CASE("different serialization deserialization tags") {

    struct no_deser {
        int i = 0;
        auto &tuple_for_serialization() const noexcept { return i; }
    };
    CHECK(uf::impl::has_tuple_for_serialization_member_ser<no_deser>::value);
    CHECK(!uf::impl::has_tuple_for_serialization_member_deser<no_deser>::value);
    CHECK(std::is_same_v<decltype(std::declval<std::remove_cvref_t<no_deser>>().tuple_for_serialization()),
                         decltype(std::declval<std::add_const_t<std::remove_cvref_t<no_deser>>>().tuple_for_serialization())>);
    CHECK(!uf::impl::has_tuple_for_serialization_member_deser<no_deser>::value);

    CHECK(uf::impl::has_tuple_for_serialization_ser<no_deser>::value);
    CHECK(!uf::impl::has_tuple_for_serialization_deser<no_deser>::value);

    CHECK(uf::is_serializable_v<no_deser>);
    CHECK(!uf::is_deserializable_v<no_deser>);

    struct setag {};
    struct detag {};
    struct tag_only {
        int i = 0;
        auto &tuple_for_serialization(setag) const noexcept { return i; }
        auto &tuple_for_serialization(detag)       noexcept { return i; }
        using auto_serialization = void;
    };

    CHECK(uf::impl::has_tuple_for_serialization_member_ser<tag_only, setag>::value);
    CHECK(!uf::impl::has_tuple_for_serialization_member_ser<tag_only, detag>::value);
    CHECK(!uf::impl::has_tuple_for_serialization_member_deser<tag_only, setag>::value);
    CHECK(uf::impl::has_tuple_for_serialization_member_deser<tag_only, detag>::value);

    CHECK(uf::impl::has_tuple_for_serialization_deser<tag_only, detag>::value);
    CHECK(!uf::impl::has_tuple_for_serialization_deser<tag_only, setag>::value);
    CHECK(!uf::impl::has_tuple_for_serialization_ser<tag_only, detag>::value);
    CHECK(uf::impl::has_tuple_for_serialization_ser<tag_only, setag>::value);

    using tag_de1 = typename uf::impl::select_tag<tag_only, uf::impl::has_tuple_for_serialization_deser, setag, detag>::type;
    using tag_de2 = typename uf::impl::select_tag<tag_only, uf::impl::has_tuple_for_serialization_deser, detag, setag>::type;
    using tag_se1 = typename uf::impl::select_tag<tag_only, uf::impl::has_tuple_for_serialization_ser, setag, detag>::type;
    using tag_se2 = typename uf::impl::select_tag<tag_only, uf::impl::has_tuple_for_serialization_ser, detag, setag>::type;
    CHECK(std::is_same_v<tag_de1, detag>);
    CHECK(std::is_same_v<tag_de2, detag>);
    CHECK(std::is_same_v<tag_se1, setag>);
    CHECK(std::is_same_v<tag_se2, setag>);

    CHECK(uf::impl::is_deserializable_f<tag_only, false, false, detag>());
    CHECK(!uf::impl::is_deserializable_f<tag_only, false, false, setag>());
    CHECK(uf::impl::is_deserializable_f<tag_only, false, false, setag, detag>());
}

TEST_CASE("Monostate pair") {
    std::monostate m;
    std::pair p("aaa", m);
    uf::any a(p);
    CHECK(a.type()=="s");
    CHECK(a.print()=="<s>\"aaa\"");
}

TEST_CASE("single element aggregate conversion") {
    //Typestring for these are the same as that of their element, but they do not
    //have the functions of the element such as reset() or emplace().
    std::array<std::optional<int>, 1> aoi1;
    std::array<std::optional<double>, 1> aod1;
    uf::any aao(aoi1);
    CHECK_NOTHROW(aao.get(aod1, uf::allow_converting_double));

    std::optional<int> caoi1[1];
    std::optional<double> caod1[1];
    uf::any acao(caoi1);
    CHECK_NOTHROW(acao.get(caod1, uf::allow_converting_double));

    std::tuple<std::optional<int>> toi1;
    std::tuple<std::optional<double>> tod1;
    uf::any ato(toi1);
    CHECK_NOTHROW(ato.get(tod1, uf::allow_converting_double));
}

TEST_CASE("number text") {
    CHECK(uf::any(uf::from_text, "-1").print() == "<i>-1");
    CHECK(uf::any(uf::from_text, "-1.").print() == "<d>-1.");
    CHECK(uf::any(uf::from_text, "-1e1").print() == "<d>-10.");
    CHECK(uf::any(uf::from_text, "1e+1").print() == "<d>10.");
    CHECK(uf::any(uf::from_text, "1e-1").print() == "<d>0.1");
    CHECK(uf::any(uf::from_text, "inf").print() == "<d>inf");

    std::string dummy, err;
    bool e;
    std::string_view sv;

    sv = "1234567891234567891123455678554";
    std::tie(err, e) = uf::impl::parse_value(dummy, sv);
    CHECK(e);
    CHECK(err=="Integer out-of range for uint64.");

    sv = "-1234567891234567891123455678554";
    std::tie(err, e) = uf::impl::parse_value(dummy, sv);
    CHECK(e);
    CHECK(err=="Integer out-of range for int64.");

    sv = "-a";
    std::tie(err, e) = uf::impl::parse_value(dummy, sv);
    CHECK(e);
    CHECK(err=="Did not recognize this: '-a'.");
    CHECK(sv=="-a");

    sv = ".a";
    std::tie(err, e) = uf::impl::parse_value(dummy, sv);
    CHECK(e);
    CHECK(err=="Did not recognize this: '.a'.");
    CHECK(sv==".a");

    sv = "ABC";
    std::tie(err, e) = uf::impl::parse_value(dummy, sv);
    CHECK(e);
    CHECK(err=="Did not recognize this: 'ABC'. (Maybe missing '\"' for strings?)");
    CHECK(sv=="ABC");

    sv = "1ea";
    std::tie(err, e) = uf::impl::parse_value(dummy, sv);
    CHECK(!e);
    CHECK(err=="i");
    CHECK(sv=="ea");

    sv = "[1ea]";
    std::tie(err, e) = uf::impl::parse_value(dummy, sv);
    CHECK(e);
    CHECK(err=="List items must be separated by ';' or ','.");
    CHECK(sv=="ea]");
}

TEST_CASE("json-like") {
    auto t = std::tuple(1, "aa", 42.2, std::pair(true, false), uf::any(23), std::optional<double>(0.1), 'x',
                        std::vector{1, 2, 3}, std::map<std::string, int>{{"a", 1}, {"b", 2}});
    uf::any a(t);
    CHECK(a.print(0, {}, '%', false) == "<t9isdt2bbaodclimsi>(1,\"aa\",42.2,(true,false),<i>23,0.1,'x',[1,2,3],{\"a\":1,\"b\":2})");
    CHECK(a.print(0, {}, '%', true) == "[1,\"aa\",42.2,[true,false],23,0.1,\"x\",[1,2,3],{\"a\":1,\"b\":2}]");
}

TEST_CASE("Heterogeneous list/map text parsing ") {
    std::vector txts = {R"({"a":1,"b":[1,1]})", R"([1,"a"])"};
    std::vector types = {"msa", "la"};
    std::vector errors = {"Mismatching mapped types: <s> and <li>.", "Mismatching types in list: <i> and <s>."};
    std::vector values = {
        "%00%00%00%02%00%00%00%01a%00%00%00%01i%00%00%00%04%00%00%00%01%00%00%00%01b%00%00%00%02li%00%00%00%0c%00%00%00%02%00%00%00%01%00%00%00%01",
        "%00%00%00%02%00%00%00%01i%00%00%00%04%00%00%00%01%00%00%00%01s%00%00%00%05%00%00%00%01a"
    };
    REQUIRE(txts.size()==types.size());
    REQUIRE(txts.size()==errors.size());
    for (unsigned u = 0; u<txts.size(); u++) {
        std::string_view value = txts[u];
        std::string to;
        auto [type, invalid] = uf::impl::parse_value(to, value, true);
        REQUIRE_FALSE(invalid);
        CHECK(type == types[u]);
        CHECK(uf::print_escaped(to)==values[u]);
        CHECK(uf::serialize_print_by_type(type, to, true)==txts[u]);
        to.clear();
        value = txts[u];
        std::tie(type, invalid) = uf::impl::parse_value(to, value, false);
        CHECK(invalid);
        CHECK(type==errors[u]);
    }
}

std::string remove_asterisk(std::string_view err_type) {
    std::string err_type_no_asterisk(err_type);
    err_type_no_asterisk.erase(std::remove(err_type_no_asterisk.begin(), err_type_no_asterisk.end(), '*'), err_type_no_asterisk.end());
    return err_type_no_asterisk;
}

/** Checks if the 'err' starts with 'err_msg' and contains 'err_type' as the problematic type.
 * The problematic type is identified as the first or second string enclosed in '<>', where
 * 'second_type' tells us if we need to look for the second type. 'info' will be printed on error. */
void check_error(std::string_view err, std::string_view err_msg, std::string_view err_type, bool second_type, std::string_view info) {
    INFO(info);
    INFO(err);
    if (err_type.size() && err_type.front()=='*') err_type.remove_prefix(1);
    CHECK(err.substr(0, err_msg.size()) == err_msg);
    size_t pos1 = err.find('<');
    if (second_type) pos1 = err.find('<', pos1+1);
    const size_t pos2 = err.find('>', pos1);
    CHECK_MESSAGE(pos2 != std::string::npos, "Could not find <> in error.");
    if (pos2 != std::string::npos) CHECK(err.substr(pos1+1, pos2-pos1-1)==err_type);
}
/** This function takes a type-value pair containing some illegal type/value combo.
 * It can either contain it at the top level typestring (naked) or encapsulated into an any.
 * It encapsulates the received type/value pair into various container types (any, list of any, 
 * map, etc.) and performs the 6 operations on it 
 * - print
 * - scan (recursively)
 * - deserialize into T
 * - deserialize convert into T
 * - cant_convert<true, false> (not actually converting)
 * - cant_convert<true, true> (actually producing a converted string)
 * and checks the resulting error msg.
 * @param [in] err_msg The error message shall start with this message. Except as with 'naked'
 * @param [in] encaps_type The requested error type, when the problem is inside an any.
 *             For T='li' (with one of the 'a's containing the erroneous 'type'/'value') this 
 *             is la(*<type>) with the * appropriately placed. This type is expected when we
 *             recursively parse into the any.
 * @param [in] linear_type The requested error type when the problem type is inside an any and
 *             the problem is with the any itself or the problem is not inside an any.
 *             For T='li' this will be 'l*a' or l*<type> with the * appropriately placed, depending 
 *             on if we have encaps or naked. The former is expected when we cannot convert the
 *             any itself (due to not having allow_convert_any), in which case we do not descend
 *             into the (otherwise erroneous) content of the any. The latter is expected 
 *             when the type is not encapsulated in an any before added to the structure.
 * @param [in] naked True if the 'type'/'value' pair contains the error NOT encapsulated in an any,
 *             but at the top level typestring.*/
template <typename T>
void test_error_msg(std::string_view type, std::string_view value,
                    std::string_view err_msg, std::string_view encaps_type, 
                    std::string_view linear_type, bool naked, const T&) {
    INFO(uf::concat("type: ", type, ", linear=", linear_type, ", encaps_type=", encaps_type, ", T=", uf::deserialize_type<T>(), ", naked=", naked));

    uf::any a(uf::from_type_value_unchecked, type, value);
    CHECK_THROWS_AS((void)a.print(), uf::value_error);
    try { std::cout<<a.print()<<std::endl; } 
    catch (const uf::value_error &e) { check_error(e.what(), err_msg, encaps_type, false, "print");  }

    std::string sa = uf::serialize(a);
    CHECK_THROWS((void)uf::any(uf::from_raw, sa, true));
    try { (void)uf::any(uf::from_raw, sa, true); }
    catch (const uf::value_error &e) { check_error(e.what(), err_msg, encaps_type, false, "scan"); }

    CHECK_THROWS((void)a.get_view_as<T>(uf::allow_converting_none));
    try { (void)a.get_view_as<T>(uf::allow_converting_none); }
    catch (const uf::value_error &e) { 
        //If the type of an any equals to 'T', we do fast path with no position error
        std::string type_to_look_for = a.type()==uf::deserialize_type<T>() ? remove_asterisk(linear_type) : std::string(linear_type);
        check_error(e.what(), naked ? err_msg : "Type mismatch when converting", 
                    type_to_look_for, !naked, "deserialize");
    }

    CHECK_THROWS((void)a.get_view_as<T>(uf::allow_converting_all));
    try { (void)a.get_view_as<T>(uf::allow_converting_all); }
    catch (const uf::value_error &e) { 
        //If the type of an any equals to 'T', we do fast path with no position error
        std::string type_to_look_for = a.type()==uf::deserialize_type<T>() ? remove_asterisk(encaps_type) : std::string(encaps_type);
        check_error(e.what(), err_msg, type_to_look_for, false, "convert1");
    }

    auto verr = uf::cant_convert(a.type(), uf::deserialize_type<T>(), uf::allow_converting_all, a.value());
    CHECK(verr);
    if (verr) check_error(verr->what(), err_msg, encaps_type, false, "convert2"); 

    if (type == uf::deserialize_type<T>())
        CHECK_NOTHROW((void)a.convert_to<T>(uf::allow_converting_all, false));

    CHECK_THROWS((void)a.convert_to<T>(uf::allow_converting_all, true));
    try { (void)a.convert_to<T>(uf::allow_converting_all, true); }
    catch (const uf::value_error &e) { check_error(e.what(), err_msg, encaps_type, false, "convert3"); }
}


/** Calls 'test_error_msg' using the provided type/value in the following contexts
* - The 'naked' versions, when it is not encapsulated in an any:
*   - by itself
*   - as a list element
*   - as a map element
*   - as an optional content
*   - as an expected content
*   - as part of a tuple
* - The 'encapsulated' versions, when the problematic type/value is embedded in an any:
*   - any by itself
*   - list of anys
*   - map of string to anys
*   - tuple of any and string
*   - optional any
*   - expected any
* - Then the double deep, when there is two layers of any encapsulation:
*   - list of tuple any,strs 
* These are all to test that the right kind of error message is reported with the right kind of type.*/
template <typename T>
void test_error_msgs(std::string_view type, std::string_view value, std::string_view err_msg, 
                     std::string_view err_type, std::string_view err_naked_type) {
    
    //Note that the problematic type must be the last one for naked runs to actually get the "unexpected end of value" situation.
    test_error_msg(type, value, err_msg, err_type, err_type, true, T{});

    test_error_msg(uf::concat('l', type),
                   uf::concat(uf::serialize(uint32_t(2)), uf::default_serialized_value(uf::deserialize_type<T>()), value),
                   err_msg, uf::concat('l', err_type), uf::concat('l', err_type), true, std::vector<T>{});

    test_error_msg(uf::concat("md", type),
                   uf::concat(uf::serialize(uint32_t(2)), uf::serialize(0.42), uf::default_serialized_value(uf::deserialize_type<T>()), uf::serialize(42.0), value),
                   err_msg, uf::concat("md", err_type), uf::concat("md", err_type), true, std::map<double, T>{});

    test_error_msg(uf::concat('o', type),
                   uf::concat('\1', value),
                   err_msg, uf::concat('o', err_type), uf::concat('o', err_type), true, std::optional<T>{});

    test_error_msg(uf::concat('x', type),
                   uf::concat('\1', value),
                   err_msg, uf::concat('x', err_type), uf::concat('x', err_type), true, uf::expected<T>{});

    test_error_msg(uf::concat("t2d", type),
                   uf::concat(uf::serialize(0.42), value),
                   err_msg, uf::concat("t2d", err_type), uf::concat("t2d", err_type), true, std::pair<double, T>{});

    //Now do the encapsulated tests: the problematic type/value combo is encapsulated in an any
    uf::any a(uf::from_type_value_unchecked, type, value);
    uf::any b(T{});
    test_error_msg("a", uf::serialize(a), err_msg, uf::concat("a(", err_type, ")"), err_naked_type, false, T{});

    std::vector<uf::any_view> la = {b, a};
    uf::any ala(la);
    test_error_msg(ala.type(), ala.value(), err_msg, uf::concat("la(", err_type, ")"), uf::concat("l", err_naked_type), false, std::vector<T>{});

    std::map<std::string, uf::any_view> msa = {{"1", b}, {"2", a}};
    uf::any amsa(msa);
    test_error_msg(amsa.type(), amsa.value(), err_msg, uf::concat("msa(", err_type, ")"), uf::concat("ms", err_naked_type), false, std::map<std::string, T>{});

    std::pair<uf::any_view, std::string> t2as = {a, "2"};
    uf::any at2as(t2as);
    test_error_msg(at2as.type(), at2as.value(), err_msg, uf::concat("t2a(", err_type, ")s"), uf::concat("t2", err_naked_type, 's'), false, std::pair<T, std::string>{});

    std::optional<uf::any_view> oa(a);
    uf::any aoa(oa);
    test_error_msg(aoa.type(), aoa.value(), err_msg, uf::concat("oa(", err_type, ")"), uf::concat("o", err_naked_type), false, std::optional<T>{});

    uf::expected<uf::any> xa(a);
    uf::any axa(xa);
    test_error_msg(axa.type(), axa.value(), err_msg, uf::concat("xa(", err_type, ")"), uf::concat("x", err_naked_type), false, uf::expected<T>{});

    //Now do a double encaps
    std::vector<std::pair<uf::any_view, std::string>> lt2as{{b, "2"}, t2as};
    uf::any alt2as(lt2as);
    test_error_msg(alt2as.type(), alt2as.value(), err_msg, uf::concat("lt2a(", err_type, ")s"), uf::concat("lt2", err_naked_type, 's'), false,
                   std::vector<std::pair<T, std::string>>{});
}

TEST_CASE("Error messages") {
    test_error_msgs<int>("i", "", "Value does not match type", "*i", "*i");
    test_error_msgs<int>("@", "", "Invalid character", "*@", "*i");
    test_error_msgs<std::pair<char, char>>("t2ccc", "ab", "Extra characters after typestring", "t2cc*c", "*t2cc");
    test_error_msgs<std::pair<char, char>>("tc", "a", "Number at least 2 expected", "t*c", "*t2cc");
    test_error_msgs<std::pair<char, char>>("t1c", "a", "Number at least 2 expected", "t1*c", "*t2cc");
    test_error_msgs<std::pair<char, char>>("t2c", "ab", "Unexpected end of typestring", "t2c*", "*t2cc");
}


TEST_CASE("noexcept ser") {
    struct tuple_for_serialization_throws {
        std::monostate tuple_for_serialization() const { throw std::runtime_error(""); }
        std::monostate tuple_for_serialization(int) const { throw std::runtime_error(""); }
        std::monostate tuple_for_serialization(double) const noexcept { return {}; }
    };
    static_assert(uf::is_serializable_v<tuple_for_serialization_throws>);
    static_assert(uf::is_serializable_v<tuple_for_serialization_throws, int>);
    static_assert(uf::is_serializable_v<tuple_for_serialization_throws, double>);
    static_assert(!uf::is_deserializable_v<tuple_for_serialization_throws>);
    static_assert(!uf::is_deserializable_v<tuple_for_serialization_throws, int>);
    static_assert(!uf::is_deserializable_v<tuple_for_serialization_throws, double>);
    static_assert(!uf::impl::has_noexcept_tuple_for_serialization_ser_f<tuple_for_serialization_throws>());
    static_assert(!uf::impl::has_noexcept_tuple_for_serialization_ser_f<tuple_for_serialization_throws, int>());
    static_assert(uf::impl::has_noexcept_tuple_for_serialization_ser_f<tuple_for_serialization_throws, double>());
    using tag1 = typename uf::impl::select_tag<tuple_for_serialization_throws, uf::impl::has_tuple_for_serialization_ser>::type;
    using tag2 = typename uf::impl::select_tag<tuple_for_serialization_throws, uf::impl::has_tuple_for_serialization_ser, int>::type;
    using tag3 = typename uf::impl::select_tag<tuple_for_serialization_throws, uf::impl::has_tuple_for_serialization_ser, double>::type;
    static_assert(std::is_void_v<tag1>);
    static_assert(std::is_same_v<tag2, int>);
    static_assert(std::is_same_v<tag3, double>);
    using uf::impl::tuple_for_serialization;
    static_assert(!noexcept(tuple_for_serialization(uf::impl::decllval<const tuple_for_serialization_throws>())));
    static_assert(!noexcept(tuple_for_serialization(uf::impl::decllval<const tuple_for_serialization_throws>(), 5)));
    static_assert(noexcept(tuple_for_serialization(uf::impl::decllval<const tuple_for_serialization_throws>(), 5.5)));
    static_assert(!uf::impl::is_noexcept_for<tuple_for_serialization_throws>(uf::impl::nt::len));
    static_assert(!uf::impl::is_noexcept_for<tuple_for_serialization_throws, int>(uf::impl::nt::len));
    static_assert(uf::impl::is_noexcept_for<tuple_for_serialization_throws, double>(uf::impl::nt::len));
    static_assert(!uf::impl::is_noexcept_for<tuple_for_serialization_throws, long int>(uf::impl::nt::len));
    static_assert(!noexcept(uf::impl::serialize_len(tuple_for_serialization_throws())));
    static_assert(!noexcept(uf::impl::serialize_len(tuple_for_serialization_throws(), 5)));
    static_assert(noexcept(uf::impl::serialize_len(tuple_for_serialization_throws(), 5.5)));
    char *ptr;
    static_assert(!noexcept(uf::impl::serialize_to(tuple_for_serialization_throws(), ptr)));
    static_assert(!noexcept(uf::impl::serialize_to(tuple_for_serialization_throws(), ptr, 5)));
    static_assert(noexcept(uf::impl::serialize_to(tuple_for_serialization_throws(), ptr, 5.5)));
    struct before_serialization_throws {
        std::monostate tuple_for_serialization() const noexcept { return {}; }
        void before_serialization() const { throw std::runtime_error(""); }
    };
    static_assert(uf::is_serializable_v<before_serialization_throws>);
    static_assert(uf::impl::has_noexcept_tuple_for_serialization_ser_f<before_serialization_throws>());
    static_assert(!uf::impl::has_noexcept_before_serialization_f<before_serialization_throws>());
    static_assert(uf::impl::is_noexcept_for<before_serialization_throws>(uf::impl::nt::len));
    static_assert(uf::impl::is_noexcept_for<before_serialization_throws, double>(uf::impl::nt::len)); //no such tag
    static_assert(!uf::impl::is_noexcept_for<before_serialization_throws>(uf::impl::nt::ser));
    static_assert(noexcept(uf::impl::serialize_len(before_serialization_throws())));
    static_assert(!noexcept(uf::impl::serialize_to(before_serialization_throws(), ptr)));
}

TEST_CASE("noexcept deser") {
    struct tuple_for_serialization_throws {
        std::monostate tuple_for_serialization() { throw std::runtime_error(""); }
        std::monostate tuple_for_serialization(int) { throw std::runtime_error(""); }
        std::monostate tuple_for_serialization(double) noexcept { return {}; }
    } t;
    static_assert( uf::is_deserializable_v<tuple_for_serialization_throws>);
    static_assert( uf::is_deserializable_v<tuple_for_serialization_throws, int>);
    static_assert( uf::is_deserializable_v<tuple_for_serialization_throws, double>);
    static_assert(!uf::is_serializable_v<tuple_for_serialization_throws>);
    static_assert(!uf::is_serializable_v<tuple_for_serialization_throws, int>);
    static_assert(!uf::is_serializable_v<tuple_for_serialization_throws, double>);
    static_assert(!uf::impl::has_noexcept_tuple_for_serialization_deser_f<tuple_for_serialization_throws>());
    static_assert(!uf::impl::has_noexcept_tuple_for_serialization_deser_f<tuple_for_serialization_throws, int>());
    static_assert( uf::impl::has_noexcept_tuple_for_serialization_deser_f<tuple_for_serialization_throws, double>());
    using tag1 = typename uf::impl::select_tag<tuple_for_serialization_throws, uf::impl::has_tuple_for_serialization_deser>::type;
    using tag2 = typename uf::impl::select_tag<tuple_for_serialization_throws, uf::impl::has_tuple_for_serialization_deser, int>::type;
    using tag3 = typename uf::impl::select_tag<tuple_for_serialization_throws, uf::impl::has_tuple_for_serialization_deser, double>::type;
    static_assert(std::is_void_v<tag1>);
    static_assert(std::is_same_v<tag2, int>);
    static_assert(std::is_same_v<tag3, double>);
    using uf::impl::tuple_for_serialization;
    static_assert(!noexcept(tuple_for_serialization(uf::impl::decllval<tuple_for_serialization_throws>())));
    static_assert(!noexcept(tuple_for_serialization(uf::impl::decllval<tuple_for_serialization_throws>(), 5)));
    static_assert( noexcept(tuple_for_serialization(uf::impl::decllval<tuple_for_serialization_throws>(), 5.5)));
    static_assert(!uf::impl::is_noexcept_for<tuple_for_serialization_throws>(uf::impl::nt::deser));
    static_assert(!uf::impl::is_noexcept_for<tuple_for_serialization_throws, int>(uf::impl::nt::deser));
    static_assert( uf::impl::is_noexcept_for<tuple_for_serialization_throws, double>(uf::impl::nt::deser));
    static_assert(!uf::impl::is_noexcept_for<tuple_for_serialization_throws, long int>(uf::impl::nt::deser));
    const char *p, *end;
    static_assert(!noexcept(uf::impl::deserialize_from<false>(p, end, t)));
    static_assert(!noexcept(uf::impl::deserialize_from<false>(p, end, t, 5)));
    static_assert( noexcept(uf::impl::deserialize_from<false>(p, end, t, 5.5)));
    struct after_deserialization_throws {
        std::monostate tuple_for_serialization() noexcept { return {}; }
        void after_deserialization(std::monostate) { throw std::runtime_error(""); }
        void after_deserialization(std::monostate, int) noexcept { }
        void after_deserialization_simple(void*) noexcept {}
    } a;
    static_assert(uf::is_deserializable_v<after_deserialization_throws>);
    static_assert(!uf::has_after_deserialization_tag_v<after_deserialization_throws, int>); //TODO: this is only true if we have a tuple_for_serialization of the same tag!
    static_assert(!uf::has_after_deserialization_simple_tag_v<after_deserialization_throws, int>);
    static_assert( uf::has_after_deserialization_simple_tag_v<after_deserialization_throws, void*>);
    static_assert(uf::impl::has_noexcept_tuple_for_serialization_deser_f<after_deserialization_throws>());
    static_assert(!uf::impl::has_noexcept_after_deserialization_f<after_deserialization_throws>());
    static_assert(!uf::impl::is_noexcept_for<after_deserialization_throws>(uf::impl::nt::deser));
    static_assert(!uf::impl::is_noexcept_for<after_deserialization_throws, int>(uf::impl::nt::deser));
    static_assert(!uf::impl::is_noexcept_for<after_deserialization_throws, double>(uf::impl::nt::deser)); //no such tag
    static_assert(!uf::impl::is_noexcept_for<after_deserialization_throws>(uf::impl::nt::ser));
    static_assert(!noexcept(uf::impl::deserialize_from<false>(p, end, a)));
    static_assert(!noexcept(uf::impl::deserialize_from<false>(p, end, a, (void*)nullptr))); //not considered due to tagless after_deserialization()
}

struct global_test {
    int a;
    int b;
};

auto tuple_for_serialization(const global_test &t) { return std::tie(t.a, t.b); }

#ifdef HAVE_BOOST_PFR
TEST_CASE("Reflection on value types") {

    //Test various aggregates/non-aggregates
    struct test {
        int a;
        int b;
    };
    CHECK(uf::serialize_type<test>()=="t2ii");
    CHECK(uf::is_ser_deser_ok_v<test>);
    struct enabled_test {
        int a;
        int b;
        using auto_serialization = double; //whatever non-void type
    };
    CHECK(uf::serialize_type<enabled_test>()=="t2ii");
    CHECK(uf::is_ser_deser_ok_v<enabled_test>);
    struct disabled_test {
        int a;
        int b;
        using auto_serialization = void;
    };
    CHECK(!uf::is_serializable_v<disabled_test>);
    CHECK(!uf::is_deserializable_v<disabled_test>);
    struct test_ctor1 {
        int a;
        int b;
        test_ctor1(int x) : a(x), b(x) {}
    };
    CHECK(!uf::is_serializable_v<test_ctor1>);
    CHECK(!uf::is_deserializable_v<test_ctor1>);
    struct test_ctor2 {
        int a;
        int b;
        test_ctor2() noexcept = default;
    };
    CHECK(!std::is_aggregate_v<test_ctor2>);
    CHECK(!uf::is_serializable_v<test_ctor2>);
    CHECK(!uf::is_deserializable_v<test_ctor2>);

    struct test_bad_member {
        double d;
        test_ctor2 i; //non-serializable
    };
    CHECK(std::is_aggregate_v<test_bad_member>);
    CHECK(!uf::is_serializable_v<test_bad_member>);
    CHECK(!uf::is_deserializable_v<test_bad_member>);

    //test ser or deser only
    struct no_deser {
        int i = 0;
        auto &tuple_for_serialization() const noexcept { return i; }
    };
    CHECK(uf::is_serializable_v<no_deser>);
    CHECK(!uf::is_deserializable_v<no_deser>);

    CHECK(uf::is_serializable_v<global_test>);
    CHECK(!uf::is_deserializable_v<global_test>);

    //This causes compiler error: boost pfr does not provide any traits to check what will it work for.
    //struct test_inherited : test {
    //    double c;
    //};
    //CHECK(uf::serialize_type<test_inherited>()=="t2t2iid");
    //CHECK(uf::is_ser_deser_ok_v<test_inherited>);

    //test simple struct with auto serialization, including printing
    struct test_str {
        int i;
        std::string s;
    };
    CHECK(uf::any(test{1,17}).print() == "<t2ii>(1,17)");
    CHECK(uf::any(test_str{1,"17"}).print() == "<t2is>(1,\"17\")");
    CHECK(uf::serialize_print(test_str{1,"17"}) == "(1,\"17\")");

    //test nested structs with auto serialization
    struct test_dup {
        test t;
        test_str ts;
    };
    CHECK(uf::any(test_dup{{42,43}, {44,"42"}}).print() == "<t2t2iit2is>((42,43),(44,\"42\"))");

    //Test that after_*** and before_** is not called even if exists.
    static int side_effect = 0;
    struct test_side1 {
        int i;
        void before_serialization() const noexcept { side_effect++; };
        void after_serialization(bool) const noexcept { side_effect++; };
        void after_deserialization_simple() noexcept { side_effect++; };
    };
    CHECK(uf::has_before_serialization_tag_v<test_side1>);
    CHECK(uf::has_after_serialization_tag_v<test_side1>);
    CHECK(uf::has_after_deserialization_simple_tag_v<test_side1>);
    CHECK(uf::any(test_side1{42}).get_as<test_side1>().i == 42);
    CHECK(side_effect==0);
    
    //Test that after_*** and before_** is called for members
    struct test_side2 {
        int i;
        void before_serialization() const noexcept { side_effect++; };
        void after_serialization(bool) const noexcept { side_effect++; };
        void after_deserialization_simple() noexcept { side_effect++; };
        auto &tuple_for_serialization() const noexcept { return i; }
        auto &tuple_for_serialization()       noexcept { return i; }
    };
    CHECK(uf::has_before_serialization_tag_v<test_side2>);
    CHECK(uf::has_after_serialization_tag_v<test_side2>);
    CHECK(uf::has_after_deserialization_simple_tag_v<test_side2>);
    struct test_iside {
        int i;
        test_side2 t;
    };
    side_effect = 0;
    uf::any atest_iside(test_iside{42,{43}});
    CHECK(side_effect==2);
    CHECK(atest_iside.get_as<test_iside>().t.i==43);
    CHECK(side_effect==3);

    //Check that components taking tags work
    struct tag {};
    struct need_tag {
        int i;
        using auto_serialization = void;
        auto &tuple_for_serialization(tag) const noexcept { return i; }
        auto &tuple_for_serialization(tag)       noexcept { return i; }
    };
    CHECK(uf::is_serializable_v<need_tag, tag>);
    CHECK(uf::is_deserializable_v<need_tag, tag>);
    CHECK(!uf::is_serializable_v<need_tag>);
    CHECK(!uf::is_deserializable_v<need_tag>);
    struct test_tag {
        int i;
        need_tag t;
    };
    CHECK(uf::is_serializable_v<test_tag, tag>);
    CHECK(uf::is_deserializable_v<test_tag, tag>);
    CHECK(!uf::is_serializable_v<test_tag>);
    CHECK(!uf::is_deserializable_v<test_tag>);
    CHECK(uf::any(test_tag{42,{42}}, uf::use_tags, tag{}).print() == "<t2ii>(42,42)");
    CHECK(uf::any(std::pair(42.2, 43.1)).get_as<test_tag>(uf::allow_converting_double, uf::use_tags, tag{}).i == 42);
    CHECK(uf::any(std::pair(42.2, 43.1)).get_as<test_tag>(uf::allow_converting_double, uf::use_tags, tag{}).t.i == 43);
}
#endif

TEST_CASE("JSON") {
    using namespace std::literals;
    auto json = R"({"x":1,"y":true,"z":null})"sv;
    uf::any a;
    REQUIRE_NOTHROW(a.assign(uf::from_text, json));
    CHECK(a.print_json()==json);
}