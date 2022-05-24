#include <benchmark/benchmark.h>
#include <ufser.h>

void BM_construct_type_mismatch_error(benchmark::State &state) {
    for (auto _ : state)
        benchmark::DoNotOptimize(uf::type_mismatch_error("error text is longer than sso, i am sure", "t5bciId", "t5bxsiId", 3, 4));

}

template <class T>
void BM_get(benchmark::State &state, uf::any_view a, T&t) {
    for (auto _ : state) {
        try {
            a.get(t);
        } catch (...) {
        }
    }
}

template <class T>
void BM_cnv(benchmark::State &state, uf::any_view a, T &t) {
    for (auto _ : state) {
        try {
            std::vector<uf::error_value> errors;
            std::vector<std::pair<size_t, size_t>> error_pos;
            uf::impl::deserialize_convert_params p(a.value(), a.type(), &t, uf::allow_converting_all, nullptr,
                                                   &errors, &error_pos);
            bool dummy;
            (void)uf::impl::deserialize_convert_from<false>(dummy, p, t);
        } catch (...) {
        }
    }
}

void BM_scn(benchmark::State &state, std::string_view type, std::string_view value) {
    for (auto _ : state) {
        try {
            benchmark::DoNotOptimize(uf::impl::serialize_scan_by_type(type, value));
        } catch (...) {}
    }
}

template <class T>
void BM_cto(benchmark::State &state, uf::any_view a, T &) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(a.converts_to<T>());
    }
}

template <class T>
void BM_ser(benchmark::State &state, const T &t) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(uf::any(t));
    }
}


struct A     {
    bool b; char c; int32_t i; int64_t I; double d;
    auto tuple_for_serialization() const noexcept { return std::tie(b, c, i, I, d); }
    auto tuple_for_serialization() noexcept { return std::tie(b, c, i, I, d); }
} a = {true, 'a', 42, 4242, 41.3};
struct A3     {
    double b; double c; int64_t i; int32_t I; int d;
    auto tuple_for_serialization() const noexcept { return std::tie(b, c, i, I, d); }
    auto tuple_for_serialization() noexcept { return std::tie(b, c, i, I, d); }
} a3; //need all - shall fail
struct AX1     {
    bool b; uf::expected<char> c; int32_t i; int64_t I; double d;
    auto tuple_for_serialization() const noexcept { return std::tie(b, c, i, I, d); }
    auto tuple_for_serialization() noexcept { return std::tie(b, c, i, I, d); }
} ax1;

struct AX2     {
    bool b; uf::expected<std::string> c; int32_t i; int64_t I; double d;
    auto tuple_for_serialization() const noexcept { return std::tie(b, c, i, I, d); }
    auto tuple_for_serialization() noexcept { return std::tie(b, c, i, I, d); }
} ax2;
struct AS {
    double b; std::string c; int64_t i; int32_t I; int d;
    auto tuple_for_serialization() const noexcept { return std::tie(b, c, i, I, d); }
    auto tuple_for_serialization() noexcept { c = "a"; return std::tie(b, c, i, I, d); } //assigment is to release previous content & force an allocation
} as = {42., "123456789012345678901234567890", 0, 0, 0};

uf::any aa(a), aas(as);

BENCHMARK(BM_construct_type_mismatch_error);

BENCHMARK_CAPTURE(BM_scn, scan_i, "i", std::string(4, '\0'));
BENCHMARK_CAPTURE(BM_scn, scan_i3_err, "i", std::string(3, '\0'));
BENCHMARK_CAPTURE(BM_scn, scan_i5_err, "i", std::string(5, '\0'));

//serialize/deserialize string
BENCHMARK_CAPTURE(BM_get, dese_t5bsiId, aas, as);

