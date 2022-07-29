#include "ufser.h"

uint32_t uf::impl::default_value(const char *&type, const char *const tend, char **to) {
    if (type > tend) throw internal_typestring_error{type};
    if (type == tend) return 0;
    switch (*type++) {
    default: throw internal_typestring_error{type};
    case 'd':
    case 'I':
    case 'a': return fill_bytes(8, to);
    case 'm':
        if (auto [len, problem] = parse_type(type, tend, false); !problem) type += len;
        else throw internal_typestring_error{type + len};
        [[fallthrough]];
    case 'l':
        if (auto [len, problem] = parse_type(type, tend, false); !problem) type += len;
        else throw internal_typestring_error{type + len};
        [[fallthrough]];
    case 'i':
    case 's': return fill_bytes(4, to);
    case 'o':
        if (auto [len, problem] = parse_type(type, tend, false); !problem) type += len;
        else throw internal_typestring_error{type + len};
        [[fallthrough]];
    case 'c':
    case 'b': return fill_bytes(1, to);
    case 'X': return fill_bytes(1, to, 1); //default for expected is to have a value
    case 'e': return fill_bytes(20, to); //3 stirngs of 4 bytes and one any of 8 bytes. All zeros
    case 'x':
        if (type == tend) throw internal_typestring_error{type};
        fill_bytes(1, to, 1);
        return 1 + default_value(type, tend, to);
    case 't':
        uint32_t size = 0, ret = 0;
        const char *const orig = type;
        while (type < tend && '0' <= *type && *type <= '9')
            size = size * 10 + *type++ - '0';
        if (size < 2)
            throw internal_typestring_error{orig};
        while (size--)
            ret += default_value(type, tend, to);
        return ret;
    }
}

uf::any_view &uf::any_view::assign(from_raw_t, std::string_view v, bool check) {
    const char *p = v.data(), *const end = p + v.length();
    std::string_view ty, va;
    if (impl::deserialize_from<true>(p, end, ty) || impl::deserialize_from<true>(p, end, va))
        throw uf::value_mismatch_error("Raw string does not contain a valid serialized uf::any.", "a", 0);
    if (p!=end)
        throw uf::value_mismatch_error("Raw string does contains extra characters after a serialized uf::any.", "a", 0);
    if (check) {
        auto [err, tlen, vlen] = impl::serialize_scan_by_type(ty, va, false, true);
        if (err) 
            err->append_msg(" (in any_view(from_raw,...))").throw_me();
        if (tlen<ty.length())
            throw uf::typestring_error(impl::ser_error_str(impl::ser::tlong), ty, tlen);
        if (vlen<va.length())
            throw uf::value_mismatch_error(impl::ser_error_str(impl::ser::vlong), ty, 0);
    }
    _type = ty;
    _value = va;
    return *this;
}

uf::any &uf::any::assign(from_raw_t, std::string_view v, bool check) {
    const char *p = v.data(), *const end = p + v.length();
    any tmp;
    if (impl::deserialize_from<false>(p, end, tmp))
        throw uf::value_mismatch_error("Raw string does not contain a valid serialized uf::any.", "a", 0);
    if (p!=end)
        throw uf::value_mismatch_error("Raw string does contains extra characters after a serialized uf::any.", "a", 0);
    if (check) {
        auto [err, tlen, vlen] = impl::serialize_scan_by_type(tmp.type(), tmp.value(), false, true);
        if (err) 
            err->append_msg(" (in any_view(from_raw,...))").throw_me();
        if (tlen<tmp.type().length())
            throw uf::typestring_error(impl::ser_error_str(impl::ser::tlong), tmp.type(), tlen);
        if (vlen<tmp.value().length())
            throw uf::value_mismatch_error(impl::ser_error_str(impl::ser::vlong), tmp.type(), 0);
    }
    return *this = std::move(tmp);
}

namespace uf::impl {
/** Parse the type from 'type' till 'target_tend'. Do not accept void.
 * In the error we return we assume this is the target type.*/
inline std::pair<std::unique_ptr<value_error>, size_t> parse_target_type_or_error(const char *type, const deserialize_convert_params &p) {
    assert(p.target_tstart<=type && type<=p.target_tend); //type is inside the source type
    if (type >= p.target_tend)
        return {create_des_typestring_target(p, ser_error_str(ser::end)), 0};
    else if (auto [tlen, problem] = parse_type(type, p.target_tend, false); !problem)
        return {std::unique_ptr<value_error>{}, tlen};
    else {
        deserialize_convert_params local_p(p);
        local_p.target_type = type+tlen;
        return {create_des_typestring_target(local_p, ser_error_str(problem)), 0};
    }
}
} //ns uf::impl

