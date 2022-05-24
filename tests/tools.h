#pragma once

template <typename T>
constexpr inline bool is_native_string_v = std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string> ||
std::is_same_v<T, char> || std::is_same_v<T, const char *> || std::is_same_v<T, char *>;

template <typename T, typename = void>
struct has_to_string : std::false_type {};

template <typename T>
struct has_to_string<T, std::void_t<decltype(to_string(std::declval<T>()))>> : std::true_type {};

template<typename T>
decltype(auto) my_to_string(const T &s) {
    using std::to_string;
    if constexpr (std::is_same_v<T, bool>)
        return s ? "true" : "false"; 
    else if constexpr (is_native_string_v<T>)
        return s;
    else if constexpr (has_to_string<T>::value) //check to_string before pointer for PyObject* to work
        return (std::string)to_string(s);
    else if constexpr (std::is_pointer_v<T>) {
        std::stringstream ss;
        ss<<(void *)s;
        return ss.str();
    } else
        return (std::string)to_string(s); //generate error
}

/** Joins a string range into a string adding a separator string in-between
 * them, with the option to make the last separator different (to achieve lists,
 * like "a, b and c"). In case of 0 or 1 elements no separator is not used,
 * in case of 2 elements, only the 'bylast' separator is used.
 * 'first' and 'last' must be an iterator to strings or string_views.
 * Like the python join() method.*/
template <typename Iter> 
requires requires (std::string s, Iter i) { { s.append(*i) }; }
inline std::string ATTR_PURE__
Join(Iter first, Iter last, const std::string &by, const std::string &bylast)
{
    //We dont use a default parameter for bylast to mean "same as by" to allow
    //anything else to be the last parameter for other overloads
    std::string ret;
    if (first==last) return ret;
    const int size = std::distance(first, last);
    ret.reserve(accumulate(first, last,
                           std::max(0, size-2)*by.length() +
                           std::max(0, size-1)*bylast.length(),
                           [](size_t u, const auto &a) {return u+a.length(); })+1);
    ret.append(*first);
    for (first++; first != last; first++)
        ret.append(std::next(first)==last ? bylast : by).append(*first);
    return ret;
}

/** Joins a string range into a string adding a separator string in-between
 * them, with the option to make the last separator different (to achieve lists,
 * like "a, b and c"). In case of 0 or 1 elements no separator is not used,
 * in case of 2 elements, only the 'bylast' separator is used.
 * 'first' and 'last' must be an iterator to strings or string_views.
 * Like the python join() method.*/
template <typename Iter>
requires (!requires (std::string s, Iter i) { { s.append(*i) }; } && 
           requires (std::string s, Iter i) { { my_to_string(*i) }; })
inline std::string ATTR_PURE__
Join(Iter first, Iter last, const std::string &by, const std::string &bylast) {
    //We dont use a default parameter for bylast to mean "same as by" to allow
    //anything else to be the last parameter for other overloads
    return Join(first, last, by, bylast, 
                [](const auto &p) { return my_to_string(p); });
}
/** Joins a string range into a string adding a separator string in-between
 * them. In case of 0 or 1 elements no separator is not used.
 * 'first' and 'last' must be an iterator to strings or string_views.
 * Like the python join() method.*/
template <typename Iter>
inline std::string ATTR_PURE__ Join(Iter first, Iter last, const std::string &by)
{
    return Join(first, last, by, by);
}

/** Joins a string container into a string adding a separator string in-between
 * them, with the option to make the last separator different (to achieve lists,
 * like "a, b and c"). In case of 0 or 1 elements no separator is not used,
 * in case of 2 elements, only the 'bylast' separator is used.
 * 'elements' must be a container of strings or string_views.
 * Like the python join() method.*/
template <typename C>
inline std::string ATTR_PURE__ Join(const C &elements, const std::string &by, const std::string &bylast)
{
    return Join(begin(elements), end(elements), by, bylast);
}

/** Joins a string array into a string adding a separator string in-between
 * them. In case of 0 or 1 elements the separator is not used.
 * 'elements' must be a container of strings or string_views.
 * Like the python join() method.*/
template <typename C>
inline std::string ATTR_PURE__ Join(const C &elements, const std::string &by)
{
    return Join(begin(elements), end(elements), by, by);
}


/** Joins a string range into a string adding a separator string in-between
 * them, with the option to make the last separator different (to achieve lists,
 * like "a, b and c"). In case of 0 or 1 elements no separator is not used,
 * in case of 2 elements, only the 'bylast' separator is used.
 * Uses 'p' to translate from 'T::value_type' to string as in string P(const T::value_type&)
 * Like the python join() method.*/
template <typename Iter, typename P, typename = std::enable_if_t<std::is_invocable_v<P, std::add_const_t<typename std::iterator_traits<Iter>::value_type>>>>
inline std::string ATTR_PURE__ 
Join(Iter first, Iter last, const std::string &by, const std::string &bylast, P p)
{
    std::string ret;
    if (first == last) return ret;
    ret.append(p(*first));
    for (first++; first != last; first++)
        ret.append(std::next(first)==last ? bylast : by).append(p(*first));
    return ret;
}

/** Joins an string range into a string adding a separator string in-between
 * them. In case of 0 or 1 elements the separator is not used.
 * Uses 'p' to translate from 'T::value_type' to string as in string P(const T::value_type&)
 * Like the python join() method.*/
template <typename Iter, typename P, typename = std::enable_if_t<std::is_invocable_v<P, std::add_const_t<typename std::iterator_traits<Iter>::value_type>>>>
inline std::string ATTR_PURE__ Join(Iter first, Iter last, const std::string &by, P p)
{
    return Join(first, last, by, by, p);
}

/** Joins a string container into a string adding a separator string in-between
 * them, with the option to make the last separator different (to achieve lists,
 * like "a, b and c"). In case of 0 or 1 elements no separator is not used,
 * in case of 2 elements, only the 'bylast' separator is used.
 * Uses 'p' to translate from 'T::value_type' to string as in string P(const T::value_type&)
 * Like the python join() method.*/
template <typename T, typename P, typename = std::enable_if_t<std::is_invocable_v<P, std::add_const_t<typename T::value_type>>>>
inline std::string ATTR_PURE__ Join(const T&elements, const std::string &by,
                                              const std::string &bylast, P p)
{
    return Join(begin(elements), end(elements), by, bylast, p);
}

/** Joins an array into a string adding a separator string in-between
* them. In case of 0 or 1 elements the separator is not used.
* Uses 'p' to translate from 'T::value_type' to string as in string P(const T::value_type&)
* Like the python join() method.*/
template <typename T, typename P, typename = std::enable_if_t<std::is_invocable_v<P, std::add_const_t<typename T::value_type>>>>
inline std::string ATTR_PURE__ Join(const T&elements, const std::string &by, P p)
{
    return Join(begin(elements), end(elements), by, by, p);
}