//deserialize a matching value into an expected
BENCHMARK_CAPTURE(BM_ser, ser_t5bciId, a);
BENCHMARK_CAPTURE(BM_get, dese_t5bciId, aa, a);
BENCHMARK_CAPTURE(BM_scn, scan_t5bciId, aa.type(), aa.value());
BENCHMARK_CAPTURE(BM_scn, scan_t5bciId_err, aa.type(), aa.value().substr(1));
//deserialize a matching value into an expected
BENCHMARK_CAPTURE(BM_get, conv_t5bciId_t5bxciId, aa, ax1);
BENCHMARK_CAPTURE(BM_cnv, conv_t5bciId_t5bxciId, aa, ax1);
BENCHMARK_CAPTURE(BM_cto, conv_t5bciId_t5bxciId, aa, ax1);
//deserialize a non-matching value into an expected
BENCHMARK_CAPTURE(BM_get, conv_t5bciId_t5bxsiId_err, aa, ax2);
BENCHMARK_CAPTURE(BM_cnv, conv_t5bciId_t5bxsiId_err, aa, ax2);
BENCHMARK_CAPTURE(BM_cto, conv_t5bciId_t5bxsiId_err, aa, ax2);
//deserialize a matching expected into a value
uf::any aax(ax1);
BENCHMARK_CAPTURE(BM_get, conv_t5bdiId_t5bxciId, aax, ax1);
BENCHMARK_CAPTURE(BM_cnv, conv_t5bdiId_t5bxciId, aax, ax1);
BENCHMARK_CAPTURE(BM_cto, conv_t5bdiId_t5bxciId, aax, ax1);
//then into a non-matching value
BENCHMARK_CAPTURE(BM_get, conv_t5bciId_t5bxsiId_err, aax, a3);
BENCHMARK_CAPTURE(BM_cnv, conv_t5bciId_t5bxsiId_err, aax, a3);
BENCHMARK_CAPTURE(BM_cto, conv_t5bciId_t5bxsiId_err, aax, a3);


//Do a more complex muti-level struct
struct AM {
    A a;
    A3 a3;
    std::vector<AX1> ax1;
    std::map<char, AX2> ax2; 
    std::optional<AS> as;
    AM() : ax1 { {} }, ax2{{'a',{}}}, as{AS{}} {}
    auto tuple_for_serialization() const noexcept { return std::tie(a, a3, ax1, ax2, as); }
    auto tuple_for_serialization() noexcept { return std::tie(a3, a, ax1, ax2, as); }  //swap first two fields!
} am;

uf::any aam(am);
BENCHMARK_CAPTURE(BM_ser, ser_am, am);
BENCHMARK_CAPTURE(BM_get, dese_am, aam, am);
BENCHMARK_CAPTURE(BM_scn, scan_am, aam.type(), aam.value());
BENCHMARK_CAPTURE(BM_scn, scan_am_err, aam.type(), aam.value().substr(1));
//deserialize a matching value into an expected
BENCHMARK_CAPTURE(BM_get, conv_am, aam, am);
BENCHMARK_CAPTURE(BM_cnv, conv_am, aam, am);
BENCHMARK_CAPTURE(BM_cto, conv_am, aam, am);

//One more level
struct AMM {
    AM a;
    AM a3;
    std::vector<AM> ax1;
    std::map<char, AM> ax2;
    std::optional<AM> as;
    auto tuple_for_serialization() const noexcept { return std::tie(a, a3, ax1, ax2, as); }
    auto tuple_for_serialization() noexcept { return std::tie(a3, a, ax1, ax2, as); }  //swap first two fields!
} amm;

uf::any aamm(amm);
BENCHMARK_CAPTURE(BM_ser, ser_amm, amm);
BENCHMARK_CAPTURE(BM_get, dese_amm, aamm, amm);
BENCHMARK_CAPTURE(BM_scn, scan_amm, aamm.type(), aamm.value());
BENCHMARK_CAPTURE(BM_scn, scan_amm_err, aamm.type(), aamm.value().substr(1));
//deserialize a matching value into an expected
BENCHMARK_CAPTURE(BM_get, conv_amm, aamm, amm);
BENCHMARK_CAPTURE(BM_cnv, conv_amm, aamm, amm);
BENCHMARK_CAPTURE(BM_cto, conv_amm, aamm, amm);


// Register the function as a benchmark
// Run the benchmark
BENCHMARK_MAIN();