template <bool has_source, bool has_target>
std::unique_ptr<uf::value_error> uf::impl::cant_convert(deserialize_convert_params& p, StringViewAccumulator* target)
{
    if constexpr (!has_source) static_assert(!has_target, "cant_convert<false, true>() is invalid");
    assert(has_target == bool(target));
    //fast path
    const auto [source_tlen, source_tproblen] = parse_type(p.type, p.tend, true);
    if (source_tproblen!=uf::impl::ser::ok) { 
        p.type += source_tlen; 
        return create_des_typestring_source(p, ser_error_str(source_tproblen)); 
    }
    //do not handle incoming void here
    if (source_tlen && p.target_type+ source_tlen <= p.target_tend && 0==memcmp(p.target_type, p.type, source_tlen)) {
        if constexpr (has_source) if (auto err = advance_source(p, target, p.type)) return err;
        p.target_type += source_tlen;
        p.type += source_tlen;
        return {};
    }
    //OK, we are not completely identical, try conversion
    if (p.type == p.tend) {//void incoming
        if (p.target_type == p.target_tend)
            return {}; //void->void
        if (*p.target_type == 'a') {
            //void->a
            if (!(p.convpolicy & uf::allow_converting_any))
                return create_des_type_error(p, allow_converting_any);
            p.target_type++;
            if constexpr (has_target)
                *target << std::string(8, char(0)); //any containing void is two four byte zero lengths
            return {}; //
        }
        if (p.target_type + 1 < p.target_tend && p.target_type[0] == 'X') {
            //void->X
            if (!(p.convpolicy & uf::allow_converting_expected))
                return create_des_type_error(p, allow_converting_expected);
            p.target_type++;
            if constexpr (has_target)
                *target << char(1); //X containing void is just a one-byte 'has value' indicator
            return {};
        }
        if (p.target_type[0] == 'o') {
            //void->oT
            auto [err, target_tlen] = parse_target_type_or_error(p.target_type, p);
            if (err) return std::move(err);
            if (!(p.convpolicy & uf::allow_converting_aux))
                return create_des_type_error(p, allow_converting_aux);
            p.target_type += target_tlen;
            if constexpr (has_target)
                *target << char(0); //oT not containing any value is just a one-byte 'has no value' indicator
            return {};
        }
        //No type other than (void, 'a', 'X', 'oT') can accept void
        return create_des_type_error(p);
    }
    const char c = p.target_type < p.target_tend ? *p.target_type : 0;
    if (c == 'a') {
        //An any can take anything
        if (*p.type != 'a' && !(p.convpolicy & allow_converting_any))
            return create_des_type_error(p, allow_converting_any);
        if constexpr (has_source) {
            if (has_target && *p.type!='a') {
                //We actually convert and there is a need for conversion as the target is not an any
                //Parse upcoming type/value
                std::string_view t(p.type, p.tend - p.type);
                const char* old_p = p.p;
                if (auto err = serialize_scan_by_type_from(t, p.p, p.end, false)) return err;
                //Encapsulate what comes here in an any
                //serialize len of type
                char buff[4]; char* pp = buff;
                serialize_to(uint32_t(t.data() - p.type), pp);
                //append len and type to target
                *target << std::string(buff, 4) << std::string_view{ p.type, size_t(t.data() - p.type) };
                //serialize len of value
                pp = buff;
                serialize_to(uint32_t(p.p-old_p), pp);
                *target << std::string(buff, 4) << std::string_view{ old_p, size_t(p.p-old_p) };
                p.type = t.data();
            } else if (auto err = advance_source(p, target)) //just checking types: skip over, a->a: just copy
                return err; 
        } else {
            auto [err, len] = parse_type_or_error(p.type, p);
            if (err) return std::move(err);
            p.type += len;
        }
        p.target_type++;
        return {};
    }
    if (c == 'x' || c == 'X') {
        if (*p.type == 'e') {
            //e->xT or e->X
            if constexpr (has_source) {
                if constexpr (has_target) *target << char(0); //prefix error with an expected indicator of no value
                if (auto err = advance_source(p, target)) return err; //step through e's content and type
            } else {
                p.type++; //step through e's type
            }
            //step through the xT type
            auto [err, len] = parse_target_type_or_error(p.target_type, p);
            if (err) return std::move(err);
            p.target_type += len;
            return {};
        }
        if (*p.type == 'x' || *p.type == 'X') {//one expected can only be deserialized into a compatible expected
            if (*p.type == 'X' && c == 'X') {
                //X->X
                p.type++;
                p.target_type++; 
                if constexpr (has_source) if (auto err = advance_source(p, target, p.type-1)) return err;
                return {};
            } else if (*p.type == 'x' && c == 'X') {
                //xT->X, check if T can disappear
                p.type++;
                if (p.type>=p.tend)
                    return create_des_typestring_source(p, ser_error_str(ser::end));
                if constexpr (has_source) {
                    if (!*p.p) {
                        //No value, copy error
                        p.type--;
                        if (auto err = advance_source(p, target)) return err; //source type pointer advances xT*, target remains *X
                    } else {
                        //Check that value of 'xT' can be converted to void
                        deserialize_convert_params local_p(p, p.tend, p.target_type); //target type will be empty
                        if (auto err = cant_convert<has_source, false>(local_p, target)) return err;
                        p.type = local_p.type;
                    }
                } else {
                    //Check that value of 'xT' can be converted to void
                    deserialize_convert_params local_p(p, p.tend, p.target_type); //target type will be empty
                    if (auto err = cant_convert<has_source, false>(local_p, target)) return err;
                    p.type = local_p.type;
                }
                p.target_type++; //step over 'X' in target type
                return {};
            } else if (*p.type == 'X' && c == 'x') {
                //X->xT, check if T can initialize from void
                p.target_type++;
                if (p.target_type >= p.target_tend)
                    return create_des_typestring_target(p, ser_error_str(ser::end));
                if constexpr (has_source) {
                    if (!*p.p) {
                        //No value, copy error
                        if (auto err = advance_source(p, target, p.type - 1)) return err; //type pointers remain at *X and x*T
                        if (auto [err, len] = parse_target_type_or_error(p.target_type, p); err)
                            return std::move(err);
                        else
                            p.target_type += len;
                    } else {
                        //Check that value of 'xT' can be converted to void
                        deserialize_convert_params local_p(p, p.type, p.target_tend); //source type will be empty
                        if (auto err = cant_convert<has_source, has_target>(local_p, target)) return err;
                        p.target_type = local_p.target_type;
                    }
                } else {
                    deserialize_convert_params local_p(p, p.type, p.target_tend); //source type will be empty
                    if (auto err = cant_convert<has_source, has_target>(local_p, target)) return err;
                    p.target_type = local_p.target_type;
                }
                p.type++; //step over 'X' in source type
                return {};
            }
            //xU->xT 
            p.type++;
            p.target_type++; //step over 'x' in target type
            if constexpr (has_source) {
                if (!*p.p) {
                    //No value. Just copy the error over and check their type for compatibility
                    if (auto err = advance_source(p, target, p.type-1)) return err; //type pointers remain at x*U and x*T
                    return cant_convert<false, false>(p, nullptr); //compare just the types - the value (error) will be possible to convert
                }
                //source xT has value: copy the indicator to target
                if constexpr (has_target) *target << std::string_view{ p.p, 1 };
                p.p++;
            }
            //fallthrough to U->T
        } else {
            //This is the U->xT or U->X case, where U is neither 'e', 'X', nor 'x'
            if (!(p.convpolicy & allow_converting_expected)) //xU->non-expected (non-error)
                return create_des_type_error(p, allow_converting_expected);
            //Push a "has value" indicator
            if constexpr (has_source) if constexpr (has_target) *target << char(1);
            p.target_type++; //step over 'x' in target type
        }
        //Try U->T
        return cant_convert<has_source, has_target>(p, target);
    }
    if (c == 'o' && *p.type != 'o') {
        //T->oU: test T->U
        //Push a "has value" indicator
        if constexpr (has_source) if constexpr (has_target) *target << char(1);
        p.target_type++; //step over 'o' in target type
        return cant_convert<has_source, has_target>(p, target);
    }
    switch (*p.type) {
    default:
        return create_des_typestring_source(p, ser_error_str(ser::chr));
    case 'b':        
        if (c == 'b') {
            if constexpr (has_source) if (auto err = advance_source(p, target, p.type)) return err;
            goto step1_1_return_true;
        }
        if (c == 'c' || c == 'i' || c == 'I') {
            if constexpr (has_source) {
                const char* old_p = p.p;
                if (auto err = advance_source(p, nullptr, p.type)) return err;
                if constexpr (has_target) {
                    char buff[8]; char* pp = buff;
                    switch (c) {
                    case 'c': *target << char(*old_p ? 1 : 0); break;
                    case 'i': serialize_to(uint32_t(*old_p ? 1 : 0), pp);  *target << std::string(buff, 4); break;
                    case 'I': serialize_to(uint64_t(*old_p ? 1 : 0), pp);  *target << std::string(buff, 8); break;
                    default: assert(0);
                    }
                }
                (void)old_p;
            }
            if (p.convpolicy & allow_converting_bool) goto step1_1_return_true;
            else return create_des_type_error(p, allow_converting_bool);
        }
        return create_des_type_error(p);
    case 'c':        
        if (c == 'c') {
            if constexpr (has_source) if (auto err = advance_source(p, target, p.type)) return err;
            goto step1_1_return_true;
        }
        if (c == 'b' || c == 'i' || c == 'I') {
            if constexpr (has_source) {
                const char* old_p = p.p;
                if (auto err = advance_source(p, nullptr, p.type)) return err;
                if constexpr (has_target) {
                    char buff[8]; char* pp = buff;
                    switch (c) {
                    case 'b': *target << char(*old_p ? 1 : 0); break;
                    case 'i': serialize_to(uint32_t(*old_p), pp);  *target << std::string(buff, 4); break;
                    case 'I': serialize_to(uint64_t(*old_p), pp);  *target << std::string(buff, 8); break;
                    default: assert(0);
                    }
                }
                (void)old_p;
            }
            const serpolicy policy = c == 'b' ? allow_converting_bool : allow_converting_ints;
            if (p.convpolicy & policy) goto step1_1_return_true;
            else return create_des_type_error(p, policy);            
        }
        return create_des_type_error(p);
    case 'i':
        if (c == 'i') {
            if constexpr (has_source) if (auto err = advance_source(p, target, p.type)) return err;
            goto step1_1_return_true;
        }
        if (c == 'b' || c == 'c' || c == 'I' || c == 'd') {
            if constexpr (has_source) {
                const char* old_p = p.p;
                if (auto err = advance_source(p, nullptr, p.type)) return err;
                if constexpr (has_target) {
                    int32_t val; 
                    if (deserialize_from<false>(old_p, old_p + 4, val))
                        return create_des_value_error(p);
                    char buff[8]; char* pp = buff;
                    switch (c) {
                    case 'b': *target << char(val ? 1 : 0); break;
                    case 'c': *target << char(val); break;
                    case 'I': serialize_to(int64_t(val), pp); *target << std::string(buff, 8); break;
                    case 'd': serialize_to(double(val), pp); *target << std::string(buff, 8); break;
                    default: assert(0);
                    }
                }
                (void)old_p;
            }
            const serpolicy policy = c == 'b' ? allow_converting_bool : c=='c' ? allow_converting_ints_narrowing : c=='I' ? allow_converting_ints : allow_converting_double;
            if ((p.convpolicy & policy)==policy) goto step1_1_return_true;
            else return create_des_type_error(p, policy);
        }
        return create_des_type_error(p);
    case 'I':
        if (c == 'I') {
            if constexpr (has_source) if (auto err = advance_source(p, target, p.type)) return err;
            goto step1_1_return_true;
        }
        if (c == 'b' || c == 'c' || c == 'i' || c == 'd') {
            if constexpr (has_source) {
                const char* old_p = p.p;
                if (auto err = advance_source(p, nullptr, p.type)) return err;
                if constexpr (has_target) {
                    int64_t val; 
                    if (deserialize_from<false>(old_p, old_p + 8, val))
                        return create_des_value_error(p);
                    char buff[8]; char* pp = buff;
                    switch (c) {
                    case 'b': *target << char(val ? 1 : 0); break;
                    case 'c': *target << char(val); break;
                    case 'i': serialize_to(int32_t(val), pp); *target << std::string(buff, 4); break;
                    case 'd': serialize_to(double(val), pp); *target << std::string(buff, 8); break;
                    default: assert(0);
                    }
                }
                (void)old_p;
            }
            const serpolicy policy = c == 'b' ? allow_converting_bool : c == 'd' ? allow_converting_double : allow_converting_ints_narrowing;
            if ((p.convpolicy & policy)==policy) goto step1_1_return_true;
            else return create_des_type_error(p, policy);
        }
        return create_des_type_error(p);
    case 'd':
        if (c == 'd') {
            if constexpr (has_source) if (auto err = advance_source(p, target, p.type)) return err;
            goto step1_1_return_true;
        }
        if (c == 'i' || c == 'I') {
            if constexpr (has_source) {
                const char* old_p = p.p;
                if (auto err = advance_source(p, nullptr, p.type)) return err;
                if constexpr (has_target) {
                    double val; 
                    if (deserialize_from<false>(old_p, old_p + 8, val))
                        return create_des_value_error(p);
                    char buff[8]; char* pp = buff;
                    switch (c) {
                    case 'i': serialize_to(int32_t(val), pp); *target << std::string(buff, 4); break;
                    case 'I': serialize_to(int64_t(val), pp); *target << std::string(buff, 8); break;
                    default: assert(0);
                    }
                }
                (void)old_p;
            }
            const serpolicy policy = allow_converting_double;
            if (p.convpolicy & policy) goto step1_1_return_true;
            else return create_des_type_error(p, policy);
        }
        return create_des_type_error(p);
    case 's':
        if constexpr (has_source) if (auto err = advance_source(p, target, p.type)) return err;
        if (c == 's') goto step1_1_return_true;
        if (c == 'l' && p.target_type + 1 < p.target_tend && p.target_type[1] == 'c') {
            if (!(p.convpolicy & allow_converting_aux))
                return create_des_type_error(p, allow_converting_aux);
            p.target_type++;
            goto step1_1_return_true;
        }
        return create_des_type_error(p);
    case 'e':        
        if (c == 'e') {
            if constexpr (has_source) if (auto err = advance_source(p, target, p.type)) return err;
            goto step1_1_return_true;
        }
        //Note e->xT is handled above
        return create_des_type_error(p);
    case 'a':
        //This is a->T
        if (!(p.convpolicy & allow_converting_any))
            return create_des_type_error(p, allow_converting_any);
        if constexpr (has_source) {
            any_view a;
            if (deserialize_from<true>(p.p, p.end, a))
                return create_des_value_error(p);
            deserialize_convert_params local_p(a.value().data(), a.value().data() + a.value().length(),
                                               a.type().data(), a.type().data(), a.type().data() + a.type().length(),
                                               p.target_tstart, p.target_type, p.target_tend,
                                               p.convpolicy, &p, p.errors, p.error_pos);
            if (auto err = cant_convert<has_source, has_target>(local_p, target)) return err;
            else if (local_p.type < local_p.tend) return create_des_typestring_source(local_p, impl::ser_error_str(impl::ser::tlong));
            p.type++;
            p.target_type = local_p.target_type;
            //p.p has already been advaced by deserialize_from
            return {};
        } else {
            p.type++;
            if (p.target_type == p.target_tend) return {}; //We accept 'a'->void if no source is present
            if (auto [err, len] = parse_target_type_or_error(p.target_type, p); err) //this call returns error on void
                return std::move(err);
            else
                p.target_type += len;
            return {};
        }
    case 'x':
    case 'X':
        //This is xT->U, where U is not an expected, we have already checked that
        if (p.target_type<p.target_tend && *p.target_type=='e') {
            //xT->e or X->e
            if (!(p.convpolicy & allow_converting_expected))
                return create_des_type_error(p, allow_converting_expected);
            //if we have source, check if we have an error
            if constexpr (has_source) {
                bool has_value;
                if (deserialize_from<false>(p.p, p.end, has_value))
                    return create_des_value_error(p);
                if (has_value)
                    return create_error_for_des(std::make_unique<uf::type_mismatch_error>("Cannot convert a ready expected to an error <%1> to <%2>", std::string_view{}, std::string_view{}), &p);
                if (auto err = advance_source(p, target, "e")) return err; //step through the error in 'xT'
            } else {
                auto [err, len] = parse_type_or_error(p.type, p); 
                if (err) return std::move(err);
                p.type += len;
            }
            p.target_type++; //step through the 'e'
            return {}; 
        }
        //xU->T or X->T (T may be void, but not 'e', 'a' or 'x')
        if (!(p.convpolicy & allow_converting_expected))
            return create_des_type_error(p, allow_converting_expected);
        if constexpr (has_source) {
            bool has;
            if (deserialize_from<false>(p.p, p.end, has))
                return create_des_value_error(p);
            if (!has) {
                //We are an error. check if we types alone are compatible (for better errors)
                const size_t orig_pos = p.type - p.tstart;
                const size_t orig_tpos = p.target_type - p.target_tstart;
                p.type++;
                //if we are an X, we can disappear (X->void, dont consume any char from the target, which is not X here)
                //else compare U->T
                if (*(p.type-1) == 'x')
                    if (auto err = cant_convert<false, false>(p, nullptr)) 
                        return err;
                //OK, this expected could have been converted to the target, but it is an error instead
                if (!p.errors)
                    return create_error_for_des(std::make_unique<uf::type_mismatch_error>("uf::expected contains error <%1> when converting to <%2>", std::string_view{}, std::string_view{}), &p);
                //eat and collect the serialized error
                if (deserialize_from<false>(p.p, p.end, p.errors->emplace_back()))
                    return create_des_value_error(p);
                if (p.error_pos)
                    p.error_pos->emplace_back(orig_pos, orig_tpos);
                return {}; //xT(e)->T conversion is OK, but we record the error in p.errors
            }
            //if xU contains a value, continue with trying to match the value of expected to the target type
        }
        p.type++;
        //if we are an X, we can disappear (X->void, dont consume any char from the target, which is not X here)
        if (*(p.type-1) == 'X') 
            return {};
        //check U->T (correct even with has_source)
        return cant_convert<has_source, has_target>(p, target);
    case 'o':
        //oT->U or oT->oU
        if constexpr (has_source) {
            //if we have source, check if we have a value
            bool has_value;
            if (deserialize_from<false>(p.p, p.end, has_value))
                return create_des_value_error(p);
            if (!has_value) {
                //empty optional can only convert to an other optional
                if (c == 'o') {
                    p.type++;
                    p.target_type++; //step over o:s
                    return cant_convert<false, false>(p, nullptr); //compare just the types
                }
                return create_error_for_des(std::make_unique<uf::type_mismatch_error>("Empty optional <%1> can only convert to an optional and not <%2>", std::string_view{}, std::string_view{}), &p);
            }
            //else fallthrough
        }
        p.type++;
        //T->U or T->oU
        return cant_convert<has_source, has_target>(p, target);
    case 'l':
        if (p.type+1<p.tend && p.type[1] == 'c' && c == 's' &&
            (p.convpolicy & allow_converting_aux)) {
            //lc->s special case
            if constexpr (has_source) if (auto err = advance_source(p, target, p.type)) return err;  //leave p.type unchanged, but copy value as is if there is a target
            p.type++;
            goto step1_1_return_true;
        } else if (c == 't') {
            if (!(p.convpolicy & allow_converting_tuple_list))
                return create_des_type_error(p, allow_converting_tuple_list);
            //lX->tXYZ special case
            p.type++;
            p.target_type++;
            uint32_t size = 0;
            while (p.target_type<p.target_tend && '0' <= *p.target_type && *p.target_type <= '9') {
                size = size * 10 + *p.target_type - '0';
                p.target_type++;
            }
            if (size<2) return create_des_typestring_target(p, ser_error_str(ser::num));
            if constexpr (has_source) {
                uint32_t size2;
                if (deserialize_from<false>(p.p, p.end, size2))
                    return create_des_value_error(p);
                if (size!=size2)
                    return create_error_for_des(std::make_unique<uf::value_mismatch_error>(uf::concat("Size mismatch when converting <%1> to <%2> (", size2, "!=", size, ").")), &p);
            }
            const char* const save_type = p.type; //the location of the list type : "l*<type>"
            while (size--) {
                p.type = save_type;
                if (auto err = cant_convert<has_source, has_target>(p, target)) 
                    return err;
            }
            return {};
        } else { //use else to create new scope to be able to declare local variables.
            //Create a limited local_p: target set to void if it is not a list
            deserialize_convert_params local_p(p, p.tend, (c != 'l') ? p.target_type : p.target_tend);
            local_p.type++;
            if (local_p.target_type < local_p.target_tend)
                local_p.target_type++;
            if constexpr (has_source) {
                //check every element. Here we know that the two list
                //are not of exactly the same type (checked at the very beginning)
                //Also target list may be void
                if constexpr (has_target) *target << std::string_view(local_p.p, 4);
                uint32_t size;
                if (deserialize_from<false>(local_p.p, local_p.end, size))
                    return create_des_value_error(local_p);
                if (size == 0) {
                    auto err = cant_convert<false, false>(local_p, nullptr); //compare just the types (target may be void)
                    p.p = local_p.p;
                    p.type = local_p.type;
                    p.target_type = local_p.target_type;
                    return err;
                }
                const char *original_type = local_p.type;
                const char *original_target_type = local_p.target_type;
                while (size--) {
                    local_p.type = original_type;
                    local_p.target_type = original_target_type;
                    if (auto err = cant_convert<has_source, has_target>(local_p, target)) {
                        p.p = local_p.p;
                        p.type = local_p.type;
                        p.target_type = local_p.target_type;
                        return err;
                    }
                }
                p.p = local_p.p;
                p.type = local_p.type;
                p.target_type = local_p.target_type;
                return {};
            } else {
                auto err = cant_convert<false, false>(local_p, nullptr); //compare just the types (target may be void)
                p.p = local_p.p;
                p.type = local_p.type;
                p.target_type = local_p.target_type;
                return err;
            }
        }
    case 'm':
        if (c == 'm') {
            //We know that maps cannot disappear (cannot be all-X types) as
            //key must be non-X. But they can degenerate to a list if mapped type is all-X.
            if constexpr (has_source) {
                //go through the map and try one-by-one
                //check every element. Here we know that the two maps
                //are not of exactly the same type (checked at the very beginning)
                if constexpr (has_target) *target << std::string_view(p.p, 4);
                uint32_t size;
                if (deserialize_from<false>(p.p, p.end, size))
                    return create_des_value_error(p);
                if (size == 0)
                    return cant_convert<false, false>(p, nullptr); //compare just the type of the two maps
                deserialize_convert_params local_p(&p);
                while (size--) {
                    local_p.type = p.type+1;
                    local_p.target_type = p.target_type + 1;
                    if (auto err = cant_convert<has_source, has_target>(local_p, target))
                        return err;
                    if (local_p.target_type == p.target_type + 1)
                        return create_error_for_des(std::make_unique<uf::type_mismatch_error>("Strange target type <%1> -> <%2>", std::string_view{}, std::string_view{}), &local_p);
                    const char *beginning_of_mapped_type = local_p.target_type;
                    if (auto err = cant_convert<has_source, has_target>(local_p, target))
                        return err;
                    if (local_p.target_type == beginning_of_mapped_type)
                        return create_error_for_des(std::make_unique<uf::type_mismatch_error>("Strange target type <%1> -> <%2>", std::string_view{}, std::string_view{}), &local_p);
                }
                p.p = local_p.p;
                p.type = local_p.type;
                p.target_type = local_p.target_type;
                return {};
            } else {
                deserialize_convert_params local_p(&p);
                local_p.type++;
                local_p.target_type++;
                if (auto err = cant_convert<false, false>(local_p, nullptr))
                    return err;
                if (local_p.target_type == p.target_type + 1)
                    return create_error_for_des(std::make_unique<uf::type_mismatch_error>("Strange target type <%1> -> <%2>", std::string_view{}, std::string_view{}), &local_p);
                const char *beginning_of_mapped_type = local_p.target_type;
                if (auto err = cant_convert<false, false>(local_p, nullptr))
                    return err;
                if (local_p.target_type == beginning_of_mapped_type)
                    return create_error_for_des(std::make_unique<uf::type_mismatch_error>("Strange target type <%1> -> <%2>", std::string_view{}, std::string_view{}), &local_p);
                p.type = local_p.type;
                p.target_type = local_p.target_type;
                return {};
            }
        }
        if (c == 'l') {
            //We can only serialize into a list if one of our key/mapped type is
            //all-X and the other deserializes into the type of the list.
            //This is tested by trimming target type to the type of the list and
            //attempting to deserialize the two types to the list
            auto [err, ttlen] = parse_target_type_or_error(p.target_type, p);
            if (err) return std::move(err);
            deserialize_convert_params local_p(p, p.tend, p.target_type+ttlen); //trim target type after list
            if constexpr (has_source) {
                //go through the list and try one-by-one
                //check every element. Here we know that the two list
                //are not of exactly the same type (checked at the very beginning)
                if constexpr (has_target) *target << std::string_view(p.p, 4);
                uint32_t size;
                if (deserialize_from<false>(p.p, p.end, size))
                    return create_des_value_error(p);
                if (size == 0)
                    return cant_convert<false, false>(p, nullptr); //compare just the type of the two maps
                local_p.p = p.p;
                while (size--) {
                    local_p.type = p.type + 1;
                    local_p.target_type = p.target_type + 1;
                    if (auto err = cant_convert<has_source, has_target>(local_p, target))
                        return err;
                    if (auto err = cant_convert<has_source, has_target>(local_p, target)) //will fail if cannot be deserialized into void
                        return err;
                }
                //if OK fallthrough
            } else {
                local_p.type++;
                local_p.target_type++;
                if (auto err = cant_convert<has_source, has_target>(local_p, target))
                    return err;
                if (auto err = cant_convert<has_source, has_target>(local_p, target)) //will fail if cannot be deserialized into void
                    return err;
                //if OK fallthrough
            }
            p.p = local_p.p;
            p.type = local_p.type;
            p.target_type = local_p.target_type;
            return {};
        }
        //We cannot deserialize into any other type
        return create_des_type_error(p);
    case 't':
        //OK, try to match our fields to that of the target
        //This includes the case when we have a single non-X element that
        //deserializes into a non-tuple type and the all-X case that serializes
        //into void or into an X or to some tuple requesting X
        //Start by trimming the target type to that of the upcoming type. (accept void)
        auto [ttlen, tproblem] = parse_type(p.target_type, p.target_tend, true);
        if (tproblem!=ser::ok) {
            p.target_type += ttlen;
            return create_des_typestring_target(p, ser_error_str(tproblem));
        }
        deserialize_convert_params local_p(p, p.tend, p.target_type+ttlen);
        StringViewAccumulator local_target;
        //First jump across our element number
        local_p.type++;
        int size1 = 0;
        while (local_p.type<local_p.tend && '0' <= *local_p.type && *local_p.type <= '9') {
            size1 = size1 * 10 + *local_p.type - '0';
            local_p.type++;
        }
        if (size1<2) return create_des_typestring_source(p, ser_error_str(ser::num));
        if (c == 't') {
            //If the other type is a tuple, jump over the element num
            local_p.target_type++;
            int size2 = 0;
            while (local_p.target_type < local_p.target_tend &&
                   '0' <= *local_p.target_type && *local_p.target_type <= '9') {
                size2 = size2 * 10 + *local_p.type - '0';
                local_p.target_type++;
            }
            if (size2<2) return create_des_typestring_target(p, ser_error_str(ser::num));
        }
        std::unique_ptr<value_error> ret;
        if (c == 'l' && (p.convpolicy & allow_converting_tuple_list)) {
            const char* const save_target_type = p.target_type+1; //jump over the 'l' in the target type
            p.type = local_p.type; //jump over the tuple number in the source type
            if constexpr (has_target) {
                char buff[4]; char* pp = buff;
                serialize_to(uint32_t(size1), pp);
                *target << std::string(buff, 4);
            }
            while (size1--) {
                p.target_type = save_target_type;
                ret = cant_convert<has_source, has_target>(p, target);
                if (ret) break;
            }
            if (!ret) return {};
            //else we try converting void-like members away - but in case of failure of that we emit the error generated here
            //as the user is likely to expect this error.
        } //else if the target is a list, but allow_converting_tuple_list is not set, we try 'converting away' some void-like members below.
        //Try deserializing our elements into t2.
        //Here we may need to do a backtracking thingy.
        //Consider, for example t2ai being converted to i. Conversion is possible if 'a' holds a void.
        //However, it will be consumed to 'i' and we will fail to convert 'i' to void.
        //So we need to backtrack and try what if 'a' would disappear.
        //For this we identify backtracking points, where
        //A type could disappear, but did not and come back to this event on failure.
        //We store the backtracking points in a stack (vector).
        struct backtrack_point
        {
            const char *p;
            const char *type;
            const char *target_type;
            size_t error_list_size;
            int remaining_size;
            uf::impl::StringViewAccumulator target;
        };
        std::vector<backtrack_point> stack;
        bool updated_error = false;
        //We need to do backtracking even if we have serialized source available,
        //as ambiguity remains with 'xa' types that carry an error. Specifically,
        //they can disappear if they carried void, thus t2xai -> i should succeed
        //(with the collected error from xa), since it could have succeeeded if
        //'a' carried void and if the user is counting on that we want to
        //give the right kind of error (unplaceable errors as opposed to type mismatch)
        while (true) {    
            bool success = true;
            while (success && size1--) {
                const char * const original_target_type = local_p.target_type;
                const bool can_dis = can_disappear(std::string_view(local_p.type, local_p.tend - local_p.type)).has_value();
                if (auto err = cant_convert<has_source, has_target>(local_p, has_target ? &local_target : nullptr)) {
                    if (!ret)
                        ret = std::move(err); //store the first error we see - the most natural
                    success = false; //will break
                } else//It is not a backtracking point only if the type could disappear but it didnt.
                    if (can_dis && original_target_type != local_p.target_type)
                        //Save stack state, size1 is already decremented
                        stack.push_back(backtrack_point{ local_p.p, local_p.type, original_target_type,
                                                        local_p.errors ? local_p.errors->size() : 0, size1, local_target });
            }
            //now test that we have exhausted all of t2's elements
            if (success && local_p.target_type == local_p.target_tend)
                break; //success
            //failure
            if (stack.size() && ret && !updated_error) {
                //For errors inside tuples, when we did backtracking,
                //Indicate that we would fail anyway.
                //(below we place the error at the front of the tuple, no msg change needed)
                ret->append_msg(" (With any incoming value.)");
                updated_error = true;
            }
            if (!ret) //generate error if not yet
                ret = create_des_type_error(p); //in the error mark the tuples as the culprit - use original p.
            //try to backtrack if we have no source.
            if (stack.size() == 0)
                return ret;
            //restore to the branching point (local_p.p can be ignored, we have no source here)
            local_p.p = stack.back().p;
            local_p.type = stack.back().type;
            local_p.target_type = stack.back().target_type;
            if (local_p.errors)
                local_p.errors->resize(stack.back().error_list_size);
            if (local_p.error_pos)
                local_p.error_pos->resize(stack.back().error_list_size);
            size1 = stack.back().remaining_size;
            local_target = std::move(stack.back().target);
            stack.pop_back();
        }
        //success
        p.p = local_p.p;
        p.type = local_p.type;
        p.target_type = local_p.target_type;
        if constexpr (has_target) *target << std::move(local_target);
        return {};
    }

step1_1_return_true:
    p.type++;
    p.target_type++;
    return {};
}

template std::unique_ptr<uf::value_error>
uf::impl::cant_convert<false, false>(deserialize_convert_params &p, StringViewAccumulator *target);
template std::unique_ptr<uf::value_error>
uf::impl::cant_convert<true, false>(deserialize_convert_params &p, StringViewAccumulator *target);
template std::unique_ptr<uf::value_error>
uf::impl::cant_convert<true, true>(deserialize_convert_params &p, StringViewAccumulator *target);
//cant_convert<false,true> is invalid


namespace uf::impl
{
/** Parses string and checks if it is a valid type description. Type can be fragmented in multiple chunks.
 * @param [in] type The typestring to check. We actually remove all scanned bytes from it.
 * @param [in] accept_void If true, we return a valid 0 on an empty 'type'
 * @param [in] more A function to add more bytes to the typestring. Called when 'type' has
 *                  been parsed to its end. Shall be f(std::string_view&)->void, when
 *                  called should set the view to the new typestring chunk. Set to empty type
 *                  to signal that no more bytes. The last (partial) chunk is returned in 'type'.
 * @returns On success the parsed type and false, whereas on
 *          error we return the partially parsed type until the error and true.*/
std::pair<std::string, ser> parse_type_chunks(std::string_view &type, bool accept_void,
                                              const std::function<void(std::string_view &)> &more) {
    if (type.empty() && more) more(type);
    if (type.empty()) return {{}, accept_void ? ser::ok : ser::end};
    const char c = type.front();
    if (c=='s' || c=='c' || c=='b' || c=='i' || c=='I' || c=='X' ||c=='d' || c=='a' || c=='e') {
        type.remove_prefix(1);
        return {{c}, ser::ok};
    }
    if (c == 'l' || c=='x' || c=='o') {
        type.remove_prefix(1);
        auto ret = parse_type_chunks(type, false, more);
        ret.first.insert(ret.first.begin(), c);
        return ret;
    }
    if (c=='m') {
        type.remove_prefix(1);
        auto ret = parse_type_chunks(type, false, more);
        ret.first.insert(ret.first.begin(), c);
        if (ret.second!=ser::ok) return ret;
        auto ret2 = parse_type_chunks(type, false, more);
        ret2.first.insert(0, ret.first);
        return ret2;
    }
    if (c=='t') {
        type.remove_prefix(1);
        if (type.empty() && more) more(type);
        std::string ret = "t";
        size_t size = 0;
        while (type.size() && '0'<=type.front() && type.front() <='9') {
            size = size*10 + type.front() - '0';
            ret.push_back(type.front());
            type.remove_prefix(1);
            if (type.empty() && more) more(type);
        }
        if (size < 2) return {{}, ser::num};
        while (size--)
            if (auto [ty, problem] = parse_type_chunks(type, false, more); problem==ser::ok)
                ret.append(std::move(ty));
            else
                return {std::move(ret), problem};
        return {std::move(ret), ser::ok};
    }
    return {{}, ser::chr};
}
} //ns


std::unique_ptr<uf::value_error> 
uf::impl::serialize_scan_by_type_from(std::string_view &type, const char*&p, const char *&end,
                                      const std::function<void(std::string_view &)> &more_type,
                                      const std::function<void(const char *&, const char *&)> &more_val,
                                      bool check_recursively) {
    if (type.empty() && more_type) more_type(type);
    if (type.empty()) return {};
    if (p == end && more_val) more_val(p, end);
    switch (type.front()) {
    case 'c':
    case 'b':
        ++p;
        if (p>end) goto value_mismatch;
        type.remove_prefix(1);
        break;
    case 'i':
        p += 4;
        if (p>end) goto value_mismatch;
        type.remove_prefix(1);
        break;
    case 'I':
    case 'd':
        p += 8;
        if (p>end) goto value_mismatch;
        type.remove_prefix(1);
        break;
    case 's':
    {
        uint32_t size;
        if (deserialize_from<false>(p, end, size)) goto value_mismatch;
        while (size) {
            if (p == end && more_val) more_val(p, end);
            if (p == end) goto value_mismatch;
            const uint32_t len = std::min<uint32_t>(size, end - p);
            p += len;
            size -= len;
        }
        type.remove_prefix(1);
        break;
    }
    case 'a':
        if (check_recursively) {
            if (p == end && more_val) more_val(p, end);
            if (p == end) goto value_mismatch;
            uint32_t tsize;
            if (deserialize_from<false>(p, end, tsize)) goto value_mismatch;
            std::string inner_type;
            inner_type.reserve(tsize);
            while (tsize) {
                if (p == end && more_val) more_val(p, end);
                if (p == end) goto value_mismatch;
                const uint32_t len = std::min<uint32_t>(tsize, end - p);
                inner_type.append(p, len);
                p += len;
                tsize -= len;
            }
            if (p == end && more_val) more_val(p, end);
            if (p == end) goto value_mismatch;
            uint32_t vsize;
            if (deserialize_from<false>(p, end, vsize)) goto value_mismatch;
            if (p == end && more_val) more_val(p, end);
            std::string_view inner_ty = inner_type;
            const char *const old_p = p;
            const bool all_in_this_chunk = end-p>=vsize;
            type.remove_prefix(1);
            if (all_in_this_chunk) {
                const char *inner_end = p+vsize;
                if (auto err = serialize_scan_by_type_from(inner_ty, p, inner_end, {}, {}, true)) {
                    err->encaps(inner_type, inner_ty, type);
                    return err;
                }
            } else if (auto err = serialize_scan_by_type_from(inner_ty, p, end, {}, more_val, true)) {
                err->encaps(inner_type, inner_ty, type);
                return err;
            }
            //If we have chunking, we have no way of checking if the amount read from 'value' is 'vsize'
            //(We could copy the value first and then scan it, but that is deemed too expensive.)
            if (inner_ty.size()) 
                return std::make_unique<uf::value_mismatch_error>(impl::ser_error_str(impl::ser::tlong),
                                                uf::concat('(', inner_type, ')', type),
                                                1+inner_type.length()-inner_ty.length());
            if (all_in_this_chunk && p < old_p+vsize)
                return std::make_unique<uf::value_mismatch_error>(impl::ser_error_str(impl::ser::vlong), type, 0);
        } else {
            for (int i = 0; i < 2; i++) {
                if (p == end && more_val) more_val(p, end);
                uint32_t size;
                if (deserialize_from<false>(p, end, size)) goto value_mismatch;
                while (size) {
                    if (p == end && more_val) more_val(p, end);
                    if (p == end) goto value_mismatch;
                    const uint32_t len = std::min<uint32_t>(size, end - p);
                    p += len;
                    size -= len;
                }
            }
            type.remove_prefix(1);
        }
        break;
    case 'x':
    case 'X':
    {
        const bool is_void = type.front() == 'X';
        bool has_value;
        if (deserialize_from<false>(p, end, has_value)) goto value_mismatch;
        type.remove_prefix(1);
        if (is_void) {
            if (has_value) break; //we have a void nothing to do
            //fallthrough for scanning error
        } else {
            if (has_value) {
                if (type.empty() && more_type) more_type(type);
                if (type.empty())
                    return std::make_unique<typestring_error>(ser_error_str(ser::end), type, 0);
                if (auto err = serialize_scan_by_type_from(type, p, end, more_type, more_val, check_recursively))
                    return err;
                break;
            } //else fallthrough for scanning error
        }
        //type now is just after the 'x' or 'X' - so that any error from scanning the 'e' is marked before
        std::string_view e = "e";
        if (auto err = serialize_scan_by_type_from(e, p, end, more_type, more_val, check_recursively)) {
            err->encaps("e", "e", type);
            return err;
        }
        if (!is_void)
            if (auto [ty, problem] = parse_type_chunks(type, false, more_type); problem!=ser::ok)
                return std::make_unique<typestring_error>(ser_error_str(problem), type, 0);
        break;
    }
    case 'o':
    {
        bool has_value;
        if (deserialize_from<false>(p, end, has_value)) goto value_mismatch;
        type.remove_prefix(1);
        if (type.empty() && more_type) more_type(type);
        if (type.empty())
            return std::make_unique<typestring_error>(ser_error_str(ser::end), type, 0);
        if (has_value) {
            if (auto err = serialize_scan_by_type_from(type, p, end, more_type, more_val, check_recursively))
                return err;
            break;
        } else if (auto [ty, problem] = parse_type_chunks(type, false, more_type); problem!=ser::ok)
            return std::make_unique<typestring_error>(ser_error_str(problem), uf::concat(ty, type), ty.length());
        break;
    }
    case 'l':
    {
        uint32_t size;
        if (deserialize_from<false>(p, end, size)) goto value_mismatch;
        type.remove_prefix(1);
        if (type.empty() && more_type) more_type(type);
        if (type.empty())
            return std::make_unique<typestring_error>(ser_error_str(ser::end), type, 0);
        const std::string_view original_type = type;
        auto [member_type, problem] = parse_type_chunks(type, false, more_type);
        if (problem!=ser::ok)
            return std::make_unique<typestring_error>(ser_error_str(problem), type, 0);
        const bool one_chunk = member_type.length()<=original_type.length();
        while (size--) {
            //Make tmp point to the same memory as 'original_type' if the latter contained the whole member type.
            std::string_view tmp(one_chunk ? original_type.substr(0, member_type.length()) : member_type);
            if (auto err = serialize_scan_by_type_from(tmp, p, end, [](std::string_view &) {}, more_val, check_recursively)) {
                if (one_chunk) //if type is in one chunk originally, report correct pos back.
                    type = {tmp.data(), size_t(original_type.length() + original_type.data() - tmp.data())}; 
                return err;
            }
        }
        break;
    }
    case 'm':
    {
        uint32_t size;
        if (deserialize_from<false>(p, end, size)) goto value_mismatch;
        type.remove_prefix(1);
        if (type.empty() && more_type) more_type(type);
        if (type.empty())
            return std::make_unique<typestring_error>(ser_error_str(ser::end), type, 0);
        const std::string_view original_type = type;
        auto [ktype, kproblem] = parse_type_chunks(type, false, more_type); //key type of the list
        if (kproblem!=ser::ok) 
            return std::make_unique<typestring_error>(ser_error_str(kproblem), type, 0);
        auto [mtype, mproblem] = parse_type_chunks(type, false, more_type); //mapped type of the list
        const bool one_chunk = ktype.length()+mtype.length()<=original_type.length();
        if (mproblem!=ser::ok) 
            return std::make_unique<typestring_error>(ser_error_str(mproblem), type, 0);
        while (size--) {
            //Make tmp point to the same memory as 'original_type' if the latter contained the whole member type.
            std::string_view ktmp(one_chunk ? original_type.substr(0, ktype.length()) : ktype);
            if (auto err = serialize_scan_by_type_from(ktmp, p, end, [](std::string_view &) {}, more_val, check_recursively)) {
                if (one_chunk) //if type is in one chunk originally, report correct pos back.
                    type = {ktmp.data(), size_t(original_type.length() + original_type.data() - ktmp.data())};
                return err;
            }
            std::string_view mtmp(one_chunk ? original_type.substr(ktype.length(), mtype.length()) : mtype);
            if (auto err = serialize_scan_by_type_from(mtmp, p, end, [](std::string_view &) {}, more_val, check_recursively)) {
                if (one_chunk) //if type is in one chunk originally, report correct pos back.
                    type = {mtmp.data(), size_t(original_type.length() + original_type.data() - mtmp.data())};
                return err;
            }
        }
        break;
    }
    case 't':
    {
        type.remove_prefix(1);
        if (type.empty() && more_type) more_type(type);
        uint32_t size = 0;
        while (type.length() && '0'<=type.front() && type.front()<='9') {
            size = size*10 + type.front() - '0';
            type.remove_prefix(1);
            if (type.empty() && more_type) more_type(type);
        }
        if (size<2)
            return std::make_unique<typestring_error>(ser_error_str(ser::num), type, 0);
        while (size--)
            if (type.empty())
                return std::make_unique<typestring_error>(ser_error_str(ser::end), type, 0);
            else if (auto err = serialize_scan_by_type_from(type, p, end, more_type, more_val, check_recursively))
                return err;
        break;
    }
    case 'e':
    {
        //'e' is in reality three strings and an any.
        std::string_view inner_type = serialize_type<decltype(std::declval<error_value>().tuple_for_serialization())>();
        type.remove_prefix(1);
        if (type.empty() && more_type) more_type(type);
        if (auto err = serialize_scan_by_type_from(inner_type, p, end, [](std::string_view &) {}, more_val, check_recursively))
            return err;
        break;
    }
    default:
        std::string tt;
        impl::print_escaped_to(tt, 0, type, {}, '%');
        return std::make_unique<typestring_error>(ser_error_str(ser::chr), tt, 0);
    }
    if (p<=end)
        return {};
value_mismatch:
    return std::make_unique<uf::value_mismatch_error>(uf::concat(ser_error_str(ser::val), " (scan) <%1>."), type, 0);
}



std::optional<std::unique_ptr<uf::value_error>>
uf::impl::serialize_print_by_type_to(std::string &to, bool json_like, unsigned max_len, std::string_view &type,
                                     const char *&p, const char *end, std::string_view chars, char escape_char,
                                     std::function<bool(std::string &, unsigned, std::string_view &,
                                                        const char *&, const char *, std::string_view, char)> expected_handler) {
    if (type.length() == 0) {
        if (json_like) to.append("null");
        return {};
    }
    switch (type.front()) {
    case 'c':
    {
        char c;
        if (deserialize_from<false>(p, end, c)) goto value_mismatch;
        to.push_back(json_like ? '\"' : '\'');
        print_escaped_to(to, max_len, std::string_view(&c, 1), chars, escape_char); //handle too long at end.
        to.push_back(json_like ? '\"' : '\'');
        type.remove_prefix(1);
        break;
    }
    case 'b':
    {
        bool b;
        if (deserialize_from<false>(p, end, b)) goto value_mismatch;
        to.append(b ? "true" : "false");
        type.remove_prefix(1);
        break;
    }
    case 'i':
    {
        int32_t i;
        if (deserialize_from<false>(p, end, i)) goto value_mismatch;
        to.append(std::to_string(i));
        type.remove_prefix(1);
        break;
    }
    case 'I':
    {
        int64_t i;
        if (deserialize_from<false>(p, end, i)) goto value_mismatch;
        to.append(std::to_string(i));
        type.remove_prefix(1);
        break;
    }
    case 'd':
    {
        double d;
        if (deserialize_from<false>(p, end, d)) goto value_mismatch;
        to.append(uf::print_floating_point(d, json_like));
        if (json_like && to.back()=='.') to.pop_back(); //Print integers as integer for JSON
        type.remove_prefix(1);
        break;
    }
    case 's':
    {
        uint32_t size;
        if (deserialize_from<false>(p, end, size)) goto value_mismatch;
        if (p+size>end) goto value_mismatch;
        type.remove_prefix(1);
        if (serialize_print_append(to, json_like, max_len, print_escaped_json_string(std::string_view(p, size)), chars, escape_char))
            return std::unique_ptr<value_error>{}; //non-empty optional containing an empty ptr: too long
        p += size;
        break;
    }
    case 'a':
    {
        any_view a;
        if (deserialize_from<true>(p, end, a)) goto value_mismatch;
        type.remove_prefix(1);
        std::string_view ty;
        auto ret = a.print_to(to, ty, max_len, chars, escape_char, json_like); 
        if (ret &&*ret) 
            (*ret)->encaps(a.type(), ty, type); //encaps the typestring reported on error
        return ret;
    }
    case 'e':
    {
        error_value err;
        if (deserialize_from<false>(p, end, err)) goto value_mismatch;
        type.remove_prefix(1);
        if (p>end) goto value_mismatch;
        serialize_print_append(to, json_like, max_len, err, chars, escape_char); //handle too long at end.
        break;
    }
    case 'x':
    case 'X':
    {
        if (expected_handler(to, max_len, type, p, end, chars, escape_char))
            break;
        const bool is_void = type.front() == 'X';
        bool has_value;
        if (deserialize_from<false>(p, end, has_value)) goto value_mismatch;
        type.remove_prefix(1);
        if (type.empty() && !is_void)
            return std::make_unique<typestring_error>(uf::concat(ser_error_str(ser::end), " <%1>"), type, 0);
        if (has_value) {
            if (!is_void)
                if (auto ret = serialize_print_by_type_to(to, json_like, max_len, type, p, end, chars, escape_char, expected_handler))
                    return ret;
        } else {
            if (!is_void) {
                if (auto [len, problem] = parse_type(type, false); !problem)
                    type.remove_prefix(len); //skip the type of the expected
                else
                    return std::make_unique<typestring_error>(uf::concat(ser_error_str(problem), " <%1>"), type, len);
            }
            std::string_view e("e");
            if (auto ret = serialize_print_by_type_to(to, json_like, max_len, e, p, end, chars, escape_char, expected_handler))
                return ret;
        }
        break;
    }
    case 'o':
    {
        bool has_value;
        if (deserialize_from<false>(p, end, has_value)) goto value_mismatch;
        type.remove_prefix(1);
        if (type.empty())
            return std::make_unique<typestring_error>(uf::concat(ser_error_str(ser::end), " <%1>"), type, 0);
        if (has_value) {
            if (auto ret = serialize_print_by_type_to(to, json_like, max_len, type, p, end, chars, escape_char, expected_handler))
                return ret;
        } else {
            if (auto [len, problem] = parse_type(type, false); !problem)
                type.remove_prefix(len); //skip the type of the optional
            else
                return std::make_unique<typestring_error>(uf::concat(ser_error_str(problem), " <%1>"), type, len);
            if (json_like)
                to.append("null");
        }
        break;
    }
    case 'l':
    {
        uint32_t size;
        if (deserialize_from<false>(p, end, size)) goto value_mismatch;
        type.remove_prefix(1);
        if (type.empty())
            return std::make_unique<typestring_error>(uf::concat(ser_error_str(ser::end), " <%1>"), type, 0);
        if (size==0) {
            to.append("[]");
            if (auto [len, problem] = parse_type(type, false); !problem)
                type.remove_prefix(len); //skip the type of the list
            else
                return std::make_unique<typestring_error>(uf::concat(ser_error_str(problem), " <%1>"), type, len);
        } else {
            to.push_back('[');
            while (size-->1) {
                std::string_view tmp = type;
                if (auto ret = serialize_print_by_type_to(to, json_like, max_len, tmp, p, end, chars, escape_char, expected_handler)) {
                    type = tmp; //for correct prefixing of the type in the error
                    return ret;
                }
                to.push_back(',');
            }
            if (auto ret = serialize_print_by_type_to(to, json_like, max_len, type, p, end, chars, escape_char, expected_handler))
                return ret;
            to.push_back(']');
        }
        break;
    }
    case 'm':
    {
        uint32_t size;
        if (deserialize_from<false>(p, end, size)) goto value_mismatch;
        type.remove_prefix(1);
        if (type.empty())
            return std::make_unique<typestring_error>(ser_error_str(ser::end), type, 0);
        auto [len, problem] = parse_type(type, false); //key type of the list
        if (problem!=ser::ok)
            return std::make_unique<typestring_error>(ser_error_str(problem), type, len);
        std::string_view ktype = type;
        type.remove_prefix(len);
        std::tie(len, problem) = parse_type(type, false); //mapped type of the list
        if (problem!=ser::ok)
            return std::make_unique<typestring_error>(ser_error_str(problem), type, len);
        std::string_view mtype = type;
        type.remove_prefix(len);
        to.push_back('{');
        while (size--) {
            std::string_view ktmp(ktype);
            if (auto ret = serialize_print_by_type_to(to, json_like, max_len, ktmp, p, end, chars, escape_char, expected_handler)) {
                type = ktmp;
                return ret;
            }
            to.push_back(':');
            std::string_view mtmp(mtype);
            if (auto ret = serialize_print_by_type_to(to, json_like, max_len, mtmp, p, end, chars, escape_char, expected_handler)) {
                type = mtmp;
                return ret;
            }
            if (size)
                to.push_back(',');
        }
        to.push_back('}');
        break;
    }
    case 't':
    {
        type.remove_prefix(1);
        uint32_t size = 0;
        while (type.length() && '0'<=type.front() && type.front()<='9') {
            size = size*10 + type.front() - '0';
            type.remove_prefix(1);
        }
        if (size<2)
            return std::make_unique<typestring_error>(ser_error_str(ser::num), type, 0);
        to.push_back(json_like ? '[' : '(');
        while (size--) {
            if (type.empty())
                return std::make_unique<typestring_error>(ser_error_str(ser::end), type, 0);
            if (auto ret = serialize_print_by_type_to(to, json_like, max_len, type, p, end, chars, escape_char, expected_handler))
                return ret;
            if (size)
                to.push_back(',');
        }
        to.push_back(json_like ? ']' : ')');
        break;
    }
    default:
        std::string tt;
        impl::print_escaped_to(tt, 0, type, {}, '%');
        return std::make_unique<typestring_error>(ser_error_str(ser::chr), tt, 0);
    }
    if (max_len && to.length()>max_len) 
        return std::unique_ptr<value_error>{}; //non-empty optional containing an empty ptr: too long
    return {};
value_mismatch:
    return std::make_unique<uf::value_mismatch_error>(uf::concat(ser_error_str(ser::val), " (print) <%1>."), type, 0);
}


std::pair<std::string, bool> uf::impl::parse_value(std::string &to, std::string_view &value, TextParseMode mode)
{
    skip_whitespace(value);
    if (value.length() == 0) return {{}, false};
    if (value.front() == '\'') {
        if (value.length()<3) return {"Strange character literal.", true};
        if (value[2]=='\'') {
            to.push_back(value[1]);
            value.remove_prefix(3);
        } else if (value[1]=='%' && value.length()>=5 && value[4]=='\'' &&
                   hex_digit(value[2])>=0 && hex_digit(value[3])>=0) {
            to.push_back((unsigned char)(hex_digit(value[2])*16 + hex_digit(value[3])));
            value.remove_prefix(5);
        } else
            return {"Strange character literal.", true};
        if (mode != TextParseMode::JSON) return { "c", false };
        //JSON: instead of the character have a single byte string
        char c = to.back();
        to.back() = 0;
        to.push_back(0);
        to.push_back(0);
        to.push_back(1);
        to.push_back(c);
        return {"s", false};
    }
    if (value.front() == '\"') {
        size_t pos = value.find_first_of('\"', 1);
        if (pos==std::string::npos) return {"Missing terminating quotation mark.", true};
        to.append(4, char(0));
        value.remove_prefix(1); //skip \"
        size_t len = to.size();
        parse_escaped_string_to(to, value.substr(0, pos-1));
        len = to.size() - len;
        char *p = &to.front()+to.length()-len-4; //p denotes where the length will go
        serialize_to(uint32_t(len), p);
        value.remove_prefix(pos);
        return {"s", false};
    }
    //detect numbers. It may be 'NaN', 'Nan(122abc_)', 'inf', 'infinity' case insensitive
    //note that an integer continuing with an 'e' is not always a float: 1ea is parsed as '1' by strtod().
    const char *d_end = value.data();
    const double d = std::strtod(value.data(), &const_cast<char *&>(d_end));
    if (errno==ERANGE) {
        errno = 0;
        return {"Number out-of range for double.", true};
    }
    if (d_end!=value.data()) { //OK this is a number - check if integer
        if (mode != TextParseMode::JSON) {
            const bool sign = ('-'==value.front()); //handle sign to be able to cover uint64_t
            //allow hex base with 0x prefix, but not octal with plain 0 prefix. That would confuse people.
            const int base = value.length()>2u+sign && value[sign] == '0' && value[sign+1] == 'x' ? 16 : 10;
            const char* start = value.data()+sign, * i_end = start;
            const uint64_t i = std::strtoull(start, &const_cast<char*&>(i_end), base);
            if (i_end == d_end) { //same length if interpreted as int or float => this is an int.
                if (sign) {
                    if (errno==ERANGE || i>=(uint64_t(1)<<63)) {
                        errno = 0;
                        return { "Integer out-of range for int64.", true };
                    }
                } else if (errno==ERANGE) {
                    errno = 0;
                    return { "Integer out-of range for uint64.", true };
                }
                //an integer
                value.remove_prefix(i_end-value.data());
                //unsigned integers larger than 0x7fffffff will end up 64-bit integers 'I'
                //This is because we convert between integers as signed and parsing 
                //<I>4'000'000'000 would end up negative otherwise.
                if (i<=0x7fffffffu) {
                    to.append(4, char(0));
                    char* p = &to.front()+to.length()-4;
                    if (sign)
                        serialize_to(-int32_t(i), p);
                    else
                        serialize_to(uint32_t(i), p);
                    return { "i", false };
                } else {
                    to.append(8, char(0));
                    char* p = &to.front()+to.length()-8;
                    if (sign)
                        serialize_to(-int64_t(i), p);
                    else
                        serialize_to(i, p);
                    return { "I", false };
                }
            } //else fallthrough to double
        } //else force double
        //Serialize as double.
        value.remove_prefix(d_end-value.data());
        to.append(8, char(0));
        char* p = &to.front()+to.length()-8;
        serialize_to(d, p);
        return { "d", false };
    }
    if (value.front()=='[') {
        value.remove_prefix(1);
        skip_whitespace(value);
        const size_t orig_len = to.size();
        const std::string_view orig_value = value;
        uint32_t size = 0;
        to.append(4, 0);
        std::string type;
        for (const bool convert_to_any : {false, true}) {
            if (mode==TextParseMode::JSON && !convert_to_any) continue;
            bool mismatchin_mapped_types = false;
            while (value.length() && value.front()!=']') {
                const size_t size_before = to.size();
                auto [t, v] = parse_value(to, value, mode);
                if (v) return {std::move(t), v};
                if (convert_to_any) {
                    //insert tlen, typestring, vlen before the value
                    const uint32_t vlen = to.size()-size_before;
                    to.insert(size_before, 4+4+t.length(), char(0));
                    char *p = to.data() + size_before;
                    serialize_to(t, p);
                    serialize_to((uint32_t)vlen, p);
                } else if (type.length()==0) type = std::move(t);
                else if (type!=t) {
                    if (mode==TextParseMode::Normal)
                        return {uf::concat("Mismatching types in list: <", type, "> and <", t, ">."), true};
                    mismatchin_mapped_types = true;
                    to.resize(orig_len+4);
                    value = orig_value;
                    type = "a";
                    size = 0;
                    break;
                }
                if (value.length()==0) return {"Missing closing ']'.", true};
                size++;
                skip_whitespace(value);
                if (value.front()==']') break;
                if (value.front()!=';' && value.front()!=',') return {"List items must be separated by ';' or ','.", true};
                value.remove_prefix(1);
                skip_whitespace(value);
            }
            if (!mismatchin_mapped_types) break; //success
        }
        if (value.length()==0) return {"Missing closing ']'.", true};
        value.remove_prefix(1); //the ]
        char *p = to.data()+orig_len;
        serialize_to(size, p);
        if (type.length())
            return {"l"+type, false};
        else
            return {"la", false}; //could not determine element type
    }
    if (value.front()=='{') {
        value.remove_prefix(1);
        skip_whitespace(value);
        const size_t orig_len = to.size();
        const std::string_view orig_value = value;
        uint32_t size = 0;
        to.append(4, 0);
        std::string key_type;
        std::string mapped_type;
        if (mode==TextParseMode::JSON) mapped_type = "a";
        for (const bool convert_to_any : {false, true}) {
            if (mode==TextParseMode::JSON && !convert_to_any) continue;
            bool mismatchin_mapped_types = false;
            while (value.length() && value.front()!='}') {
                auto [t, v] = parse_value(to, value, mode);
                if (v) return {std::move(t), v};
                if (key_type.length()==0) key_type = std::move(t);
                else if (key_type!=t) return {uf::concat("Mismatching key types : <", key_type, "> and <", t, ">."), true};
                skip_whitespace(value);
                if (value.length() == 0) return {"Missing mapped value and closing '}'.", true};
                if (value.front()!=':' && value.front()!='=') return {"Keys and values must be separated by ':' or '='.", true};
                value.remove_prefix(1);
                skip_whitespace(value);
                const size_t size_before = to.size();
                std::tie(t, v) = parse_value(to, value, mode);
                if (v) return {std::move(t), v};
                if (convert_to_any) {
                    //insert tlen, typestring, vlen before the value
                    const uint32_t vlen = to.size()-size_before;
                    to.insert(size_before, 4+4+t.length(), char(0));
                    char *p = to.data() + size_before;
                    serialize_to(t, p);
                    serialize_to((uint32_t)vlen, p);
                    assert(p == to.data() + size_before + 4+4+t.length());
                } else if (mapped_type.length()==0) mapped_type = std::move(t);
                else if (mapped_type!=t) {
                    if (mode==TextParseMode::Normal)
                        return {uf::concat("Mismatching mapped types: <", key_type, "> and <", t, ">."), true};
                    mismatchin_mapped_types = true;
                    to.resize(orig_len+4);
                    value = orig_value;
                    mapped_type = "a";
                    size = 0;
                    break;
                }
                if (value.length()==0) return {"Missing closing '}'.", true};
                size++;
                skip_whitespace(value);
                if (value.front()=='}') break;
                if (value.front()!=';' && value.front()!=',') return  {"Map items must be separated by ';' or ','.", true};
                value.remove_prefix(1);
                skip_whitespace(value);
            }
            if (!mismatchin_mapped_types) break; //success
        }
        if (value.length()==0) return {"Missing closing '}'.", true};
        value.remove_prefix(1); //the }
        char *p = to.data()+orig_len;
        serialize_to(size, p);
        if (key_type.length() && mapped_type.length())
            return {"m"+key_type+mapped_type, false};
        else
            return {"maa", false}; //could not determine map type
    }
    if (value.front()=='(') {
        value.remove_prefix(1);
        skip_whitespace(value);
        std::string type;
        unsigned num = 0;
        while (value.length() && value.front()!=')') {
            auto [t, v] = parse_value(to, value, mode);
            if (v) return {std::move(t), v};
            type.append(std::move(t));
            num++;
            skip_whitespace(value);
            if (value.length()==0) return {"Missing closing ')'.", true};
            if (value.front()==')') break;
            if (value.front()!=';' && value.front()!=',') return {"Tuple items must be separated by ';' or ','", true};
            value.remove_prefix(1);
            skip_whitespace(value);
        }
        if (value.length()==0) return {"Missing closing ')'.", true};
        value.remove_prefix(1); //the )
        if (num<2) return {"Tuples need at least 2 elements.", true};
        return {uf::concat('t', num, type), false};
    }
    if (value.front()=='<') {
        std::string_view save = value;
        value.remove_prefix(1);
        skip_whitespace(value);
        if (value.length() == 0) {
            value = save;
            return {"Missing typestring or closing '>' after '<'.", true};
        }
        auto [pos, problem] = value.front()=='>' 
            ? std::pair{size_t(0), ser::ok} //"<>" is a valid type of zero len
            : parse_type(value, true);
        const std::string_view type1 = value.substr(0, pos);
        value.remove_prefix(pos);
        if (problem!=ser::ok) return {uf::concat(ser_error_str(problem), '.'), true};
        skip_whitespace(value);
        if (value.front() != '>') return {"Missing closing '>'.", true};
        value.remove_prefix(1);
        skip_whitespace(value);
        any a;
        if (value.length() && value.front()!=']' && value.front()!=')' && value.front() != '}' &&
            value.front()!=';' && value.front()!=',') {
            //it seems the user has specified a value after the type
            std::string raw;
            auto [type2, v] = parse_value(raw, value, mode);
            if (v) return {std::move(type2), v}; //error parsing
            if (type1.empty())
                a = any(from_type_value_unchecked, std::move(type2), std::move(raw));
            else
                try {
                auto raw2 = uf::convert(type2, type1, allow_converting_all, raw);
                a = any(from_type_value_unchecked, std::move(type1), std::move(raw2 ? *raw2 : raw)); //converted data may be the same as original (probably no conversion)
            } catch (const uf::value_mismatch_error &) {
                assert(0); //we should not return an incompatible type2,raw combo
            } catch (const uf::value_error &e) {
                return {e.what(), true};
            }
        } else if (type1.length())
            //user has specified a type but no value
            return {"There is a type, but a void value follows.", true};
        //OK, we are a void value
        const size_t len = serialize_len(a);
        to.append(len, 0);
        char *p = &to.back()-len+1;
        serialize_to(a, p);
        return {"a", false};
    }
    if (value.length()>=4 && tolower(value[0])=='t' && tolower(value[1])=='r' &&
        tolower(value[2])=='u' && tolower(value[3])=='e') {
        to.push_back(1);
        value.remove_prefix(4);
        return {"b", false};
    }
    if (value.length()>=5 && tolower(value[0])=='f' && tolower(value[1])=='a' &&
        tolower(value[2])=='l' && tolower(value[3])=='s' && tolower(value[4])=='e') {
        to.push_back(0);
        value.remove_prefix(5);
        return {"b", false};
    }
    if (value.starts_with("null")) {
        value.remove_prefix(4);
        return { "", false };
    }
    if (value.substr(0, 5)=="error") {
        value.remove_prefix(5);
        skip_whitespace(value);
        if (value.length() == 0 || value.front() != '(') return {"Missing 1-4 elements of error_value", true};
        auto [inner_type, v] = parse_value(to, value, TextParseMode::Liberal); //parse tuple (liberal: come back here for good error)
        if (v) return {std::move(inner_type), v};
        if (inner_type=="s") { //just the type
            std::string_view remaining = "(\"\", \"\", <>)"; //two additional empty strings and one any
            parse_value(to, remaining, TextParseMode::Normal);  //normal=perf opt
        } else if (inner_type == "t2ss") { //type and id
            std::string_view remaining = "(\"\", <>)"; //an additional empty string and any
            parse_value(to, remaining, TextParseMode::Normal);  //normal=perf opt
        } else if (inner_type == "t3ssa") { //type, id and message
            std::string_view remaining = "<>"; //an additional any
            parse_value(to, remaining, TextParseMode::Normal);  //normal=perf opt
        } else if (inner_type!="t4sssa") //also covers the case of error and inner_type being empty
            return {"Error must contain 's', 't2ss', 't3sss' or 't4sssa'.", true}; //error_value must be a t4sssa (or part of it)
        return {"e", false};
    }
    return {uf::concat("Did not recognize this: '", value.substr(0,7),
                        value.length()>7 ? "...'." : "'.",
                        isalpha(value.front()) ? " (Maybe missing '\"' for strings?)" : ""),
             true};
}


std::variant<uf::impl::parse_any_content_result, std::unique_ptr<uf::value_error>>
uf::impl::parse_any_content(std::string_view _type, std::string_view _value,
                            uint32_t max_no) {
    parse_any_content_result ret;
    if (_type.empty()) {
        ret.typechar = 0;
        return ret;
    }
    ret.typechar = _type.front();
    if (max_no==0) return ret;
    if (_type.front() == 'l' || _type.front() == 'm' || _type.front() == 'o' ||
        _type.front() == 'x' || _type.front() == 'X') {
        if (_type.front() == 'X') {
            //keep inner types empty this is a void
        } else if (auto [tl, problem] = impl::parse_type(_type.substr(1), false); problem!=ser::ok)
            return std::make_unique<uf::typestring_error>(uf::concat(ser_error_str(problem), " <%1>"), _type, tl + 1);
        else {
            ret.inner_type1 = {_type.data() + 1, tl};
            if (_type.front() == 'm') {
                if (auto [tl2, problem2] = impl::parse_type(_type.substr(1 + tl), false); !problem2)
                    ret.inner_type2 = {_type.data() + 1 + tl, tl2};
                else
                    return std::make_unique<uf::typestring_error>(uf::concat(ser_error_str(problem), " <%1>"), _type, tl + 1 + tl2);
            }
        }
    }
    if (_type.front() == 'l') {
        const char *p = _value.data(), *const end = p + _value.length();
        uint32_t size;
        if (impl::deserialize_from<false>(p, end, size)) goto value_mismatch;
        ret.size = _value.substr(0, 4);
        size = std::min(size, max_no);
        ret.elements.reserve(size);
        while (size--) {
            std::string_view t = _type.substr(1);
            const char *st = p;
            if (auto err = impl::serialize_scan_by_type_from(t, p, end, false)) {
                err->types[0].prepend('l');
                err->regenerate_what();
                return err;
            }
            ret.elements.emplace_back(_type.substr(1, _type.length() - t.length() - 1),
                                      std::string_view{st, size_t(p - st)});
        }
        return ret;
    }
    if (_type.front() == 'm') {
        const char *p = _value.data(), *const end = p + _value.length();
        uint32_t size;
        if (impl::deserialize_from<false>(p, end, size)) goto value_mismatch;
        ret.size = _value.substr(0, 4);
        size = std::min(size, max_no);
        ret.elements.reserve(size);
        while (size--) {
            std::string_view t = _type.substr(1);
            const char *st = p;
            if (auto err = impl::serialize_scan_by_type_from(t, p, end, false)) {
                err->types[0].prepend('m');
                err->regenerate_what();
                return err;
            }
            ret.elements.emplace_back(_type.substr(1, _type.length() - t.length() - 1),
                                        std::string_view{st, size_t(p - st)});
            std::string_view t2 = t;
            st = p;
            if (auto err = impl::serialize_scan_by_type_from(t, p, end, false)) {
                err->types[0].prepend('m');
                err->regenerate_what();
                return err;
            }
            ret.elements.emplace_back(t2.substr(0, t2.length() - t.length()),
                                        std::string_view{st, size_t(p - st)});
        }
        return ret;
    }
    if (_type.front() == 't') {
        std::string_view t = _type.substr(1);
        uint32_t size = 0;
        while (t.length() && '0' <= t.front() && t.front() <= '9') {
            size = size * 10 + (t.front() - '0');
            t.remove_prefix(1);
        }
        if (size<2)
            return std::make_unique<uf::typestring_error>(uf::concat(ser_error_str(ser::num), " <%1>"), _type, t.data()-_type.data());
        ret.size = _type.substr(1, t.data()-_type.data()-1);
        size = std::min(size, max_no);
        ret.elements.reserve(size);
        const char *p = _value.data(), *const end = p + _value.length();
        while (size--) {
            std::string_view t_st = t;
            const char *st = p;
            if (auto err = impl::serialize_scan_by_type_from(t, p, end, false)) {
                err->prepend_type0(_type, t);
                return err;
            }
            ret.elements.emplace_back(t_st.substr(0, t_st.length() - t.length()),
                                      std::string_view{st, size_t(p - st)});
        }
        return ret;
    }
    if (_type.front() == 'a') {
        const char *p = _value.data(), *const end = p + _value.length();
        any_view a;
        if (impl::deserialize_from<true>(p, end, a)) goto value_mismatch;
        ret.elements.emplace_back(a.type(), a.value(),
                                  _value.substr(0, 4), std::string_view{_value.data() + 4 + a.type().length(), 4});
        return ret;
    }
    if (_type.front() == 'e') {
        const char *p = _value.data(), *const end = p + _value.length();
        ret.elements.reserve(4);
        for (auto &sv :{"s","s","s","a"}) {
            std::string_view tsv = sv;
            const char *const tp = p;
            if (auto err = impl::serialize_scan_by_type_from(tsv, p, end, false)) {
                err->types[0].type = _type;
                err->types[0].pos = uint16_t(0);
                err->regenerate_what();
                return err;
            }
            ret.elements.emplace_back(sv, std::string_view{tp, size_t(p - tp)});
            if (--max_no == 0) break;
        }
        return ret;
    }
    if (_type.front() == 'x' || _type.front() == 'X') {
        ret.size = _value.substr(0, 1);
        if (!_value.front())
            ret.elements.emplace_back("e", _value.substr(1));
        else if (ret.typechar == 'X') {
            ret.elements.emplace_back(std::string_view{_value.data()+1, 0}, std::string_view{}); //add a void content
        } else {
        scan_x_o_content:
            std::string_view t = _type.substr(1);
            const char *p = _value.data(), *const end = p+_value.size();
            p++; //jump over byte
            if (auto err = impl::serialize_scan_by_type_from(t, p, end, false)) {
                err->prepend_type0(_type, t);
                return err;
            }
            ret.elements.emplace_back(t.substr(0, t.length() - _type.length() - 1),
                                      std::string_view{_value.data()+1, size_t(p - _value.data()-1)});
        }
        return ret;
    }
    if (_type.front() == 'o') {
        ret.size = _value.substr(0, 1);
        if (_value.front()) goto scan_x_o_content;
        return ret;
    }
    if (_type.front() == 'i' || _type.front() == 'I' || _type.front() == 'd' ||
        _type.front() == 'c' || _type.front() == 'b' || _type.front() == 's')
        return ret;
    return std::make_unique<uf::typestring_error>(uf::concat(ser_error_str(ser::chr), " <%1>"), _type, 0);
value_mismatch:
    return std::make_unique<uf::value_mismatch_error>(uf::concat(ser_error_str(ser::val), " (any_content) <%1>."), _type, 0);
}

namespace {

template <typename C, typename Func>
[[nodiscard]] auto comprehend(const C& container, Func transformer)
{
    auto begin = std::begin(container), end = std::end(container);
    std::vector<std::remove_cvref_t<decltype(transformer(*begin))>> ret;
    ret.reserve(std::distance(begin, end));
    while (begin != end) {
        ret.push_back(transformer(*begin));
        begin = std::next(begin);
    }
    return ret;
}

}

std::vector<uf::any_view> uf::any_view::get_content(uint32_t max_no) const {
    auto v = impl::parse_any_content(_type, _value, max_no);
    if (std::holds_alternative<std::unique_ptr<value_error>>(v)) {
        if (auto &err = std::get<std::unique_ptr<value_error>>(v)) 
            err->throw_me(); 
        return {};
    }
    auto &p = std::get<uf::impl::parse_any_content_result>(v).elements;
    if (p.empty()) return {};
    if (_type.empty() || _type.front() != 'm')
        return comprehend(p, [](auto &p) {return any_view(p.type, p.value); });
    //Handle maps - keep only the keys
    std::vector<any_view> ret;
    ret.reserve(p.size() / 2);
    for (auto i = p.begin(); i != p.end(); i += 2)
        ret.push_back(any_view(i->type, i->value));
    return ret;
}


std::vector<std::pair<uf::any_view, uf::any_view>> uf::any_view::get_map_content(uint32_t max_no) const {
    auto v = impl::parse_any_content(_type, _value, max_no);
    if (std::holds_alternative<std::unique_ptr<value_error>>(v)) {
        if (auto &err = std::get<std::unique_ptr<value_error>>(v))
            err->throw_me();
        return {};
    }
    auto &p = std::get<uf::impl::parse_any_content_result>(v).elements;
    if (p.empty()) return {};
    if (_type.front()=='m') {
        std::vector<std::pair<any_view, any_view>> ret;
        ret.reserve(p.size() / 2);
        for (auto i = p.begin(); i != p.end(); i += 2)
            ret.emplace_back(any_view(i->type, i->value), any_view(std::next(i)->type, std::next(i)->value));
        return ret;
    }
    //for other types just do get-content and add a void<> to each
    return comprehend(p, [](auto &pair) {return std::pair{any_view{ pair.type, pair.value }, any_view{}}; });
}

uint32_t uf::any_view::get_content_size() const noexcept {
    if (_type.empty()) return 0;
    switch (_type.front()) {
    case 'l':
    case 'm':
    {
        uint32_t size = 0;
        const char *p = _value.data(), *const end = p+4;
        (void)uf::impl::deserialize_from<false>(p, end, size); //if we fail, size will be 0, fine
        return size;
    }
    case 'o':
    case 'x':
    case 'X':
        if (_value.empty()) return 0;
        return _value.front() ? 1 : 0;
    default:
        return 0;
    case 't':
        std::string_view t = _type.substr(1);
        uint32_t size = 0;
        while (t.length() && '0'<= t.front() && t.front()<='9') {
            size = size * 10 + t.front() - '0';
            t.remove_prefix(1);
        }
        return size;
    }
}

std::optional<std::unique_ptr<uf::value_error>> 
uf::any_view::print_to(std::string &to, std::string_view &ty, unsigned max_len,
                       std::string_view chars, char escape_char, bool json_like) const {
    ty = _type;
    if (is_void()) {
        to.append(json_like ? "null" : "<>");
    } else {
        if (!json_like) {
            to.push_back('<');
            to.append(_type);
            if (max_len && to.length() > max_len)
                return std::unique_ptr<value_error>{}; //non-empty optional containing an empty ptr: too long
            to.push_back('>');
        }
        const char *p = _value.data();
        if (auto ret = impl::serialize_print_by_type_to(to, json_like, max_len, ty, p, _value.data()+_value.length(), chars, escape_char))
            return ret;
        if (ty.size())
            return std::make_unique<uf::typestring_error>(impl::ser_error_str(impl::ser::tlong), ty, 0);
    }
    return {};
}

uf::from_text_t uf::from_text;
uf::from_raw_t uf::from_raw;
uf::from_typestring_t uf::from_typestring;
uf::from_type_value_t uf::from_type_value;
uf::from_type_value_unchecked_t uf::from_type_value_unchecked;
uf::use_tags_t uf::use_tags;

void uf::expected_with_error::regenerate_what(std::string_view format) {
    value_error::regenerate_what(format);
    size_t pos = 0;
    while (std::string::npos != (pos = my_what.find("%e", pos))) {
        std::string errstr;
        for (const auto &e : errors) {
            if (&e != &errors.front()) errstr.push_back(';');
            errstr.append(e.what());
        }
        my_what.replace(pos, 2, errstr);
    }
}
