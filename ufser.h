#pragma once
/// @file ufser.h Serialization library.
#include <memory>
#include <chrono>
#include <array>
#include <tuple>
#include <functional>
#include <unordered_map>
#include <optional>
#include <variant>
#include <map>
#include <cstring>
#include <cassert>
#include <string>
#include <vector>
#include <variant>
#include <numeric>
#include <sstream>

#ifdef HAVE_BOOST_PFR
#include <boost/pfr.hpp>
#endif

#define ATTR_PURE__ __attribute__((pure)) 

/** @defgroup serialization Serialization library
* @brief Serialization without a schema language for C++ and Python
* Its primary goal is to ease the programmer's work and allow compatibility between
* these languages.

* It is not as complete as boost serialization, for example, only
* a subset of types can be serialized. All types required to be simple value types.
* Types that you want to put in containers must be default constructible (preferably cheaply).
*
* Basic Types
* ===========
*
* Each type can be represented with a "typestring", where each character represents
* a primitive or compound type. These also show what types can and cannot be serialized.
* Typestrings are prefix codes, that is, you can always tell where they end.
* We have these primitive types:
* - i: integer and unsigned (transmitted as 32-bit),
* - I: uint64 and int64 are transmitted as a separate type (8 bytes)
* - d: floating-point number (transmitted as double)
* - s: string (not null terminated, a byte-array really, convention is UTF-8, if you actually pass a string.)
* - c: bytes and chars as a byte
* - b: `bool` in 1 byte.
* - a: `uf::any` - a special type that can hold any serializable type. Internally its typestring and its
*      serialized value is stored.
* - e: `uf::error_value` - the error type used in expected values, see below.
* 
* Any 16-bit integer is converted to and from a 32-bit one. For any integer
* signed and unsigned are silently converted to each other. In case of conversion
* 32-bit to 64 (and back), we assume signed.
* All lengths and enums are serialized as 32 bit integers.
* Note that "lc" is serialized byte-wise exactly as an "s". Nevertheless, we keep
* string a separate type, since python for example has a very different notion
* of a list of chars than a string.
* Also, "b" is serialized the same as "c", but we keep the distinction.
* Finally, mXY (map) serializes to the same bytes as lt2XY (a list of pairs), but we
* keep the distinction, so that in Python (and javascript) we can deserialize into a
* dict (object) and not to a list (array).
*
* Compound types
* ==============
*
* - xT: `uf::expected<T>`. This either holds a `T` or a `uf::error_value` in the spirit of the C++23(?)
*                        `std::expected<T,E>`. `T` must be default constructible.
* - X: `uf::expected<void>` 
* - oT: `std::optional<T>`: Either a T or nothing. Smart and raw pointers and `std::optional` serializes to this in C++.
*                    On deserialization such values can either deserialize back to a smart pointer
*                    or convert to a variable of T in C++. If a missing value is attempted to deserialize
*                    to a variable of type T, we get a value_error. In Python empty optionals deserialize
*                    to None, while in Go we simply panic. T must not be void and must be default constructible.
* - tNT1T2...TN: tuples (fixed number of heterogeneous values, where N is a decimal element count,
*                followed by the typestrings of the elements. E.g., t2ii is a pair of a two ints.
*                t0 is illegal. t1T is always encoded as simply T.
*                std::array is serialized as a tuple of same elements (length known at compile time)
*                C arrays also (except for `char[]` and `const char[]`, but true for wide chars)
* - lT: lists(variable number of homogenous values). `std::vector`, `std::list`, `std::set` all can serialize to and from
*       a list. Note also that in order to deserialize into `std::vector`/`list`/etc`<T>` `T` shall be a
*       default constructible type.
* - mT1T2: maps. For saving more of the type info for dynamic languages, we also
*                have a _map_ type, which is encoded the same as list of pairs.
*                Note that the key type (`T1`) cannot be a void-like type nor a type that only
*                has `uf::expected<void>` in it (`like uf::expected<void>` itself or, for example,
*                `std::list<uf::expected<void>>` and similar compound types having no other primitive type).
*                Note also that in order to deserialize into `std::map<T1, T2>` both `T1` and `T2` shall be a
*                default constructible type.
*
* Empty Python (or JS) lists/arrays or dicts/objects are serialized to type <la> and <maa>,
* since we cannot determine the element type. These empty lists/dicts will deserialize
* into any list/map type in C++ (if appropriate conversion flags enable it).
* However, if you serialize an empty list<int> in C++ you will get a <li>, not <la>.
* On serialization we maintain the order of the
* container (sorted for set and map), as is for vector and list and random
* for unsorted maps and sets. In case of multimaps and sets, values with the
* same keys are serialized.
* On deserialization values are inserted to the container. For lists and
* vectors in the order received, for other containers via `insert()`. If the
* key already exists, insert may not do anything.
*
* Please, please do not override the unary `operator &` for types that you use with the serialization lib.
*
* Void-like types
* ===============
*
* For the type of `void` (and the Python `None` value or JSON's 'null') the type string and serialized
* value bytes are both of zero len. A few other C++ types behave like this, we call them void-like:
* - Empty tuples. Or tuples containing only void-like types.
* - Zero-length `std::array`s (or C arrays) or `std::array`s (or C arrays) of void-like types.
* - Lists of void-like types.
* - Maps with both a void-like key type and mapped type.
* 
* If these appear as members of a tuple, they are simply omitted both from the type and the
* serialized value (which is natural as they are of zero length in both). If this makes the tuple
* have just one non-void member, it is encoded as simply that member. If a tuple has only void
* members, it becomes void itself: zero length type and serialized value.
* Deserializing a `void` value is valid into any void type.
* Note that `uf::expected<void>` is not a void type as it may carry some info (the error).
*
* Deserializing views
* ===================
*
* You can deserialize a "view". This has impact only for strings or any. If the type
* you deserialize into contains `std::string_view`/`uf::any_view` objects, those will take values
* pointing to the raw memory you deserialize from. Thus if the raw memory is
* freed your "view" (specifically the string_views in it) will become invalid.
* But OTOH you save a lot of string copies, potentially. All other types
* are copied. `uf::is_deserializable_view_v<T>` is true for types that have
* `std::string_view`s or `uf::any_view`s in it.
* Regular deserialization does not match to types that contain string_views,
* you need to have actual strings that can hold a value. To test if a type
* can be deserialized into you can use `uf::is_deserializable_v<T>` to test.
*
* Pointers
* ========
*
* As a convenience you can serialize from/to smart pointers: `std::unique_ptr`
* and `std::shared_ptr`. These serialize into an optional of the pointed to object.
* Values of `T` also deserialize into smart pointers of the same or compatible type.
* You can also serialize from a raw pointer (as optional), but cannot deserialize into it.
* Two exceptions:
* - a `void*` is not possible to serialize from.
* - `const char *` (and `char*`) will be serialized as a null-terminated string and not
*   as a single `optional<char>`.
*
* Serialization of containers
* ===========================
*
* Any type `T` that is not a basic type, not an expected, optional, pointer or tuple and has
* no 'tuple_for_serialization()' free or member function (see structs below) will be
* attempted to be treated as a container.
* The type is considered a map (and get an "m" typestring), if it has `begin()`, `end()`, `size()`
* member functions and `value_type`, `key_type`, `mapped_type` member typedefs.
*
* For serialization of non-map containers (which get the "l" typestring), we seek `begin(const T&)`
* and `end(const T&)` free functions (think ADL), including potentially matching `std::begin()` and
* `std::end()`, which will call the corresponding member function, if it exists.)
* (Of course, the type of the element of the container must be serializable itself. We deduce the
* value type from `*begin()`.)
* If we find these, we will iterate over the container's elements and serialize them one-by-one.
* The size of the container is queried before the serialization using the `T::size()` const if
* exists, else we use `std::distance(end()-begin())`. So you can pass in a range of forward iterators,
* if you will (but not input iterators, since we will need to compute the size).
*
* On deserialization, we seek `begin(T&)` and `end(T&)` free functions, plus `T::clear()` and
* `T::push_back(value_type&&)` or `T::insert(value_type&&)` or `T::insert(iterator, value_type&&)`
* member functions (probed in this order). We deduce the value type from `*begin()`.
* Naturally the value_type must be deserializable itself - and it also has to be default
* constructible, because there is no mechanism as of yet to construct an object from serialized data.
* If these are found, we call `clear()` at the beginning of deserialization; issue a reserve() if exists
* and then for each element we default construct a `T::value_type`, deserialize it and then move 
* `insert()` or move `push_back()` it to the container.
*
* Serialization of structs
* ========================
*
* There is limited support to serialize structs as tuples. 
* 
* Auto serialization
* ------------------
* 
* Simple Aggregate structures are automatically serializable to/from without any user code. 
* Specifically, the following conditions must be met for this to work:
* - The struct must be an aggregate, that is
*   - No constructors/destructors
*   - No virtual functions   
*   - Only public non-static members
*   - No virtual or non-public base classes
* - Only empty base classes (or empty struct with only one non-empty base class)
*   This practically means no base classes.
* - No const members.
* - No reference or C-array members
* - Each member must be copy constructible (or move constructible and move assignable)
* - Empty structs are not auto serialized, by design.
* - Structs that look like containers (see "Serialization of containers" above) are 
*   serialized as containers not as auto serialized structures.
* - Structs that have (any) tuple_for_serialization(void) member/free functions use
*   those (see below how) and are not auto serialized. This is to turn auto serialization 
*   off when doing manual serialization.
* 
* In short, you can have simple, public collection of values. You can also have:
* - default initializers
* - static data members
* - member functions
* 
* Of course, the members of the structs must also be serializable/deserializable.
* 
* If you dont want a structure to be auto serializable (e.g., because you want it to be
* serializable/deserializable only with tags), you can disable it in two ways
* - Add 'using auto_serialization = void;' to the struct; and/or
* - Specialize 'allow_auto_serialization' for your type to false: 
*   'template <> constexpr bool allow_auto_serialization<MyType> = false;'
* 
* With auto serialize you cannot 
* - omit certain members, all will be serialized/deserialized;
* - take actions before and after serialization (like maintaining class invariants or
*   locking);
* - specify tags (see below). (But member types having tags will work.)
* 
* If you want these, read on to manual serialization below.
* 
* Here are a few notes that are not making sense right now, so please read on to the 
* next subsections for more details.
* - If you specify only one of the const or non-const tuple_for_serialization()
*   function for a struct then auto serialization will be turned off both for serialization 
*   and deserialization.
* - If you specify tuple_for_serialization(tag) with a tag argument,
*   auto serialization will still be turned on, making this struct serializable/deserializable
*   without tags. Disable auto serialization if you dont want this.
* - If you specify a before/after_serialization() or after_serialization_xxx() member
*   or free function for a struct that is using auto serialization, the above functions 
*   will NOT be used.
*
* Manual serialization
* --------------------
* To have more control over serialization or to serialize non-aggregate types, add a function
* `auto tuple_for_serialization() const noexcept {return std::tie(member1, member2,..);}`
* to the struct, then the listed members will automatically serialize,
* even if part of a tuple or list (serialize_len and serialize_type will
* also work). You dont need to list all members, just the ones to serialize.
* Note, that you can also use a free `tuple_for_serialization(const T&)`
* function. (Put that into the namespace where `T` is defined, so that ADL find it.)
* If you add a non-const `tuple_for_serialization()` function (or a free variant) then
* deserialization will also work.
* NOTE: The return type of the const and non-const `tuple_for_serialization()` functions
* must not be *exactly* the same. This normally happens automatically if you return 
* references to members as one will contain const refs, another non-const refs.
* It is good practice to make these noexcept, but not mandatory.
* \code
* struct mystruct {
*     mystruct(std::string text); //no-aggregate type: auto serialization doesn't work
*     int a, b, c;
*     std::string s;
*     other_struct o;
*     std::vector<double> vd;
*     auto tuple_for_serialization() const noexcept {return std::tie(a,b,c,s,o,vd);}
*     auto tuple_for_serialization()       noexcept {return std::tie(a,b,c,s,o,vd);}
* };
* \endcode
*
* Never return void from tuple_for_serialization. If you don't want to serialize
* or deserialize anything, return 'std::monostate'. That makes your type void-like.
*
* Avoid circular return types, so do not return anything that will resolve to
* the same type. So implementing a list of ints like
* \code
*   struct ilist {
*       int i;
*       ilist *next;
*       auto tuple_for_serialization() const { return std::tie(i, next); }
*   };
* \endcode
* will not work as its typestring would be infinitely long. Also avoid re-using
* the type in a member container, such as `std::vector<ilist>` in 'mystruct' above.
*
* Any free function for `tuple_for_serialization()` will be accepted if it is callable 
* with a const reference of your type (or non-const lvalue reference for deserialization). 
* So instead of the member functions above you can also write
* \code
* auto tuple_for_serialization(const mystruct &m) noexcept {return std::tie(m.a,m.m.b,m.c,m.s,m.o,m.vd);}
* auto tuple_for_serialization(      mystruct &m) noexcept {return std::tie(m.a,m.m.b,m.c,m.s,m.o,m.vd);}
* \endcode
* This is useful if you use a 3rd party type which you cannot modify, but have access
* to its members. (If you have access to members only through getters/setters read on.)
* Note, that when we search for a `tuple_for_serialization()` we always check matching
* free functions first. So in the above situation adding a `mystruct::tuple_for_serialization()`
* member function will have no effect as the free function takes precedence. We dont warn on 
* this condition, so be aware. 
* (Same rules apply to before/after_serialization/deserialization below.)
*
* The free function feature may have unintended side effects.
* For example, f you inherit your type B from a type A for which there is a free
* `tuple_for_serialization(A&)`, it will be called for your type 'B'. In such a case, either
* this is what you want (because serialization for B is exactly as for A), or you should
* define a `tuple_for_serialization(B&)` specifically for your type. You can also say
* `tuple_for_serialization(B&)=delete;` to prevent serialization of your type B (or
* to promote a B::tuple_for_serialization() member function).
*
* FYI, to re-iterate some name matching rules of C++, if you have type A, which defines
* both a const and non-const `tuple_for_serialization()` member function
* and in a descendant type B you define only one of them, then the other one will not be
* found for B, unless you say 'using A::tuple_for_serialization;' in the definition of B.
*
* Note that the typestrings of the const and non-const versions may differ
* for serialization and deserialization - these two aspects of a type are
* completely separate in the serialization lib. This has few uses, IMO.
* You may also omit either the const or non-const version, so your type may be only
* serializable from, but not deserializable into (use another type to receive
* serialized versions) or vice versa. However, in most cases
* you probably want the typestring of the two tuples returned from the const
* and non-const version to be the same, so your type is serializable from
* and into the same way. Use the `uf::is_ser_deser_ok_v<T>` type trait
* to check (for any type) if it is both serializable and deserializable
* (as owning) and the two typestrings are the same.  
* `static_assert(uf:is_ser_deser_ok_v<mystruct>);`
* You can also use `uf::is_ser_deser_view_ok_v<T>` to test view types.
*
* You may return anything from `tuple_for_serialization()` not just
* a tuple of references, as `std::tie()` would do. This is useful for serializing
* types with non-directly-serializable members, see below.
*
* Note again, however, that the return type of const and non-const tuple_for_serialization() 
* functions must be different for deserialization to work. Otherwise it is not possible to 
* selectively detect if the const or non-const version exists (and have ADL, that is). 
* This normally works automatically if you return tuples to refs to members (they will have 
* const or non-const refs).
* Usually a problem when returning by value from both (and using after_deserialization()).
* If you really want to return the same 'Type' from both, use std::tuple<Type> for the const 
* version to make the two types technically different.
*
* Simple helpers for non-supported members
* ----------------------------------------
*
* The serialization library provides support for a few cases when a member of a tuple
* is not natively supported.
* - Since in `tuple_for_serialization()` you may return anything, not just a tuple of references
*   as std::tie does, a simple helper is available to mix references to members (that
*   serialize as-is) and other, non lvalue-reference types.
*   Using `uf::tie()`, you can also list computed values, not just members.
*   E.g., if you have an 'std::filesystem::path file_path` member you can simply write
*   \code
*   auto tuple_for_serialization() const {return uf::tie(...., file_path.native(), ....);
*   \endcode
*   which will insert an std::string
*   representation of the path into the returned tuple by value. Then this value will be
*   serialized as if it were a member (and use the "s" typestring). This is an easy way to
*   do a quick conversion of a member that is otherwise not serializable.
* - To deserialize such string values back into a proper member of non-string type you can
*   use `uf::string_deserializer<Func>(Func f)` class. `f` must be a lambda taking a string_view
*   parameter and carrying out the deserialization. So to deserialize the above 
*   `std::filesystem::path file_path` member, just return in the non-const `tuple_for_serialization()`
*   \code
*   auto tuple_for_serialization() {
*       return uf::tie(..., uf::string_deserializer([this](std::string_view s){file_path=s;}), ....);
*   }
*   \endcode
*   If you want to have a view type, use `uf::string_deserializer_view<Func>` instead. Returning this
*   as part of the tuple in the non-const tuple_for_serialization(), then the type will be
*   deserializable only as a view. (It cannot be used in `uf::deserialize()`, `uf::any::get()`, 
*   `uf::any::get_as()` calls, only in 
*   `uf::deserialize_view()` and the `get_view()` or `get_view_as()` variants. 
* - For deserialization back to a C array use
*   `uf::array_inserter<T>(T*p, int max_size, int*size=nullptr, bool throv=true)`.
*   Specify the location and how many elements can the space accomodate. You can also specify
*   a third parameter, where the deserialization will store the number of elements in the input list.
*   If the fourth parameter is true, we throw `uf::value_mismatch_error` if the input does not fit into
*   max_size. If false, we drop the ones not fitting and size will contain the elements in the input
*   data, not now many have been stored. These classes can also be returned from
*   `tuple_for_serialization()`, making seialization for static arrays composable, see below.
*   \code
*    struct S {
*        constexpr unsigned MAX =  200;
*        int len;
*        double d[MAX];
*        auto tuple_for_serialization() const {return std::span(d, len);}
*        auto tuple_for_serialization() {return uf::array_inserter(d, MAX, &len);}
*    };
*   \endcode
* More complex examples soon below.
*
* Thread-safe serialization
* -------------------------
*
* Serialization is essentially a two-step process: we query the length and reserve the space
* and then do the actual serialization. Thus, we call `tuple_for_serialization()` twice. (And
* discard its result after use in both cases, so this better be a cheap function.)
* If you need to maintain class integrity in-between these two calls with a lock, you can
* also supply a `before_serialization(void)` const member (or `before_serialization(const T&)`
* free function), which will be called before these two steps. Also `after_serialization(bool)`
* will be called after, allowing you to lock and unlock the mutex, respectively.
* It will be called with a boolean indicating success of the entire serialization operation 
* (of a type this type may only be a part of). This allows to unambigously keep or pass
* ownership of some resource from the object to its serialized variant.
* (Note that if you provide any of these functions for members of a struct - ie what
* `tuple_for_serialization()` returns (or constitutent types of a member has them),
* `tuple_for_serialization()` will be called more than twice - to get to call before or
* `after_serialization()` for the members.)
*
* It is guaranteed that for any object we have called `before_serialization()` for, 
* `after_serialization(bool)` will also be called (also in case of exceptions, even if 
* `before_serialization()` was the function that has thrown). 
* The only case when we cannot guarantee such pairing is when `tuple_for_serialization()` of a
* component type does not throw when called as part of the before_serialization pass neither
* when doing length calculation nor when doing the actual serialization, but only when doing
* the after_serialization pass. (Note that all of these are calls to the same const object,
* so different behaviour in terms of throwing has to be very weird, but may happen via
* mutable members (e.g., unlocking a mutable mutex member twice) or via globals, such as
* a logging system throwing.)
* In that case there is no way we can call after_serialization for the result of
* `tuple_for_serialization()`, so we give up calling them for this struct.
* So if you want to throw in `tuple_for_serialization()` do it consistently.
* All in all, best to make it cheap and noexcept.
*
* Thread safe deserialization and maintaining class invariants
* ------------------------------------------------------------
*
* Deserialization is a one-step process, so the non-const `tuple_for_serialization()` is
* called only once. You can also provide either a `after_deserialization_simple()` or an
* `after_deserialization(U&&) member (or corresponding free functions taking a (first) argument
* of non-const ref to your struct), which will be called after the deserialization into the
* what was returned by tuple_for_serialization(). Here are the deserialization steps taken for a struct.
* 1. Call `&&x = tuple_for_serialization();` Note that tuple_for_serialization() may return a reference.
* 2. deserialize the bytes into 'x'.
* 3. If an exception was thrown by step \#1 or \#2, call `after_deserialization_error()`
*    if exists (which must be noexcept). Re-throw the exception from step #1 or #2.
* 4. Else if `after_deserialization_simple()` exists, call it.
* 5. Else if `after_deserialization(std::move(x))` is callable, call it.
* 6. If steps \#4 or \#5 throw an exception, let it percolate up.
* 
* Note that it is easy to mistype some part (name, type of U, constness, etc.) of 
* after_deserialization(U&&) and you get no error, a badly formatted after_deserialization() will
* simply not be called. So it is good practice to add
* `static_assert(uf::has_after_deserialization_tag_v<T>);' after you defined these functions.
* Note that you have all the following traits (all of them true if either a member or a free
* function variant exists (with the latter taking precedence over the former if both exist).
* - uf::has_tuple_for_serialization_tag_v<bool, T>
* - uf::has_before_serialization_tag_v<T>
* - uf::has_after_serialization_tag_v<T>
* - uf::has_after_deserialization_tag_v<T> 
* - uf::has_after_deserialization_simple_tag_v<T>
* - uf::has_after_deserialization_error_tag_v<T>
* 
* (The term 'tag' in the name of these traits is explained below.)
* Note that these return true even if your struct has 
* - no tuple_for_serialization() member with this tag nor without any tags, so these
*   functions can never be invoked;
* - no tuple_for_serialization() member at all and is using auto serialization,
*   which, again means that these functions are not invoked at all.
*
* Below is a list of typical situations and the best practice.
* - If you only serialize a subset of your member variables and you want to maintain class
*   invariants, like filling in the rest or checking, etc.: Provide a `tuple_for_serialization()`
*   that `std::tie`s the members to serialize and an `after_deserialization_simple()`, in which you
*   can ensure the class invariants after the members listed in `tuple_for_serialization()` were
*   deserialized into.
* - If you need to lock the structure for the duration of the deserialization: Add a mutex to 
*   the data structure; lock it at the beginning of `tuple_for_serialization()`
*   (as there is no `before_deserialization()` function looked up); unlock it in both
*   `after_deserialization_error()` and `after_deserialization_simple()`/`after_deserialization(U&&)`.
* - If you have members that cannot be serialized directly, but only a transformed version
*   of them: See below.
*
* Non-serializable members
* ------------------------
*
* If you have members that cannot be serialized directly, but only a transformed version
* of them, do the following.
* - Serialization: in 'tuple_for_serialization() const' return the transformed version by value.
* - Deserialization: in 'tuple_for_serialization()' return a placeholder of the transformed
*   by value; in after_deserialization(U&&) take the transformed version and create the original
*   in the member.
* For example, assuming we have an atomic_int (which is not natively serializable, but has a
* trivial conversion to-from int -- you can model more complex cases with this) as a member
* variable.
* \code
* struct S {
*     double d;
*     std::atomic_int i;
*     auto tuple_for_serialization() const noexcept { return uf::tie(d, int(i)); }
*     auto tuple_for_serialization()       noexcept { return uf::tie(d, int(0)); } //placeholder
*     void after_deserialization(std::tuple<double&, int> &&t) {i = std::get<1>(t);}
* };
* \endcode
*
* Tags: Selection of helper functions and providing context
* ---------------------------------------------------------
*
* It is sometimes desirable to have more control over serialization/deserialization
* 1) You may want a type to be serializable in multiple different ways or want to perform
*    different side effects at serialization/deserialization; or
* 2) You may want to provide some context at serialization/deserialization, e.g., to have 
*    a code book that needs to be looked up at both serialization/deserialization.
* 
* Both these cases can be solved via tags. Whenever you define any of the 7 helper function 
* (tuple_for_serialization (const/non-const), before/after_serialization and 
* after_deserialization(_simple/_error), you can also provide an argument called
* a tag. This is true in case of both member or free helper functions. For free functions
* declare the tag argument after the reference to the object serialized, like
* 'tuple_for_serialization(const T&, Tagtype tag). For after_deserialization(T&, U&&)
* you should specify it after U&&. 
* This way you can specify several of each helper function with different tag types.
* This may even lead to different typestrings, if the return value of 
* 'tuple_for_serialization()' depends on the tags. For example, the below struct 
* can be both serialized/deserialized as a double or as a string. The use of a 
* zero-length 'as_string'/'as_double' structs represents zero runtime overhead, 
* they just select the helper function variant.
* \code
* struct S {
*     struct as_string {};
*     struct as_double {};
*     double d;
*     auto tuple_for_serialization(as_double) const noexcept { return uf::tie(d); }
*     auto tuple_for_serialization(as_double)       noexcept { return uf::tie(d); } 
*     auto tuple_for_serialization(as_string) const          { return std::to_string(d); }
*     auto tuple_for_serialization(as_string)       noexcept { return std::string(); }
*     void after_deserialization(std::string &&s, as_string) noexcept { d = std::atof(s.c_str()); }
* };
* \endcode
*
* Then at invoking any serialization/deserialziation operation, you can specify a list of tags.
* (The list of tags must be preceeded with uf::use_tags to avoid misunderstanding the function
* arguments.)
* As a result, whenever a helper function needs to be called, the one with the tag type on
* the list will be called. If more than one matches a type on the tag list, tags earlier in the
* list have precedence. If the type does not have a helper function with any of the tags, the
* version of the helper functions without a tag is invoked. If that does not exist, the type
* is not serializable/deserializable with this tag list.
* Thus, for the above type
*\code
* struct S s{42.42};
* uf::serialize_type<S>()       //error: no tags and no 'tuple_for_serialization() const' (without a tag)
* uf::serialize_type<S, int>()  //error: no tags and no 'tuple_for_serialization() const' or 'tuple_for_serialization(int)' const 
* uf::serialize_type<S, S::as_string>()   //good: yields "s"
* uf::serialize_type<S, S::as_double>()   //good: yields "d"
* uf::serialize(s)                                               //error: not serializable without tags
* uf::serialize(s, uf::use_tags, S::as_string())                 //good, returns a serialized string
* uf::serialize(s, uf::use_tags, S::as_string(), int())          //good, returns a serialized string, the tag 'int' is unused
* uf::serialize(s, uf::use_tags, S::as_string(), S::as_double()) //good, returns a serialized string, the tag 'S::as_double' has lower precedence
* uf::any a1(s);                                //error: cannot serialize 's' without tags
* uf::any a2(s, uf::use_tags, S::as_string());  //good, 'a2' now contains a string
* a2.get_as<std::string>();                     //good, we can get it out.
* uf::any a3(3.14);            //a3 now contains a double
* a3.get(s);                   //error: cannot get any value into 's' without a tag
* a3.get(s, uf::allow_converting_all, uf::use_tags, S::as_string());   //error: 's' with a tag 'S::as_string' expects a string. This throws a type_mismatch_error 'd'->'s'
* a3.get(s, uf::allow_converting_all, uf::use_tags, S::as_double());   //good: 's' with a tag 'S::as_double' expects a double
* a3.get_as<S>(uf::allow_converting_all, uf::use_tags, S::as_double());//good, too
* uf::any a4(42);              //a4 now contains a int
* a4.get(s);                   //error: cannot get any value into 's' without a tag
* a4.get(s, uf::allow_converting_all, uf::use_tags, S::as_string());   //error: 's' with a tag 'S::as_string' expects a string. This throws a type_mismatch_error 'd'->'s'
* a4.get(s, uf::allow_converting_double, uf::use_tags, S::as_double());//good: 's' with a tag 'S::as_double' expects a double and we can convert an int
* a4.get(s, uf::allow_converting_none, uf::use_tags, S::as_double());  //error: 's' with a tag 'S::as_double' expects a double and we can NOT convert an int
*\endcode
*
* Note that helper functions are selected individually. Assume you provide 2 versions of 
* 'tuple_for_serialization()' one with tag 'Tag' and one with no tags and only one version
* of 'after_deserialization_simple()' with 'Tag' to maintain a class invariant.
* In this case the type will be deserializable with any set of tags due to the existence of
* the tag-less 'tuple_for_serialization()'. However, after_deserialization_simple() will only be
* called when the tag list includes 'Tag'.
* NOTE: As an exception to the above rule an 'after_deserialization(U&&, tag)' function will only be
* detected if there is a 'tuple_for_serialization(tag)' with the same tag AND the latter returns 'U'.
* Thus, specifying a tagless 'U tuple_for_serialization()' and 'after_deserialization(U&&, tag)'
* will never call the latter even if the tag is specified. Use uf::has_after_deserialization_tag_v<T, tag> 
* to check. It is true only if 'U tuple_for_serialization(tag)' and 'after_deserialization(U&&, tag)'
* are defined (either as member or free functions).
* 
* In all the examples above tags were zero-length structs. However, tags can actually carry a value.
* This is useful if you want to provide some context to the serialization/deserialization 
* process. But be aware that internally these tags are always passed by value, thus they have
* to be copy constructable, preferably cheaply. If you want to pass a larger const value around,
* use a 'const Context*' as the tag type. If you also want to update the context, pass a non-const
* pointer around. Note that you do not need smart pointers for this, as during the serialization/
* deserialization process, ownership will not change. 
* (The decision of not to pass the tags around by (any kind of) reference was made
* so that zero-length tags remain overhead-free. As a result you have to use pointers for any larger
* context.)
*
* Note well: Try avoiding tags that are convertible to each other - it will likely trigger unwanted
* functions (like int and double) and it will be hard to debug.
* Note, as well: If the tag set provided allows 'after_deserialization()' function (either because 
* there is an 'after_deserialization()' with one of the tags or there is a tagless one, then
* 'after_deserialization_simple()' functions are not considered at all - even if there is one
* with a tag that is on the tag list.
*
* Debugging
* =========
*
* When you find that serialization of one of your types doesn't work, use the following type traits
* to see where is the problem. These type traits are always true (thus they shall be used in 
* static_assert()s), but compiling them will trigger other static assertions explaining what is the
* cause of the problem.
* @code
* uf::is_ser_deser_ok_v<T, tags...>       //Type is both serializable and deserializable and the two typestrings are the same
* uf::is_ser_deser_view_ok_v<T, tags...>  //Type is both serializable and deserializable as a view and the two typestrings are the same
* uf::is_ser_ok_v<T, tags...>             //Type is serializable 
* uf::is_deser_view_ok_v<T, tags...>      //Type is deserializable as a view
* uf::is_deser_ok_v<T, tags...>           //Type is deserializable as owning
* @endcode
*
* When decoding the error messages you get, consider the following example.
* @code
*    struct test_bad {
*        test tuple_for_serialization() { return 5; }
*    };
*    static_assert(uf::is_ser_deser_ok_v<std::vector<test_bad>>);
* @endcode
* This gives the following error messages
* @code{.unparsed}
* 1. ufserialize.h: In instantiation of 'constexpr bool uf::impl::is_serializable_f() [with T = const test_bad; bool emit_error = true; tags = {}]':
* 2. ufserialize.h:736:108: required from 'constexpr bool uf::impl::is_ser_deser_ok_f() [with T = std::vector<test_bad>; bool as_view = false; bool emit_error = true; tags = {}]'
* 3. ufserialize.h:6978:95: required from 'constexpr const bool uf::is_ser_deser_ok_v<std::vector<test_bad> >'
* 4. example_code.cc:4:23:  required from here
* 5. ufserialize.h:1857:55: in 'constexpr' expansion of 'uf::impl::is_serializable_f<std::vector<test_bad>, true>()'
* 6. ufserialize.h:777:35:  error: static assertion failed: Structure has no tuple_for_serialization() const member/free function, nor seem to be a container.
* @endcode
* - Line #4 (the last 'required from' line) shows the location of the check.
* - Then we see in line #3 that the type is question is 'std::vector<test_bad>' This may be a structure of many components,
*   walking up the 'required_from' list we get deeper into the type towards the problem.
* - Then the first line shows the type of the actual problem type: 'T = const test_bad'. 
* - The last line then tells what is the problem: "Structure has no tuple_for_serialization() const member/free function". Because we only
*   have a non-const member and for serialization we need a const version.
*
* Default values
* ==============
*
* Each type has a default value (usually zero and empty). You can create a serialized value for a
* type representing its default value.
* - uf::any::create_default(typestring)
* - std::string default_serialized_value(typestring)
* Default values are:
* - b: false
* - c, i, I, d: the zero value (-0.0 for double, to be precise).
* - s, l, m, o: Empty string, list, map or optional (of whatever type).
* - a: an any containing void.
* - x: An expected containin a default value for its type.
* - e: Empty type, id and message and a void any.
*
* Type conversion at deserialization
* ==================================
*
* Type conversion can be requested during deserialization is, if the typestring of the value to
* deserialize does not match the type to deserialize into.
* Note optionals always convert to their carried value and vice versa, but not to an expected.
* The uf::serpolicy enum can be used to govern what conversions are allowed. See its documentation for
* details.
* 
* Note on void-like tuple members. Sometimes a tuple member may become void-like during 
* conversion. E.g., t2Xi will happily convert to 'i' if the 'X' holds a value (void).
* (If the policy includes 'allow_converting_expected'.)
* The pathological case of t2Xli will therefore happily convert to 'li' with 'allow_converting_expected',
* but also to 'la' with 'allow_converting_expected' and 'allow_converting_any' - by wrapping the
* integers in the list to an uf::any.
* On the other hand, if you include 'allow_converting_tuple_list', then t2Xli will be converted to a 
* list member-by-member. This results in a list that has always the same number of elements as many
* members the tuple had. That is, it will include an uf::any holding a uf::expected<void> and a second
* uf::any holding a std::vector<int>. (Note that std::tuple<std::monospace,int,int> counts as two
* members, since std::monospace is known to be void-like already during compilation, so its type is
* 't2ii' and will convert to a list of two elements.) The above also means that 't2Xli' will not 
* convert to 'li' if 'allow_converting_tuple_list' is specified ('allow_converting_all' will also 
* include it), because member-by-member conversion fails as none of the tuple's members can be 
* converted to an integer.
* 
* Error handling
* ==============
* 
* Serialization related errors are thrown as exceptions. All of the below are 
* descendants of uf::value_error, which can be used to catch all of them.
* - uf::value_mismatch_error: Thrown when a serialzied value does not match a typestring
*   (or a C++ type). Thrown in deserialization, serialize_scan, printing.
* - uf::typestring_error: When a typestring is invalid (invalid character, no number after 't', etc.)
* - uf::type_mismatch_error: Thrown when conversion is not possible
* - uf::expected_with_error: When we need to convert an expected value to their holding type
*   (xT->T, such as xi->i), but the expected contains an error. This exception holds the errors,
*   (all of them if there was more than one such occurrence).
* - uf::not_serializable_error: Thrown when serializing invalid expected:s
*   or when a non-serializable Python object is serialized.
* 
* In these errors (the first 3), we display the typestring with an asterisk marking where the 
* error happened. E.g, on conversion, we say: could not convert <t2s*i> -> <t2s*s>, showing that
* it was the second element of the tuple failed conversion. Note that during conversion void-like
* values may simply decay, so it is possible to convert a 3-element tuple to a 2-element one.
* So converting t3as*a->t2is* does not fail upfront, since it may succeed if the first any holds
* an integer and the second holds a void and we allow any packing/unpacking during conversion
* (uf::allow_converting_any). In some cases we need to parse the content of 'any's, and some
* type error may happen there, this is indicated with a parenthesis showing the type inside
* the any. In the above example if the first any contained a string, we would get conversion
* error: t3a(*s)sa -> t2*is, showing that the string inside the any cannot be converted to an int.
* Note that you do not allow any unpacking, the above would result in error t3*asa->t2*is 
* (even if the any contains an int), but we would also indicate that uf::allow_converting_any
* policy would allow conversion (of this particular bit).
*
* API levels
* ==========
*
* The API has three levels.
* - Level 1: The lowest level, recursively callable serialization, deserialization, print and parse
*            functions, with a lot of technical parameters, coded to be easy to optimize and
*            do not fully honour before* and after* functions. Should not be used from user functions.
*            These are in namespace uf::impl:: This level is not centrally documented.
* - Level 2: functions to serialize, deserialize, give the typestring, parse or print serialized values.
*            These no longer lend themselves to recursive calling, but are honouring before* after*
*            helper functions fully and are safe to use from user functions. These are in namespace uf::
*            This level is documented below.
* - Level 3: uf::any and uf::any_view, which package the above operations conveniently into a class
*            with little loss of performance compared to level 2. This level is documented at `uf::any` and
*            `uf::any_view`;
*
* The Level 2 API is as follows:
* - `string default_serialized_value(typestring)`
*       Creates a string representing the serialized version of the default value for `typestring`.
* - `string_view serialize_type(const T&)`
* - `serialize_type<T>()`
* - `string_view serialize_type(const T&, uf::use_tags, tags...)`
* - `serialize_type<T, tags...>()`
*       Produces a typestring of what this type will serialize into using these tags.
* - You also have the same variants for deserialize_type(), which gives you what typestring
*   can be deserialized into this variable.
* - `string serialize(const T&t)`
* - `string serialize(const T&t, use_tags, tags...)`
        Allocates memory and encodes `t` (using the given tags)
* - `void serialize(Alloc alloc, const T&t)`
* - `void serialize(Alloc alloc, const T&t, uf::use_tags, tags...)`
*       Lets the user allocate memory and encodes `t`. `alloc` is a char*(size_t) function 
*       taking the length and returning a char pointer where the serialized data has to be placed.
* - `string_view deserialize_as<T>(string_view s, bool allow_longer=false)`
* - `string_view deserialize_as<T>(string_view s, bool allow_longer, uf::use_tags, tags...)`
* - `string_view deserialize_view_as<T>(string_view s, bool allow_longer=false)`
* - `string_view deserialize_view_as<T>(string_view s, bool allow_longer, uf::use_tags, tags...)`
*       Deserializes s into type `T` using the tags assuming s is a serialized form of 
*       deserilize_type<T, tags...>. Else a value_mismatch_error is thrown. 
*       If `allow_longer` is true, we accept if data remains after the deserialization. 
*       This can be used to deserialize only the beginning of a tuple.
*       No conversions applied, the types are expected to match completely.
*       The `view` variants allow `T` to be a view type and contain any_view or string_view members
*       that dont own the data. Deserializing as a view is cheaper, as no memory needs allocation
*       but the original data must outlive the deserialized variable.
* - `T deserialize_convert_as<T>(string_view s, string_view from_type, 
*                                          serpolicy policy=all, bool allow_longer=false)`
* - `T deserialize_convert_as<T>(string_view s, string_view from_type, 
*                                          serpolicy policy=all, bool allow_longer, uf::use_tags, tags...)`
* - `T deserialize_view_convert_as<T>(string_view s, string_view from_type, 
*                                               serpolicy policy=all, bool allow_longer=false)`
* - `T deserialize_view_convert_as<T>(string_view s, string_view from_type, 
*                                               serpolicy policy=all, bool allow_longer, uf::use_tags, tags...)`
*       Deserialize either as owning or as a view from `s` as serialized value (assuming it 
*       is of type `from_type`) using `policy`. If the deserialize type of `T` (with `tags`) is
*       not exactly the same as `from_type`, we apply conversion. This is slower a bit.
* - `string_view deserialize(string_view s, T&v, bool allow_longer=false)`
* - `string_view deserialize(string_view s, T&v, bool allow_longer, uf::use_tags, tags...)`
* - `string_view deserialize_view(string_view s, T&v, bool allow_longer=false)`
* - `string_view deserialize_view(string_view s, T&v, bool allow_longer, uf::use_tags, tags...)`
* - `string_view deserialize_convert(string_view s, string_view from_type, T&v,
*                                          serpolicy policy=all, bool allow_longer=false)`
* - `string_view deserialize_convert(string_view s, string_view from_type, T&v,
*                                          serpolicy policy=all, bool allow_longer, uf::use_tags, tags...)`
* - `string_view deserialize_view_convert(string_view s, string_view from_type, T&v,
*                                               serpolicy policy=all, bool allow_longer=false)`
* - `string_view deserialize_view_convert(string_view s, string_view from_type, T&v,
*                                               serpolicy policy=all, bool allow_longer, uf::use_tags, tags...)`
*       These varians serialize into an existing lvalue as opposed to returning the 
*       deserialized value. This also means that you do not have to specify the type 
*       excplicitly as a template parameter. We return the serialized data remaining after
*       deserialization. If allow_longer==false, this is empty. Else it is the back of `s`.
* - `std::optional<type_mismatch_error>
*        cant_convert(string_view from_type, string_view to_type, serpolicy policy);`
*   Tells if one type is convertible to another using `policy`. It cannot check one thing:
*   If we allow any to be converted to other types, we cannot check if the any will actually
*   contain a type compatible with its target. So we assume so here. To check if the value is
*   also known use
* - `std::optional<std::variant<type_mismatch_error, expected_with_error>>
        cant_convert(string_view from_type, string_view to_type, serpolicy policy, string_view from_value);`
*   If conversion CANNOT happen, it returns a the exception that would be thrown (but does not throw)
*   Best used like `if (auto err = cant_convert()) {handle_error(*err)};`
*   Note that a bad typestring always throws a `uf::typestring_error` and mismatching `from_type` and
*   `from_value` always throws a `uf::value_mismatch_error`.
* - `std::pair<std::string, bool> 
        convert(string_view from_type, string_view to_type, serpolicy policy, string_view from_value);`
*    If converts a serialized value of type `from_type` to `to_type`. Throws if not possible with the policy specified,
*    expecteds to be converted to their contained type contain errors, one of the typestrings is invalid or the value
*    does not fit the type. Returns the empty string and a true, if the converted value is the same as `from_value`.
* - `string serialize_print(T&t, json_like = false, max_len = 0, chars = {}, escape_char=`%`)
* - `string serialize_print(T&t, json_like = false, max_len = 0, chars = {}, escape_char=`%`,
*                           uf::use_tags, tags...)`
*       Prints a the value of `t` for humans.
* - `string serialize_print_by_type(string_view type, string_view serialized, json_like = false, max_len = 0, chars = {}, escape_char=`%`)`
*   Prints a serialized type for humans. You can specify a maximum length and characters to escape.
* - `size_t parse_type(string_view type)`: Parses a type string to its end.
*          Since type strings are prefix codes, we can tell accurately where it ends.
*          If not a valid typestring, returns 0 (~it could only read the void type)
* - `is_serializable_v<T, tags...>`
* - `is_deserializable_v<T, tags...>`
* - `is_deserializable_view_v<T, tags...>`
*          Tells us if a type is possible to serialize or to deserialize into (using these tags).
* - `has_tuple_for_serialization_tag_v<deser, T, tags...>` //True if T has tuple_for_serialization() (deser=false: const; deser=true: non-const)
* - `has_before_serialization_tag_v<T, tags...>`           //True if T has before_serialization()
* - `has_after_serialization_tag_v<T, tags...>`            //True if T has after_serialization(bool, tags...)
* - `has_after_deserialization_tag_v<T, tags...>`          //True if T has after_deserialization(U&&) (where U= return type of tuple_for_serialization())
* - `has_after_deserialization_simple_tag_v<T, tags...>`   //True if T has after_deserialization() 
* - `has_after_deserialization_error_tag_v<T, tags...>`    //True if T has after_deserialization_error()
*          Verify that user supplied member or free functions actually exist. The `tag` is part of the name
*          to remind you that these apply only to a particular set of tags. If tags... is empty then the 
*          non tagged version.
* - `is_ser_deser_ok_v<T, tags...>`       //Type is both serializable and deserializable and the two typestrings are the same
* - `is_ser_deser_view_ok_v<T, tags...>`  //Type is both serializable and deserializable as a view and the two typestrings are the same
* - `is_ser_ok_v<T, tags...>`             //Type is serializable
* - `is_deser_view_ok_v<T, tags...>`      //Type is deserializable as a view
* - `is_deser_ok_v<T, tags...>`           //Type is deserializable as a owning
*          These always return true, but static_assert()ing these will give detailed errors on problems with a type.
*
* Use of `std::tie` helps a lot. Simply deserializing a string and an int
* is easy: `str = serialize(tie(s,i));` Then getting them back is also easy
* `deserialize(str, tie(s,i));`
*/

namespace uf
{

/** @defgroup tools Tools
 * @{*/

// This is slow + incomplete: we are waiting for std::fmt
template<typename... TT>
auto concat(TT const&... tt) { std::ostringstream o; (o << ... << tt); return o.str(); }

struct error : public std::runtime_error {
    [[nodiscard]] explicit error(std::string &&msg) : runtime_error(std::move(msg)) {}
    [[nodiscard]] error(const error&) = default;
    [[nodiscard]] error(error&&) noexcept = default;
    error &operator=(const error&) = default;
    error& operator=(error&&) noexcept = default;
};

/** Serialization or type mismatch exceptions.
 * It can take 2 types with positions. Descendats may use one or both,
 * - value_mismatch_error uses one or none.
 * - typestring_error uses one.
 * - type_mismatch_error and expected_with_error uses two.
 * - not_serializable_error uses none. 
 * It has a mechanism to store more than one position in each type. This is used to mark
 * problematic expecteds in expected_with_error.*/
struct value_error : public error {
    struct type_pos {
        std::string type;
        std::basic_string<uint16_t> pos; //use basic_string for SSO
        [[nodiscard]] std::string format(bool front_caret) const
        {
            std::string ret{type.length() ? type : "void"};
            for (int i = pos.size()-1; i>=0; i--) //do it backwards
                if ((front_caret || pos[i]>0) && pos[i] <= ret.length())
                    ret.insert(ret.begin() + pos[i], '*');
            return ret;
        }
        /** True if we have no type, no position or only a single position at 
         * the front or way larger then the type length. */
        [[nodiscard]] bool front_only() const noexcept { return type.empty() || pos.size() == 0 || (pos.size() == 1 && (pos.front() == 0 || pos.front()>type.size())); }
        void prepend(char c) {
            type.insert(type.begin(), c);
            for (auto &p : pos) p++;
        }
        void prepend(std::string_view s) {
            type.insert(0, s);
            for (auto &p : pos) p += s.size();
        }
    };
    std::string my_what;            ///<what() returns a pointer into this
    std::string msg;                ///<Informational message before types.
    std::array<type_pos, 2> types;  ///<The two types with position of error

    [[nodiscard]] value_error(const value_error &) = default;
    [[nodiscard]] value_error(value_error &&) noexcept = default;
    value_error &operator=(const value_error &) = default;
    value_error &operator=(value_error &&) noexcept = default;
    const char *what() const noexcept override { return my_what.c_str(); }
    /** Returns a formatted error.
     * %1 will be replaced to formatted type1
     * %2 will be replaced to formatted type2 */
    virtual void regenerate_what(std::string_view format ={})
    {
        if (format.length()) msg = format;
        else if (types[0].type.length() && msg.find("%1") == std::string::npos) {
            //Add typestring if we have type(s) but not part of message.
            if (types[1].type.length() && msg.find("%2") == std::string::npos)
                msg.append(" (<%1> -> <%2>)");
            else
                msg.append(" (<%1>)");
        }
        my_what = msg;
        const bool front_caret = types[0].front_only() && types[1].front_only();//dont show caret at the beginning of both
        char buff[3] = "%i";
        for (int i = 0; i < 2; i++) {
            size_t pos = 0;
            buff[1] = '1' + i;
            while (std::string::npos != (pos = my_what.find(buff, pos)))
                my_what.replace(pos, 2, types[i].format(!front_caret));
        }
    }

    /** Encaps and extend the type in type[0] assuming encapsualtion in an 'any'. 
     * Useful for operations (scanning, printing), which report only the remaining type in their errors
     * at the lowest level recursive functions. This is perhaps
     * best explained via an example. Assume we are working on a 't2ai' type (scanning its value, 
     * converting or printing it)  where the 'a' contains a map: 'mad' and there is one element in the
     * map, where the 'any' contains an invalid typestring: "@". Once that is detected and a value_error
     * (a typestring_error to be specific) is formed at the bottom layer this function will be called 
     * from the place processing the map as follows
     * => type[0] = "@" (pos=0), original_inner_type="@", remaining_inner_type="", remaining_outer_type="d"
     * and it will update type[0] to be "(@)d" (pos=1). 
     * Then, in the place, where we process the outer tuple, it will be called again
     * => type[0] = "(@)d" (pos=3), original_inner_type="mad", remaining_inner_type="d", remaining_outer_type="i"
     * and it will update type[0] to be "(ma(@)d)i" (pos=4). 
     * Then the top level function will prepend the processed part of the outer type: t2a, resulting in the
     * type in the error message: t2a(ma(*@)d)i. */
    value_error &encaps(std::string_view original_inner_type, std::string_view remaining_inner_type,
                        std::string_view remaining_outer_type) {
        const size_t consumed = original_inner_type.length() - remaining_inner_type.length();
        types[0].prepend(original_inner_type.substr(0, consumed)); //also updates 'pos'
        types[0].prepend('(');
        types[0].type.push_back(')');
        types[0].type.append(remaining_outer_type);
        regenerate_what();
        return *this;
    }

    /** Prepend the prefix of the 'original_type' not part of 'remaining_type'. 
     * Assumes 'original_type' ends in 'remaining_type'. */
    value_error &prepend_type0(std::string_view original_type, std::string_view remaining_type) {
        const size_t consumed = original_type.length() - remaining_type.length();
        types[0].prepend(original_type.substr(0, consumed));
        regenerate_what();
        return *this;
    }
    /** Throws the exception we contain or does nothing if we dont.*/
    [[noreturn]] virtual void throw_me() const = 0;
    value_error &append_msg(std::string_view message) {
        msg.append(message);
        regenerate_what();
        return *this;
    }
protected:
    value_error(std::string_view _format,
                std::string_view t1, std::string_view t2,
                size_t pos_t1 = std::string::npos, size_t pos_t2 = std::string::npos) :
        error(std::string{}), msg(_format)
    {
        types ={type_pos{ std::string(t1), {(uint16_t)std::min(size_t(std::numeric_limits<uint16_t>::max()), pos_t1)} },
                  type_pos{ std::string(t2), {(uint16_t)std::min(size_t(std::numeric_limits<uint16_t>::max()), pos_t2)} }};
        regenerate_what();
    }
};

/** Type mismatch exceptions.*/
struct type_mismatch_error : public value_error {
    [[nodiscard]] type_mismatch_error(std::string_view _format,
                                      std::string_view t1, std::string_view t2,
                                      size_t pos_t1 = std::string::npos, size_t pos_t2 = std::string::npos) 
        : value_error(_format, t1, t2, pos_t1, pos_t2) {}
    [[nodiscard]] type_mismatch_error(const type_mismatch_error&) = default;
    [[nodiscard]] type_mismatch_error(type_mismatch_error&&) noexcept = default;
    type_mismatch_error& operator=(const type_mismatch_error&) = default;
    type_mismatch_error& operator=(type_mismatch_error&&) noexcept = default;
    [[noreturn]] void throw_me() const override { throw *this; };
};

/** When a typestring is invalid.*/
struct typestring_error : public value_error {
    [[nodiscard]] explicit typestring_error(std::string_view _msg, std::string_view type, size_t pos_type = std::string::npos) 
        : value_error(_msg, type, {}, pos_type) {}
    [[nodiscard]] typestring_error(const typestring_error&) = default;
    [[nodiscard]] typestring_error(typestring_error&&) noexcept = default;
    typestring_error& operator=(const typestring_error&) = default;
    typestring_error& operator=(typestring_error&&) noexcept = default;
    [[noreturn]] void throw_me() const override { throw *this; };
};

/** When a value does not match its type string.
 * We may construct it with or without a type string.*/
struct value_mismatch_error : public value_error {
    [[nodiscard]] value_mismatch_error(std::string_view _msg) : value_error(_msg, {}, {}) {}
    [[nodiscard]] value_mismatch_error(std::string_view _msg, std::string_view type, size_t pos_type = std::string::npos)
        : value_error(_msg, type, {}, pos_type) {}
    [[nodiscard]] value_mismatch_error(const value_mismatch_error&) = default;
    [[nodiscard]] value_mismatch_error(value_mismatch_error&&) noexcept = default;
    value_mismatch_error& operator=(const value_mismatch_error&) = default;
    value_mismatch_error& operator=(value_mismatch_error&&) noexcept = default;
    [[noreturn]] void throw_me() const override { throw *this; };
};

/** When a varaible cannot be serialized (python or it contains invalid expected:s). */
struct not_serializable_error : public value_error {
    [[nodiscard]] not_serializable_error(std::string_view _msg) : value_error(_msg, {}, {}) {}
    [[noreturn]] void throw_me() const override { throw *this; };
};

struct error_value;

/** When expected objects containing error cannot be converted to their receiving type.*/
struct expected_with_error : public value_error {
    std::vector<error_value> errors;
    [[nodiscard]] explicit
        expected_with_error(std::string_view _msg, std::string_view _t1, std::string_view _t2,
                            std::vector<error_value> &&_e, std::vector<std::pair<size_t, size_t>> &&_p) 
            : value_error(_msg, _t1, _t2), errors(std::move(_e)) {
        for (auto &p : _p) {
            types[0].pos.push_back((uint16_t)std::min(size_t(std::numeric_limits<uint16_t>::max()), p.first));
            types[1].pos.push_back((uint16_t)std::min(size_t(std::numeric_limits<uint16_t>::max()), p.second));
        }
    }
    ~expected_with_error() noexcept = default;
    [[nodiscard]] expected_with_error(const expected_with_error &) = default;
    [[nodiscard]] expected_with_error(expected_with_error &&) noexcept = default;
    expected_with_error &operator=(const expected_with_error &) = default;
    expected_with_error &operator=(expected_with_error &&) noexcept = default;
    void regenerate_what(std::string_view format = {}) override;
    [[noreturn]] void throw_me() const override { throw *this; };
};

/** Invalid use of an API call: bad parameter values, etc.*/
struct api_error : public error {
    [[nodiscard]] explicit api_error(std::string &&msg) noexcept : error(std::move(msg)) {}
    [[nodiscard]] api_error(const api_error&) = default;
    [[nodiscard]] api_error(api_error&&) noexcept = default;
    api_error& operator=(const api_error&) = default;
    api_error& operator=(api_error&&) noexcept = default;
};

/** @}  tools */

namespace impl {

/** Helper to concatenate strings effectively.
 * Use StringViewAccumulator a; a << "x" << "y" + "z".
 * Copy and move compatible. It captures string refs fed into it as views, string&& is stored.*/
class StringViewAccumulator
{
    using element = std::variant<std::string_view, std::string, char>;
    std::vector<element> parts;
protected:
    mutable std::string result;
public:
    StringViewAccumulator& operator<<(const char* c) { if (c && c[0]) { parts.push_back(std::string(c)); result.clear(); } return *this; }
    StringViewAccumulator& operator<<(const std::string& c) { if (c.size()) { parts.push_back(std::string_view(c)); } result.clear(); return *this; }
    StringViewAccumulator& operator<<(std::string&& c) { if (c.size()) { parts.push_back(std::move(c)); } result.clear(); return *this; }
    StringViewAccumulator& operator<<(std::string_view c)
    {
        if (c.empty()) return *this;
        if (parts.size() && parts.back().index()==0 && std::get<0>(parts.back()).data() + std::get<0>(parts.back()).length() == c.data())
            std::get<0>(parts.back()) = { std::get<0>(parts.back()).data(), std::get<0>(parts.back()).length() + c.length() };
        else
            parts.push_back(c);
        result.clear();
        return *this;
    }
    StringViewAccumulator& operator<<(char c) { parts.push_back(c); result.clear(); return *this; }
    //StringViewAccumulator &operator << (PyObject*o);
    template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T> || std::is_pointer_v<T>>>
    StringViewAccumulator& operator<<(const T& c)
    {
        if constexpr (std::is_arithmetic_v<T>)
            return operator<<(std::to_string(c));
        else if constexpr (std::is_pointer_v<T>) {
                std::stringstream s;
                s << (void*)c;
                return operator<<(s.str());
            } 
        else
            static_assert(std::is_void_v<T>, "I cannot print this type."); //std::is_void_v<T> here is just to make the condition dependent on T
        return *this;
    }
    StringViewAccumulator& operator<<(const StringViewAccumulator& c)
    {
        parts.reserve(parts.size() + c.parts.size());
        for (auto& e : c.parts)
            switch (e.index()) {
            case 0: operator<<(std::get<0>(e)); break;
            case 1: operator<<(std::string(std::get<1>(e))); break;
            case 2: operator<<(std::get<2>(e)); break;
            }
        return *this;
    }
    StringViewAccumulator& operator<<(StringViewAccumulator&& c)
    {
        parts.reserve(parts.size() + c.parts.size());
        for (auto& e : c.parts)
            switch (e.index()) {
            case 0: operator<<(std::get<0>(e)); break;
            case 1: operator<<(std::move(std::get<1>(e))); break;
            case 2: operator<<(std::get<2>(e)); break;
            }
        return *this;
    }

    template <typename T>
    StringViewAccumulator& operator+(T&& c) { return *this << std::forward(c); }

    operator const std::string& ()const
    {
        if (result.length() == 0) {
            result.reserve(std::accumulate(parts.begin(), parts.end(), 0, [](size_t u, const element& c) {return u + (c.index()==0 ? std::get<0>(c).length() : c.index() == 1 ? std::get<1>(c).length() : 1); }));
            for (const element& c : parts)
                switch (c.index()) {
                case 0: result += std::get<0>(c); break;
                case 1: result += std::get<1>(c); break;
                case 2: result.push_back(std::get<2>(c)); break;
                }
        }
        return result;
    }
    operator std::string () const
    {
        operator const std::string& ();
        std::string ret = std::move(result); result.clear();
        return ret;
    }
    /** A potentially cheap flattening. 
     * If there is only one part, it is just a view to that. 
     * On multiple parts, we flatten and return a view to the internal string.
     * The view returned will be invalidated by any operation. */
    operator std::string_view() const
    {
        if (parts.empty()) return {};
        if (parts.size() == 1) switch (parts.front().index()) {
        case 0: return std::get<0>(parts.front()); break;
        case 1: return std::get<1>(parts.front()); break;
        case 2: return { &std::get<2>(parts.front()), 1 }; break;
        }
        return operator const std::string & ();
    }

    operator std::variant<std::string, std::string_view>() const
    {
        if (parts.size() <= 1) return operator std::string_view();
        return operator std::string();
    }
};



/** Helper to turn several types into a tuple, one type to itself, zero types to void.*/
template <typename ...TT>
struct single_type { using type = std::tuple<TT...>; };

/** Helper to turn several types into a tuple, one type to itself, zero types to void.*/
template <typename T>
struct single_type<T> { using type = T; };

/** Helper to turn several types into a tuple, one type to itself, zero types to void.*/
template <>
struct single_type<void> { using type = void; };

/** Helper to turn several types into a tuple, one type to itself, zero types to void.*/
template< class ...TT>
using single_type_t = typename single_type<TT...>::type;

} //ns impl



//Code proudly taken from https://github.com/crazy-eddie/crazycpp

namespace impl
{

template <unsigned I>
struct unsigned_
{
    constexpr operator unsigned() const noexcept
    {
        return I;
    }
};

template < unsigned ... Nums >
struct indices
{
    template < unsigned I >
    static constexpr auto append(unsigned_<I>) noexcept
    {
        return indices<Nums..., I>();
    }
};

constexpr indices<> make_indices(unsigned_<0>) noexcept
{
    return indices<>();
}

template < unsigned I >
constexpr auto make_indices(unsigned_<I>) noexcept
{
    using prev = unsigned_<I-1>;
    return make_indices(prev()).append(prev());
}

} //ns impl


template < unsigned Size >
constexpr auto make_string(char const (&)[Size]) noexcept;

template < unsigned Size >
struct static_string
{
    using data_type = char const (&)[Size+1];

    constexpr unsigned size() const noexcept { return Size; }
    constexpr data_type c_str() const noexcept { return arr; }
    //operator [] is valid to be called with size() - returns terminating null
    constexpr char operator[](size_t i) const noexcept { return arr[i]; }
    constexpr char& operator[](size_t i) noexcept { return arr[i]; }
    constexpr static_string<Size+1> push_back(char c) const noexcept { return push_back(c, impl::make_indices(impl::unsigned_<Size>())); }
    constexpr static_string<Size+1> push_front(char c) const noexcept { return push_front(c, impl::make_indices(impl::unsigned_<Size>())); }

    template < unsigned I >
    constexpr static_string<Size+I-1> append(char const (&cstr)[I]) const noexcept
    { return append(impl::make_indices(impl::unsigned_<Size>()), impl::make_indices(impl::unsigned_<I-1>()), cstr); }

    template < unsigned I >
    constexpr static_string<Size+I> append(static_string<I> const& s) const noexcept
    { return append(impl::make_indices(impl::unsigned_<Size>()), impl::make_indices(impl::unsigned_<I>()), s.c_str()); }

    template < unsigned N >
    constexpr static_string<Size*N> repeat() const noexcept
    { if constexpr (N==0) return {}; else if constexpr (N==1) return *this; else return append(repeat<N-1>()); }


    constexpr static_string(char const (&str)[Size+1]) noexcept
        : static_string(str, impl::make_indices(impl::unsigned_<Size>())) {}

    constexpr static_string() noexcept : arr{} {}

private:
    char arr[Size+1];

    template < unsigned I, unsigned ... Indices >
    constexpr static_string(char const (&str)[I], impl::indices<Indices...>) noexcept
        : arr{ str[Indices]..., '\0' } {}

    template < unsigned ... Indices >
    constexpr static_string<Size+1> push_back(char c, impl::indices<Indices...>) const noexcept
    {
        char const newArr[] = { arr[Indices]..., c, '\0' };
        return static_string<Size+1>(newArr);
    }

    template < unsigned ... Indices >
    constexpr static_string<Size+1> push_front(char c, impl::indices<Indices...>) const noexcept
    {
        char const newArr[] = { c, arr[Indices]..., '\0' };
        return static_string<Size+1>(newArr);
    }

    template < unsigned ... ThisIndices, unsigned ... ThatIndices, unsigned I >
        constexpr static_string<Size+I-1> append(impl::indices<ThisIndices...>,
                                                 impl::indices<ThatIndices...>,
                                                 char const (&cstr)[I]) const noexcept
    {
        char const newArr[] = { arr[ThisIndices]..., cstr[ThatIndices]..., '\0' };
        return static_string<Size+I-1>(newArr);
    }
};

template < unsigned Size >
constexpr auto make_string(char const (&cstr)[Size]) noexcept
{
    return static_string<Size-1>(cstr);
}

template < unsigned I >
constexpr static_string<I + 1> operator + (static_string<I> const& s, char c) noexcept
{
    return s.push_back(c);
}

template < unsigned I >
constexpr static_string<I + 1> operator + (char c, static_string<I> const& s) noexcept
{
    return s.push_front(c);
}

template < unsigned I0, unsigned I1>
constexpr static_string<I0+I1> operator + (static_string<I0> const& s0, static_string<I1> const& s1) noexcept
{
    return s0.append(s1);
}

template <unsigned I0, unsigned I1>
constexpr bool operator == (static_string<I0> const& s0, static_string<I1> const& s1) noexcept
{
    if constexpr (I0!=I1) return false;
    else if constexpr (I0==0) return true;
    else for (unsigned u = 0; u<I0; u++)
        if (s0.c_str()[u]!=s1.c_str()[u]) return false;
    return true;
}

template <unsigned I0, unsigned I1>
constexpr bool operator == (static_string<I0> const &s0, const char (&s1)[I1]) noexcept {
    return make_string(s1)==s0;
}

template <unsigned I0, unsigned I1>
constexpr bool operator == (const char(&s1)[I1], static_string<I0> const &s0) noexcept {
    return make_string(s1)==s0;
}


inline std::string print_double(double d) {
    char s[40];
    snprintf(s, sizeof(s), "%.8g", d);
    std::string ret = s;    
    if (ret.find('e') != std::string::npos) return ret; //scientific notation, we are done
    if (ret.find('.') != std::string::npos) //has a dot, remove trailing zeros
        while (ret.size() > 1 && ret.back() == '0')
            ret.pop_back();
    else if (ret.size() && '0'<=ret.back() && ret.back()<='9') //No dot, but number - append one to indicate double value.
        ret.push_back('.');
    return ret;
}

/** Creates a tuple, where lvalue references are kept as references,
 * plain values/rvalue references are copied.
 * So syaing uf::tie(a, b); will create a tuple of two references (cheap)
 * But you can also say uf::tie(a, 3); which will create a tuple of a ref and an int.
 * See https://stackoverflow.com/questions/33421323/remove-rvalueness-keep-lvalue-references-standard-type-trait-available
 * on why this works.*/
template<typename... Elements>
constexpr auto tie(Elements&&... elements)
{
    return std::tuple<Elements...>(std::forward<Elements>(elements)...);
}

namespace impl
{
template <typename Tuple, typename F, std::size_t ...Indices>
constexpr void for_each_with_index_impl(Tuple &&tuple, F &&f, std::index_sequence<Indices...>) {
    using swallow = int[];
    (void)swallow { 1, (f(Indices, std::get<Indices>(std::forward<Tuple>(tuple))), void(), int{})... };
}
template <typename Tuple, typename F, std::size_t ...Indices>
constexpr void for_each_impl(Tuple &&tuple, F &&f, std::index_sequence<Indices...>) {
    using swallow = int[];
    (void)swallow { 1, (f(std::get<Indices>(std::forward<Tuple>(tuple))), void(), int{})... };
}
}  //ns impl

/** Calls 'f' for all elements of the 'tuple'. (Usually a generic lambda)
 * 'f' shall have the signature: f(size_t index, auto element).*/
template <typename Tuple, typename F>
constexpr void for_each_with_index(Tuple &&tuple, F &&f) {
    constexpr std::size_t N = std::tuple_size<std::remove_reference_t<Tuple>>::value;
    impl::for_each_with_index_impl(std::forward<Tuple>(tuple), std::forward<F>(f),
                  std::make_index_sequence<N>{});
}

/** Calls 'f' for all elements of the 'tuple'. (Usually a generic lambda)
 * 'f' shall have the signature: f(auto element).*/
//https://codereview.stackexchange.com/a/163802
template <typename Tuple, typename F>
constexpr void for_each(Tuple &&tuple, F &&f) {
    constexpr std::size_t N = std::tuple_size<std::remove_reference_t<Tuple>>::value;
    impl::for_each_impl(std::forward<Tuple>(tuple), std::forward<F>(f),
                  std::make_index_sequence<N>{});
}

struct any;
struct any_view;
struct error_value;
//We disallow exception types (error_value is one and expected<T> types)
template <typename T, typename = std::enable_if_t<!std::is_base_of<std::exception, T>::value>>
class expected;

/** Helper type to deserialize something we have serialized as a string.
 * Disallows desrializing a view. (After this call the serialized data is gone.)
 * Provide a lambda that takes a string_view*/
template<typename Func>
struct string_deserializer
{
    static_assert(std::is_invocable_v<Func, std::string_view>, "The lambda provided must take a single string_view argument.");
public:
    Func receiver;
    static constexpr bool is_noexcept = noexcept(std::declval<Func>()(std::declval<std::string_view>()));
    string_deserializer(Func f) noexcept : receiver(std::move(f)) {}
};

/** Helper type to deserialize something we have serialized as a string.
 * Allows deserializing a view. Provide a lambda that takes a string_view*/
template<typename Func>
struct string_deserializer_view
{
    static_assert(std::is_invocable_v<Func, std::string_view>, "The lambda provided must take a single string_view argument.");
public:
    Func receiver;
    static constexpr bool is_noexcept = noexcept(std::declval<Func>()(std::declval<std::string_view>()));
    string_deserializer_view(Func f) noexcept : receiver(f) {}
};

template <typename T>
constexpr bool allow_auto_serialization = true;

namespace impl
{

//** Helper template to check if a type is a basic serializable type or not.*/
template <typename T>
struct is_serializable_primitive : std::false_type {};
template<> struct is_serializable_primitive<bool> : std::true_type {};
template<> struct is_serializable_primitive<unsigned char> : std::true_type {};
template<> struct is_serializable_primitive<signed char> : std::true_type {};
template<> struct is_serializable_primitive<char> : std::true_type {};
template<> struct is_serializable_primitive<uint16_t> : std::true_type {};
template<> struct is_serializable_primitive<int16_t> : std::true_type {};
template<> struct is_serializable_primitive<uint32_t> : std::true_type {};
template<> struct is_serializable_primitive<int32_t> : std::true_type {};
template<> struct is_serializable_primitive<uint64_t> : std::true_type {};
template<> struct is_serializable_primitive<int64_t> : std::true_type {};
template<> struct is_serializable_primitive<float> : std::true_type {};
template<> struct is_serializable_primitive<double> : std::true_type {};
template<> struct is_serializable_primitive<long double> : std::true_type {};
template<> struct is_serializable_primitive<std::string> : std::true_type {};
template<> struct is_serializable_primitive<any> : std::true_type {};

template <typename T> struct is_string_deserializer : std::false_type {};
template <typename T> struct is_string_deserializer<string_deserializer<T>> : std::true_type {};

template <typename T> struct is_string_deserializer_view : std::false_type {};
template <typename T> struct is_string_deserializer_view<string_deserializer<T>> : std::true_type {};
template <typename T> struct is_string_deserializer_view<string_deserializer_view<T>> : std::true_type {};

template <typename T>
struct is_serializable_view_primitive : std::false_type {};
template<> struct is_serializable_view_primitive<std::string_view> : std::true_type {};
template<> struct is_serializable_view_primitive<any_view> : std::true_type {};

template <typename T> struct is_smart_ptr : std::false_type {};
template <typename T> struct is_smart_ptr<typename std::unique_ptr<T>> : std::true_type {};
template <typename T> struct is_smart_ptr<typename std::shared_ptr<T>> : std::true_type {};

template <typename T> struct is_optional : std::false_type {};
template <typename T> struct is_optional<typename std::optional<T>> : std::true_type {};

template <typename T> struct is_expected : std::false_type {};
template <typename T> struct is_expected<typename uf::expected<T>> : std::true_type {};

/** Helper template to detect if something has size() member*/
template <typename T, typename = void>
struct has_size_member : std::false_type {};
template<typename T>
struct has_size_member<T, std::void_t<decltype(std::declval<T>().size())>> : std::true_type {};

/** Helper template to detect if something has reserve() member*/
template <typename T, typename = void>
struct has_reserve_member : std::false_type {};
template<typename T>
struct has_reserve_member<T, std::void_t<decltype(std::declval<T>().reserve(size_t(0)))>> : std::true_type {};

using std::begin;
using std::end;

/** Helper template to detect if something has value_type, begin(), end(), size()
 * plus key_type and mapped_type. Those will be serialized as a map 'm'.
 * both map and unordered_map has them.
 * This is a superset of is_serializable_container<>.*/
template<typename T, typename _ = void>
struct is_map_container : std::false_type {};

template<typename T>
struct is_map_container <
    T,
    std::void_t<
        typename T::value_type,
        typename T::key_type,
        typename T::mapped_type,
        decltype(std::declval<T>().size()),
        decltype(std::declval<T>().begin()),
        decltype(std::declval<T>().end())
    >
> : std::true_type {};

/** Helper template to detect if something has begin() and end() (global functions)
 * and value_type. This latter is false for C arrays.*/
template<typename T, typename = void>
struct is_serializable_container : std::false_type {};
template<typename T>
struct is_serializable_container<T, std::void_t<
    decltype(*begin(std::declval<const T>())), //we have a dereferencable type from begin
    decltype(end(std::declval<const T>()))
>> : std::true_type {};

/** Helper to get the size of a serializable container/range. */
template<typename T, typename = std::enable_if_t<is_serializable_container<T>::value>>
ptrdiff_t size(const T &o) {
    if constexpr (has_size_member<T>::value) return o.size();
    else return std::distance(end(o), begin(o));
}

/** Helper to get the value type of a serializable container/range. */
template <typename T, typename = void>
struct serializable_value_type {};
template<typename T>
struct serializable_value_type<T, std::enable_if_t<is_serializable_container<T>::value>>
{ using type = std::remove_reference_t<decltype(*begin(std::declval<const T>()))>; };

/** Helper template to detect if something has value_type, begin(), end() and
 * plus clear() and insert(iteraror value_type&&) */
template<typename T, typename = void>
struct is_deserializable_container_with_insert_itr : std::false_type {};
template<typename T>
struct is_deserializable_container_with_insert_itr<T, std::void_t<
    decltype(*begin(std::declval<T>())), //we have a dereferencable type from begin
    decltype(end(std::declval<T>())),    //has end
    decltype(std::declval<T>().clear()), //has clear
    decltype(std::declval<T>().insert(begin(std::declval<T>()), std::move(*begin(std::declval<T>())))) //has insert(iterator, element)
>> : std::true_type {};

/** Helper template to detect if something has value_type, begin(), end() and
 * plus clear() and insert(iterator, value_type&&) */
template<typename T, typename = void>
struct is_deserializable_container_with_insert : std::false_type {};
template<typename T>
struct is_deserializable_container_with_insert<T, std::void_t<
    decltype(*begin(std::declval<T>())), //we have a dereferencable type from begin
    decltype(end(std::declval<T>())),    //has end
    decltype(std::declval<T>().clear()), //has clear
    decltype(std::declval<T>().insert(std::move(*begin(std::declval<T>())))) //has insert(element)
>> : std::true_type {};

/** Helper template to detect if something has value_type, begin(), end() and
 * plus clear() and push_back(value_type&&) */
template<typename T, typename = void>
struct is_deserializable_container_with_push_back : std::false_type {};
template<typename T>
struct is_deserializable_container_with_push_back<T, std::void_t<
    decltype(*begin(std::declval<T>())), //we have a dereferencable type from begin
    decltype(end(std::declval<T>())),    //has end
    decltype(std::declval<T>().clear()), //has clear
    decltype(std::declval<T>().push_back(std::move(*begin(std::declval<T>())))) //has push_back(element)
>> : std::true_type {};

template<typename T, typename = void>
struct is_deserializable_container : std::false_type {};
template<typename T>
struct is_deserializable_container<T, std::enable_if_t<
    is_deserializable_container_with_insert_itr<T>::value ||
    is_deserializable_container_with_insert<T>::value ||
    is_deserializable_container_with_push_back<T>::value
>> : std::true_type {};

/** Helper to get the value type of a serializable container/range. */
template <typename T, typename = void>
struct deserializable_value_type {};
template<typename T>
struct deserializable_value_type<T, std::enable_if_t<is_deserializable_container<T>::value && !is_map_container<T>::value>>
{ using type = std::remove_cvref_t<decltype(*begin(std::declval<T>()))>; };
template<typename T>
struct deserializable_value_type<T, std::enable_if_t<is_deserializable_container<T>::value && is_map_container<T>::value>>
{ using type = std::pair<typename T::key_type, typename T::mapped_type>; };

template<typename T, typename = std::enable_if_t<is_deserializable_container<T>::value>>
void add_element_to_container(T&container, typename deserializable_value_type<T>::type &&element)
{
    if constexpr (is_deserializable_container_with_push_back<T>::value) container.push_back(std::move(element));
    else if constexpr (is_deserializable_container_with_insert<T>::value) container.insert(std::move(element));
    else container.insert(container.end(), std::move(element));
}

static_assert(is_deserializable_container<std::map<int, int>>::value);
static_assert(is_deserializable_container<std::vector<int>>::value);
static_assert(std::is_same_v<deserializable_value_type<std::map<int, double>>::type, std::pair<int, double>>);
static_assert(std::is_same_v<deserializable_value_type<std::vector<int>>::type, int>);

template <typename T>
constexpr bool is_really_auto_serializable_v = 
      std::is_class<std::remove_cvref_t<T>>::value
  &&  std::is_aggregate<std::remove_cvref_t<T>>::value
  && !std::is_empty<std::remove_cvref_t<T>>::value
  && !is_serializable_container<T>::value
  && !is_deserializable_container<T>::value
  &&  allow_auto_serialization<T>                                           //user provided input outside T
  && !requires { requires std::is_void_v<typename T::auto_serialization>; } //user provided input inside T
  && !requires (const T &t) { tuple_for_serialization(t); }                 //any tuple_for_serialization(void) free... 
  && !requires (const T &t) { t.tuple_for_serialization(); };               //...or member fn prevents auto serialization

/** Helper to return an lvalue reference to a type.*/
template<typename T>
inline typename std::add_lvalue_reference<T>::type decllval() noexcept {
    struct s { static constexpr typename std::add_lvalue_reference<T>::type __delegate(); };
    return s::__delegate();
}

/** Helper to ignore unused parameter pack argument. (void)pack...; does not work.*/
struct ignore_pack { template<typename ...Args> ignore_pack(Args const & ...) {} };

//tuple_for_serialization
/** Helper template to check if a struct has tuple_for_serialization() member (const)*/
template<typename T, typename tag=void, typename = void> struct has_tuple_for_serialization_member_ser : std::false_type {};
template<typename T> struct has_tuple_for_serialization_member_ser<T, void, std::void_t<decltype(decllval<std::add_const_t<std::remove_cvref_t<T>>>().tuple_for_serialization())>> : std::true_type {};
template<typename T, typename tag> struct has_tuple_for_serialization_member_ser<T, tag, std::void_t<decltype(decllval<std::add_const_t<std::remove_cvref_t<T>>>().tuple_for_serialization(std::declval<tag>()))>> : std::true_type {};

/** Helper template to check if a struct has tuple_for_serialization() member (non-const)*/
template<typename T, typename tag = void, typename = void> struct has_tuple_for_serialization_member_deser : std::false_type {};
template<typename T> struct has_tuple_for_serialization_member_deser<T, void, std::void_t<decltype(decllval<std::remove_cvref_t<T>>().tuple_for_serialization())>> {
    static constexpr bool has_const = has_tuple_for_serialization_member_ser<T>::value; //if 'T::tuple_for_serialization() const' exists
    using plainT = std::remove_cvref_t<T>;
    //const T if 'T::tuple_for_serialization() const' exists, else T.
    //This is to prevent trying to find a T::tuple_for_serialization() const below if it does not exist
    using constT = std::conditional_t<has_const, std::add_const_t<plainT>, plainT>; 
    static constexpr bool value = !has_const || 
        !std::is_same_v<decltype(decllval<plainT>().tuple_for_serialization()), 
                        decltype(decllval<constT>().tuple_for_serialization())>;
};
template<typename T, typename tag> struct has_tuple_for_serialization_member_deser<T, tag, std::void_t<decltype(decllval<std::remove_cvref_t<T>>().tuple_for_serialization(std::declval<tag>()))>> {
    static constexpr bool has_const = has_tuple_for_serialization_member_ser<T, tag>::value; //if 'T::tuple_for_serialization() const' exists
    using plainT = std::remove_cvref_t<T>;
    //const T if 'T::tuple_for_serialization() const' exists, else T.
    //This is to prevent trying to find a T::tuple_for_serialization() const below if it does not exist
    using constT = std::conditional_t<has_const, std::add_const_t<plainT>, plainT>; 
    static constexpr bool value = !has_const ||
        !std::is_same_v<decltype(decllval<plainT>().tuple_for_serialization(std::declval<tag>())),
                        decltype(decllval<constT>().tuple_for_serialization(std::declval<tag>()))>;
};

/** Free function calling the tuple_for_serialization (const) member.*/
template <typename T, typename = std::enable_if_t<has_tuple_for_serialization_member_ser<T>::value>>
decltype(auto) tuple_for_serialization(const T& t) noexcept(noexcept(t.tuple_for_serialization())) { return t.tuple_for_serialization(); }
/** Free function calling the tuple_for_serialization (const) member with a tag.*/
template <typename T, typename tag, typename = std::enable_if_t<has_tuple_for_serialization_member_ser<T, tag>::value>>
decltype(auto) tuple_for_serialization(const T &t, tag tg) noexcept(noexcept(t.tuple_for_serialization(tg))) { return t.tuple_for_serialization(tg); }

/** Free function calling the tuple_for_serialization (non-const) member.*/
template <typename T, typename = std::enable_if_t<has_tuple_for_serialization_member_deser<T>::value && !std::is_const_v<std::remove_reference_t<T>>>>
decltype(auto) tuple_for_serialization(T& t) noexcept(noexcept(t.tuple_for_serialization())) { return t.tuple_for_serialization(); }
/** Free function calling the tuple_for_serialization (non-const) member with a tag.*/
template <typename T, typename tag, typename = std::enable_if_t<has_tuple_for_serialization_member_deser<T, tag>::value && !std::is_const_v<std::remove_reference_t<T>>>>
decltype(auto) tuple_for_serialization(T &t, tag tg) noexcept(noexcept(t.tuple_for_serialization(tg))) { return t.tuple_for_serialization(tg); }

/** Helper template to check if a struct has tuple_for_serialization() free function*/
template<typename T, typename tag = void, typename = void> struct has_tuple_for_serialization_ser : std::false_type {};
template<typename T> struct has_tuple_for_serialization_ser<T, void, std::void_t<decltype(tuple_for_serialization(decllval<const T>()))>> : std::true_type {};
template<typename T, typename tag> struct has_tuple_for_serialization_ser<T, tag, std::void_t<decltype(tuple_for_serialization(decllval<const T>(), std::declval<tag>()))>> : std::true_type {};
template<typename T, typename tag = void, typename = void> struct has_tuple_for_serialization_deser : std::false_type {};
template<typename T> struct has_tuple_for_serialization_deser<T, void, std::void_t<decltype(tuple_for_serialization(decllval<std::remove_cvref_t<T>>()))>> {
    static constexpr bool has_const = has_tuple_for_serialization_ser<T>::value; //if 'T::tuple_for_serialization() const' exists
    using plainT = std::remove_cvref_t<T>;
    //const T if 'T::tuple_for_serialization() const' exists, else T.
    //This is to prevent trying to find a T::tuple_for_serialization() const below if it does not exist
    using constT = std::conditional_t<has_const, std::add_const_t<plainT>, plainT>; //const T if 'T::tuple_for_serialization() const' exists, else T.
    static constexpr bool value = !has_const ||
        !std::is_same_v<decltype(tuple_for_serialization(decllval<constT>())),
                        decltype(tuple_for_serialization(decllval<plainT>()))>;
};
template<typename T, typename tag> struct has_tuple_for_serialization_deser<T, tag, std::void_t<decltype(tuple_for_serialization(decllval<std::remove_cvref_t<T>>(), std::declval<tag>()))>> {
    static constexpr bool has_const = has_tuple_for_serialization_ser<T, tag>::value; //if 'T::tuple_for_serialization() const' exists
    using plainT = std::remove_cvref_t<T>;
    //const T if 'T::tuple_for_serialization() const' exists, else T.
    //This is to prevent trying to find a T::tuple_for_serialization() const below if it does not exist
    using constT = std::conditional_t<has_const, std::add_const_t<plainT>, plainT>; //const T if 'T::tuple_for_serialization() const' exists, else T.
    static constexpr bool value = !has_const ||
        !std::is_same_v<decltype(tuple_for_serialization(decllval<constT>(), std::declval<tag>())),
                        decltype(tuple_for_serialization(decllval<plainT>(), std::declval<tag>()))>;
};

struct no_applicable_tag {};

/** Given a type and a trait function (has_tuple_for_serialization_ser, 
 * has_tuple_for_serialization_deser, has_before_serialization,
 * has_after_serialization, etc...) and a set of tags packed in a tuple, 
 * we return the first tag the type T has the trait function with.
 * We define both a type member and an index member that holds the 
 * (zero based) index of the selected tag in the param pack.
 * If the type has no tagged trait function with any of the tags, 
 * we return void (and -1) if the type has an untagged such function
 * and no_applicable_tag (and -2) if it has not.*/
template <typename T, template<typename, typename, typename> typename trait, typename tag_tuple> struct select_tag_tuple {
    static_assert(std::is_same_v<std::tuple<>, tag_tuple>, //dummy test for making this template parameter dependent.
                  "select_tag_tuple instantiated with a non-tuple third argument.");
};
template <typename T, template<typename, typename, typename> typename trait>
struct select_tag_tuple<T, trait, std::tuple<>> {
    using type = typename std::conditional_t<trait<T, void, void>::value, void, no_applicable_tag>;
    static constexpr int index = trait<T, void, void>::value ? -1 : -2;
};
template <typename T, template<typename, typename, typename> typename trait, typename head_tag, typename ...trail_tags>
struct select_tag_tuple<T, trait, std::tuple<head_tag, trail_tags...>> {
    using type = typename std::conditional_t<trait<T, head_tag, void>::value, head_tag,
        typename select_tag_tuple<T, trait, std::tuple<trail_tags...>>::type>;
    static constexpr int index = trait<T, head_tag, void>::value ? 0 :
        select_tag_tuple<T, trait, std::tuple<trail_tags...>>::index + 
        (select_tag_tuple<T, trait, std::tuple<trail_tags...>>::index>=0 ? 1 : 0); //dont increment -1 and -2
};

/** Given a type and a trait function (has_tuple_for_serialization_ser,
 * has_tuple_for_serialization_deser, has_before_serialization,
 * has_after_serialization, etc...) and a set of tags,
 * we return the first tag the type T has the trait function with.
 * We define both a type member and an index member that holds the
 * (zero based) index of the selected tag in the param pack.
 * If the type has no tagged trait function with any of the tags,
 * we return void (and -1) if the type has an untagged such function
 * and no_applicable_tag (and -2) if it has not.*/
template <typename T, template<typename, typename, typename> typename trait, typename ...tags> 
using select_tag = select_tag_tuple<T, trait, std::tuple<tags...>>;

/** True if type 'T' has a trait function (use trait types, like e.g., has_before_serialization)
 * with one of 'tags'.*/
template <typename T, template<typename, typename, typename> typename trait, typename ...tags>
inline constexpr bool has_trait_v = !std::is_same_v<typename select_tag<T, trait, tags...>::type, no_applicable_tag>;
/** True if type 'T' has a trait function (use trait types, like e.g., has_before_serialization)
 * with one member of 'tag_tuple'.*/
template <typename T, template<typename, typename, typename> typename trait, typename tag_tuple>
inline constexpr bool has_trait_tup_v = !std::is_same_v<typename select_tag_tuple<T, trait, tag_tuple>::type, no_applicable_tag>;

template <typename T, typename ...tags>
inline constexpr bool has_tuple_for_serialization_ser_tags_v = has_trait_v<T, has_tuple_for_serialization_ser, tags...>;
template <typename T, typename ...tags>
inline constexpr bool has_tuple_for_serialization_deser_tags_v = has_trait_v<T, has_tuple_for_serialization_deser, tags...>;
template <bool deser, typename T, typename ...tags>
inline constexpr bool has_tuple_for_serialization_v = deser ? has_tuple_for_serialization_deser_tags_v<T, tags...> : has_tuple_for_serialization_ser_tags_v<T, tags...>;
template<bool deser, typename T, typename ...tags>
struct has_tuple_for_serialization : std::conditional_t<has_tuple_for_serialization_v<deser, T, tags...>, std::true_type, std::false_type> {};

template <typename T, typename tag_tuple>
inline constexpr bool has_tuple_for_serialization_ser_tuptags_v = has_trait_tup_v<T, has_tuple_for_serialization_ser, tag_tuple>;
template <typename T, typename tag_tuple>
inline constexpr bool has_tuple_for_serialization_deser_tuptags_v = has_trait_tup_v<T, has_tuple_for_serialization_deser, tag_tuple>;
template <bool deser, typename T, typename tag_tuple>
inline constexpr bool has_tuple_for_serialization_tup_v = deser ? has_tuple_for_serialization_deser_tuptags_v<T, tag_tuple> : has_tuple_for_serialization_ser_tuptags_v<T, tag_tuple>;
template<bool deser, typename T, typename tag_tuple>
struct has_tuple_for_serialization_tup : std::conditional_t<has_tuple_for_serialization_tup_v<deser, T, tag_tuple>, std::true_type, std::false_type> {};

template <typename T, typename ...tags>
constexpr bool has_noexcept_tuple_for_serialization_deser_f() {
    constexpr int index = select_tag<T, has_tuple_for_serialization_deser, tags...>::index;
    if constexpr (index==-1) return noexcept(tuple_for_serialization(decllval<T>()));
    else if constexpr (index>=0) return noexcept(tuple_for_serialization(decllval<T>(), std::declval<std::tuple_element_t<index, std::tuple<tags...>>>()));
    else return true;
}

template <typename T, typename ...tags>
constexpr bool has_noexcept_tuple_for_serialization_ser_f() {
    constexpr int index = select_tag<T, has_tuple_for_serialization_ser, tags...>::index;
    if constexpr (index==-1) return noexcept(tuple_for_serialization(decllval<const T>()));
    else if constexpr (index>=0) return noexcept(tuple_for_serialization(decllval<const T>(), std::declval<std::tuple_element_t<index, std::tuple<tags...>>>()));
    else return true;
}

template <typename T, typename ...tags>
decltype(auto) invoke_tuple_for_serialization(T &t, tags... tt) //decltype(auto) perfect forwards the return value
    noexcept(has_noexcept_tuple_for_serialization_deser_f<T, tags...>())
{ 
    ignore_pack(tt...);
    constexpr int index = select_tag<T, has_tuple_for_serialization_deser, tags...>::index;
    if constexpr (index==-1)
        return tuple_for_serialization(t);
    if constexpr (index>=0)
        //This formulation perfectly forwards an argument from a tuple
        return tuple_for_serialization(t, std::get<index>(std::make_tuple(tt...)));
}
template <typename T, typename ...tags>
decltype(auto) invoke_tuple_for_serialization(const T &t, tags... tt) 
    noexcept(has_noexcept_tuple_for_serialization_ser_f<T, tags...>())
{
    ignore_pack(tt...);
    constexpr int index = select_tag<T, has_tuple_for_serialization_ser, tags...>::index;
    if constexpr (index==-1)
        return tuple_for_serialization(t);
    else if constexpr (index>=0)
        return tuple_for_serialization(t, std::get<index>(std::make_tuple(tt...)));
}

template <typename T, typename ...tags>
decltype(auto) invoke_tuple_for_serialization_tup(T &t, std::tuple<tags...> tt) 
    noexcept(has_noexcept_tuple_for_serialization_deser_f<T, tags...>())
{
    (void)tt;
    constexpr int index = select_tag<T, has_tuple_for_serialization_deser, tags...>::index;
    if constexpr (index==-1)
        return tuple_for_serialization(t);
    if constexpr (index>=0)
        //This formulation perfectly forwards an argument from a tuple
        return tuple_for_serialization(t, std::get<index>(tt));
}
template <typename T, typename ...tags>
decltype(auto) invoke_tuple_for_serialization_tup(const T &t, std::tuple<tags...> tt) 
    noexcept(has_noexcept_tuple_for_serialization_ser_f<T, tags...>())
{
    (void)tt;
    constexpr int index = select_tag<T, has_tuple_for_serialization_ser, tags...>::index;
    if constexpr (index==-1)
        return tuple_for_serialization(t);
    else if constexpr (index>=0)
        return tuple_for_serialization(t, std::get<index>(tt));
}

//before_serialization
/** Helper template to check if a struct has before_serialization() member
 * Note, it will be true even if it is a non-const member. We will likely fail to invoke it for
 * a const T during serialization - but at least we gave an error to the user.*/
template<typename T, typename tag = void, typename = void> struct has_before_serialization_member : std::false_type {};
template<typename T> struct has_before_serialization_member<T, void, std::void_t<decltype(std::declval<const T>().before_serialization())>> : std::true_type {};
template<typename T, typename tag> struct has_before_serialization_member<T, tag, std::void_t<decltype(std::declval<const T>().before_serialization(std::declval<tag>()))>> : std::true_type {};

/** Free function calling the member.*/
template <typename T, typename = std::enable_if_t<has_before_serialization_member<T, void>::value>>
void before_serialization(const T& t) noexcept(noexcept(t.before_serialization())) { t.before_serialization(); }
/** Free function calling the member.*/
template <typename T, typename tag, typename = std::enable_if_t<has_before_serialization_member<T, tag>::value>>
void before_serialization(const T &t, tag tt) noexcept(noexcept(t.before_serialization(tt))) { t.before_serialization(tt); }

/** Helper template to check if a struct has before_serialization() or before_serialization(tag) free function
 * Note, it will be true even if it is a non-const member. We will likely fail to invoke it for
 * a const T during serialization - but at least we gave an error to the user.*/
template<typename T, typename tag = void, typename = void> struct has_before_serialization_tag : std::false_type {};
template<typename T> struct has_before_serialization_tag<T, void, std::void_t<decltype(before_serialization(decllval<const T>()))>> : std::true_type {};
template<typename T, typename tag> struct has_before_serialization_tag<T, tag, std::void_t<decltype(before_serialization(decllval<const T>(), std::declval<tag>()))>> : std::true_type {};

template <typename T, typename tag_tuple>
inline constexpr bool has_before_serialization_tup_v = has_trait_tup_v<T, has_before_serialization_tag, tag_tuple>;
template <typename T, typename ...tags>
inline constexpr bool has_before_serialization_v = has_trait_v<T, has_before_serialization_tag, tags...>;

template <typename T, typename ...tags>
constexpr bool has_noexcept_before_serialization_f() {
    constexpr int index = select_tag<T, has_before_serialization_tag, tags...>::index;
    if constexpr (index==-1) return noexcept(before_serialization(decllval<const T>()));
    else if constexpr (index>=0) return noexcept(before_serialization(decllval<const T>(), std::declval<std::tuple_element_t<index, std::tuple<tags...>>>()));
    else return true;
}

template <typename T, typename ...tags>
void invoke_before_serialization(const T &t, tags... tt) noexcept (has_noexcept_before_serialization_f<T, tags...>()) {
    ignore_pack(tt...);
    constexpr int index = select_tag<T, has_before_serialization_tag, tags...>::index;
    if constexpr (index==-1)
        return before_serialization(t);
    else if constexpr (index>=0)
        return before_serialization(t, std::get<index>(std::make_tuple(tt...)));
}

//after_serialization
/** Helper template to check if a struct has after_serialization(bool) member
 * Note, it will be true even if it is a non-const member. We will likely fail to invoke it for 
 * a const T during serialization - but at least we gave an error to the user.*/
template<typename T, typename tag = void, typename = void> struct has_after_serialization_member : std::false_type {};
template<typename T> struct has_after_serialization_member<T, void, std::void_t<decltype(std::declval<const T>().after_serialization(true))>> : std::true_type {};
template<typename T, typename tag> struct has_after_serialization_member<T, tag, std::void_t<decltype(std::declval<const T>().after_serialization(true, std::declval<tag>()))>> : std::true_type {};

/** Free function calling the member.*/
template <typename T, typename = std::enable_if_t<has_after_serialization_member<T, void>::value>>
void after_serialization(const T& t, bool success) noexcept { t.after_serialization(success);
    static_assert(noexcept(t.after_serialization(true)), "after_serialization(bool) functions must be noexcept."); }
template <typename T, typename tag, typename = std::enable_if_t<has_after_serialization_member<T, tag>::value>>
void after_serialization(const T &t, bool success, tag tt) noexcept {
    t.after_serialization(success, tt);
    static_assert(noexcept(t.after_serialization(true, tt)), "after_serialization(bool, tag) functions must be noexcept.");
}

/** Helper template to check if a struct has after_serialization(bool) free function with or without a tag
 *Note, it will be true even if it is only for a non-const T. We will likely fail to invoke it for
 * a const T during serialization - but at least we gave an error to the user.*/
template<typename T, typename tag = void, typename = void> struct has_after_serialization_tag : std::false_type {};
template<typename T> struct has_after_serialization_tag<T, void, std::void_t<decltype(after_serialization(decllval<const T>(), true))>> : std::true_type {};
template<typename T, typename tag> struct has_after_serialization_tag<T, tag, std::void_t<decltype(after_serialization(decllval<const T>(), true, std::declval<tag>()))>> : std::true_type {};

template <typename T, typename tag_tuple>
inline constexpr bool has_after_serialization_tup_v = has_trait_tup_v<T, has_after_serialization_tag, tag_tuple>;
template <typename T, typename ...tags>
inline constexpr bool has_after_serialization_v = has_trait_v<T, has_after_serialization_tag, tags...>;

template <typename T, typename ...tags>
void invoke_after_serialization(const T &t, bool success, tags... tt) noexcept {
    ignore_pack(tt...);
    constexpr int index = select_tag<T, has_after_serialization_tag, tags...>::index;
    if constexpr (index==-1) {
        return after_serialization(t, success);
        static_assert(noexcept(after_serialization(t, success)), "after_serialization(bool) functions must be noexcept.");
    } else if constexpr (index>=0) {
        return after_serialization(t, success, std::get<index>(std::make_tuple(tt...)));
        static_assert(noexcept(after_serialization(t, success, std::declval<std::tuple_element_t<index, std::tuple<tags...>>>())), "after_serialization(bool) functions must be noexcept.");
    }
}

//after_deserialization_error
/** Helper template to check if a struct has after_deserialization_error() member*/
template<typename T, typename tag = void, typename = void> struct has_after_deserialization_error_member : std::false_type {};
template<typename T> struct has_after_deserialization_error_member<T, void, std::void_t<decltype(std::declval<T>().after_deserialization_error())>> : std::true_type {};
template<typename T, typename tag> struct has_after_deserialization_error_member<T, tag, std::void_t<decltype(std::declval<T>().after_deserialization_error(std::declval<tag>()))>> : std::true_type {};

/** Free function calling the member.*/
template <typename T, typename = std::enable_if_t<has_after_deserialization_error_member<T, void>::value>>
void after_deserialization_error(T& t) noexcept(noexcept(std::declval<T>().after_deserialization_error())) { t.after_deserialization_error(); 
    static_assert(noexcept(t.after_serialization(true)), "after_deserialization_error() functions must be noexcept.");
}
/** Free function calling the member.*/
template <typename T, typename tag, typename = std::enable_if_t<has_after_deserialization_error_member<T, tag>::value>>
void after_deserialization_error(T &t, tag tt) noexcept(noexcept(std::declval<T>().after_deserialization_error(std::declval<tag>()))) { t.after_deserialization_error(tt);
    static_assert(noexcept(t.after_serialization(true)), "after_deserialization_error() functions must be noexcept.");
}

/** Helper template to check if a struct has after_deserialization_error() free function with or without tags*/
template<typename T, typename tag = void, typename = void> struct has_after_deserialization_error_tag : std::false_type {};
template<typename T> struct has_after_deserialization_error_tag<T, void, std::void_t<decltype(after_deserialization_error(decllval<T>()))>> : std::true_type {};
template<typename T, typename tag> struct has_after_deserialization_error_tag<T, tag, std::void_t<decltype(after_deserialization_error(decllval<T>(), std::declval<tag>()))>> : std::true_type {};

template <typename T, typename tag_tuple>
inline constexpr bool has_after_deserialization_error_tup_v = has_trait_tup_v<T, has_after_deserialization_error_tag, tag_tuple>;
template <typename T, typename ...tags>
inline constexpr bool has_after_deserialization_error_v = has_trait_v<T, has_after_deserialization_error_tag, tags...>;

template <typename T, typename ...tags>
void invoke_after_deserialization_error(T &t, tags... tt) noexcept {
    ignore_pack(tt...);
    static_assert(!std::is_const_v<T>, "No const for deserialization");
    constexpr int index = select_tag<T, has_after_deserialization_error_tag, tags...>::index;
    if constexpr (index==-1) {
        return after_deserialization_error(t);
        static_assert(noexcept(after_deserialization_error(t)), "after_deserialization_error() functions must be noexcept.");
    } else if constexpr (index>=0) {
        return after_deserialization_error(t, std::get<index>(std::make_tuple(tt...)));
        static_assert(noexcept(after_deserialization_error(t, std::declval<std::tuple_element_t<index, std::tuple<tags...>>>())), "after_deserialization_error() functions must be noexcept.");
    }
}

//after_deserialization_simple()
/** Helper template to check if a struct has after_deserialization_simple() member*/
template<typename T, typename tag = void, typename = void> struct has_after_deserialization_simple_member : std::false_type {};
template<typename T> struct has_after_deserialization_simple_member<T, void, std::void_t<decltype(std::declval<T>().after_deserialization_simple())>> : std::true_type {};
template<typename T, typename tag> struct has_after_deserialization_simple_member<T, tag, std::void_t<decltype(std::declval<T>().after_deserialization_simple(std::declval<tag>()))>> : std::true_type {};

/** Free function calling the member.*/
template <typename T, typename = std::enable_if_t<has_after_deserialization_simple_member<T, void>::value>>
void after_deserialization_simple(T& t) noexcept(noexcept(std::declval<T>().after_deserialization_simple())) { t.after_deserialization_simple(); }
/** Free function calling the member.*/
template <typename T, typename tag, typename = std::enable_if_t<has_after_deserialization_simple_member<T, tag>::value>>
void after_deserialization_simple(T &t, tag tt) noexcept(noexcept(std::declval<T>().after_deserialization_simple(std::declval<tag>()))) { t.after_deserialization_simple(tt); }

/** Helper template to check if a struct has after_deserialization_simple() free function*/
template<typename T, typename tag = void, typename = void> struct has_after_deserialization_simple_tag : std::false_type {};
template<typename T> struct has_after_deserialization_simple_tag<T, void, std::void_t<decltype(after_deserialization_simple(decllval<T>()))>> : std::true_type {};
template<typename T, typename tag> struct has_after_deserialization_simple_tag<T, tag, std::void_t<decltype(after_deserialization_simple(decllval<T>(), std::declval<tag>()))>> : std::true_type {};

template <typename T, typename tag_tuple>
inline constexpr bool has_after_deserialization_simple_tup_v = has_trait_tup_v<T, has_after_deserialization_simple_tag, tag_tuple>;
template <typename T, typename ...tags>
inline constexpr bool has_after_deserialization_simple_v = has_trait_v<T, has_after_deserialization_simple_tag, tags...>;

template <typename T, typename ...tags>
constexpr bool has_noexcept_after_deserialization_simple_f() {
    constexpr int index = select_tag<T, has_after_deserialization_simple_tag, tags...>::index;
    if constexpr (index==-1) return noexcept(after_deserialization_simple(decllval<T>()));
    else if constexpr (index>=0) return noexcept(after_deserialization_simple(decllval<T>(), std::declval<std::tuple_element_t<index, std::tuple<tags...>>>()));
    else return true;
}

template <typename T, typename ...tags>
void invoke_after_deserialization_simple(T &t, tags... tt) noexcept(has_noexcept_after_deserialization_simple_f<T,tags...>()) {
    ignore_pack(tt...);
    static_assert(!std::is_const_v<T>, "No const for deserialization");
    constexpr int index = select_tag<T, has_after_deserialization_simple_tag, tags...>::index;
    if constexpr (index==-1)
        return after_deserialization_simple(t);
    else if constexpr (index>=0)
        return after_deserialization_simple(t, std::get<index>(std::make_tuple(tt...)));
}


//after_deserialization(tuple_for_serialization())
/** Helper template to check if a struct has after_deserialization(U&) member, where U is the return type of tuple_for_serialization*/
template<typename T, typename tag = void, typename = void> struct has_after_deserialization_member : std::false_type {};
template<typename T> struct has_after_deserialization_member<T, void, std::void_t<decltype(std::declval<T>().after_deserialization(tuple_for_serialization(decllval<std::remove_cvref_t<T>>())))>> : std::true_type {};
template<typename T, typename tag> struct has_after_deserialization_member<T, tag, std::void_t<decltype(std::declval<T>().after_deserialization(tuple_for_serialization(decllval<std::remove_cvref_t<T>>()), std::declval<tag>(), std::declval<tag>()))>> : std::true_type {};

/** Free function calling the member.*/
template <typename T, typename = std::enable_if_t<has_after_deserialization_member<T, void>::value>>
void after_deserialization(T& t, std::remove_cvref_t<decltype(tuple_for_serialization(decllval<std::remove_cvref_t<T>>()))> &&tmp) 
   noexcept(noexcept(std::declval<T>().after_deserialization(std::declval<decltype(tmp)>())))
{ t.after_deserialization(std::move(tmp)); }
/** Free function calling the member.*/
template <typename T, typename tag, typename = std::enable_if_t<has_after_deserialization_member<T, tag>::value>>
void after_deserialization(T &t, std::remove_cvref_t<decltype(tuple_for_serialization(decllval<std::remove_cvref_t<T>>()))> &&tmp, tag tt) 
    noexcept(noexcept(std::declval<T>().after_deserialization(std::declval<decltype(tmp)>(), std::declval<tag>())))
{ t.after_deserialization(std::move(tmp), tt); }

/** Helper template to check if a struct has after_deserialization(U&) free function, where U is the return type of tuple_for_serialization*/
template<typename T, typename tag = void, typename = void> struct has_after_deserialization_tag : std::false_type {};
template<typename T> struct has_after_deserialization_tag<T, void, 
    std::void_t<decltype(after_deserialization(decllval<std::remove_cvref_t<T>>(),
                                               tuple_for_serialization(decllval<std::remove_cvref_t<T>>())))>> : std::true_type {};
//Note here that here we need an after_deserialization(U&&, tag), where U is the return value of tuple_serialization(tag).
//Thus we cannot have a tag-less tuple_for_serialization and an after_deserialization(tag) with a tag - the latter will not be 
//called.
//TODO: fix this and allow what is the natural behaviour.
template<typename T, typename tag> struct has_after_deserialization_tag<T, tag,
    std::void_t<decltype(after_deserialization(decllval<std::remove_cvref_t<T>>(), 
                                               tuple_for_serialization(decllval<std::remove_cvref_t<T>>(), std::declval<tag>()),
                                               std::declval<tag>()))>> : std::true_type {};

template <typename T, typename tag_tuple>
inline constexpr bool has_after_deserialization_tup_v = has_trait_tup_v<T, has_after_deserialization_tag, tag_tuple>;
template <typename T, typename ...tags>
inline constexpr bool has_after_deserialization_v = has_trait_v<T, has_after_deserialization_tag, tags...>;

template <typename T, typename ...tags>
constexpr bool has_noexcept_after_deserialization_f() {
    using tuple_type = decltype(invoke_tuple_for_serialization(decllval<T>(), std::declval<tags>()...));
    constexpr int index = select_tag<T, has_after_deserialization_tag, tags...>::index;
    if constexpr (index==-1) return noexcept(after_deserialization(decllval<T>(), std::declval<tuple_type>()));
    else if constexpr (index>=0) return noexcept(after_deserialization(decllval<T>(), std::declval<tuple_type>(), std::declval<std::tuple_element_t<index, std::tuple<tags...>>>()));
    else return true;
}

template <typename T, typename ...tags>
void invoke_after_deserialization(T &t, decltype(invoke_tuple_for_serialization(decllval<T>(), std::declval<tags>()...)) &&r, 
                                  tags... tt) 
    noexcept(noexcept(has_noexcept_after_deserialization_f<T, tags...>()))
{
    ignore_pack(tt...);
    static_assert(!std::is_const_v<T>, "No const for deserialization");
    constexpr int index = select_tag<T, has_after_deserialization_tag, tags...>::index;
    if constexpr (index==-1)
        return after_deserialization(t, std::move(r));
    else if constexpr (index>=0)
        return after_deserialization(t, std::move(r), std::get<index>(std::make_tuple(tt...)));
}



template <typename T> struct is_pair : std::false_type {};
template<typename A, typename B> struct is_pair<std::pair<A, B>> : std::true_type {};

template <typename T> struct is_char_array : std::false_type {};
template <size_t LEN> struct is_char_array<char[LEN]> : std::true_type {};
template <size_t LEN> struct is_char_array<const char[LEN]> : std::true_type {};

template <typename T> struct is_C_array : std::false_type {};
template <size_t LEN> struct is_C_array<char[LEN]> : std::false_type {}; //char arrays are treated as strings!!
template <size_t LEN> struct is_C_array<const char[LEN]> : std::false_type {}; //char arrays are treated as strings!!
template <typename U, size_t LEN> struct is_C_array<U[LEN]> : std::true_type {}; //char arrays are treated as strings!!

template <typename T> struct is_std_array : std::false_type {};
template <typename T, size_t L> struct is_std_array<std::array<T, L>> : std::true_type {};

template <typename T> struct is_tuple : std::false_type {};
template <typename ...T> struct is_tuple<std::tuple<T...>> : std::true_type {};

template <typename T> struct is_single_element_tuple : std::false_type {};
template <typename T> struct is_single_element_tuple<std::tuple<T>> : std::true_type {};

template <typename T> struct is_single_element_array : std::false_type {};
template <typename T> struct is_single_element_array<std::array<T, 1>> : std::true_type {};
template <typename T> struct is_single_element_array<T[1]> : std::true_type {};

template <typename T> struct tuple_split {};
template <typename T, typename ...TT> struct tuple_split<std::tuple<T, TT...>> { using first = T;  using rest = std::tuple<TT...>; };

template <bool des, typename T, typename, typename tag_tuple> struct is_void_like_tup_ : std::false_type {};
template <typename tag_tuple> struct is_void_like_tup_<false, void, void, tag_tuple> : std::true_type {};
template <typename tag_tuple> struct is_void_like_tup_<true, void, void, tag_tuple> : std::true_type {};
template <typename tag_tuple> struct is_void_like_tup_<false, std::tuple<>, void, tag_tuple> : std::true_type {};
template <typename tag_tuple> struct is_void_like_tup_<true, std::tuple<>, void, tag_tuple> : std::true_type {};
template <typename tag_tuple> struct is_void_like_tup_<false, std::monostate, void, tag_tuple> : std::true_type {};
template <typename tag_tuple> struct is_void_like_tup_<true, std::monostate, void, tag_tuple> : std::true_type {};
template <typename T, typename tag_tuple> struct is_void_like_tup_<false, std::array<T, 0>, void, tag_tuple> : std::true_type {};
template <typename T, typename tag_tuple> struct is_void_like_tup_<true, std::array<T, 0>, void, tag_tuple> : std::true_type {};
template <bool des, typename T, typename ...TT, typename tag_tuple> struct is_void_like_tup_<des, std::tuple<T, TT...>, void, tag_tuple>
    { static constexpr bool value = is_void_like_tup_<des, std::remove_cvref_t<T>, void, tag_tuple>::value && is_void_like_tup_<des, std::tuple<TT...>, void, tag_tuple>::value; };
template <bool des, typename T, typename tag_tuple> struct is_void_like_tup_<des, T, std::enable_if_t<!des && is_serializable_container<T>::value && !has_tuple_for_serialization_tup<des, T, tag_tuple>::value >, tag_tuple>
    { static constexpr bool value = is_void_like_tup_<des, typename serializable_value_type<T>::type, void, tag_tuple>::value; };
template <bool des, typename T, typename tag_tuple> struct is_void_like_tup_<des, T, std::enable_if_t<des && is_deserializable_container<T>::value && !has_tuple_for_serialization_tup<des, T, tag_tuple>::value >, tag_tuple>
    { static constexpr bool value = is_void_like_tup_<des, typename deserializable_value_type<T>::type, void, tag_tuple>::value; };
template <typename T, typename tag_tuple> struct is_void_like_tup_<false, T, std::enable_if_t<has_tuple_for_serialization_tup<false, T, tag_tuple>::value>, tag_tuple>
    { static constexpr bool value = is_void_like_tup_<false, decltype(invoke_tuple_for_serialization_tup(decllval<const std::remove_cvref_t<T>>(), std::declval<tag_tuple>())),
                                                  void, tag_tuple>::value; };
template <typename T, typename tag_tuple> struct is_void_like_tup_<true, T, std::enable_if_t<has_tuple_for_serialization_tup<true, T, tag_tuple>::value>, tag_tuple>
    { static constexpr bool value = is_void_like_tup_<true, decltype(invoke_tuple_for_serialization_tup(decllval<std::remove_cvref_t<T>>(), std::declval<tag_tuple>())),
                                                  void, tag_tuple>::value; };

template <bool des, typename T, typename tag_tuple> using is_void_like_tup = is_void_like_tup_<des, T, void, tag_tuple>;
template <bool des, typename T, typename ...tags> using is_void_like = is_void_like_tup_<des, T, void, std::tuple<tags...>>;

/** Returns true if the type is default constructible and either
 * moveable or copyable.
 * We can only deserialize into container types (list, map) which have
 * such elements as they need to own the result.*/
template <typename T>
inline constexpr bool is_deserializable_container_element_f() noexcept
{
    return std::is_default_constructible<T>::value &&
        (std::is_copy_constructible<T>::value || std::is_move_constructible<T>::value);
}

/** Returns true if the type is void-like or contains only X primitive values.
 * It maintains const-ness: if you call it with a const type only tuple_for_serialization() const will be considered*/
template <bool des, typename T, typename ...tags>
inline constexpr bool is_all_X_f() noexcept
{
    using plainT = std::remove_reference_t<T>;
    if constexpr (is_void_like<des, plainT, tags...>::value) return true;
    else if constexpr (is_expected<plainT>::value)
        return is_void_like<des, typename plainT::value_type, tags...>::value;
    if constexpr (is_serializable_container<plainT>::value && !des)
        return !is_map_container<plainT>::value && is_all_X_f<des, typename serializable_value_type<plainT>::type, tags...>();
    else if constexpr (is_deserializable_container<plainT>::value && des)
        return !is_map_container<plainT>::value && is_all_X_f<des, typename deserializable_value_type<plainT>::type, tags...>();
    else if constexpr (is_std_array<plainT>::value)
        return is_all_X_f<des, typename plainT::value_type, tags...>();
    else if constexpr (is_C_array<plainT>::value)
        return is_all_X_f<des, typename std::remove_reference_t<decltype(*std::begin(std::declval<T&>()))>, tags...>();
    else if constexpr (is_pair<plainT>::value)
        return is_all_X_f<des, typename plainT::first_type, tags...>() && is_all_X_f<des, typename plainT::second_type, tags...>();
    else if constexpr (is_tuple<plainT>::value) {
        if constexpr (std::tuple_size<plainT>::value == 0)
            return true;
        else
            return is_all_X_f<des, typename tuple_split<plainT>::first, tags...>() && is_all_X_f<des, typename tuple_split<plainT>::rest, tags...>();
    } else if constexpr (!des && std::is_pointer<plainT>::value)
        return is_all_X_f<des, decltype(*std::declval<T>()), tags...>();
    else if constexpr (is_smart_ptr<plainT>::value)
        return is_all_X_f<des, plainT::element_type, tags...>();
    else { //optional cannot be void-like, so we dont check for it
        if constexpr (des) {
            using tag = typename select_tag<T, has_tuple_for_serialization_deser, tags...>::type; //The tag that applies to tuple_for_serialization(), void or no_applicable_tag.
            if constexpr (std::is_void_v<tag>)
                return is_all_X_f<des, decltype(tuple_for_serialization(decllval<std::remove_cvref_t<T>>())), tags...>();
            else if constexpr (!std::is_same_v<tag, no_applicable_tag>)
                return is_all_X_f<des, decltype(tuple_for_serialization(decllval<std::remove_cvref_t<T>>(), std::declval<tag>())), tags...>();
#ifdef HAVE_BOOST_PFR
            else if constexpr (is_really_auto_serializable_v<plainT>)
                return is_all_X_f<des, decltype(boost::pfr::structure_tie(decllval<std::remove_cvref_t<T>>())), tags...>();
#endif
            else
                return false;
        } else {
            using tag = typename select_tag<plainT, has_tuple_for_serialization_ser, tags...>::type; //The tag that applies to tuple_for_serialization() const, void or no_applicable_tag.
            if constexpr (std::is_void_v<tag>)
                return is_all_X_f<des, decltype(tuple_for_serialization(decllval<const std::remove_cvref_t<T>>())), tags...>();
            else if constexpr (!std::is_same_v<tag, no_applicable_tag>)
                return is_all_X_f<des, decltype(tuple_for_serialization(decllval<const std::remove_cvref_t<T>>(), std::declval<tag>())), tags...>();
#ifdef HAVE_BOOST_PFR
            else if constexpr (is_really_auto_serializable_v<plainT>)
                return is_all_X_f<des, decltype(boost::pfr::structure_tie(decllval<const std::remove_cvref_t<T>>())), tags...>();
#endif
            else
                return false;
        }
    }
}

/** Helper template to check if a type is serializable or not with the given tag set.
 * If emit error is true, we will emit compilation errors via static assert that
 * detail what is the problem making the type not serializable.*/
template <typename T, bool emit_error=false, typename ...tags>
inline constexpr bool is_serializable_f() noexcept {
    using plainT = std::remove_cvref_t<T>; //A plain non-const, non-ref, non-volatile type
    if constexpr (is_serializable_primitive<plainT>::value) return true;
    else if constexpr (is_serializable_view_primitive<plainT>::value) return true;
    else if constexpr (is_void_like<false, plainT, tags...>::value) return true;
    else if constexpr (std::is_enum<plainT>::value) return true;
    else if constexpr (is_char_array<plainT>::value) return true;
    else if constexpr (std::is_same<plainT, char *>::value) return true;
    else if constexpr (std::is_same<plainT, const char *>::value) return true;
    else if constexpr (is_std_array<plainT>::value)
        return is_serializable_f<typename plainT::value_type, emit_error, tags...>();
    else if constexpr (is_C_array<plainT>::value)
        return is_serializable_f<typename std::remove_reference_t<decltype(*std::begin(std::declval<plainT &>()))>, emit_error, tags...>();
    else if constexpr (is_serializable_container<plainT>::value) {
        //maps cannot have void-like or all-X key types
        if constexpr (is_map_container<plainT>::value)
            if constexpr (is_all_X_f<false, typename plainT::key_type, tags...>()) {
                static_assert(!emit_error, "Key type of map is a void-like type.");
                return false;
            }
        return is_serializable_f<typename serializable_value_type<const plainT>::type, emit_error, tags...>();
    } else if constexpr (is_pair<plainT>::value)
        return is_serializable_f<typename plainT::first_type, emit_error, tags...>() && is_serializable_f<typename plainT::second_type, emit_error, tags...>();
    else if constexpr (is_tuple<plainT>::value) {
        if constexpr (std::tuple_size<plainT>::value == 0)
            return true;
        else
            return is_serializable_f<typename tuple_split<plainT>::first, emit_error, tags...>() && is_serializable_f<typename tuple_split<plainT>::rest, emit_error, tags...>();
    } else if constexpr (std::is_pointer<plainT>::value) {
        if constexpr (is_void_like<false, decltype(*std::declval<plainT>()), tags...>::value) {
            static_assert(!emit_error, "Pointer to a void-like type is not serializable.");
            return false;
        } else
            return is_serializable_f<decltype(*std::declval<T>()), emit_error, tags...>();
    } else if constexpr (is_smart_ptr<plainT>::value) {
        if constexpr (is_void_like<false, typename plainT::element_type, tags...>::value) {
            static_assert(!emit_error, "Pointer to a void-like type is not serializable.");
            return false;
        } else
            return is_serializable_f<typename plainT::element_type, emit_error, tags...>();
    } else if constexpr (is_optional<plainT>::value) {
        if constexpr (is_void_like<false, typename plainT::value_type, tags...>::value) {
            static_assert(!emit_error, "Optional of a void-like type is not serializable.");
            return false;
        } else
            return is_serializable_f<typename plainT::value_type, emit_error, tags...>();
    } else if constexpr (is_expected<plainT>::value)
        return is_void_like<false, typename plainT::value_type, tags...>::value || is_serializable_f<typename plainT::value_type, emit_error, tags...>();
    else {
        using tag = typename select_tag<plainT, has_tuple_for_serialization_ser, tags...>::type; //The tag that applies to tuple_for_serialization() const, void or no_applicable_tag.
        if constexpr (std::is_void_v<tag>) {
            static_assert(!std::is_void_v<decltype(tuple_for_serialization(decllval<const plainT>()))>, "Function 'tuple_for_serialization() const' returns void. Did you forget 'return' in its body?");
            return is_serializable_f<decltype(tuple_for_serialization(decllval<const plainT>())), emit_error, tags...>();
        } else if constexpr (!std::is_same_v<tag, no_applicable_tag>) {
            static_assert(!std::is_void_v<decltype(tuple_for_serialization(decllval<const plainT>(), std::declval<tag>()))>, "Function 'tuple_for_serialization(tag) const' returns void. Did you forget 'return' in its body?");
            return is_serializable_f<decltype(tuple_for_serialization(decllval<const plainT>(), std::declval<tag>())), emit_error, tags...>();
        } else {
#ifdef HAVE_BOOST_PFR
            if constexpr (is_really_auto_serializable_v<plainT>)
                return is_serializable_f<decltype(boost::pfr::structure_tie(decllval<const plainT>())), emit_error, tags...>();
            else
#endif
            if constexpr (std::is_class<plainT>::value) {
                if constexpr (!allow_auto_serialization<plainT>)
                    static_assert(!emit_error, "Auto serialization is disabled for this class, because uf::allow_auto_serialization is specialized as false for it.");
                else if constexpr (requires () { requires std::is_void_v<typename plainT::auto_serialization>; })
                    static_assert(!emit_error, "Auto serialization is disabled for this class, because it (or a base class) has 'auto_serialization = void' member typedef.");
                else if constexpr (std::is_empty_v<plainT>)
                    static_assert(!emit_error, "Empty classes are not auto serialized. Perhaps missing a tuple_for_serialization() const member/free function?");
                else {
                    if constexpr (sizeof...(tags))
                        static_assert(!emit_error, "Class is not an aggregate (no auto serialization), has no tuple_for_serialization() const member/free function with or without any of these tags, nor seem to be a container.");
                    else
                        static_assert(!emit_error, "Class is not an aggregate (no auto serialization), has no tuple_for_serialization() const member/free function, nor seem to be a container.");
                }
            } else if constexpr (std::is_union_v<plainT>)
                static_assert(!emit_error, "Unions are not serializable.");
            else if constexpr (std::is_function_v<plainT>)
                static_assert(!emit_error, "Functions are not serializable.");
            else if constexpr (std::is_member_pointer_v<plainT>)
                static_assert(!emit_error, "Member pointers are not serializable.");
            else
                static_assert(!emit_error, "Type is not a serializable primitive.");
            return false;
        }
    }
}

/** Helper template to check if a type is deserializable into a view or not with the given tag set.
* Const types are excluded, otherwise the same as is_serializable_f().
* We still allow for constant container elements.
* If emit error is true, we will emit compilation errors via static assert that
* detail what is the problem making the type not view deserializable.*/
template <typename T, bool as_view = false, bool emit_error = false, typename ...tags>
inline constexpr bool is_deserializable_f() noexcept
{
    using plainT = std::remove_volatile_t<std::remove_reference_t<T>>; //A const or non-const plain type
    //Allow const tuples and pairs, as these may hold non-const references
    if constexpr (is_pair<plainT>::value)
        return is_deserializable_f<typename plainT::first_type, as_view, emit_error, tags...>() && 
               is_deserializable_f<typename plainT::second_type, as_view, emit_error, tags...>();
    else if constexpr (is_tuple<plainT>::value) {
        if constexpr (std::tuple_size<plainT>::value == 0)
            return true;
        else
            return is_deserializable_f<typename tuple_split<plainT>::first, as_view, emit_error, tags...>() &&
                   is_deserializable_f<typename tuple_split<plainT>::rest, as_view, emit_error, tags...>();
    } else if constexpr (std::is_const_v<plainT>) {//Everything else must be non-const
        static_assert(!emit_error, "Const types cannot be deserialized into.");
        return false;
    } else if constexpr (is_serializable_primitive<plainT>::value) return true;
    else if constexpr (is_void_like<true, plainT, tags...>::value) return true;
    else if constexpr (as_view && is_string_deserializer_view<plainT>::value) return true;
    else if constexpr (!as_view && is_string_deserializer<plainT>::value) return true;
    else if constexpr (is_serializable_view_primitive<plainT>::value) {
        if constexpr (!as_view)
            static_assert(!emit_error, "This is a view type and can only be deserialized into as a view.");
        return as_view;
    } else if constexpr (std::is_enum<std::remove_cvref_t<T>>::value) return true;
    else if constexpr (is_std_array<plainT>::value)
        return is_deserializable_f<typename plainT::value_type, as_view, emit_error, tags...>();
    else if constexpr (is_C_array<std::remove_cvref_t<T>>::value)
        return is_deserializable_f<typename std::remove_reference_t<decltype(*std::begin(std::declval<plainT&>()))>, as_view, emit_error, tags...>();
    else if constexpr (std::is_enum_v<plainT>) return true;
    else if constexpr (is_deserializable_container<plainT>::value) {
        //maps cannot have void-like or all-X key types
        if constexpr (is_map_container<std::remove_cvref_t<T>>::value)
            if constexpr (is_all_X_f<true, typename std::remove_cvref_t<T>::key_type, tags...>()) {
                static_assert(!emit_error, "Key type of map is a void-like type.");
                return false;
            }
        //allow for const value types (set)
        if constexpr (is_pair<std::remove_reference_t<typename plainT::value_type>>::value) {//allow for const first member (map)
            if constexpr (!is_deserializable_container_element_f<typename plainT::value_type>()) {
                static_assert(!emit_error, "Container elements must be default and copy/move constructible.");
                return false;
            }
            return is_deserializable_f<typename std::remove_cvref_t<typename plainT::value_type::first_type>, as_view, emit_error, tags...>() &&
                   is_deserializable_f<typename plainT::value_type::second_type, as_view, emit_error, tags...>();
        } else if constexpr (is_deserializable_f<typename deserializable_value_type<plainT>::type, as_view, emit_error, tags...>()) { //allow for const value types (set)
            if constexpr (is_deserializable_container_element_f<typename plainT::value_type>()) 
                return true;
            else
                static_assert(!emit_error, "Container elements must be default and copy/move constructible.");
            return false;
        } else  
            return false; //non deserializable value type
    } else if constexpr (is_smart_ptr<plainT>::value) {
        if constexpr (!is_void_like<true, typename std::remove_cvref_t<T>::element_type, tags...>::value)
            return is_deserializable_f<typename plainT::element_type, as_view, emit_error, tags...>();
        else
            static_assert(!emit_error, "Pointer to a void-like type is not serializable nor deserializable.");
        return false;
    } else if constexpr (is_optional<plainT>::value) {
        if constexpr (!is_void_like<true, typename std::remove_cvref_t<T>::value_type, tags...>::value)
            return is_deserializable_f<typename plainT::value_type, as_view, emit_error, tags...>();
        else
            static_assert(!emit_error, "Optional of a void-like type is not serializable nor deserializable.");
        return false;
    } else if constexpr (is_expected<typename std::remove_const_t<plainT>>::value)
        return is_void_like<true, typename std::remove_const_t<plainT>::value_type, tags...>::value ||
        is_deserializable_f<typename std::remove_const_t<plainT>::value_type, as_view, emit_error, tags...>();
    else if constexpr (std::is_pointer<plainT>::value) {
        static_assert(!emit_error, "Raw pointers cannot be deserialized into.");
        return false;
    } else {
        using tag = typename select_tag<plainT, has_tuple_for_serialization_deser, tags...>::type; //The tag that applies to tuple_for_serialization(), void or no_applicable_tag.
        if constexpr (std::is_void_v<tag>) {
            static_assert(!std::is_void_v<decltype(tuple_for_serialization(decllval<plainT>()))>, "Function 'tuple_for_serialization()' returns void. Did you forget 'return' in its body?");
            return is_deserializable_f<decltype(tuple_for_serialization(decllval<plainT>())), as_view, emit_error, tags...>();
        } else if constexpr (!std::is_same_v<tag, no_applicable_tag>) {
            static_assert(!std::is_void_v<decltype(tuple_for_serialization(decllval<plainT>(), std::declval<tag>()))>, "Function 'tuple_for_serialization(tag)' returns void. Did you forget 'return' in its body?");
            return is_deserializable_f<decltype(tuple_for_serialization(decllval<plainT>(), std::declval<tag>())), as_view, emit_error, tags...>();
        } else {
#ifdef HAVE_BOOST_PFR
            if constexpr (is_really_auto_serializable_v<plainT>)
                return is_deserializable_f<decltype(boost::pfr::structure_tie(decllval<plainT>())), as_view, emit_error, tags...>();
            else
#endif
            if constexpr (std::is_class<plainT>::value) {
                if constexpr (!allow_auto_serialization<plainT>)
                    static_assert(!emit_error, "Auto serialization is disabled for this class, because uf::allow_auto_serialization is specialized as false for it.");
                else if constexpr (requires () { requires std::is_void_v<typename plainT::auto_serialization>; })
                    static_assert(!emit_error, "Auto serialization is disabled for this class, because it (or a base class) has 'auto_serialization = void' member typedef.");
                else if constexpr (std::is_empty_v<plainT>)
                    static_assert(!emit_error, "Empty classes are not auto serialized. Perhaps missing a tuple_for_serialization() const member/free function?");
                else {
                    if constexpr (sizeof...(tags))
                        static_assert(!emit_error, "Class is not an aggregate (no auto serialization), has no tuple_for_serialization() const member/free function with or without any of these tags, nor seem to be a container.");
                    else
                        static_assert(!emit_error, "Class is not an aggregate (no auto serialization), has no tuple_for_serialization() const member/free function, nor seem to be a container.");
                }
            } else if constexpr (std::is_union_v<plainT>)
                static_assert(!emit_error, "Unions are not serializable.");
            else if constexpr (std::is_function_v<plainT>)
                static_assert(!emit_error, "Functions are not serializable.");
            else if constexpr (std::is_member_pointer_v<plainT>)
                static_assert(!emit_error, "Member pointers are not serializable.");
            else
                static_assert(!emit_error, "Type is not a serializable primitive.");
            return false;
        }
    }
}

/** Noexcept traversable for */
enum class nt {
    len,
    ser,
    deser
};

/** Helper template to check if a type is traversable using tuple_for_serialization()
 * of its components in a noexcept way. For primitive types this is always true.
 * For compound types (list, map, optional, expected, tuples) it is so, if all contained types
 * are noexcept traversable. For structs, we require tuple_for_serialization() be noexcept
 * and its result noexcept traversable. In addition, we have extra requirements below.
 * @param [in] reason Determines what operation we are looking for.
 *           - len: we only require tuple_for_serialization() to be noexcept
 *           - ser: in addition, we require before_serialization() to be noexcept
 *             (after_serialization() is mandatory to be noexcept)
 *           - deser: we require after_deserialization() (or in its absence 
 *             after_deserialization_simple()) to be noexcept (or missing). 
 *             Plus we require that the type does not allocate at deserialization 
 *             (even for non-structs). This latter thing effectively excludes string, 
 *             containers and smart pointers.
 * Note that this function may return true for non-(de)serializable types, so this is not a
 * check for serializability or such.*/
template <typename T, typename ...tags>
inline constexpr bool is_noexcept_for(nt n) noexcept {
    using plainT = std::remove_cvref_t<T>; //A plain non-const, non-ref, non-volatile type
    if constexpr (std::is_same_v<std::string, T>) return n!=nt::deser; //string allocates on deser
    else if constexpr (is_serializable_primitive<plainT>::value) return true;
    else if constexpr (is_serializable_view_primitive<plainT>::value) return true;
    else if constexpr (std::is_enum<plainT>::value) return true;
    else if constexpr (is_char_array<plainT>::value) return true;
    else if constexpr (std::is_same<plainT, char *>::value) return true;
    else if constexpr (std::is_same<plainT, const char *>::value) return true;
    else if constexpr (is_std_array<plainT>::value)
        return is_noexcept_for<typename plainT::value_type, tags...>(n);
    else if constexpr (is_C_array<plainT>::value)
        return is_noexcept_for<typename std::remove_reference_t<decltype(*std::begin(std::declval<plainT &>()))>, tags...>(n);
    else if constexpr (is_serializable_container<plainT>::value)
        return n!=nt::deser && is_noexcept_for<typename serializable_value_type<const plainT>::type, tags...>(n);
    else if constexpr (is_pair<plainT>::value)
        return is_noexcept_for<typename plainT::first_type, tags...>(n) && is_noexcept_for<typename plainT::second_type, tags...>(n);
    else if constexpr (is_tuple<plainT>::value) {
        if constexpr (std::tuple_size<plainT>::value == 0)
            return true;
        else
            return is_noexcept_for<typename tuple_split<plainT>::first, tags...>(n) && is_noexcept_for<typename tuple_split<plainT>::rest, tags...>(n);
    } else if constexpr (std::is_pointer<plainT>::value)
        return is_noexcept_for<decltype(*std::declval<T>()), tags...>(n);
    else if constexpr (is_smart_ptr<plainT>::value)
        return n!=nt::deser && is_noexcept_for<typename plainT::element_type, tags...>(n);
    else if constexpr (is_optional<plainT>::value)
        return is_noexcept_for<typename plainT::value_type, tags...>(n);
    else if constexpr (is_expected<plainT>::value)
        return n!=nt::deser && is_noexcept_for<typename plainT::value_type, tags...>(n);
    else {
        bool ret = true;
        if (n==nt::deser) {
            using tag = typename select_tag<plainT, has_tuple_for_serialization_deser, tags...>::type; //The tag that applies to tuple_for_serialization() NON-CONST, void or no_applicable_tag.
            if constexpr (std::is_void_v<tag>) 
                ret &= noexcept(tuple_for_serialization(decllval<plainT>())) && is_noexcept_for<decltype(tuple_for_serialization(decllval<plainT>())), tags...>(n);
            else if constexpr (!std::is_same_v<tag, no_applicable_tag>)
                ret &= noexcept(tuple_for_serialization(decllval<plainT>(), std::declval<tag>())) && is_noexcept_for<decltype(tuple_for_serialization(decllval<plainT>(), std::declval<tag>())), tags...>(n);
            else if constexpr (is_void_like<true, plainT, tags...>::value) 
                return true;  //test void here, since a struct may have a tuple_for_serialization() returning a void-like type, but throwing
#ifdef HAVE_BOOST_PFR
            else if constexpr (is_really_auto_serializable_v<plainT>)
                return is_noexcept_for<decltype(boost::pfr::structure_tie(decllval<plainT>())), tags...>(n);
#endif
            else
                return false; //Type is not deserializable - better return false;
            using tag2 = typename select_tag<plainT, has_after_deserialization_tag, tags...>::type; //The tag that applies to after_deserialization(), void or no_applicable_tag.
            if constexpr (std::is_void_v<tag2>)
                ret &= noexcept(after_deserialization(decllval<plainT>(), tuple_for_serialization(decllval<plainT>())));
            else if constexpr (!std::is_same_v<tag2, no_applicable_tag>)
                ret &= noexcept(after_deserialization(decllval<plainT>(), tuple_for_serialization(decllval<plainT>(), std::declval<tag2>()), std::declval<tag2>()));
            else {
                using tag3 = typename select_tag<plainT, has_after_deserialization_simple_tag, tags...>::type; //The tag that applies to after_deserialization_simple(), void or no_applicable_tag.
                if constexpr (std::is_void_v<tag3>)
                    ret &= noexcept(after_deserialization_simple(decllval<plainT>()));
                else if constexpr (!std::is_same_v<tag3, no_applicable_tag>)
                    ret &= noexcept(after_deserialization_simple(decllval<plainT>(), std::declval<tag3>()));
            }
        } else {
            using tag = typename select_tag<plainT, has_tuple_for_serialization_ser, tags...>::type; //The tag that applies to tuple_for_serialization() CONST, void or no_applicable_tag.
            if constexpr (std::is_void_v<tag>)
                ret &= noexcept(tuple_for_serialization(decllval<const plainT>())) && is_noexcept_for<decltype(tuple_for_serialization(decllval<const plainT>())), tags...>(n);
            else if constexpr (!std::is_same_v<tag, no_applicable_tag>)
                ret &= noexcept(tuple_for_serialization(decllval<const plainT>(), std::declval<tag>())) && is_noexcept_for<decltype(tuple_for_serialization(decllval<const plainT>(), std::declval<tag>())), tags...>(n);
            else if constexpr (is_void_like<false, plainT, tags...>::value) 
                return true;  //test void here, since a struct may have a tuple_for_serialization() returning a void-like type, but throwing
#ifdef HAVE_BOOST_PFR
            else if constexpr (is_really_auto_serializable_v<plainT>)
                return is_noexcept_for<decltype(boost::pfr::structure_tie(decllval<const plainT>())), tags...>(n);
#endif
            else
                return false; //Type is not serializable - better return false;
            if (n==nt::ser) {
                using tag2 = typename select_tag<plainT, has_before_serialization_tag, tags...>::type; //The tag that applies to before_serialization(), void or no_applicable_tag.
                if constexpr (std::is_void_v<tag2>)
                    ret &= noexcept(before_serialization(decllval<const plainT>()));
                else if constexpr (!std::is_same_v<tag2, no_applicable_tag>)
                    ret &= noexcept(before_serialization(decllval<const plainT>(), std::declval<tag2>()));
            }
        }
        return ret;
    }
}

} //ns impl

/** Type trait to verify that a type has a (free or member) tuple_for_serialization()
 * function. You can test for const (deser=false) and non-const version (deser=true).
 * You can test the existence of such a function without tags (tag=void) or with a specific tag.*/
template<bool deser, typename T, typename tag = void>
inline constexpr bool has_tuple_for_serialization_tag_v = deser ? impl::has_tuple_for_serialization_deser<T, tag>::value : impl::has_tuple_for_serialization_ser<T, tag>::value;
/** Type trait to verify that a type has a (free or member) before_serialization() const function.
 * You can test the existence of such a function without tags (tag=void) or with a specific tag.*/
template<typename T, typename tag = void>
inline constexpr bool has_before_serialization_tag_v = impl::has_before_serialization_tag<T, tag>::value;
/** Type trait to verify that a type has a (free or member) after_serialization(bool) const function.
 * You can test the existence of such a function without tags (tag=void) or with a specific tag.*/
template<typename T, typename tag = void>
inline constexpr bool has_after_serialization_tag_v = impl::has_after_serialization_tag<T, tag>::value;
/** Type trait to verify that a type has a (free or member) after_deserialization(U&&) function,
 * where U is the return type of the non-const tuple_for_serialization().
 * You can test the existence of such a function without tags (tag=void) or with a specific tag
 * (in the latter case we also test tuple_for_serialization() with that tag.*/
template<typename T, typename tag = void>
inline constexpr bool has_after_deserialization_tag_v = impl::has_after_deserialization_tag<T, tag>::value;
/** Type trait to verify that a type has a (free or member) after_deserialization_simple() function taking no
 * arguments (other than tags potentially).
 * You can test the existence of such a function without tags (tag=void) or with a specific tag.*/
template<typename T, typename tag = void>
inline constexpr bool has_after_deserialization_simple_tag_v = impl::has_after_deserialization_simple_tag<T, tag>::value;
/** Type trait to verify that a type has a (free or member) after_deserialization_error() function.
 * You can test the existence of such a function without tags (tag=void) or with a specific tag.*/
template<typename T, typename tag = void>
inline constexpr bool has_after_deserialization_error_tag_v = impl::has_after_deserialization_error_tag<T, tag>::value;

/** Helper template to check if a type is serializable or not. */
template <typename T, typename ...tags>
inline constexpr bool is_serializable_v = impl::is_serializable_f<T, false, tags...>();
/** Helper template to check if a type is serializable or not. */
template<typename T, typename ...tags>
struct is_serializable : std::conditional_t<is_serializable_v<T, tags...>, std::true_type, std::false_type> {};

/** Helper template to check if a type is deserializable into a view or not.
 * Const types are excluded, otherwise the same as is_serializable_v.
 * We still allow for constant container elements.*/
template <typename T, typename ...tags>
inline constexpr bool is_deserializable_view_v = impl::is_deserializable_f<T, true, false, tags...>();
/** Helper template to check if a type is deserializable into a view or not.
 * Const types are excluded, otherwise the same as is_serializable.
 * We still allow for constant container elements.*/
template<typename T, typename ...tags>
struct is_deserializable_view : std::conditional_t<is_deserializable_view_v<T, tags...>, std::true_type, std::false_type> {};

/** Helper template to check if a type is deserializable or not.
 * The same as is_deserializable_view_v, but we dont allow string_view and any_view anywhere.*/
template <typename T, typename ...tags>
inline constexpr bool is_deserializable_v = impl::is_deserializable_f<T, false, false, tags...>();
/** Helper template to check if a type is deserializable or not.
 * The same as is_deserializable_view, but we dont allow string_view and any_view anywhere.*/
template<typename T, typename ...tags>
struct is_deserializable : std::conditional_t<is_deserializable_v<T, tags...>, std::true_type, std::false_type> {};

/** Flags to control how conversion at deserialization happens.*/
enum serpolicy : unsigned char
{
    /** No conversions allowed*/
    allow_converting_none = 0,
    /**Conversion between c, i, I integer types, but only towards a larger size.
     * signed integers and unsigned char assumed. No double conversions.*/
    allow_converting_ints = 1,
    /**Conversion between c, i, I integer types including narrowing ones.
     * signed integers and unsigned char assumed. Overflows are ignored.
     * No double conversions.*/
    allow_converting_ints_narrowing = 3,
    /**Conversion between d and i/I. Overflows are ignored. No double conversions.*/
    allow_converting_double = 4,
    /**Conversion between b and c/i/I.
     * true is always 1, false is zero. Other values are converted to true.
     * No double conversions.*/
    allow_converting_bool = 8,
    /** Conversion of xT->T and T->xT, when needed.
     * If the expected to be converted to a non-expected type contains an error
     * we throw that or note that for later processing.
     * Double convesions in the sense that we convert xU<->T if U<->T is convertible.
     * 'e' is converted to any expected (and vice versa) if this is set.
     * X may simply disappear, which may change the element size of tuples. */
    allow_converting_expected = 16,
    /** Conversion of any type T to an uf::any (T->a) and vice versa (a->T).
     * The latter is successful only if any contains a type convertible to T.
     * Thus on a(T)->U we do double convesions, first any to T then T to U.
     * Also, an any containing a void deserialize to a void type, which
     * means it may simply disappear. This may change the element size of
     * tuples.*/
    allow_converting_any = 32,
    /** Convert s to lc. void to oT. Maybe others will be added in the future.*/
    allow_converting_aux = 64,
    /** Convert tuples to lists and back. ('tuples' also include std::pair,
     * std::array, C-style arrays and structs, as well.) */
    allow_converting_tuple_list = 128,
    /** All conversions allowed*/
    allow_converting_all = 255
};

constexpr serpolicy operator | (serpolicy a, serpolicy b) { return serpolicy(((unsigned char)a)|((unsigned char)b)); }
constexpr serpolicy operator & (serpolicy a, serpolicy b) { return serpolicy(((unsigned char)a)&((unsigned char)b)); }
constexpr serpolicy operator ~ (serpolicy a) { return serpolicy((~(unsigned char)a)&(unsigned char)allow_converting_all); }


std::unique_ptr<value_error> cant_convert(std::string_view from_type, std::string_view to_type, serpolicy policy);
std::unique_ptr<value_error> cant_convert(std::string_view from_type, std::string_view to_type, serpolicy policy,
                                          std::string_view serialized_data);
std::optional<std::string>
convert(std::string_view from_type, std::string_view to_type, serpolicy convpolicy, 
        std::string_view serialized_data, bool check = false);

inline std::string to_string(uf::serpolicy convpolicy)
{
    std::string ret;
    if (convpolicy == uf::allow_converting_all) ret = "all";
    else if (convpolicy == uf::allow_converting_none) ret = "none";
    else {
        if (convpolicy&uf::allow_converting_ints_narrowing&~uf::allow_converting_ints) ret += "ints(narrowing)|";
        else if (convpolicy&uf::allow_converting_ints) ret += "ints(widening only)|";
        if (convpolicy&uf::allow_converting_bool) ret += "bool|";
        if (convpolicy&uf::allow_converting_double) ret += "double|";
        if (convpolicy&uf::allow_converting_expected) ret += "expected|";
        if (convpolicy&uf::allow_converting_any) ret += "any|";
        if (convpolicy&uf::allow_converting_aux) ret += "aux|";
        if (convpolicy&uf::allow_converting_tuple_list) ret += "tuple_list|";
        ret.pop_back();
    }
    ret = "convert:" + ret;
    return ret;
}

/** @}  serialization */


namespace impl
{

/** Helper template to drop first element of a tuple*/
template < std::size_t... Ns, typename T, typename... Ts >
std::tuple<Ts&...> tuple_tail_impl(std::index_sequence<Ns...>, std::tuple<T, Ts...> &t)
{ return  std::tie(std::get<Ns+1u>(t)...); }

/** Helper template to drop first element of a tuple*/
template < typename T, typename... Ts >
std::tuple<Ts&...> tuple_tail (std::tuple<T, Ts...> &t)
{ return  tuple_tail_impl(std::make_index_sequence<sizeof...(Ts)>(), t); }

/** Helper template to drop first element of a tuple*/
template < std::size_t... Ns, typename T, typename... Ts >
std::tuple<const Ts&...> tuple_tail_impl(std::index_sequence<Ns...>, const std::tuple<T, Ts...> &t)
{ return  std::tie(std::get<Ns+1u>(t)...); }

/** Helper template to drop first element of a tuple*/
template <typename T, typename... Ts >
std::tuple<const Ts&...> tuple_tail(const std::tuple<T, Ts...> &t)
{ return  tuple_tail_impl(std::make_index_sequence<sizeof...(Ts)>(), t); }

template <bool des, typename tag_tuple, typename ...TT, size_t ...I>
constexpr size_t number_of_non_void_impl(std::index_sequence<I...>)
{ return (0 + ... + (is_void_like_tup<des, std::remove_cvref_t<std::tuple_element_t<I, std::tuple<TT...>>>, tag_tuple>::value ? 0 : 1)); }

template <bool des, typename tag_tuple, typename ...TT>
constexpr size_t number_of_non_void_tup()
{ return number_of_non_void_impl<des, tag_tuple, TT...>(std::make_index_sequence<sizeof...(TT)>{}); }

template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const void*, const tag_tuple* = nullptr)  noexcept { return make_string(""); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const bool*, const tag_tuple* = nullptr)  noexcept { return make_string("b"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const unsigned char *, const tag_tuple* = nullptr) noexcept { return make_string("c"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const signed char*, const tag_tuple* = nullptr) noexcept { return make_string("c"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const char*, const tag_tuple* = nullptr) noexcept { return make_string("c"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const uint16_t*, const tag_tuple* = nullptr) noexcept { return make_string("i"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const int16_t*, const tag_tuple* = nullptr) noexcept { return make_string("i"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const uint32_t*, const tag_tuple* = nullptr) noexcept { return make_string("i"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const int32_t*, const tag_tuple* = nullptr) noexcept { return make_string("i"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const uint64_t*, const tag_tuple* = nullptr) noexcept { return make_string("I"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const int64_t*, const tag_tuple* = nullptr) noexcept { return make_string("I"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const float*, const tag_tuple* = nullptr) noexcept { return make_string("d"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const double*, const tag_tuple* = nullptr) noexcept { return make_string("d"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const long double*, const tag_tuple* = nullptr) noexcept { return make_string("d"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const std::string*, const tag_tuple* = nullptr) noexcept { return make_string("s"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const std::string_view*, const tag_tuple* = nullptr) noexcept { return make_string("s"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const char *const *, const tag_tuple* = nullptr) noexcept { return make_string("s"); }
template <bool des, size_t L, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const char (*)[L], const tag_tuple* = nullptr) noexcept { return make_string("s"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const any*, const tag_tuple* = nullptr) noexcept { return make_string("a"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const any_view*, const tag_tuple* = nullptr) noexcept { return make_string("a"); }
template <bool des, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const error_value*, const tag_tuple* = nullptr) noexcept { return make_string("e"); }
template <bool des, typename T, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const uf::string_deserializer<T>*, const tag_tuple* = nullptr) noexcept { return make_string("s"); }
template <bool des, typename T, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const uf::string_deserializer_view<T>*, const tag_tuple* = nullptr) noexcept { return make_string("s"); }
template <bool des, typename T, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const expected<T>*, const tag_tuple* = nullptr) noexcept;
template <bool des, typename E, typename tag_tuple=std::tuple<>> constexpr typename std::enable_if_t<std::is_enum_v<E>, static_string<1>>
serialize_type_static_impl(const E *, const tag_tuple* = nullptr) noexcept;
template <bool des, typename C, typename tag_tuple=std::tuple<>> constexpr auto
serialize_type_static_impl(const C *, const tag_tuple* = nullptr, std::enable_if_t<!des && is_serializable_container<C>::value && !is_map_container<C>::value && !has_tuple_for_serialization_tup_v<des, C, tag_tuple>>* = nullptr) noexcept;
template <bool des, typename C, typename tag_tuple=std::tuple<>> constexpr auto
serialize_type_static_impl(const C *, const tag_tuple* = nullptr, std::enable_if_t<des && is_deserializable_container<C>::value && !is_map_container<C>::value && !has_tuple_for_serialization_tup_v<des, C, tag_tuple>>* = nullptr) noexcept;
template <bool des, typename M, typename tag_tuple=std::tuple<>> constexpr auto
serialize_type_static_impl(const M *, const tag_tuple* = nullptr, std::enable_if_t<is_map_container<M>::value && !has_tuple_for_serialization_tup_v<des, M, tag_tuple>>* = nullptr) noexcept;
template <bool des, typename S, typename tag_tuple=std::tuple<>> constexpr auto
serialize_type_static_impl(const S *, const tag_tuple* = nullptr, std::enable_if_t<has_tuple_for_serialization_tup_v<des, S, tag_tuple>>* = nullptr) noexcept; //both static and non-static tuple_for_serialization will match this
template <bool des, typename S, typename tag_tuple = std::tuple<>> constexpr auto
serialize_type_static_impl(const S *, const tag_tuple* = nullptr, std::enable_if_t<is_really_auto_serializable_v<S> && !has_tuple_for_serialization_tup_v<des, S, tag_tuple>>* = nullptr) noexcept; //both static and non-static tuple_for_serialization will match this
template <bool des, typename A, typename B, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const std::pair<A, B> *p, const tag_tuple* = nullptr) noexcept;
template <bool des, typename ...TT, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const std::tuple<TT...> *t, const tag_tuple* = nullptr) noexcept;
template <bool des, typename T, size_t L, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const std::array<T, L> *, const tag_tuple* = nullptr) noexcept;
template <bool des, typename T, size_t L, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(T const (*)[L], const tag_tuple* = nullptr) noexcept;
template <bool des, typename T, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const std::unique_ptr<T> *p, const tag_tuple* = nullptr) noexcept;
template <bool des, typename T, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const std::shared_ptr<T> *p, const tag_tuple* = nullptr) noexcept;
template <bool des, typename T, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const T*const *p, const tag_tuple* = nullptr) noexcept;
template <bool des, typename T, typename tag_tuple=std::tuple<>> constexpr auto serialize_type_static_impl(const std::optional<T> *p, const tag_tuple* = nullptr) noexcept;

template <bool des, typename T, typename tag_tuple> constexpr auto serialize_type_static_impl(const expected<T>*, const tag_tuple*) noexcept
{
    if constexpr (is_void_like_tup<des, T, tag_tuple>::value) {
        return make_string("X");
    } else {
        T *x = nullptr; return make_string("x") + serialize_type_static_impl<des>(x, (const tag_tuple*)nullptr);
    }
}
template <bool des, typename E, typename tag_tuple> constexpr typename std::enable_if_t<std::is_enum_v<E>, static_string<1>>
serialize_type_static_impl(const E *, const tag_tuple *) noexcept { return make_string("i"); }
template <bool des, typename C, typename tag_tuple>
constexpr auto serialize_type_static_impl(const C *, const tag_tuple*, std::enable_if_t<!des && is_serializable_container<C>::value && !is_map_container<C>::value && !has_tuple_for_serialization_tup_v<des, C, tag_tuple>>*) noexcept {
        if constexpr (is_void_like_tup<des, typename serializable_value_type<const C>::type, tag_tuple>::value) return make_string(""); //serializable containers may not have C::value_type
        else { typename serializable_value_type<const C>::type * x = nullptr; return make_string("l") + serialize_type_static_impl<des>(x, (const tag_tuple*)nullptr); }
}
template <bool des, typename C, typename tag_tuple>
constexpr auto serialize_type_static_impl(const C *, const tag_tuple*, std::enable_if_t<des && is_deserializable_container<C>::value && !is_map_container<C>::value && !has_tuple_for_serialization_tup_v<des, C, tag_tuple>>*) noexcept {
    if constexpr (is_void_like_tup<des, typename deserializable_value_type<C>::type, tag_tuple>::value) return make_string("");
    else { typename deserializable_value_type<C>::type* x = nullptr; return make_string("l") + serialize_type_static_impl<des>(x, (const tag_tuple*)nullptr); }
}
template <bool des, typename M, typename tag_tuple>
constexpr auto serialize_type_static_impl(const M *, const tag_tuple*, std::enable_if_t<is_map_container<M>::value && !has_tuple_for_serialization_tup_v<des, M, tag_tuple>>*) noexcept {
    if constexpr (is_void_like_tup<des, typename std::remove_cvref_t<typename M::key_type>, tag_tuple>::value && is_void_like_tup<des, typename std::remove_cvref_t<typename M::mapped_type>, tag_tuple>::value) return make_string("");
    else if constexpr (is_void_like_tup<des, typename std::remove_cvref_t<typename M::key_type>, tag_tuple>::value) {typename M::mapped_type* y = nullptr; return make_string("l") + serialize_type_static_impl<des>(y, (const tag_tuple*)nullptr);}
    else if constexpr (is_void_like_tup<des, typename std::remove_cvref_t<typename M::mapped_type>, tag_tuple>::value) {typename M::key_type* x = nullptr; return make_string("l") + serialize_type_static_impl<des>(x, (const tag_tuple*)nullptr);}
    else {typename M::key_type* x = nullptr; typename M::mapped_type* y = nullptr; return make_string("m") + serialize_type_static_impl<des>(x, (const tag_tuple*)nullptr) + serialize_type_static_impl<des>(y, (const tag_tuple*)nullptr);}
}
template <bool des, typename S, typename tag_tuple>
constexpr auto serialize_type_static_impl(const S *, const tag_tuple*, std::enable_if_t<has_tuple_for_serialization_tup_v<des, S, tag_tuple>>*) noexcept {
    if constexpr (des) {
        using tag = typename select_tag_tuple<S, has_tuple_for_serialization_deser, tag_tuple>::type;
        if constexpr (std::is_void_v<tag>) {
            typename std::add_pointer<decltype(tuple_for_serialization(decllval<std::remove_cvref_t<S>>()))>::type t = nullptr;
            return serialize_type_static_impl<des>(t, (const tag_tuple*)nullptr);
        } else if constexpr (!std::is_same_v<tag, no_applicable_tag>) {
            typename std::add_pointer<decltype(tuple_for_serialization(decllval<std::remove_cvref_t<S>>(), std::declval<tag>()))>::type t = nullptr;
            return serialize_type_static_impl<des>(t, (const tag_tuple*)nullptr);
        } else
            return make_string("");
    } else {
        using tag = typename select_tag_tuple<S, has_tuple_for_serialization_ser, tag_tuple>::type;
        if constexpr (std::is_void_v<tag>) {
            typename std::add_pointer<decltype(tuple_for_serialization(decllval<const std::remove_cvref_t<S>>()))>::type t = nullptr;
            return serialize_type_static_impl<des>(t, (const tag_tuple*)nullptr);
        } else if constexpr (!std::is_same_v<tag, no_applicable_tag>) {
            typename std::add_pointer<decltype(tuple_for_serialization(decllval<const std::remove_cvref_t<S>>(), std::declval<tag>()))>::type t = nullptr;
            return serialize_type_static_impl<des>(t, (const tag_tuple*)nullptr);
        } else
            return make_string("");
    }
}
#ifdef HAVE_BOOST_PFR
template <bool des, typename S, typename tag_tuple = std::tuple<>> 
constexpr auto serialize_type_static_impl(const S *, const tag_tuple *, std::enable_if_t<is_really_auto_serializable_v<S> && !has_tuple_for_serialization_tup_v<des, S, tag_tuple>> *) noexcept {
    if constexpr (des) {
        typename std::add_pointer<decltype(boost::pfr::structure_tie(decllval<      std::remove_cvref_t<S>>()))>::type t = nullptr;
        return serialize_type_static_impl<des>(t, (const tag_tuple *)nullptr);
    } else {
        typename std::add_pointer<decltype(boost::pfr::structure_tie(decllval<const std::remove_cvref_t<S>>()))>::type t = nullptr;
        return serialize_type_static_impl<des>(t, (const tag_tuple *)nullptr);
    }
}
#endif
template <bool des, typename A, typename B, typename tag_tuple> //pair
constexpr auto serialize_type_static_impl(const std::pair<A, B> *, const tag_tuple*) noexcept {
    if constexpr (is_void_like_tup<des, typename std::remove_cvref_t<A>, tag_tuple>::value && is_void_like_tup<des, typename std::remove_cvref_t<B>, tag_tuple>::value) return make_string("");
    else if constexpr (is_void_like_tup<des, typename std::remove_cvref_t<A>, tag_tuple>::value) return serialize_type_static_impl<des>((B*)nullptr, (const tag_tuple*)nullptr);
    else if constexpr (is_void_like_tup<des, typename std::remove_cvref_t<B>, tag_tuple>::value) return serialize_type_static_impl<des>((A*)nullptr, (const tag_tuple*)nullptr);
    else return make_string("t2") + serialize_type_static_impl<des>((std::remove_reference_t<A>*)nullptr, (const tag_tuple*)nullptr) + serialize_type_static_impl<des>((std::remove_reference_t<B>*)nullptr, (const tag_tuple*)nullptr);
}
template <bool des, typename tag_tuple> constexpr auto serialize_typelist_static(const std::tuple<>*, const tag_tuple* = nullptr) noexcept { return make_string(""); }
template <bool des, typename tag_tuple> constexpr auto serialize_typelist_static(const std::monostate*, const tag_tuple * = nullptr) noexcept { return make_string(""); }
template <bool des, typename T, typename tag_tuple> //tuples
constexpr auto serialize_typelist_static(const std::tuple<T> *, const tag_tuple*) noexcept { typename std::add_pointer<std::remove_reference_t<T>>::type x = nullptr; return serialize_type_static_impl<des>(x, (const tag_tuple*)nullptr); }
template <bool des, typename T, typename ...TT, typename tag_tuple> //tuples
constexpr auto serialize_typelist_static(const std::tuple<T, TT...> *, const tag_tuple*) noexcept { typename std::add_pointer<std::remove_reference_t<T>>::type y = nullptr; typename std::tuple<TT...>*x = nullptr; return serialize_type_static_impl<des>(y, (const tag_tuple*)nullptr) + serialize_typelist_static<des>(x, (const tag_tuple*)nullptr); }
template <bool des, typename T, typename tag_tuple> //single element tuple (not part of a recursive processing of a larger tuple) is really its type. Avoid "t1" that only leads to ambiguity
constexpr auto serialize_type_static_impl(const std::tuple<T> *, const tag_tuple*) noexcept { typename std::add_pointer<std::remove_reference_t<T>>::type x = nullptr; return serialize_type_static_impl<des>(x, (const tag_tuple*)nullptr); }
template <size_t N>
constexpr auto serialize_num_to() noexcept { if constexpr (N>9) return serialize_num_to<N/10>().push_back('0'+N%10); else return make_string("").push_back('0'+N%10); }
template <bool des, typename ...TT, typename tag_tuple> //tuples
constexpr auto serialize_type_static_impl(const std::tuple<TT...> *t, const tag_tuple*) noexcept
{
    (void)t;
    constexpr size_t num = number_of_non_void_tup<des, tag_tuple, TT...>();
    if constexpr (num==0) return make_string("");
    else if constexpr (num==1) return serialize_typelist_static<des>(t, (const tag_tuple*)nullptr); //all the types concatenated - only one of them non-empty
    else return make_string("t") + serialize_num_to<num>() + serialize_typelist_static<des>(t, (const tag_tuple*)nullptr);
}
template <bool des, typename T, size_t L, typename tag_tuple>
constexpr auto serialize_type_static_impl(const std::array<T, L> *, const tag_tuple*) noexcept {
    if constexpr (L==0) return make_string("");
    else if constexpr (L==1) return serialize_type_static_impl<des>((T*)nullptr, (const tag_tuple*)nullptr);
    else return make_string("t") + serialize_num_to<L>() + serialize_type_static_impl<des>((T*)nullptr, (const tag_tuple*)nullptr).template repeat<L>();
}
template <bool des, typename T, size_t L, typename tag_tuple>
constexpr auto serialize_type_static_impl(T const (*)[L], const tag_tuple*) noexcept
{
    if constexpr (L == 0) return make_string("");
    else if constexpr (L == 1) return serialize_type_static_impl<des>((T*)nullptr, (const tag_tuple*)nullptr);
    else return make_string("t") + serialize_num_to<L>() + serialize_type_static_impl<des>((T*)nullptr, (const tag_tuple*)nullptr).template repeat<L>();
}
template <bool des, typename T, typename tag_tuple> constexpr auto serialize_type_static_impl(const std::unique_ptr<T> *, const tag_tuple*) noexcept { T *x=nullptr; return make_string("o") + serialize_type_static_impl<des>(x, (const tag_tuple*)nullptr);}
template <bool des, typename T, typename tag_tuple> constexpr auto serialize_type_static_impl(const std::shared_ptr<T> *, const tag_tuple*) noexcept { T *x=nullptr; return make_string("o") + serialize_type_static_impl<des>(x, (const tag_tuple*)nullptr);}
template <bool des, typename T, typename tag_tuple> constexpr auto serialize_type_static_impl(const T *const *, const tag_tuple*) noexcept { T* x = nullptr; return make_string("o") + serialize_type_static_impl<des>(x, (const tag_tuple*)nullptr); }
template <bool des, typename T, typename tag_tuple> constexpr auto serialize_type_static_impl(const std::optional<T> *, const tag_tuple*) noexcept { T* x = nullptr; return make_string("o") + serialize_type_static_impl<des>(x, (const tag_tuple*)nullptr); }

/** A variant with template parameters only.*/
template <bool des, typename T, typename ...tags>
constexpr auto serialize_type_static() { return serialize_type_static_impl<des>((typename std::remove_cvref_t<T> *)nullptr, (std::tuple<tags...> *)nullptr); }

/** Helper function to check if a type has the same typestring for serialization and deserialization.
 * If emit error is true, we fire a static_assert explaining the problem. 
 * For non-serializable and/or non-deserializable types we also emit an error.
 * We always return true.*/
template <typename T, bool as_view, bool emit_error, typename ...tags>
constexpr bool is_ser_deser_ok_f() {
    if constexpr (!is_serializable_f<T, true, tags...>()) return true;
    else if constexpr (!is_deserializable_f<T, as_view, true, tags...>()) return true;
    else if constexpr (!(serialize_type_static<false, T, tags...>() == serialize_type_static<true, T, tags...>()))
        static_assert(!emit_error, "The serialization and deserialization typestrings differ.");
    return true;
}


/** Enumerates common deserialization problems. */
enum class ser {
    ok,   ///<no problem
    end,  ///<Unexpected end of typestring
    chr,  ///<Invalid character
    num,  ///<Number at least 2 expected
    val,  ///<Value does not match type
    tlong,///<Too long type
    vlong,///<Too long value
};

constexpr bool operator !(ser s) noexcept { return s==ser::ok; }

constexpr std::string_view ATTR_PURE__ ser_error_str(ser problem) noexcept {
    switch (problem) {
    case ser::ok: return {};
    case ser::end: return std::string_view("Unexpected end of typestring");
    case ser::chr: return std::string_view("Invalid character");
    case ser::num: return std::string_view("Number at least 2 expected");
    case ser::val: return std::string_view("Value does not match type");
    case ser::tlong: return std::string_view("Extra characters after typestring");
    case ser::vlong: return std::string_view("Extra bytes after value");
    default: return std::string_view("Strange return from parse_type");
    }
}

/** Parses string and checks if it is a valid type description.
 * @param [in] type The typestring to check. 
 * @param [in] accept_void If true, we return a valid 0 on an empty 'type'
 * @returns On success the offset of the next char after the type and zero, whereas on
 *          error we return the offset of the error and the kind of the error.*/
inline std::pair<size_t, ser> ATTR_PURE__ parse_type(const char *type, const char* tend, bool accept_void) noexcept
{
    if (type > tend) return {0, ser::end};
    if (type == tend) {
        return {0, accept_void ? ser::ok : ser::end}; //ok or truncated type
    }
    if (*type=='s' || *type=='c' || *type=='b' ||
        *type=='i' || *type=='I' || *type=='X' ||
        *type=='d' || *type=='a' || *type=='e')
        return { 1, ser::ok };
    if (*type == 'l' || *type=='x' || *type=='o') {
        auto [off, problem] = parse_type(type + 1, tend, false);
        return { 1 + off, problem };
    }
    if (*type=='m') {
        auto [off1, problem1] = parse_type(type+1, tend, false);
        if (problem1!=ser::ok) return { 1 + off1, problem1 };
        auto [off2, problem2] = parse_type(type + 1 + off1, tend, false);
        return { 1 + off1 + off2, problem2 };
    }
    if (*type=='t') {
        size_t start_from = 1;
        size_t size = 0;
        while (type+start_from < tend && '0'<= type[start_from] && type[start_from] <='9')
            size = size*10 + type[start_from++] - '0';
        if (size <= 1) return {start_from, ser::num};
        while (size--)
            if (auto [len, problem] = parse_type(type+start_from, tend, false); !problem)
                start_from += len;
            else
                return {start_from+len, problem};
        return { start_from, ser::ok};
    }
    return {0, ser::chr}; //bad char
}

inline std::pair<size_t, ser> ATTR_PURE__ parse_type(std::string_view type, bool accept_void) noexcept {
    return parse_type(type.data(), type.data()+type.length(), accept_void);
}

template <typename T, typename tag_tuple> 
constexpr typename std::enable_if<!is_serializable_container<T>::value && !has_tuple_for_serialization_tup_v<false, T, tag_tuple> && !is_really_auto_serializable_v<T>, bool>::type
has_before_serialization_inside_f(const T*, tag_tuple*) noexcept { return false; } //default generic: non container, non struct
template <typename T, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const expected<T>*, tag_tuple *) noexcept;
template <typename C, typename tag_tuple>
constexpr typename std::enable_if<is_serializable_container<C>::value && !has_tuple_for_serialization_tup_v<false, C, tag_tuple>, bool>::type 
has_before_serialization_inside_f(const C*, tag_tuple *) noexcept;
template <typename S, typename tag_tuple> constexpr typename std::enable_if<has_tuple_for_serialization_tup<false, S, tag_tuple>::value, bool>::type
has_before_serialization_inside_f(const S*, tag_tuple *) noexcept;
template <typename S, typename tag_tuple> constexpr typename std::enable_if<is_really_auto_serializable_v<S> && !has_tuple_for_serialization_tup<false, S, tag_tuple>::value, bool>::type
has_before_serialization_inside_f(const S *, tag_tuple *) noexcept;
template <typename A, typename B, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const std::pair<A, B> *, tag_tuple *) noexcept;
template <typename T, typename ...TT, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const std::tuple<T, TT...> *, tag_tuple *) noexcept;
template <typename T, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const std::tuple<T> *, tag_tuple *) noexcept;
template <typename T, size_t L, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const std::array<T, L> *, tag_tuple *);
template <typename T, size_t L, typename tag_tuple> constexpr bool has_before_serialization_inside_f(T const (*)[L], tag_tuple *);
template <typename T, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const std::unique_ptr<T> *, tag_tuple *);
template <typename T, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const std::shared_ptr<T> *, tag_tuple *);
template <typename T, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const T * const *, tag_tuple *);
template <typename T, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const std::optional<T> *, tag_tuple *);

template <typename T, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const expected<T>*, tag_tuple *tags) noexcept
{ std::remove_cvref_t<T> *x = nullptr; return has_before_serialization_inside_f(x, tags); }
template <typename C, typename tag_tuple> 
constexpr typename std::enable_if<is_serializable_container<C>::value && !has_tuple_for_serialization_tup_v<false, C, tag_tuple>, bool>::type 
has_before_serialization_inside_f(const C*, tag_tuple *tags) noexcept
{typename serializable_value_type<C>::type *x = nullptr; return has_before_serialization_inside_f(x, tags); }
template <typename S, typename tag_tuple> constexpr typename std::enable_if<has_tuple_for_serialization_tup<false, S, tag_tuple>::value, bool>::type
has_before_serialization_inside_f(const S *s, tag_tuple *tags) noexcept
{ std::remove_cvref_t<decltype(invoke_tuple_for_serialization_tup(*s, std::move(*tags)))> *x = nullptr; return has_before_serialization_tup_v<S, tag_tuple> || has_before_serialization_inside_f(x, tags); }
#ifdef HAVE_BOOST_PFR
template <typename S, typename tag_tuple> constexpr typename std::enable_if<is_really_auto_serializable_v<S> && !has_tuple_for_serialization_tup<false, S, tag_tuple>::value, bool>::type
has_before_serialization_inside_f(const S *s, tag_tuple *tags) noexcept
{ std::remove_cvref_t<decltype(boost::pfr::structure_tie(*s))> *x = nullptr; return has_before_serialization_inside_f(x, tags); }
#endif
template <typename A, typename B, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const std::pair<A, B> *, tag_tuple *tags) noexcept
{ std::remove_cvref_t<A>* x = nullptr; std::remove_cvref_t <B>*y = nullptr; return has_before_serialization_inside_f(x, tags) || has_before_serialization_inside_f(y, tags); }
template <typename T, typename ...TT, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const std::tuple<T, TT...> *, tag_tuple *tags) noexcept
{ std::remove_cvref_t<T> *x = nullptr; std::tuple<TT...> *y=nullptr; return has_before_serialization_inside_f(x, tags) || has_before_serialization_inside_f(y, tags); }
template <typename T, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const std::tuple<T> *, tag_tuple *tags) noexcept
{ std::remove_cvref_t<T> *x = nullptr; return has_before_serialization_inside_f(x, tags); }
template <typename T, size_t L, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const std::array<T, L> *, tag_tuple *tags)
{ std::remove_cvref_t<T> *x = nullptr; return has_before_serialization_inside_f(x, tags); }
template <typename T, size_t L, typename tag_tuple> constexpr bool has_before_serialization_inside_f(T const (*)[L], tag_tuple *tags)
{ std::remove_cvref_t<T> *x = nullptr; return has_before_serialization_inside_f(x, tags); }
template <typename T, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const std::unique_ptr<T> *, tag_tuple *tags)
{ std::remove_cvref_t<T> *x = nullptr; return has_before_serialization_inside_f(x, tags); }
template <typename T, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const std::shared_ptr<T> *, tag_tuple *tags)
{ std::remove_cvref_t<T> *x = nullptr; return has_before_serialization_inside_f(x, tags); }
template <typename T, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const T * const *, tag_tuple *tags) //works for char*, too (will return false)
{ std::remove_cvref_t<T> *x = nullptr; return has_before_serialization_inside_f(x, tags); }
template <typename T, typename tag_tuple> constexpr bool has_before_serialization_inside_f(const std::optional<T> *, tag_tuple *tags)
{ std::remove_cvref_t<T> *x = nullptr; return has_before_serialization_inside_f(x, tags); }

template <typename T, typename ...tags> 
constexpr bool has_before_serialization_inside_v = has_before_serialization_inside_f(std::add_pointer_t<std::add_const_t<std::remove_cvref_t<T>>>(),
                                                                                     (std::tuple<tags...> *)nullptr);

template <typename T, typename tag_tuple> 
constexpr typename std::enable_if<!is_serializable_container<T>::value && !has_tuple_for_serialization_tup_v<false, T, tag_tuple> && !is_really_auto_serializable_v<T>, bool>::type
has_after_serialization_inside_f(const T*, tag_tuple*) noexcept { return false; } //default generic: non container, non struct
template <typename T, typename tag_tuple> constexpr bool has_after_serialization_inside_f(const expected<T>*, tag_tuple *) noexcept;
template <typename C, typename tag_tuple> 
constexpr typename std::enable_if<is_serializable_container<C>::value && !has_tuple_for_serialization_tup_v<false, C, tag_tuple>, bool>::type 
has_after_serialization_inside_f(const C*, tag_tuple *) noexcept;
template <typename S, typename tag_tuple> constexpr typename std::enable_if<has_tuple_for_serialization_tup_v<false, S, tag_tuple>, bool>::type 
has_after_serialization_inside_f(const S*, tag_tuple *) noexcept;
template <typename S, typename tag_tuple> constexpr typename std::enable_if<is_really_auto_serializable_v<S> && !has_tuple_for_serialization_tup_v<false, S, tag_tuple>, bool>::type
has_after_serialization_inside_f(const S *, tag_tuple *) noexcept;
template <typename A, typename B, typename tag_tuple>
constexpr bool has_after_serialization_inside_f(const std::pair<A, B> *, tag_tuple *) noexcept;
template <typename T, typename ...TT, typename tag_tuple> 
constexpr bool has_after_serialization_inside_f(const std::tuple<T, TT...> *, tag_tuple *) noexcept;
template <typename T, typename tag_tuple> constexpr bool has_after_serialization_inside_f(const std::tuple<T> *, tag_tuple *) noexcept;
template <typename T, size_t L, typename tag_tuple> constexpr bool has_after_serialization_inside_f(const std::array<T, L> *, tag_tuple *);
template <typename T, size_t L, typename tag_tuple> constexpr bool has_after_serialization_inside_f(T const (*)[L], tag_tuple *);
template <typename T, typename tag_tuple> constexpr bool has_after_serialization_inside_f(const std::unique_ptr<T> *, tag_tuple *);
template <typename T, typename tag_tuple> constexpr bool has_after_serialization_inside_f(const std::shared_ptr<T> *, tag_tuple *);
template <typename T, typename tag_tuple> constexpr bool has_after_serialization_inside_f(const T * const *, tag_tuple *);
template <typename T, typename tag_tuple> constexpr bool has_after_serialization_inside_f(const std::optional<T> *, tag_tuple *);

template <typename T, typename tag_tuple> constexpr bool has_after_serialization_inside_f(const expected<T>*, tag_tuple *tags) noexcept
{ std::remove_cvref_t<T> *x = nullptr; return has_after_serialization_inside_f(x, tags); }
template <typename C, typename tag_tuple> 
constexpr typename std::enable_if<is_serializable_container<C>::value && !has_tuple_for_serialization_tup_v<false, C, tag_tuple>, bool>::type 
has_after_serialization_inside_f(const C*, tag_tuple *tags) noexcept
{typename serializable_value_type<C>::type *x = nullptr; return has_after_serialization_inside_f(x, tags); }
template <typename S, typename tag_tuple> constexpr typename std::enable_if<has_tuple_for_serialization_tup_v<false, S, tag_tuple>, bool>::type
has_after_serialization_inside_f(const S *s, tag_tuple *tags) noexcept
{ std::remove_cvref_t<decltype(invoke_tuple_for_serialization_tup(*s, std::move(*tags)))> *x = nullptr; return has_after_serialization_tup_v<S, tag_tuple> || has_after_serialization_inside_f(x, tags); }
#ifdef HAVE_BOOST_PFR
template <typename S, typename tag_tuple> constexpr typename std::enable_if<is_really_auto_serializable_v<S> && !has_tuple_for_serialization_tup_v<false, S, tag_tuple>, bool>::type
has_after_serialization_inside_f(const S *s, tag_tuple *tags) noexcept
{ std::remove_cvref_t<decltype(boost::pfr::structure_tie(*s))> *x = nullptr; return has_after_serialization_inside_f(x, tags); }
#endif
template <typename A, typename B, typename tag_tuple> 
constexpr bool has_after_serialization_inside_f(const std::pair<A, B> *, tag_tuple *tags) noexcept
{ std::remove_cvref_t<A>* x = nullptr; std::remove_cvref_t <B>*y = nullptr; return has_after_serialization_inside_f(x, tags) || has_after_serialization_inside_f(y, tags); }
template <typename T, typename ...TT, typename tag_tuple> 
constexpr bool has_after_serialization_inside_f(const std::tuple<T, TT...> *, tag_tuple *tags) noexcept
{ std::remove_cvref_t<T> *x = nullptr; std::tuple<TT...> *y=nullptr; return has_after_serialization_inside_f(x, tags) || has_after_serialization_inside_f(y, tags); }
template <typename T, typename tag_tuple> constexpr bool has_after_serialization_inside_f(const std::tuple<T> *, tag_tuple *tags) noexcept
{ std::remove_cvref_t<T> *x = nullptr; return has_after_serialization_inside_f(x, tags); }
template <typename T, size_t L, typename tag_tuple> constexpr bool has_after_serialization_inside_f(const std::array<T, L> *, tag_tuple *tags)
{ std::remove_cvref_t<T> *x = nullptr; return has_after_serialization_inside_f(x, tags); }
template <typename T, size_t L, typename tag_tuple> constexpr bool has_after_serialization_inside_f(T const (*)[L], tag_tuple *tags)
{ std::remove_cvref_t<T> *x = nullptr; return has_after_serialization_inside_f(x, tags); }
template <typename T, typename tag_tuple> constexpr bool has_after_serialization_inside_f(const std::unique_ptr<T> *, tag_tuple *tags)
{ std::remove_cvref_t<T> *x = nullptr; return has_after_serialization_inside_f(x, tags); }
template <typename T, typename tag_tuple> constexpr bool has_after_serialization_inside_f(const std::shared_ptr<T> *, tag_tuple *tags)
{ std::remove_cvref_t<T> *x = nullptr; return has_after_serialization_inside_f(x, tags); }
template <typename T, typename tag_tuple> constexpr bool has_after_serialization_inside_f(const T * const *, tag_tuple *tags) //works for char*, too (will return false)
{ std::remove_cvref_t<T> *x = nullptr; return has_after_serialization_inside_f(x, tags); }
template <typename T, typename tag_tuple> constexpr bool has_after_serialization_inside_f(const std::optional<T> *, tag_tuple *tags)
{ std::remove_cvref_t<T> *x = nullptr; return has_after_serialization_inside_f(x, tags); }

template <typename T, typename ...tags> 
constexpr bool has_after_serialization_inside_v = has_after_serialization_inside_f(std::add_pointer_t<std::add_const_t<std::remove_cvref_t<T>>>(),
                                                                                   (std::tuple<tags...> *)nullptr);

struct err_place {
    std::exception_ptr ex;
    const void *obj = nullptr;
    err_place() = default;
    err_place(std::exception_ptr &&e, const void *o) : ex(std::move(e)), obj(o) {}
};

template <typename T, typename ...tags> 
inline typename std::enable_if<!is_serializable_container<T>::value && !has_tuple_for_serialization<false, T, tags...>::value && !is_really_auto_serializable_v<T>, err_place>::type
call_before_serialization(const T*, tags...) noexcept { return {}; } //default generic: non container, non struct
template <typename T, typename ...tags> inline err_place call_before_serialization(const uf::expected<T> *, tags...) noexcept;
template <typename C, typename ...tags> 
inline typename std::enable_if<is_serializable_container<C>::value && !has_tuple_for_serialization<false, C, tags...>::value, err_place>::type 
call_before_serialization(const C*, tags...) noexcept;
template <typename S, typename ...tags> 
inline typename std::enable_if<has_tuple_for_serialization<false, S, tags...>::value, err_place>::type call_before_serialization(const S*, tags...) noexcept;
template <typename S, typename ...tags> 
inline typename std::enable_if<is_really_auto_serializable_v<S> && !has_tuple_for_serialization<false, S, tags...>::value, err_place>::type call_before_serialization(const S*, tags...) noexcept;
template <typename A, typename B, typename ...tags> inline err_place call_before_serialization(const std::pair<A, B> *, tags...) noexcept;
template <typename T, typename ...TT, typename ...tags> inline err_place call_before_serialization(const std::tuple<T, TT...> *, tags...) noexcept;
template <typename T, typename ...tags> inline err_place call_before_serialization(const std::tuple<T> *, tags...) noexcept;
template <typename T, size_t L, typename ...tags> inline err_place call_before_serialization(const std::array<T, L> *, tags...) noexcept;
template <typename T, size_t L, typename ...tags> inline err_place call_before_serialization(T const (*)[L], tags...) noexcept;
template <typename T, typename ...tags> inline err_place call_before_serialization(const std::unique_ptr<T> *, tags...) noexcept;
template <typename T, typename ...tags> inline err_place call_before_serialization(const std::shared_ptr<T> *, tags...) noexcept;
template <typename T, typename ...tags> inline err_place call_before_serialization(const T * const *, tags...) noexcept;
template <typename T, typename ...tags> inline err_place call_before_serialization(const std::optional<T> *, tags...) noexcept;

template <typename C, typename ...tags> 
inline typename std::enable_if<is_serializable_container<C>::value && !has_tuple_for_serialization<false, C, tags...>::value, err_place>::type 
call_before_serialization(const C*c, tags... tt) noexcept
{ (void)c; ignore_pack(tt...);
    if constexpr (has_before_serialization_inside_v<C, tags...>) for (auto &e : *c) 
        try { if (auto r = call_before_serialization(&e, tt...); r.obj) return r; } 
        catch (...) { return {std::current_exception(), &e}; } return {};
}
template <typename S, typename ...tags> 
inline typename std::enable_if<has_tuple_for_serialization<false, S, tags...>::value, err_place>::type 
call_before_serialization(const S *s, tags... tt) noexcept
{ try {
    if constexpr (has_before_serialization_v<S, tags...>) invoke_before_serialization(*s, tt...);
    if constexpr (has_before_serialization_inside_v<decltype(invoke_tuple_for_serialization(*s, tt...)), tags...>) 
        { auto &&x = invoke_tuple_for_serialization(*s, tt...);  return call_before_serialization(&x, tt...); }
  } catch (...) { return { std::current_exception(), s }; } return {};
}
#ifdef HAVE_BOOST_PFR
template <typename S, typename ...tags> 
inline typename std::enable_if<is_really_auto_serializable_v<S> && !has_tuple_for_serialization<false, S, tags...>::value, err_place>::type
call_before_serialization(const S *s, tags... tt) noexcept
{ try {
    if constexpr (has_before_serialization_inside_v<decltype(boost::pfr::structure_tie(*s)), tags...>)
        { auto &&x = boost::pfr::structure_tie(*s);  return call_before_serialization(&x, tt...); }
  } catch (...) { return { std::current_exception(), s }; } return {};
}
#endif
template <typename A, typename B, typename ...tags> inline err_place call_before_serialization(const std::pair<A, B> *p, tags... tt) noexcept
{ (void)p; ignore_pack(tt...);
  if constexpr(has_before_serialization_inside_v<A, tags...>) if (auto r = call_before_serialization(&p->first, tt...); r.obj) return r;
  if constexpr(has_before_serialization_inside_v<B, tags...>) return call_before_serialization(&p->second, tt...); else return {}; }
template <typename T, typename ...TT, typename ...tags> inline err_place call_before_serialization(const std::tuple<T, TT...> *t, tags... tt) noexcept
{ (void)t; ignore_pack(tt...);
  if constexpr (has_before_serialization_inside_v<T, tags...>) if (auto r = call_before_serialization(&std::get<0>(*t), tt...); r.obj) return r;
  if constexpr (has_before_serialization_inside_v<std::tuple<TT...>, tags...>) { auto ttail = tuple_tail(*t); return call_before_serialization(&ttail, tt...);} return {}; }
template <typename T, typename ...tags> inline err_place call_before_serialization(const std::tuple<T> *t, tags... tt) noexcept
{ (void)t; ignore_pack(tt...);  if constexpr (has_before_serialization_inside_v<T, tags...>) if (auto r = call_before_serialization(&std::get<0>(*t), tt...); r.obj) return r; return {}; }
template <typename T, size_t L, typename ...tags> inline err_place call_before_serialization(const std::array<T, L> *c, tags... tt) noexcept
{ (void)c; ignore_pack(tt...);  if constexpr (has_before_serialization_inside_v<std::array<T,L>, tags...>) for (auto &e : *c) 
      try { if (auto r = call_before_serialization(&e, tt...); r.obj) return r; } 
      catch (...) { return {std::current_exception(), &e}; } return {}; }
template <typename T, size_t L, typename ...tags> inline err_place call_before_serialization(T const (*t)[L], tags... tt) noexcept
{ (void)t; ignore_pack(tt...); if constexpr (has_before_serialization_inside_v<T, tags...>) for (size_t u = 0; u<L; u++) 
    if (auto r = call_before_serialization(t+u, tt...); r.obj) return r; 
   return {}; }
template <typename T, typename ...tags> inline err_place call_before_serialization(const std::unique_ptr<T> *p, tags... tt) noexcept
{ (void)p; ignore_pack(tt...); if constexpr (has_before_serialization_inside_v<T, tags...>) if (*p) 
    if (auto r = call_before_serialization(p->get(), tt...); r.obj) return r; 
  return {}; }
template <typename T, typename ...tags> inline err_place call_before_serialization(const std::shared_ptr<T> *p, tags... tt) noexcept
{ (void)p; ignore_pack(tt...); if constexpr (has_before_serialization_inside_v<T, tags...>) if (*p) 
    if (auto r = call_before_serialization(p->get(), tt...); r.obj) return r; 
   return {}; }
template <typename T, typename ...tags> inline err_place call_before_serialization(const T * const *p, tags... tt) noexcept //works for char*, too (will return false)
{ (void)p; ignore_pack(tt...); if constexpr (has_before_serialization_inside_v<T, tags...>) if (*p) 
    if (auto r = call_before_serialization(*p, tt...); r.obj) return r; 
   return {}; }
template <typename T, typename ...tags> inline err_place call_before_serialization(const std::optional<T> *o, tags... tt) noexcept
{ (void)o; if constexpr(has_before_serialization_inside_v<T, tags...>) if (*o) 
    if (auto r = call_before_serialization(&o->value(), tt...); r.obj) return r; 
  return {}; }


template <typename T, typename ...tags> 
inline typename std::enable_if<!is_serializable_container<T>::value && !has_tuple_for_serialization<false, T, tags...>::value && !is_really_auto_serializable_v<T>, void>::type
call_after_serialization(const T*, bool, tags...) noexcept {} //default generic: non container, non struct
template <typename T, typename ...tags> inline void call_after_serialization(const uf::expected<T> *, bool, tags...) noexcept(is_noexcept_for<T, tags...>(nt::len));
template <typename C, typename ...tags> inline typename std::enable_if<is_serializable_container<C>::value && !has_tuple_for_serialization<false, C, tags...>::value, void>::type call_after_serialization(const C*, bool, tags...) noexcept(is_noexcept_for<C, tags...>(nt::len));
template <typename S, typename ...tags> inline typename std::enable_if<has_tuple_for_serialization<false, S, tags...>::value, void>::type call_after_serialization(const S*, bool, tags...) noexcept(is_noexcept_for<S, tags...>(nt::len));
template <typename S, typename ...tags> inline typename std::enable_if<is_really_auto_serializable_v<S> && !has_tuple_for_serialization<false, S, tags...>::value, void>::type call_after_serialization(const S *, bool, tags...) noexcept(is_noexcept_for<S, tags...>(nt::len));
template <typename A, typename B, typename ...tags> inline void call_after_serialization(const std::pair<A, B> *, bool, tags...) noexcept(is_noexcept_for<A, tags...>(nt::len) && is_noexcept_for<B, tags...>(nt::len));
template <typename T, typename ...TT, typename ...tags> inline void call_after_serialization(const std::tuple<T, TT...> *, bool, tags...) noexcept(is_noexcept_for<std::tuple<T, TT...>, tags...>(nt::len));
template <typename T, typename ...tags> inline void call_after_serialization(const std::tuple<T> *, bool, tags...) noexcept(is_noexcept_for<T, tags...>(nt::len));
template <typename T, size_t L, typename ...tags> inline void call_after_serialization(const std::array<T, L> *, bool, tags...) noexcept(is_noexcept_for<T, tags...>(nt::len));
template <typename T, size_t L, typename ...tags> inline void call_after_serialization(T const (*)[L], bool, tags...) noexcept(is_noexcept_for<T, tags...>(nt::len));
template <typename T, typename ...tags> inline void call_after_serialization(const std::unique_ptr<T> *, bool, tags...) noexcept(is_noexcept_for<T, tags...>(nt::len));
template <typename T, typename ...tags> inline void call_after_serialization(const std::shared_ptr<T> *, bool, tags...) noexcept(is_noexcept_for<T, tags...>(nt::len));
template <typename T, typename ...tags> inline void call_after_serialization(const T * const *, bool, tags...) noexcept(is_noexcept_for<T, tags...>(nt::len));
template <typename T, typename ...tags> inline void call_after_serialization(const std::optional<T> *, bool, tags...) noexcept(is_noexcept_for<T, tags...>(nt::len));

template <typename C, typename ...tags> 
inline typename std::enable_if<is_serializable_container<C>::value && !has_tuple_for_serialization<false, C, tags...>::value, void>::type 
call_after_serialization(const C*c, bool success, tags... tt) noexcept(is_noexcept_for<C, tags...>(nt::len))
{ (void)c; ignore_pack(tt...);  if constexpr (has_after_serialization_inside_v<C, tags...>) for (auto &e : *c) call_after_serialization(&e, success, tt...); }
template <typename S, typename ...tags> inline typename std::enable_if<has_tuple_for_serialization<false, S, tags...>::value, void>::type 
call_after_serialization(const S *s, bool success, tags... tt) noexcept(is_noexcept_for<S, tags...>(nt::len))
{ if constexpr (has_after_serialization_inside_v<decltype(invoke_tuple_for_serialization(*s, tt...)), tags...>) 
      { auto &&x = invoke_tuple_for_serialization(*s, tt...);  return call_after_serialization(&x, success, tt...); }
  if constexpr (has_after_serialization_v<S, tags...>) invoke_after_serialization(*s, success, tt...); }
#ifdef HAVE_BOOST_PFR
template <typename S, typename ...tags> inline typename std::enable_if<is_really_auto_serializable_v<S> && !has_tuple_for_serialization<false, S, tags...>::value, void>::type
call_after_serialization(const S *s, bool success, tags... tt) noexcept(is_noexcept_for<S, tags...>(nt::len))
{ if constexpr (has_after_serialization_inside_v<decltype(boost::pfr::structure_tie(*s)), tags...>)
      { auto &&x = boost::pfr::structure_tie(*s);  return call_after_serialization(&x, success, tt...); } }
#endif
template <typename A, typename B, typename ...tags> inline void call_after_serialization(const std::pair<A, B> *p, bool success, tags... tt) noexcept(is_noexcept_for<A, tags...>(nt::len) && is_noexcept_for<B, tags...>(nt::len))
{ (void)p; ignore_pack(tt...); if constexpr (has_after_serialization_inside_v<A, tags...>) call_after_serialization(&p->first, success, tt...);
  if constexpr (has_after_serialization_inside_v<B, tags...>) call_after_serialization(&p->second, success, tt...);}
template <typename T, typename ...TT, typename ...tags> inline void call_after_serialization(const std::tuple<T, TT...> *t, bool success, tags... tt) noexcept(is_noexcept_for<std::tuple<T,TT...>, tags...>(nt::len))
{ (void)t; ignore_pack(tt...); if constexpr (has_after_serialization_inside_v<T, tags...>) call_after_serialization(&std::get<0>(*t), success, tt...);
  if constexpr (has_after_serialization_inside_v<std::tuple<TT...>, tags...>) {auto x = tuple_tail(*t); call_after_serialization(&x, success, tt...);} }
template <typename T, typename ...tags> inline void call_after_serialization(const std::tuple<T> *t, bool success, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::len))
{ (void)t; ignore_pack(tt...); if constexpr (has_after_serialization_inside_v<T, tags...>) call_after_serialization(&std::get<0>(*t), success, tt...); }
template <typename T, size_t L, typename ...tags> inline void call_after_serialization(const std::array<T, L> *c, bool success, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::len))
{ (void)c; ignore_pack(tt...); if constexpr (has_after_serialization_inside_v<T, tags...>) for (auto &e : *c) call_after_serialization(&e, success, tt...); }
template <typename T, size_t L, typename ...tags> inline void call_after_serialization(T const (*c)[L], bool success, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::len))
{ (void)c; ignore_pack(tt...); if constexpr (has_after_serialization_inside_v<T, tags...>) for (size_t u=0;u<L;u++) call_after_serialization(c+u, success, tt...); }
template <typename T, typename ...tags> inline void call_after_serialization(const std::unique_ptr<T> *p, bool success, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::len))
{ (void)p; ignore_pack(tt...); if constexpr (has_after_serialization_inside_v<T, tags...>) if (*p) call_after_serialization(p->get(), success, tt...); }
template <typename T, typename ...tags> inline void call_after_serialization(const std::shared_ptr<T> *p, bool success, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::len))
{ (void)p; ignore_pack(tt...);  if constexpr (has_after_serialization_inside_v<T, tags...>) if (*p) call_after_serialization(p->get(), success, tt...); }
template <typename T, typename ...tags> inline void call_after_serialization(const T * const *p, bool success, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::len)) //works for char*, too (will return false)
{ (void)p; ignore_pack(tt...); if constexpr (has_after_serialization_inside_v<T, tags...>) if (*p) call_after_serialization(*p, success, tt...); }
template <typename T, typename ...tags> inline void call_after_serialization(const std::optional<T> *o, bool success, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::len))
{ (void)o; ignore_pack(tt...); if constexpr (has_after_serialization_inside_v<T, tags...>) if (*o) call_after_serialization(&o->value(), success, tt...); }

/** Call this override if the before_serialization or tuple_for_serialization has thrown an exception.
 * Walk the data type and go into all branch even if there is no after_serialization there, so that we
 * can stop at the object that has thrown. If that object is found, call after_serialization for it
 * (if exists) and re-throw the exception.*/
template <typename T, typename ...tags> 
inline typename std::enable_if<!is_serializable_container<T>::value && !has_tuple_for_serialization_v<false, T, tags...> && !is_really_auto_serializable_v<T>, void>::type
call_after_serialization(const T*, err_place, tags...) noexcept {} //default generic: non container, non struct
template <typename T, typename ...tags> inline void call_after_serialization(const uf::expected<T> *, err_place, tags...) noexcept(is_noexcept_for<T, tags...>(nt::ser));
template <typename C, typename ...tags> inline typename std::enable_if<is_serializable_container<C>::value && !has_tuple_for_serialization_v<false, C, tags...>, void>::type call_after_serialization(const C*, err_place, tags...) noexcept(is_noexcept_for<C, tags...>(nt::ser));
template <typename S, typename ...tags> inline typename std::enable_if<has_tuple_for_serialization_v<false, S, tags...>, void>::type call_after_serialization(const S*, err_place, tags...) noexcept(is_noexcept_for<S, tags...>(nt::ser));
template <typename S, typename ...tags> inline typename std::enable_if<is_really_auto_serializable_v<S> && !has_tuple_for_serialization_v<false, S, tags...>, void>::type call_after_serialization(const S *, err_place, tags...) noexcept(is_noexcept_for<S, tags...>(nt::ser));
template <typename A, typename B, typename ...tags> inline void call_after_serialization(const std::pair<A, B> *, err_place, tags...) noexcept(is_noexcept_for<A, tags...>(nt::ser) && is_noexcept_for<B, tags...>(nt::ser));
template <typename T, typename ...TT, typename ...tags> inline void call_after_serialization(const std::tuple<T, TT...> *, err_place, tags...) noexcept(is_noexcept_for<std::tuple<T,TT...>, tags...>(nt::ser));
template <typename T, typename ...tags> inline void call_after_serialization(const std::tuple<T> *, err_place, tags...) noexcept(is_noexcept_for<T, tags...>(nt::ser));
template <typename T, size_t L, typename ...tags> inline void call_after_serialization(const std::array<T, L> *, err_place, tags...) noexcept(is_noexcept_for<T, tags...>(nt::ser));
template <typename T, size_t L, typename ...tags> inline void call_after_serialization(T const (*)[L], err_place, tags...) noexcept(is_noexcept_for<T, tags...>(nt::ser));
template <typename T, typename ...tags> inline void call_after_serialization(const std::unique_ptr<T> *, err_place, tags...) noexcept(is_noexcept_for<T, tags...>(nt::ser));
template <typename T, typename ...tags> inline void call_after_serialization(const std::shared_ptr<T> *, err_place, tags...) noexcept(is_noexcept_for<T, tags...>(nt::ser));
template <typename T, typename ...tags> inline void call_after_serialization(const T * const *, err_place, tags...) noexcept(is_noexcept_for<T, tags...>(nt::ser));
template <typename T, typename ...tags> inline void call_after_serialization(const std::optional<T> *, err_place, tags...) noexcept(is_noexcept_for<T, tags...>(nt::ser));

template <typename C, typename ...tags> inline typename std::enable_if<is_serializable_container<C>::value && !has_tuple_for_serialization_v<false, C, tags...>, void>::type call_after_serialization(const C*c, err_place err, tags... tt)  noexcept(is_noexcept_for<C, tags...>(nt::ser))
{ for (auto &e : *c) call_after_serialization(&e, err, tt...); }
template <typename S, typename ...tags> inline typename std::enable_if<has_tuple_for_serialization_v<false, S, tags...>, void>::type call_after_serialization(const S *s, err_place err, tags... tt)  noexcept(is_noexcept_for<S, tags...>(nt::ser))
{ if constexpr (has_after_serialization_v<S, tags...>) invoke_after_serialization(*s, false, tt...);
  if (err.obj == s) std::rethrow_exception(err.ex);
  std::exception_ptr ex;
  auto &&x = invoke_tuple_for_serialization(*s, tt...);
  return call_after_serialization(&x, err, tt...); }
#ifdef HAVE_BOOST_PFR
template <typename S, typename ...tags> inline typename std::enable_if<is_really_auto_serializable_v<S> && !has_tuple_for_serialization_v<false, S, tags...>, void>::type call_after_serialization(const S *s, err_place err, tags... tt)  noexcept(is_noexcept_for<S, tags...>(nt::ser))
{ if (err.obj == s) std::rethrow_exception(err.ex);
  std::exception_ptr ex;
  auto &&x = boost::pfr::structure_tie(*s);
  return call_after_serialization(&x, err, tt...); }
#endif
template <typename A, typename B, typename ...tags> inline void call_after_serialization(const std::pair<A, B> *p, err_place err, tags... tt) noexcept(is_noexcept_for<A, tags...>(nt::ser) && is_noexcept_for<B, tags...>(nt::ser))
{ call_after_serialization(&p->first, err, tt...); call_after_serialization(&p->second, err, tt...); }
template <typename T, typename ...TT, typename ...tags> inline void call_after_serialization(const std::tuple<T, TT...> *t, err_place err, tags... tt) noexcept(is_noexcept_for<std::tuple<T, TT...>, tags...>(nt::ser))
{ call_after_serialization(&std::get<0>(*t), err, tt...); auto x = tuple_tail(*t); call_after_serialization(&x, err, tt...); }
template <typename T, typename ...tags> inline void call_after_serialization(const std::tuple<T> *t, err_place err, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::ser))
{ return call_after_serialization(&std::get<0>(*t), err, tt...); }
template <typename T, size_t L, typename ...tags> inline void call_after_serialization(const std::array<T, L> *c, err_place err, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::ser))
{ for (auto &e : *c) call_after_serialization(&e, err, tt...); }
template <typename T, size_t L, typename ...tags> inline void call_after_serialization(T const (*c)[L], err_place err, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::ser))
{ for (size_t u=0;u<L;u++) call_after_serialization(c+u, err, tt...); }
template <typename T, typename ...tags> inline void call_after_serialization(const std::unique_ptr<T> *p, err_place err, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::ser))
{ if (*p) call_after_serialization(p->get(), err, tt...); }
template <typename T, typename ...tags> inline void call_after_serialization(const std::shared_ptr<T> *p, err_place err, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::ser))
{ if (*p) call_after_serialization(p->get(), err, tt...); }
template <typename T, typename ...tags> inline void call_after_serialization(const T * const *p, err_place err, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::ser))//works for char*, too (will return false)
{ if (*p) call_after_serialization(*p, err, tt...); }
template <typename T, typename ...tags> inline void call_after_serialization(const std::optional<T> *o, err_place err, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::ser))
{ if (*o) call_after_serialization(&o->value(), err, tt...); }

template <typename ...tags> constexpr size_t serialize_len(const bool&, tags...) noexcept { return 1; }
template <typename ...tags> constexpr size_t serialize_len(const unsigned char&, tags...) noexcept{ return 1; }
template <typename ...tags> constexpr size_t serialize_len(const signed char&, tags...) noexcept { return 1; }
template <typename ...tags> constexpr size_t serialize_len(const char&, tags...) noexcept { return 1; }
template <typename ...tags> constexpr size_t serialize_len(const uint16_t&, tags...) noexcept { return 4; }
template <typename ...tags> constexpr size_t serialize_len(const int16_t&, tags...) noexcept { return 4; }
template <typename ...tags> constexpr size_t serialize_len(const uint32_t&, tags...) noexcept { return 4; }
template <typename ...tags> constexpr size_t serialize_len(const int32_t&, tags...) noexcept { return 4; }
template <typename ...tags> constexpr size_t serialize_len(const uint64_t&, tags...) noexcept { return 8; }
template <typename ...tags> constexpr size_t serialize_len(const int64_t&, tags...) noexcept { return 8; }
template <typename ...tags> constexpr size_t serialize_len(const float&, tags...) noexcept { return 8; }
template <typename ...tags> constexpr size_t serialize_len(const double&, tags...) noexcept { return 8; }
template <typename ...tags> constexpr size_t serialize_len(const long double&, tags...) noexcept { return 8; }
template <typename ...tags> inline size_t serialize_len(const std::string &s, tags...) noexcept { return 4+s.length(); }
template <typename ...tags> inline size_t serialize_len(const std::string_view &s, tags...) noexcept { return 4+s.length(); }
//If I enable the below overload, I get ambigous resolution for serialize_len(const char [X]). 
//As a workaround I let the serialize_len(const char *) be selected.
//Note this is why the serialize_len(const T[X]) overload excludes T=char.
//template <size_t LEN, typename ...tags>
//constexpr size_t serialize_len(char const (&b)[LEN], tags...) noexcept { static_assert(LEN); assert(!b[LEN - 1]); (void)b;  return 4 + LEN - 1; }
template <typename ...tags> inline size_t serialize_len(const char* const &s, tags...) noexcept { return 4+strlen(s); }
template <typename T, typename ...tags> constexpr size_t serialize_len(const expected<T>&, tags...) noexcept(is_noexcept_for<T, tags...>(nt::len));
template <typename E, typename ...tags>
constexpr typename std::enable_if<std::is_enum<E>::value, size_t>::type serialize_len(const E &, tags...) noexcept;
template <typename C, typename ...tags>
constexpr typename std::enable_if<is_serializable_container<C>::value && !has_tuple_for_serialization<false, C, tags...>::value, size_t>::type 
serialize_len(const C &, tags...) noexcept(is_noexcept_for<C, tags...>(nt::len));
template <typename S, typename ...tags>
constexpr typename std::enable_if<has_tuple_for_serialization<false, S, tags...>::value, size_t>::type serialize_len(const S &, tags...)  noexcept(is_noexcept_for<S, tags...>(nt::len));
template <typename S, typename ...tags>
constexpr typename std::enable_if<is_really_auto_serializable_v<S> && !has_tuple_for_serialization<false, S, tags...>::value, size_t>::type serialize_len(const S &, tags...)  noexcept(is_noexcept_for<S, tags...>(nt::len));
template <typename A, typename B, typename ...tags> constexpr size_t serialize_len(const std::pair<A, B> &, tags...)  noexcept(is_noexcept_for<A, tags...>(nt::len) && is_noexcept_for<B, tags...>(nt::len));
template <typename T, typename ...TT, typename ...tags> constexpr size_t serialize_len(const std::tuple<T, TT...> &, tags...)  noexcept(is_noexcept_for<std::tuple<T, TT...>, tags...>(nt::len));
template <typename ...tags> constexpr size_t serialize_len(const std::tuple<>&, tags...) noexcept { return 0; }
template <typename ...tags> constexpr size_t serialize_len(const std::monostate&, tags...) noexcept { return 0; }
template <typename T, size_t L, typename ...tags> constexpr size_t serialize_len(const std::array<T, L> &, tags...) noexcept(is_noexcept_for<T, tags...>(nt::len));
template <typename T, size_t L, typename ...tags, typename = std::enable_if_t<!std::is_same_v<char, T>>> constexpr size_t serialize_len(T const (&)[L], tags...) noexcept(is_noexcept_for<T, tags...>(nt::len));
template <typename T, typename ...tags> constexpr size_t serialize_len(const std::unique_ptr<T> &, tags...) noexcept(is_noexcept_for<T, tags...>(nt::len));
template <typename T, typename ...tags> constexpr size_t serialize_len(const std::shared_ptr<T> &, tags...) noexcept(is_noexcept_for<T, tags...>(nt::len));
template <typename T, typename ...tags> constexpr size_t serialize_len(const T* const &, tags...) noexcept(is_noexcept_for<T, tags...>(nt::len));
template <typename T, typename ...tags> constexpr size_t serialize_len(const std::optional<T> &, tags...) noexcept(is_noexcept_for<T, tags...>(nt::len));

template <typename E, typename ...tags> //enum
constexpr typename std::enable_if<std::is_enum<E>::value, size_t>::type
serialize_len(const E &, tags...)  noexcept { return 4; }
template <typename C, typename ...tags> //containers with begin() end() size() clear() and value_type includes maps, too!
constexpr typename std::enable_if<is_serializable_container<C>::value && !has_tuple_for_serialization<false, C, tags...>::value, size_t>::type
serialize_len(const C &c, tags... tt)  noexcept(is_noexcept_for<C, tags...>(nt::len)) {
    if constexpr (is_void_like<false, C>::value) return 0;
    else {size_t ret = 4; for (auto &e : c) ret += serialize_len(e, tt...); return ret;}
}
template <typename ...tags> inline size_t serialize_len(const std::vector<bool>& c, tags...) noexcept { return 4 + c.size(); }
template <typename S, typename ...tags>
constexpr typename std::enable_if<has_tuple_for_serialization<false, S, tags...>::value, size_t>::type
serialize_len(const S &s, tags... tt) noexcept(is_noexcept_for<S, tags...>(nt::len)) { return serialize_len(invoke_tuple_for_serialization(s, tt...), tt...); }
#ifdef HAVE_BOOST_PFR
template <typename S, typename ...tags>
constexpr typename std::enable_if<is_really_auto_serializable_v<S> && !has_tuple_for_serialization<false, S, tags...>::value, size_t>::type
serialize_len(const S &s, tags... tt) noexcept(is_noexcept_for<S, tags...>(nt::len)) { return serialize_len(boost::pfr::structure_tie(s), tt...); }
#endif
template <typename A, typename B, typename ...tags> //pair
constexpr size_t serialize_len(const std::pair<A, B> &p, tags... tt) noexcept(is_noexcept_for<A, tags...>(nt::len) && is_noexcept_for<B, tags...>(nt::len))
{ return serialize_len(p.first, tt...)+serialize_len(p.second, tt...); }
template <typename T, typename ...TT, typename ...tags> //tuples
constexpr size_t serialize_len(const std::tuple<T, TT...> &t, tags... tt) noexcept(is_noexcept_for<std::tuple<T, TT...>, tags...>(nt::len))
{ return serialize_len(std::get<0>(t), tt...)+serialize_len(tuple_tail(t), tt...); }
template <typename T, size_t L, typename ...tags>
constexpr size_t serialize_len(const std::array<T, L> &o, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::len)) 
{ size_t ret = 0; for (auto &t : o) ret += serialize_len(t, tt...); return ret; }
template <typename T, size_t L, typename ...tags, typename>
constexpr size_t serialize_len(T const (&o)[L], tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::len)) 
{ size_t ret = 0; for (auto &t : o) ret += serialize_len(t, tt...); return ret; }
template <typename T, typename ...tags> constexpr size_t serialize_len(const std::unique_ptr<T> &p, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::len)) 
{ if (p) return 1 + serialize_len(*p, tt...); else return 1; }
template <typename T, typename ...tags> constexpr size_t serialize_len(const std::shared_ptr<T> &p, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::len))
{ if (p) return 1 + serialize_len(*p, tt...); else return 1; }
template <typename T, typename ...tags> constexpr size_t serialize_len(const T* const &p, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::len))
{ if (p) return 1 + serialize_len(*p, tt...); else return 1; }
template <typename T, typename ...tags> constexpr size_t serialize_len(const std::optional<T> &o, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::len))
{ if (o) return 1 + serialize_len(*o, tt...); else return 1; }

template <typename ...tags> inline void serialize_to(const bool &o, char *&p, tags...) noexcept { *(p++) = o ? 1 : 0; }
template <typename ...tags> inline void serialize_to(const unsigned char &o, char *&p, tags...) noexcept { *(p++) = o; }
template <typename ...tags> inline void serialize_to(const signed char &o, char *&p, tags...) noexcept { *(p++) = o; }
template <typename ...tags> inline void serialize_to(const char &o, char *&p, tags...) noexcept { *(p++) = o; }
template <typename ...tags> inline void serialize_to(const uint16_t &o, char *&p, tags...) noexcept { *reinterpret_cast<uint32_t*>(p) = htobe32(uint32_t(o)); p += 4; }
template <typename ...tags> inline void serialize_to(const int16_t &o, char *&p, tags...) noexcept { *reinterpret_cast<int32_t*>(p) = htobe32(int32_t(o)); p += 4; }
template <typename ...tags> inline void serialize_to(const uint32_t &o, char *&p, tags...) noexcept { *reinterpret_cast<uint32_t*>(p) = htobe32(o); p += 4; }
template <typename ...tags> inline void serialize_to(const int32_t &o, char *&p, tags...) noexcept { *reinterpret_cast<int32_t*>(p) = htobe32(o); p += 4; }
template <typename ...tags> inline void serialize_to(const uint64_t &o, char *&p, tags...) noexcept { *reinterpret_cast<uint64_t*>(p) = htobe64(o); p += 8; }
template <typename ...tags> inline void serialize_to(const int64_t &o, char *&p, tags...) noexcept { *reinterpret_cast<uint64_t*>(p) = htobe64(o); p += 8; }
template <typename ...tags> inline void serialize_to(const float &o, char *&p, tags...) noexcept { *reinterpret_cast<double*>(p) = o; p += 8; }
template <typename ...tags> inline void serialize_to(const double &o, char *&p, tags...) noexcept { *reinterpret_cast<double*>(p) = o; p += 8; }
template <typename ...tags> inline void serialize_to(const long double o, char *&p, tags...) noexcept { *reinterpret_cast<double*>(p) = o; p += 8; }
template <typename ...tags> inline void serialize_to(const std::string &s, char *&p, tags...) noexcept { serialize_to(uint32_t(s.size()), p); memcpy(p, s.data(), s.size()); p += s.size(); }
template <typename ...tags> inline void serialize_to(const std::string_view &s, char *&p, tags...) noexcept { serialize_to(uint32_t(s.size()), p); memcpy(p, s.data(), s.size()); p += s.size(); }
//See serialize_len() (of same signature) above why this is disabled.
//template <size_t LEN, typename ...tags> inline void serialize_to(char const (&s)[LEN], char *&p, tags...) noexcept { static_assert(LEN); assert(!s[LEN-1]); serialize_to(uint32_t(LEN-1), p); memcpy(p, s, LEN-1); p += LEN-1; }
template <typename ...tags> inline void serialize_to(const char * const &s, char *&p, tags...) noexcept { const uint32_t len = strlen(s); serialize_to(len, p); memcpy(p, s, len); p += len; }
template <typename T, typename ...tags> void serialize_to(const expected<T>&, char *&p, tags...tt) noexcept(is_noexcept_for<T, tags...>(nt::ser));
template <typename E, typename ...tags> typename std::enable_if<std::is_enum<E>::value>::type serialize_to(const E &e, char *&p, tags...) noexcept;
template <typename C, typename ...tags> typename std::enable_if<is_serializable_container<C>::value && !is_std_array<C>::value && !has_tuple_for_serialization<false, C, tags...>::value>::type 
serialize_to(const C &c, char *&p, tags...) noexcept(is_noexcept_for<C, tags...>(nt::ser));
template <typename S, typename ...tags> typename std::enable_if<has_tuple_for_serialization<false, S, tags...>::value && is_serializable_v<S, tags...>>::type 
serialize_to(const S &c, char *&p, tags...) noexcept(is_noexcept_for<S, tags...>(nt::ser));
template <typename S, typename ...tags> typename std::enable_if<is_really_auto_serializable_v<S> && !has_tuple_for_serialization<false, S, tags...>::value && is_serializable_v<S, tags...>>::type
serialize_to(const S &c, char *&p, tags...) noexcept(is_noexcept_for<S, tags...>(nt::ser));
template <typename A, typename B, typename ...tags> void serialize_to(const std::pair<A, B> &t, char *&p, tags...) noexcept(is_noexcept_for<A, tags...>(nt::ser) && is_noexcept_for<B, tags...>(nt::ser));
template <typename T, typename ...TT, typename ...tags> void serialize_to(const std::tuple<T, TT...> &t, char *&p, tags...) noexcept(is_noexcept_for<std::tuple<T, TT...>, tags...>(nt::ser));
template <typename ...tags> inline void serialize_to(const std::tuple<>&, char*&, tags...) noexcept {}
template <typename ...tags> inline void serialize_to(const std::monostate&, char*&, tags...) noexcept {}
template <typename T, size_t L, typename ...tags> void serialize_to(const std::array<T, L> &, char *&p, tags...) noexcept(is_noexcept_for<T, tags...>(nt::ser));
template <typename T, size_t L, typename ...tags, typename = std::enable_if_t<!std::is_same_v<char, T>>> void serialize_to(T const (&)[L], char *&p, tags...) noexcept(is_noexcept_for<T, tags...>(nt::ser));
template <typename T, typename ...tags> inline void serialize_to(const std::unique_ptr<T> &pp, char *&p, tags...) noexcept(is_noexcept_for<T, tags...>(nt::ser));
template <typename T, typename ...tags> inline void serialize_to(const std::shared_ptr<T> &pp, char *&p, tags...) noexcept(is_noexcept_for<T, tags...>(nt::ser));
template <typename T, typename ...tags> inline void serialize_to(const T* const &pp, char *&p, tags...) noexcept(is_noexcept_for<T, tags...>(nt::ser));
template <typename T, typename ...tags> inline void serialize_to(const std::optional<T> &o, char *&p, tags...) noexcept(is_noexcept_for<T, tags...>(nt::ser));

template <typename E, typename ...tags> inline typename std::enable_if<std::is_enum<E>::value>::type
serialize_to(const E &e, char *&p, tags...) noexcept { serialize_to(uint32_t(e), p); }
template <typename C, typename ...tags> inline typename std::enable_if<is_serializable_container<C>::value && !is_std_array<C>::value && !has_tuple_for_serialization<false, C, tags...>::value>::type
serialize_to(const C &c, char *&p, tags... tt) noexcept(is_noexcept_for<C, tags...>(nt::ser)) {
    if constexpr (is_void_like<false, C>::value) return;
    serialize_to(uint32_t(c.size()), p); for (auto &e : c) serialize_to(e, p, tt...);
}
template <typename ...tags> inline void serialize_to(const std::vector<bool> &c, char *&p, tags...) noexcept
{ serialize_to(uint32_t(c.size()), p); for (bool e : c) serialize_to(e, p); }
template <typename S, typename ...tags> inline typename std::enable_if<has_tuple_for_serialization<false, S, tags...>::value && is_serializable_v<S, tags...>>::type
serialize_to(const S &s, char *&p, tags... tt) noexcept(is_noexcept_for<S, tags...>(nt::ser)) { serialize_to(invoke_tuple_for_serialization(s, tt...), p, tt...); }
#ifdef HAVE_BOOST_PFR
template <typename S, typename ...tags> inline typename std::enable_if<is_really_auto_serializable_v<S> && !has_tuple_for_serialization<false, S, tags...>::value &&is_serializable_v<S, tags...>>::type
serialize_to(const S &s, char *&p, tags... tt) noexcept(is_noexcept_for<S, tags...>(nt::ser)) { serialize_to(boost::pfr::structure_tie(s), p, tt...); }
#endif
template <typename A, typename B, typename ...tags> //pair
inline void serialize_to(const std::pair<A, B> &t, char *&p, tags... tt) noexcept(is_noexcept_for<A, tags...>(nt::ser) && is_noexcept_for<B, tags...>(nt::ser))
{ serialize_to(t.first, p, tt...); serialize_to(t.second, p, tt...); }
template <typename T, typename ...TT, typename ...tags> //tuples
inline void serialize_to(const std::tuple<T, TT...> &t, char *&p, tags... tt) noexcept(is_noexcept_for<std::tuple<T, TT...>, tags...>(nt::ser)) 
{ serialize_to(std::get<0>(t), p, tt...);  serialize_to(tuple_tail(t), p, tt...); }
template <typename T, size_t L, typename ...tags> void serialize_to(const std::array<T, L> &o, char *&p, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::ser)) 
{ for(auto &t:o) serialize_to(t, p, tt...); }
template <typename T, size_t L, typename ...tags, typename> void serialize_to(T const (&o)[L], char *&p, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::ser)) 
{ for(auto &t:o) serialize_to(t, p, tt...); }
template <typename T, typename ...tags> inline void serialize_to(const std::unique_ptr<T> &pp, char *&p, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::ser)) 
{ serialize_to(bool(pp), p); if (pp) serialize_to(*pp, p, tt...); }
template <typename T, typename ...tags> inline void serialize_to(const std::shared_ptr<T> &pp, char *&p, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::ser)) 
{ serialize_to(bool(pp), p); if (pp) serialize_to(*pp, p, tt...); }
template <typename T, typename ...tags> inline void serialize_to(const T* const &pp, char *&p, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::ser)) 
{ serialize_to(bool(pp), p); if (pp) serialize_to(*pp, p, tt...); }
template <typename T, typename ...tags> inline void serialize_to(const std::optional<T> &o, char *&p, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::ser)) 
{ serialize_to(o.has_value(), p); if (o) serialize_to(*o, p, tt...);}

//return true on overrun
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, bool &o, tags...) noexcept { if (p>=end) return true; o = *(p++)!=0; return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, unsigned char &o, tags...) noexcept { if (p>=end) return true; o = *(p++); return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, signed char &o, tags...) noexcept { if (p>=end) return true; o = *(p++); return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, char &o, tags...) noexcept { if (p>=end) return true; o = *(p++); return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, uint16_t &o, tags...) noexcept { if (p+4>end) return true; o = (uint16_t)be32toh(*reinterpret_cast<const uint32_t*>(p)); p += 4; return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, int16_t &o, tags...) noexcept { if (p+4>end) return true; o = (int16_t)be32toh(*reinterpret_cast<const int32_t*>(p)); p += 4; return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, uint32_t &o, tags...) noexcept { if (p+4>end) return true; o = be32toh(*reinterpret_cast<const uint32_t*>(p)); p += 4; return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, int32_t &o, tags...) noexcept { if (p+4>end) return true; o = be32toh(*reinterpret_cast<const int32_t*>(p)); p += 4; return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, uint64_t &o, tags...) noexcept { if (p+8>end) return true; o = be64toh(*reinterpret_cast<const uint64_t*>(p)); p += 8; return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, int64_t &o, tags...) noexcept { if (p+8>end) return true; o = be64toh(*reinterpret_cast<const uint64_t*>(p)); p += 8; return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, float &o, tags...) noexcept { if (p+8>end) return true; o = (float)*reinterpret_cast<const double*>(p); p += 8; return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, double &o, tags...) noexcept { if (p+8>end) return true; o = *reinterpret_cast<const double*>(p); p += 8; return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, long double &o, tags...) noexcept { if (p+8>end) return true; o = *reinterpret_cast<const double*>(p); p += 8; return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, std::string &s, tags...) 
{ uint32_t size; if (deserialize_from<false>(p, end, size) || p+size>end) return true; s.assign(p, size); p += size; return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, std::string_view &s, tags...) noexcept
{ static_assert(view); uint32_t size; if (deserialize_from<false>(p, end, size) || p+size>end) return true; s = std::string_view(p, size); p += size; return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, any &s, tags...);
template <bool view, typename T, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, string_deserializer_view<T> &o, tags...) noexcept(string_deserializer_view<T>::is_noexcept)
{ static_assert(view); std::string_view s; if (deserialize_from<true>(p, end, s)) return true; o.receiver(s); return false; }
template <bool view, typename T, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, string_deserializer<T>&o, tags...) noexcept(string_deserializer<T>::is_noexcept)
{ std::string_view s; if (deserialize_from<true>(p, end, s)) return true; o.receiver(s); return false; }
template <bool view, typename T, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, expected<T> &o, tags...);
template <bool view, typename E, typename ...tags> typename std::enable_if<std::is_enum<E>::value, bool>::type deserialize_from(const char *&p, const char *end, E &e, tags...) noexcept;
template <bool view, typename C, typename ...tags> typename std::enable_if<is_deserializable_container<C>::value && !is_std_array<C>::value && !std::is_same_v<C, std::string> && !has_tuple_for_serialization<true, C, tags...>::value, bool>::type
deserialize_from(const char *&p, const char *end, C &c, tags...);
template <bool view, typename S, typename ...tags> typename std::enable_if<has_tuple_for_serialization<true, S, tags...>::value && impl::is_deserializable_f<S, view, false, tags...>(), bool>::type
deserialize_from(const char *&p, const char *end, S &s, tags...) noexcept(is_noexcept_for<S, tags...>(nt::deser));
template <bool view, typename S, typename ...tags> typename std::enable_if<is_really_auto_serializable_v<S> && !has_tuple_for_serialization<true, S, tags...>::value &&impl::is_deserializable_f<S, view, false, tags...>(), bool>::type
deserialize_from(const char *&p, const char *end, S &s, tags...) noexcept(is_noexcept_for<S, tags...>(nt::deser));
template <bool view, typename A, typename B, typename ...tags>
bool deserialize_from(const char *&p, const char *end, std::pair<A, B> &t, tags...) noexcept(is_noexcept_for<A, tags...>(nt::deser) && is_noexcept_for<B, tags...>(nt::deser));
template <bool view, typename T, typename ...TT, typename ...tags> bool deserialize_from(const char *&p, const char *end, std::tuple<T, TT...> &t, tags...) noexcept(is_noexcept_for<std::tuple<T, TT...>, tags...>(nt::deser));
template <bool view, typename T, typename ...TT, typename ...tags> bool deserialize_from(const char *&p, const char *end, std::tuple<T, TT...> &&t, tags...) noexcept(is_noexcept_for<std::tuple<T, TT...>, tags...>(nt::deser));
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&, const char *, std::tuple<> &, tags...) noexcept { return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char *&, const char *, std::tuple<> &&, tags...) noexcept { return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char*&, const char*, std::monostate&, tags...) noexcept { return false; }
template <bool view, typename ...tags> [[nodiscard]] inline bool deserialize_from(const char*&, const char*, std::monostate&&, tags...) noexcept { return false; }
template <bool view, typename T, size_t L, typename ...tags> bool deserialize_from(const char *&p, const char *end, std::array<T, L> &o, tags...) noexcept(is_noexcept_for<T, tags...>(nt::deser));
template <bool view, typename T, size_t L, typename ...tags> bool deserialize_from(const char *&p, const char *end, T (&)[L], tags...) noexcept(is_noexcept_for<T, tags...>(nt::deser));
template <bool view, typename T, typename ...tags> bool deserialize_from(const char *&p, const char *end, std::unique_ptr<T> &pp, tags...);
template <bool view, typename T, typename ...tags> bool deserialize_from(const char *&p, const char *end, std::shared_ptr<T> &pp, tags...);
template <bool view, typename T, typename ...tags> bool deserialize_from(const char *&p, const char *end, std::optional<T> &o, tags...) noexcept(is_noexcept_for<T, tags...>(nt::deser));

template <bool view, typename E, typename ...tags> inline typename std::enable_if<std::is_enum<E>::value, bool>::type
deserialize_from(const char *&p, const char *end, E &e, tags...) noexcept { uint32_t val; if (deserialize_from<view>(p, end, val)) return true; e = E(val); return false; }
template <bool view, typename C, typename ...tags> inline typename std::enable_if<is_deserializable_container<C>::value && !is_std_array<C>::value && !std::is_same_v<C, std::string> && !has_tuple_for_serialization<true, C, tags...>::value, bool>::type
deserialize_from(const char *&p, const char *end, C &c, tags... tt) {
    c.clear(); ignore_pack(tt...);
    if constexpr (!is_void_like<true, C, tags...>::value) {
        uint32_t size;  if(deserialize_from<view>(p, end, size)) return true; 
        if constexpr (has_reserve_member<C>::value) c.reserve(size);
        typename deserializable_value_type<C>::type e;  
        while (size--) if(deserialize_from<view>(p, end, e, tt...)) return true; else add_element_to_container(c, std::move(e));
    }
    return false;
}
template <bool view, typename S, typename ...tags> typename std::enable_if_t<has_tuple_for_serialization<true, S, tags...>::value && impl::is_deserializable_f<S, view, false, tags...>(), bool>
deserialize_from(const char *&p, const char *end, S &s, tags... tt) noexcept(is_noexcept_for<S, tags...>(nt::deser)) {
    static_assert(!has_after_deserialization_v<S, tags...> || !has_after_deserialization_simple_v<S, tags...>,
                  "Only one of after_deserialization_simple(tags...) or after_deserialization(U&&, tags...) should be defined.");
    bool need_to_call_after = true;
    try {
        auto &&tmp = invoke_tuple_for_serialization(s, tt...);
        if (deserialize_from<view>(p, end, tmp, tt...)) return true;
        need_to_call_after = false; //dont call after_deserialization_error if after_deserialization{,_simple}() throws
        if constexpr (has_after_deserialization_v<S, tags...>) invoke_after_deserialization(s, std::move(tmp), tt...);
        else if constexpr (has_after_deserialization_simple_v<S, tags...>) invoke_after_deserialization_simple(s, tt...);
    } catch (...) {
        if (need_to_call_after)
            if constexpr (has_after_deserialization_error_v<S, tags...>)
                invoke_after_deserialization_error(s, tt...);
        if constexpr (!is_noexcept_for<S, tags...>(nt::deser)) throw; //suppress 'always calls std::terminate' warnings
    }
    return false;
}
#ifdef HAVE_BOOST_PFR
template <bool view, typename S, typename ...tags> typename std::enable_if_t<is_really_auto_serializable_v<S> && !has_tuple_for_serialization<true, S, tags...>::value && impl::is_deserializable_f<S, view, false, tags...>(), bool>
deserialize_from(const char *&p, const char *end, S &s, tags... tt) noexcept(is_noexcept_for<S, tags...>(nt::deser)) {
    try {
        auto &&tmp = boost::pfr::structure_tie(s);
        return deserialize_from<view>(p, end, tmp, tt...);
    } catch (...) {
        if constexpr (!is_noexcept_for<S, tags...>(nt::deser)) throw; //suppress 'always calls std::terminate' warnings
        return false;
    }
}
#endif
template <bool view, typename A, typename B, typename ...tags> //pair
[[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, std::pair<A, B> &t, tags... tt) noexcept(is_noexcept_for<A, tags...>(nt::deser) && is_noexcept_for<B, tags...>(nt::deser)) {
    return deserialize_from<view>(p, end, const_cast<typename std::add_lvalue_reference<typename std::remove_const<A>::type>::type>(t.first), tt...) //remove const needed for maps
        || deserialize_from<view>(p, end, t.second, tt...); } 
template <bool view, typename T, typename ...TT, typename ...tags> //tuples
[[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, std::tuple<T, TT...> &t, tags... tt) noexcept(is_noexcept_for<std::tuple<T, TT...>, tags...>(nt::deser)) {
    if (deserialize_from<view>(p, end, std::get<0>(t), tt...)) return true;
    auto tail = tuple_tail(t); return deserialize_from<view>(p, end, tail, tt...); }
template <bool view, typename T, typename ...TT, typename ...tags> //tuples
[[nodiscard]] inline bool deserialize_from(const char *&p, const char *end, std::tuple<T, TT...> &&t, tags... tt) noexcept(is_noexcept_for<std::tuple<T, TT...>, tags...>(nt::deser)) {
    if (deserialize_from<view>(p, end, std::get<0>(t), tt...)) return true;
    auto tail = tuple_tail(t); return deserialize_from<view>(p, end, tail, tt...); }
template <bool view, typename T, size_t L, typename ...tags> bool deserialize_from(const char *&p, const char *end, std::array<T, L> &o, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::deser))
{ for (T &t:o) if (deserialize_from<view>(p, end, t, tt...)) return true; 
  return false; }
template <bool view, typename T, size_t L, typename ...tags> bool deserialize_from(const char *&p, const char *end, T (&o)[L], tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::deser))
{ for (T &t:o) if (deserialize_from<view>(p, end, t, tt...)) return true; 
  return false; }
template <bool view, typename T, typename ...tags> bool deserialize_from(const char *&p, const char *end, std::unique_ptr<T> &pp, tags... tt) 
{ bool h; if (deserialize_from<view>(p, end, h)) return true; if (h) { pp = std::make_unique<T>(); if (deserialize_from<view>(p, end, *pp, tt...)) return true; } else pp.reset(); return false; }
template <bool view, typename T, typename ...tags> bool deserialize_from(const char *&p, const char *end, std::shared_ptr<T> &pp, tags... tt)
{ bool h; if (deserialize_from<view>(p, end, h)) return true; if (h) { pp = std::make_shared<T>(); if (deserialize_from<view>(p, end, *pp, tt...)) return true; } else pp.reset(); return false; }
template <bool view, typename T, typename ...tags> bool deserialize_from(const char *&p, const char *end, std::optional<T> &o, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::deser))
{ bool h; if (deserialize_from<view>(p, end, h)) return true; if (h) { o.emplace(); if (deserialize_from<view>(p, end, *o, tt...)) return true; } else o.reset(); return false; }

std::pair<std::string, bool> parse_value(std::string &to, std::string_view &value, bool liberal=true);

/** Finds the length of a serialized value given its textual type description.
 * @param type The string view containing the type of the serialized value.
 *             As we progress, we consume chars from this string view via
 *             remove_prefix(). Note that we may not consume all of the type
 *             if it contains the description of more than one type. On this
 *             occasion we do not throw an error, but return the remainder.
 * @param p A pointer reference to the raw value to deserialize from. As we
 *          progress, we move it forward.
 * @param [in] end The character beyond the last character of the raw serialized value.
 *                 The function will not read this character or beyond. If serialization
 *                 would need more characters, a uf::value_mismatch_error is thrown.
 * @param [in] more_type A function to add more bytes to the typestring. Called when 'type' has
 *                  been parsed to its end. Shall be f(std::string_view&)->void, when
 *                  called should set the view to the next chunk of type info. Set to empty view
 *                  to signal that no more bytes. Not called, when the function is left empty.
 *                  The last (partial) chunk is returned in 'type'.
 * @param [in] more_val A function to add more bytes to the value string. Called when 'p' has
 *                  been parsed to 'end'. Shall be f(const char*&p, const char*&end)->void, when
 *                  called should set p and end. Return p==end to signal that no more bytes.
 *                  The last (partial) chunk is returned in ['p'..'end').
 *                  If left empty, it will not be called - in that case all the value is in a single chunk.
 * @param [in] check_recursively When set, we also parse the type and value contained in
 *                  'any' values inside the value we parse (recursively). The result of those
 *                  parse steps are discarded on success, but reported as an error on a problem
 *                  (uf::value_mismatch_error or uf::typestring_error). In these cases the
 *                  type reported in the error may be not an exact suffix of the 'type' parameter
 *                  as it may contain parenthesis indicating the inner type of any any. E.g.,
 *                  for t2as (with an invalid type "@" in the any) we return "(*@)s" in the exception
 *                  and leave "s" in the 'type' parameter. From this latter value the caller needs
 *                  to figure out that "t2a" must be prepended to the type returned in the error,
 *                  resulting in the correct "t2a(*@)s" error type.
 * Note on chunking. You can split the typestring as you want.
 * As for values, you cannot split any of 'iId' or the length field in 'salm'. You can split
 * the length and has_value bytes off from the remaining content, between any element of lmt, or
 * anywhere inside an 'sa' (apart from inside the length).
 * On errors we throw typestring_error or value_mismatch_error.
 * If we have the typestring in multiple chunks, the type reported in these errors will only contain
 * the last chunk. Even if the type is only in one chunk, they will only contain the remainder
 * of the type after the error.*/
[[nodiscard]] std::unique_ptr<value_error> serialize_scan_by_type_from(std::string_view& type, const char*& p, const char* &end,
                                                 const std::function<void(std::string_view &)> &more_type,
                                                 const std::function<void(const char *&, const char *&)> &more_val,
                                                 bool check_recursively);
/** Finds the length of a serialized value given its textual type description.
 * @param type The string view containing the type of the serialized value.
 *             As we progress, we consume chars from this string view via
 *             remove_prefix(). Note that we may not consume all of the type
 *             if it contains the description of more than one type. On this
 *             occasion we do not throw an error, but return the remainder.
 * @param p A pointer reference to the raw value to deserialize from. As we
 *          progress, we move it forward.
 * @param [in] end The character beyond the last character of the raw serialized value.
 *                 The function will not read this character or beyond. If serialization
 *                 would need more characters, a uf::value_mismatch_error is thrown.
 * @param [in] check_recursively If set, we parse internal 'any' elements (just checking validity)
 * @return Errors or an empty std::unique_ptr<value_error> on success. The typestring reported in these errors
 * will only contain the remainder of the type after the error. Note that if 'check_recursively' is
 * set then these types may not be the exact suffix of 'type', because they may indicate errors
 * inside any any. E.g., for 't2as' the returned type may be "(*@)s" if the typestring inside the any
 * is bad ('@' in the example).*/
[[nodiscard]] inline std::unique_ptr<value_error> serialize_scan_by_type_from(std::string_view& type, const char*& p, const char* end, bool check_recursively)
{ return serialize_scan_by_type_from(type, p, end, {}, {}, check_recursively); }

bool print_escaped_to(std::string& to, unsigned max_len, std::string_view s, std::string_view chars, char escape_char);
/** Finds the length of a serialized value given its textual type description.
 * @param [in] type The string view containing the type of the serialized value.
 * @param [in] value The string view containing the value of the serialized value.
 * @param [in] allow_longer If false, we throw an uf::value error if 'value' has bytes left.
 * @param [in] check_recursively If set, we parse internal 'any' elements (just checking validity)
 * @Returns any error and if none, then the length of the type and value.*/
[[nodiscard]] inline std::tuple<std::unique_ptr<value_error>, uint32_t, uint32_t>
serialize_scan_by_type(std::string_view type, std::string_view value, 
                       bool allow_longer = false, bool check_recursively = false)
{
    const char *p = value.data(), *end = p + value.length(); //end must not be const
    std::string_view worktype = type;
    if (std::unique_ptr<value_error> e = serialize_scan_by_type_from(worktype, p, end, {}, {}, check_recursively)) {
        e->prepend_type0(type, worktype);
        return {std::move(e), 0, 0};
    };
    if (worktype.size())
        return {std::make_unique<uf::typestring_error>(ser_error_str(ser::tlong), type, type.size()-worktype.size()), 0, 0 };
    if (allow_longer || p == end) return {std::unique_ptr<value_error>{}, worktype.data() - type.data(), p - value.data()};
    std::string val;
    print_escaped_to(val, 100, value, {}, '%');
    return {std::make_unique<value_mismatch_error>(ser_error_str(ser::vlong), type), 0,0};
}

} //ns impl

struct from_text_t {};
struct from_raw_t {};
struct from_typestring_t {};
struct from_type_value_t {};
struct from_type_value_unchecked_t {};
struct use_tags_t {};
extern from_text_t from_text;
extern from_raw_t from_raw;
extern from_typestring_t from_typestring;
extern from_type_value_t from_type_value;
extern from_type_value_unchecked_t from_type_value_unchecked;
extern use_tags_t use_tags;

/** @addtogroup serialization
 * @{ */

/** Returns the string description of a type if serializable from.
 * Use it on a variable, like uf::serialize_type(a).
 * You can also provide tags to select the appropriate helper functions.
 * @returns a string view to a statically allocated string constant. Very cheap.*/
template <typename T, typename ...tags>
inline std::string_view serialize_type(const T&, use_tags_t = {}, tags...) noexcept {
    static_assert(uf::impl::is_serializable_f<T, true, tags...>(), "Type must be serializable.");
    if constexpr (uf::impl::is_serializable_f<T, false, tags...>()) {
        static const auto val = impl::serialize_type_static<false, T, tags...>();
        return {val.c_str(), val.size()};
    } else
        return {};
}

/** Returns the string description of a type if serializable from.
 * Use it on a type, like uf::serialize_type<T>() or on a series of types
 * (ending up in a tuple), like uf::serialize_type<T,U,V>(). Handles uf::serialize_type<void>.
 * You can also provide tags to select the appropriate helper functions.
 * @returns a string view to a statically allocated string constant. Very cheap. */
template <typename T, typename ...tags>
inline std::string_view serialize_type() noexcept {
    static_assert(std::is_void_v<T> || uf::impl::is_serializable_f<T, true, tags...>(), "Type must be void or serializable.");
    if constexpr (std::is_void_v<T> || uf::impl::is_serializable_f<T, false, tags...>()) {
        static const auto val = impl::serialize_type_static<false, T, tags...>();
        return {val.c_str(), val.size()};
    } else
        return {};
}

/** Returns the string description of a type if deserializable to.
 * Use it on a variable, like uf::deserialize_type(a).
 * You can also provide tags to select the appropriate helper functions.
 * @returns a string view to a statically allocated string constant. Very cheap.*/
template <typename T, typename ...tags>
inline std::string_view deserialize_type(const T &, use_tags_t = {}, tags...) noexcept {
    static_assert(uf::impl::is_deserializable_f<T, true, true, tags...>(), "Type must be possible to deserialize into.");
    if constexpr (uf::impl::is_deserializable_f<T, true, false, tags...>()) {
        static const auto val = impl::serialize_type_static<true, T, tags...>();
        return {val.c_str(), val.size()};
    } else
        return {};
}

/** Returns the string description of a type if deserializable into.
 * Use it on a type, like uf::deserialize_type<T>() or on a series of types
 * (ending up in a tuple), like uf::deserialize_type<T,U,V>(). Handles uf::deserialize_type<void>.
 * @returns a string view to a statically allocated string constant.Very cheap. */
template <typename T, typename ...tags>
inline std::string_view deserialize_type() noexcept {
    static_assert(std::is_void_v<T> || uf::impl::is_deserializable_f<T, true, true, tags...>(), "Type must be void or possible to deserialize into.");
    if constexpr (uf::impl::is_deserializable_f<T, true, false, tags...>()) {
        static const auto val = impl::serialize_type_static<true, T, tags...>();
        return {val.c_str(), val.size()};
    } else
        return {};
}

/** @} */

namespace impl {

/** Structure to hold all relevant information during a converting
 * deserialization operation. It contains the serialized bytes we
 * deserialize from (p and end), the source type (tstart, type, tend)
 * and the destination type (target_tstart, target_type, target_tend)
 * and the conversion policy to apply.
 * For the two types we maintain a range (tstart,tend) and a current
 * pointer (type).
 * During deserialization we create stacks of these descriptors for 2
 * reasons
 * - during a compound type deserialization we dont want to run off
 *   the child type, so we push a deserialize_convert_params with the
 *   tend set to the end of the compound type. E.g., during
 *   deserialization of a t2lis, when we process the li, we set the
 *   tend to just after the 'i', so we dont eat into the 's'.
 * - when we get to an 'a' in the source type and have some other
 *   type in the destination, we attempt to deserialize the content of the
 *   'a' into that type. For this we create a new deserialize_convert_params
 *   with the content of the 'a' as source and the destination type (only)
 *   as target.
 * When we report an error we need to process this stack. Eliminate (combine)
 * the elements created for the first reason and show the elements created for
 * the second reason. This is done in create_error_for_des().*/
struct deserialize_convert_params
{
    const char *p;                    //the current position of the serialized value we read from
    const char * const end;           //the char beyond the end of the serialized value we read from
    const char * const tstart;        //the start of the type info we deserialize (incoming type)
    const char *type;                 //the current position of the type info we deserialize (incoming type)
    const char * const tend;          //the char beyond the end of the type info we deserialize (incoming type)
    const char * const target_tstart; //the start of the type info of the variable we deserialize into (target type)
    const char *target_type;          //the current position of the type info of the variable we deserialize into (target type)
    const char * const target_tend;   //the char beyond the end of the type info of the variable we deserialize into (target type)
    const serpolicy convpolicy;       //the serialization policy we apply
    const deserialize_convert_params * const prev;//in a stack of deserialization attempts this is the previous one.
    std::vector<uf::error_value>
        *errors = nullptr;            //if not null, we collect errors from expected values that cannot be converted to 'T'
    std::vector<std::pair<size_t, size_t>>
        *error_pos = nullptr;         //if not null, the position in source and target types of an unplacable expected error

    /** This is a constructor initializing all fields.*/
    explicit deserialize_convert_params(const char *_p, const char *_end,
                                        const char *_tstart, const char *_type, const char *_tend,
                                        const char *_target_start, const char *_target_type, const char *_target_tend,
                                        serpolicy _convpolicy, const deserialize_convert_params *_prev,
                                        std::vector<uf::error_value> *_e, std::vector<std::pair<size_t, size_t>> *_pos) :
        p(_p), end(_end), tstart(_tstart), type(_type), tend(_tend),
        target_tstart(_target_start), target_type(_target_type), target_tend(_target_tend),
        convpolicy(_convpolicy), prev(_prev), errors(_e), error_pos(_pos) {}
    /** This is a constructor copying a deserialize_convert_params and limiting the type ends.*/
    explicit deserialize_convert_params(const deserialize_convert_params &par, const char *new_tend, const char *new_target_tend) :
        p(par.p), end(par.end), tstart(par.tstart), type(par.type), tend(new_tend),
        target_tstart(par.target_tstart), target_type(par.target_type), target_tend(new_target_tend),
        convpolicy(par.convpolicy), prev(&par), errors(par.errors), error_pos(par.error_pos) {}
    /** Initialize us from a previous one. (use pointer to differentiate from copy ctr)*/
    explicit deserialize_convert_params(const deserialize_convert_params *par) :
        p(par->p), end(par->end), tstart(par->tstart), type(par->type), tend(par->tend),
        target_tstart(par->target_tstart), target_type(par->target_type), target_tend(par->target_tend),
        convpolicy(par->convpolicy), prev(par), errors(par->errors), error_pos(par->error_pos) {}
    /** This is a constructor taking a value+type attempting to convert to a C++ type.*/
    template <typename T, typename ...tags, typename = std::enable_if_t <is_deserializable_view_v<T, tags...>>>
    explicit deserialize_convert_params(std::string_view _value, std::string_view _type, const T*, serpolicy _convpolicy,
                                        const deserialize_convert_params *_prev,
                                        std::vector<uf::error_value> *_e, std::vector<std::pair<size_t, size_t>> *_pos, 
                                        tags...) :
        p(_value.data()), end(_value.data() + _value.length()),
        tstart(_type.data()), type(_type.data()), tend(_type.data() + _type.length()),
        target_tstart(_prev ? _prev->target_tstart : deserialize_type<T, tags...>().data()), //for good error reporting use the same target_tstart as in the parent (so that we show the entire target type in error messages)
        target_type(_prev ? _prev->target_type : target_tstart),                             //...but of course only if _prev is non-null. If so, we simply use the typestring of 'o'
        target_tend(target_type+deserialize_type<T, tags...>().length()),                    //limit the target type to the length typestring of 'o'
        convpolicy(_convpolicy), prev(_prev), errors(_e), error_pos(_pos) 
    {
        //verify that the original target_type continues with the typestring of 'o'
        assert(_prev==nullptr || std::string_view(_prev->target_type, _prev->target_tend-_prev->target_type).starts_with(deserialize_type<T, tags...>()));
    }
};

//Forward declare deserialize_convert_from variants
template<bool view, typename T, typename ...tags,
    typename = std::enable_if_t<view ? is_deserializable_view<T, tags...>::value : is_deserializable<T, tags...>::value>>
[[nodiscard]] inline std::unique_ptr<value_error> deserialize_convert_from(bool &can_disappear, deserialize_convert_params &p, T &o, tags... tt);

/** Helper to throw a deserialization convert error.
 * Process the stack of deserialize_convert_params, eliminate the ones not needed
 * (see the comment for deserialize_convert_params) and create a single
 * type_mismatch_error, that contains the whole stack.
 * Essentially we keep only elements of the stack where the source type was 'a',
 * but we have source data and could determine what was the content of the any,
 * and it did not match the target. In these cases we create a source type
 * descriptor where the content of the 'a' is inserted after the 'a' in
 * parenthesis, like 'a(t2i*s)' not matching 't2i*d'.*/
inline std::unique_ptr<value_error> &&create_error_for_des(std::unique_ptr<value_error> &&e, const deserialize_convert_params *p)
{
    if (!p) e = {};
    if (!e) return std::move(e);
    std::string source_type;
    int source_pos = -1;
    const char *deepest_target_type = p->target_type;
    const char *local_target_tend = p->target_tend;
    const char *local_target_tstart= p->target_tstart;
    while (true) {
        const char *local_tend = p->tend;
        const deserialize_convert_params *ctx = p->prev;
        while (ctx && p->tstart == ctx->tstart && p->target_tstart == ctx->target_tstart) {
            //If our context is the same types (but we may be shorter), use the longer type
            //but our positioning info and skip our immediate parent context.
            //This is to simplify: t2i>s -> t2i>i where the context is t2is->t2ii
            //(The ends may be different if there is another type after, like
            //the parent is t2>t2isa -> t2>t2iia and we are t2t2i>s -> t2t2i>ia
            //In this case we need to add the missing 'a' from our context, but keep the pos.
            //target is null-terminated so its end is always good.
            local_tend = std::max(local_tend, ctx->tend);
            local_target_tend = std::max(local_target_tend, ctx->target_tend);
            ctx = ctx->prev;
        }
        if (source_pos == -1) {
            //The first (deepest) element of the stack
            source_type.assign(p->tstart, local_tend);
            source_pos = p->type - p->tstart;
        } else {
            //OK, right now p->type must point to an 'a'
            //Where we pushed the content of the 'a' onto the stack, now in 'source_type'
            assert(*p->type == 'a');
            if (source_type.length() == 0)
                source_type = "void"; //This is to avoid a(*), when any holds void: have a(*void) instead
            source_type = uf::concat(std::string_view(p->tstart, p->type - p->tstart + 1), '(', source_type, ')',
                                     std::string_view(p->type + 1, local_tend - p->type - 1));
            source_pos += p->type - p->tstart + 2; //we have inserted this many characters before source_type
        }
        if (!ctx) break;
        p = ctx;
    }
    e->types[0].type = source_type;
    e->types[1].type = std::string_view(local_target_tstart, local_target_tend - local_target_tstart);
    e->types[0].pos = source_pos;
    e->types[1].pos = deepest_target_type - local_target_tstart;
    e->regenerate_what();
    return std::move(e);
}


inline std::unique_ptr<value_error> create_des_type_error(const deserialize_convert_params &p) {
    return create_error_for_des(std::make_unique<uf::type_mismatch_error>("Type mismatch when converting <%1> to <%2>", std::string_view{}, std::string_view{}), &p);
}

inline std::unique_ptr<value_error> create_des_type_error(const deserialize_convert_params &p, serpolicy reason) {
    return create_error_for_des(std::make_unique<uf::type_mismatch_error>(uf::concat("Type mismatch when converting <%1> to <%2> (missing flag: ", to_string(reason), ')'), std::string_view{}, std::string_view{}), &p);
}

inline std::unique_ptr<value_error> create_des_value_error(const deserialize_convert_params &p) {
    return create_error_for_des(std::make_unique<uf::value_mismatch_error>(uf::concat(ser_error_str(ser::val), " <%1>.")), &p);
}

inline std::unique_ptr<value_error> create_des_typestring_source(const deserialize_convert_params &p, std::string_view msg) {
    return create_error_for_des(std::make_unique<uf::typestring_error>(uf::concat(msg, " <%1>."), std::string_view{}), &p);
}

inline std::unique_ptr<value_error> create_des_typestring_target(const deserialize_convert_params &p, std::string_view msg) {
    return create_error_for_des(std::make_unique<uf::typestring_error>(uf::concat(msg, " <%2>."), std::string_view{}), &p);
}

/** Thrown at the place of a typestring error. */
struct internal_typestring_error
{
    const char* typestring_pos = nullptr;
};

inline uint32_t fill_bytes(uint32_t len, char**to, char c = 0) { if (to) { memset(*to, c, len); *to += len; } return len; }

/** Create a serialized version of the default value for a typesting or return its length.
 * @param type A pointer to the beginning of the typestring. Will be moved to the end of the type parsed.
 * @param tend A pointe beyond the end of the typestring
 * @param to A pointer to a pointer where the default value shall be stored. Will be moved to the end of the stored value.
 *           The caller must ensure that there is sufficient buffer space. If set to null, no values are copied,
 *           this is how you can find out the space needed.
 * @returns The number of bytes needed.
 * @exception uf::impl::internal_typestring_error with the location of the error or null if cannot be determined.*/
uint32_t default_value(const char *&type, const char *const tend, char **to);

} //ns impl

/** @addtogroup serialization
 * @{ */

/** Creates the serialized version of the default value for a typestring. 
 * @param [in] typestring The typestring to create the default value for.
 * @param [inout] append_to If you set it, the value will be appended.
 * @returns The default value prepended by whatever was in 'append_to'. 
 * @exception uf::typestring_error if the the typestring is invalid.*/
inline std::string default_serialized_value(std::string_view typestring, std::string &&append_to = {})
{
    const char* t = typestring.data(), * const end = t + typestring.length();
    const size_t off = append_to.length();
    try {
        append_to.resize(off+impl::default_value(t, end, nullptr));
        if (t != end) throw impl::internal_typestring_error{ t };
        t = typestring.data();
        char* to = append_to.data()+off;
        impl::default_value(t, end, &to);
        return append_to;
    } catch (const impl::internal_typestring_error & e) {
        throw uf::typestring_error("Invalid typestring when creating a default value: <%1>", typestring,
                                   e.typestring_pos ? e.typestring_pos - typestring.data() : -1);
    }
}

/** A view to a struct any.
 * It can point to an exitsing any - or any part of it.
 * Same as string_view, it does not manage storage though and is
 * suggested to be used as a value type and to be passed as value.
 *
 * It is serialized and deserialized exactly as 'any', so you can
 * exchange them. (Of course, you cannot deserialize into an any_view
 * as this is not an owning type, but you can deserialize_view into it. */
struct any_view
{
    [[nodiscard]] any_view() = default;
    [[nodiscard]] any_view(const any_view &o) noexcept = default;
    [[nodiscard]] any_view(any_view &&o) noexcept = default;
    [[nodiscard]] any_view(const any &o) noexcept;
    [[nodiscard]] any_view(any &&) = delete;
    /** Create an 'any_view' from the serialized version of another any.
     * @param [in] v The string containing the serialized (type,value)
     * @param [in] check If set, we check the content of contained
     *             any elements recursively. On OK, this is justa perf degradation,
     *             on failure it is an exception.*/
    [[nodiscard]] any_view(from_raw_t, std::string_view v, bool check=true) { assign(from_raw, v, check); }

    /** Create an 'any_view' to the typestring and serialized version of a value.
     * We throw a value_mismatch_error if the type does not match the value.
     * @param [in] t The typestring
     * @param [in] v The serialized value.
     * @param [in] allow_longer If set, both the type and value may be longer
     *             than their actual value, we simply trim them the appropriate length.*/
    [[nodiscard]] any_view(from_type_value_t, std::string_view t, std::string_view v, bool allow_longer = false)
    { assign(from_type_value, t, v, allow_longer); }
    /** Create an 'any_view' to the typestring and serialized version of a value.
     * We accept that the value matches the type and perform no such checks.
     * @param [in] t The typestring
     * @param [in] v The serialized value.*/
    [[nodiscard]] any_view(from_type_value_unchecked_t, std::string_view t, std::string_view v) noexcept :
        _type(t), _value(v) {}

    any_view &operator=(const any_view &o) noexcept = default;
    any_view &operator=(any_view &&o) noexcept = default;
    any_view &operator=(any &&) = delete;

    [[nodiscard]] bool operator==(const any_view &o) const noexcept { return _type==o._type && _value==o._value; }
    [[nodiscard]] bool operator!=(const any_view &o) const noexcept { return !operator==(o); }
    [[nodiscard]] bool operator<(const any_view &o) const noexcept { return std::tie(_type, _value)<std::tie(o._type, o._value); }
    [[nodiscard]] bool operator>(const any_view &o) const noexcept { return o<*this; }

    /** Convert to true if value is non-void. */
    explicit operator bool() const noexcept { return _value.length(); }

    /** Sets the value void. */
    void clear() noexcept { _type = _value = {}; }

    void swap(any_view&o) noexcept { _type.swap(o._type); _value.swap(o._value); }
    auto tuple_for_serialization() const noexcept { return std::tie(_type,_value); }
    auto tuple_for_serialization()       noexcept { return std::tie(_type,_value); }

    /** Sets us to the typestring and serialized version of a value
     * We accept that the value matches the type and perform no such checks.
     * @param [in] t The typestring
     * @param [in] v The serialized value.
     * @returns us.*/
    any_view &assign(from_type_value_unchecked_t, std::string_view t, std::string_view v) {
        _type = t;
        _value = v;
        return *this;
    }
    /** Sets us to the typestring and serialized version of a value.
     * @param [in] t The typestring
     * @param [in] v The serialized value.
     * @param [in] allow_longer If set, both the type and value may be longer
     *             than their actual value, we simply trim them the appropriate length.
     * @returns us.
     * @exception uf::value_mismatch_error if the value does not match the type
     * @exception uf::typestring_error if the any contains a bad typestring*/
    any_view& assign(from_type_value_t, std::string_view t, std::string_view v, bool allow_longer=false) {
        auto [err, tlen, vlen] = impl::serialize_scan_by_type(t, v, allow_longer, true);
        if (err) err->throw_me();
        return assign(from_type_value_unchecked, t.substr(0, tlen), v.substr(0, vlen));
    }
    /** Set to the serialized version of another any.
     * On exception we leave this unchanged.
     * @param [in] v The string containing the serialized (type,value) 
     * @param [in] check If set, we check the content of contained
     *             any elements recursively. On OK, this is justa perf degradation,
     *             on failure it is an exception.*/
    any_view &assign(from_raw_t, std::string_view v, bool check = true);

    /** Wraps whatever type is in the input into an 'any'.
     * The output any will always contain a value of type "a". */
    [[nodiscard]] any wrap() const;

    /** If we point to an 'any', take the type and value of that.
     * @returns true in that case (if we have changed).
     * @exception uf::value_error on bad content (should not happen)*/
    bool unwrap()
    {
        if (_type != "a") return false;
        const char* p = _value.data(), * const end = _value.data() + _value.length();
        if (impl::deserialize_from<true>(p, end, _type)
            || impl::deserialize_from<true>(p, end, _value))
            throw uf::value_mismatch_error(uf::concat(impl::ser_error_str(impl::ser::val), " (unwrap) <%1>."));
        return true;
    }

    /** Returns the type description of the value contained. */
    [[nodiscard]] std::string_view type() const noexcept { return _type; }
    /** Returns the serialized value contained. */
    [[nodiscard]] std::string_view value() const noexcept { return _value; }

    /** Returns true if we contain no value (and no type). */
    [[nodiscard]] bool is_void() const noexcept { return _type.length()==0; }

    /** Returns true if we contain a structured type (list, map, tuple, any). */
    [[nodiscard]] bool is_structured_type() const noexcept { return _type.length() && (_type.front()=='l'||_type.front()=='m'||_type.front()=='t'||_type.front()=='a'); }

    /** Return the contained values of structred types as any_view objects. 
     * Order of elements is as they appear in the serialized data.
     * - For lists or tuples we return views to their elements.
     * - For maps we return the list of keys.
     * - For any, we return a single element: the content of the any
     * - For optional we return 1 or 0 element, depending on if the optional has a value or not.
     * - For expected, we return one element: the contained value or an 'e'.
     * - For 'e', we return 4 elements: error type, id, message and value (as in \<t4sssa>).
     * - For primitive types, we return a single element ourselves.
     * - For void, we return the empty vector.
     * @param [in] max_no The maximum number of elements to return ignoring the rest.*/
    [[nodiscard]] std::vector<any_view> get_content(uint32_t max_no = std::numeric_limits<uint32_t>::max()) const;

    /** For maps we return their keys and values, in the order they appear in 
     * the serialized data.
     * For any other type we return the content accompanied by a void view.
     * For primitive types, we return ourselves, plus a void.
     * @param[in] max_no The maximum number of elements to return ignoring the rest.*/
    [[nodiscard]] std::vector<std::pair<any_view, any_view>> get_map_content(uint32_t max_no = std::numeric_limits<uint32_t>::max()) const;

    /** Return how many elements a structured type contains.
     * This is a quick noexcept call, cheaper than calling get_content() just for
     * the size. Note, we do not always return how many elements a get_content() call
     * will return.
     * For lists, tuples and maps, we return the number of elements (same as get_content().size()).
     * For optional and expected, we return 1 if the have a value, 0 otherwise.
     * (For expected with error this differs from get_content().size(), which is 1.)
     * For all primitive types (including any and string) we return 0 (differs from 
     * get_content().size(), which is 1).
     * For an empty of the unlikely case of an invalid 'any', we also return 0. */
    [[nodiscard]] uint32_t get_content_size() const noexcept;

    /** Extract the value from us into an arbitrary C++ variable.
     * You can also provide tags to select the appropriate helper functions.
     * @exception uf::type_mismatch_error the types dont agree.
     * @exception uf::expected_with_error the any contains an expected with an error
     * and the receiving type has no expected there to take the error.
     * The type must be able to hold the value, so no string_views allowed.*/
    template<typename T, typename ...tags>
    void get(T& t, serpolicy convpolicy = allow_converting_all, 
             uf::use_tags_t = {}, tags... tt) const
    {
        static_assert(uf::impl::is_deserializable_f<T, false, true, tags...>(), "Type must be possible to deserialize into.");
        if constexpr (uf::impl::is_deserializable_f<T, false, false, tags...>()) {
        //fast path, exactly equal types
            if (_type == deserialize_type<T, tags...>()) {
                const char *p = _value.data(), *const end = p+_value.length();
                if (impl::deserialize_from<false>(p, end, t, tt...))
                    throw value_mismatch_error(uf::concat(impl::ser_error_str(impl::ser::val), " (any::get) <%1>."), deserialize_type<T, tags...>(), 0);
            } else {
                std::vector<error_value> errors;
                std::vector<std::pair<size_t, size_t>> error_pos;
                impl::deserialize_convert_params p(_value, _type, &t, convpolicy, nullptr,
                                                   &errors, &error_pos, tt...);
                bool can_disappear;
                if (auto err = impl::deserialize_convert_from<false>(can_disappear, p, t, tt...))
                    err->throw_me();
                if (p.type < p.tend) create_des_typestring_source(p, impl::ser_error_str(impl::ser::tlong))->throw_me();
                if (p.target_type < p.target_tend) create_des_type_error(p)->throw_me();
                if (errors.size())
                    throw uf::expected_with_error("In uf::any_view<%1>::get(<%2>) cannot place errors in expected values. Errors: %e",
                                                  _type, deserialize_type<T, tags...>(),
                                                  std::move(errors), std::move(error_pos));
            }
        }
    }
    /** Extract the value from us into an any. A specialization.
     * Note that here we copy our content into an 'any', whcih is strictly speaking a conversion.
     * I we contain another any, we essentially unwrap. But if some other type is whithin,
     * then we copy it to the target only if policy allows it, else we throw type mismach. */
    void get(any &t, serpolicy policy = allow_converting_all) const;

    /** Extract the value from us into a C++ prvalue. You need to specify the type explicitly.
     * You can specify multiple types meaning a tuple of them, like get_as<int, double>().
     * You can also provide tags to select the appropriate helper functions.
     * @exception uf::type_mismatch_error the types dont agree.
     * @exception uf::expected_with_error the any contains an expected with an error
     * and the receiving type has no expected there to take the error.
     * The type must be able to hold the value, so no string_views allowed.
     * It is allowed to have void-like types, then we attempt to extract to a void value.
     * This can only succeed if the any contains only
     * expected<void> members and we want to check that none of these contain errors.*/
    template<typename ...T, typename ...tags>
    [[nodiscard]] typename uf::impl::single_type_t<T...> 
        get_as(serpolicy convpolicy = allow_converting_all, 
               uf::use_tags_t = {}, tags... tt) const
    { 
        using TT = typename uf::impl::single_type_t<T...>;
        static_assert(std::is_void_v<TT> || uf::impl::is_deserializable_f<TT, false, true, tags...>(), "Type must be void or possible to deserialize into.");
        static_assert(std::is_void_v<TT> || std::is_default_constructible_v<TT>, "The listed types must be default constructible.");
        if constexpr (std::is_void_v<TT>) {
            std::monostate void_value; get(void_value, convpolicy, uf::use_tags, tt...);
        } else if constexpr (uf::impl::is_deserializable_f<TT, false, false, tags...>() 
                             && std::is_default_constructible_v<TT>) {
            TT ret; get(ret, convpolicy, uf::use_tags, tt...); return ret;
        } 
    }

    /** Extract the value from us into an arbitrary C++ variable as a view.
     * You can also provide tags to select the appropriate helper functions.
     * @exception uf::type_mismatch_error the types dont agree.
     * @exception uf::expected_with_error the any contains an expected with an error
     * and the receiving type has no expected there to take the error.
     * The type dont have to be able to hold the value, so string_views are allowed.*/
    template<typename T, typename ...tags>
    void get_view(T& t, serpolicy convpolicy = allow_converting_all, 
                  uf::use_tags_t = {}, tags... tt) const
    {
        static_assert(uf::impl::is_deserializable_f<T, true, true, tags...>(), "Type must be possible to deserialize into.");
        if constexpr (uf::impl::is_deserializable_f<T, true, false, tags...>()) {
            //fast path, exactly equal types
            if (_type == deserialize_type<T, tags...>()) {
                const char *p = _value.data(), *const end = p + _value.length();
                if (impl::deserialize_from<true>(p, end, t, tt...))
                    throw value_mismatch_error(uf::concat(impl::ser_error_str(impl::ser::val), " (any::get_view) <%1>."), deserialize_type<T, tags...>(), 0);
            } else {
                std::vector<error_value> errors;
                std::vector<std::pair<size_t, size_t>> error_pos;
                impl::deserialize_convert_params p(_value, _type, &t, convpolicy, nullptr,
                                                   &errors, &error_pos, tt...);
                bool can_disappear;
                if (auto err = impl::deserialize_convert_from<true>(can_disappear, p, t, tt...))
                    err->throw_me();
                if (p.type < p.tend) create_des_typestring_source(p, impl::ser_error_str(impl::ser::tlong))->throw_me();
                if (p.target_type < p.target_tend) create_des_type_error(p)->throw_me();
                if (errors.size())
                    throw uf::expected_with_error("In uf::any_view<%1>::get_view(<%2>) cannot place errors in expected values. Errors: %e",
                                                  _type, deserialize_type<T, tags...>(),
                                                  std::move(errors), std::move(error_pos));
            }
        }
    }
    /** Extract the value from us into an any. A specialization.
     * Note that here we copy our content into an 'any', whcih is strictly speaking a conversion.
     * I we contain another any, we essentially unwrap. But if some other type is whithin,
     * then we copy it to the target only if policy allows it, else we throw type mismach. */
    void get_view(any& t, serpolicy policy = allow_converting_all) const { get(t, policy); }
    /** Extract the value from us into another any_view. A specialization.
     * Note that here we copy our content into an 'any', whcih is strictly speaking a conversion.
     * I we contain another any, we essentially unwrap. But if some other type is whithin,
     * then we copy it to the target only if policy allows it, else we throw type mismach. */
    void get_view(any_view& t, serpolicy policy = allow_converting_all) const {
        if (_type != "a" && !(policy & allow_converting_any))
            throw uf::type_mismatch_error("Type mismatch when converting <%1> to <%2> (missing flag: convert:any)",
                                          _type, "a", 0, 0);
        t = *this;
        t.unwrap();
    }
    /** Extract the value from us into a C++ prvalue as a view. You need to specify the type explicitly.
     * You can specify multiple types meaning a tuple of them, like get_as<int, double>().
     * You can also provide tags to select the appropriate helper functions.
     * @exception uf::type_mismatch_error the types dont agree.
     * @exception uf::expected_with_error the any contains an expected with an error
     * and the receiving type has no expected there to take the error.
     * The type dont have to be able to hold the value, so string_views are allowed.
     * It is allowed to have void-like types, then we attempt to extract to a void value. 
     * This can only succeed if the any contains only 
     * expected<void> members and we want to check that none of these contain errors.*/
    template<typename ...T, typename ...tags>
    [[nodiscard]] typename uf::impl::single_type_t<T...> 
    get_view_as(serpolicy convpolicy = allow_converting_all, 
                uf::use_tags_t = {}, tags... tt) const {
        using TT = typename uf::impl::single_type_t<T...>;
        static_assert(std::is_void_v<TT> || uf::impl::is_deserializable_f<TT, true, true, tags...>(), "Type must be possible to deserializable into.");
        static_assert(std::is_void_v<TT> || std::is_default_constructible_v<TT>, "The listed types must be default constructible.");
        if constexpr (std::is_void_v<TT>) {
            std::monostate void_value; get_view(void_value, convpolicy, uf::use_tags, tt...);
        } else {
            uf::impl::single_type_t<T...> ret; get_view(ret, convpolicy, uf::use_tags, tt...); return ret;
        }
    }

    /* If we hold a string return a string_view to it.
     * Special member for strings - throws type_mismatch_error if not a string*/
    [[nodiscard]] std::string_view peek_if_string() const {
        if (_type != "s")
            throw type_mismatch_error("Type not a string in peek_if_string, but <%1>.", _type, "s");
        std::string_view ret = _value;
        ret.remove_prefix(4);
        return ret;
    }
    /** Appends a textual description of us to a string.
     * First the type encosed in '<'/'>' symbols, then the value.
     * Printout is suitable to show on ascii displays. Binary values are
     * printed as %xx.
     * Not for direct use: in case of errors, the type will be reported bad, see below.
     * @param [out] to The string to append to.
     * @param [out] ty The typestring remaining after an exception. Empty on successful return.
     * @param [in] chars Specify a list of characters to also encode as %xx.
     *                   Urls require, the space, quotation mark and the ampersand
     *                   These are in the macro URL_CHARS.
     * @param [in] max_len A maximum length. Characters beyond are trimmed. Zero is unlimited.
     * @param [in] escape_char What escape character to use.
     * @param [in] json_like Tf true, we attempt to be as json compatible as possible.
     *             - characters are printed as a single character string
     *             - void values and empty optionals are printed as 'null'
     *             - strings will contain backslash escaped backspace, tab, cr, lf, ff, quotation mark and backslash (in addition to 'chars')
     *             - errors are printed as string
     *             - tuples are printed as arrays.
     * @returns an empty optional on success; an optional with no error if we have exceeeded the max length and need to be trimmed; or
     *          an optional with some error. The exceptions in the errors will contain the typestring suffix remaining after the error.
     *          This has to be adjusted in callers*/
    std::optional<std::unique_ptr<value_error>> print_to(std::string &to, std::string_view &ty, unsigned max_len = 0, std::string_view chars = {}, char escape_char = '%', bool json_like = false) const;
    /** Returns a textual description of us as a string.
     * First the type encosed in '<'/'>' symbols, then the value.
     * Printout is suitable to show on ascii displays. Binary values are
     * printed as %xx.
     * @param [in] chars Specify a list of characters to also encode as %xx.
     *                   Urls require, the space, quotation mark and the ampersand
     *                   These are in the macro URL_CHARS. Note that a few characters
     *                   will always be emitted, like numbers, letters and "()[];<>\"\'"
     * @param [in] max_len A maximum length. Characters beyond are trimmed. Zero is unlimited.
     * @param [in] escape_char What escape character to use.
     * @param [in] json_like Tf true, we attempt to be as json compatible as possible.
     *             - we dont print the type for uf::any
     *             - characters are printed as a single character string
     *             - void values and empty optionals are printed as 'null'
     *             - strings will contain backslash escaped backspace, tab, cr, lf, ff, quotation mark and backslash (in addition to 'chars')
     *             - errors are printed as string
     *             - tuples are printed as arrays.
     * @returns the textual description. If the string is trimmed, it ends in ...*/
    [[nodiscard]] std::string print(unsigned max_len = 0, std::string_view chars = {}, char escape_char = '%', bool json_like = false) const {
        std::string ret;
        std::string_view ty;
        if (auto err = print_to(ret, ty, max_len, chars, escape_char, json_like)) {
            if (*err) 
                (*err)->prepend_type0(type(), ty).throw_me();
            //non-empty optional and empty std::unique_ptr<value_error>: too long
            ret.resize(max_len);
            ret.append("...");
        }
        return ret;
    }

    /** Syntactic sugar for print(..., ..., ..., json_like = true). */
    [[nodiscard]] std::string print_json(unsigned max_len = 0, std::string_view chars = {}, char escape_char = '%') const {
        return print(max_len, chars, escape_char, true);
    }

    /** Checks if our content is exactly the same type that the given typestring.
     * Equivalent, but faster than 'converts_to(allow_converting_none)'.*/
    [[nodiscard]] bool is(std::string_view t) const noexcept { return t==_type; }

    /** Checks if our content is exactly the same type that the given type
     * (at least as a view, using these tags).
     * Equivalent, but faster than 'converts_to(allow_converting_none)'.*/
    template <typename T, typename ...tags>
    [[nodiscard]] bool is(uf::use_tags_t = uf::use_tags, tags...) const noexcept {
        static_assert(uf::impl::is_deserializable_f<T, true, true, tags...>(), "Type must be possible to deserialize into.");
        return _type == deserialize_type<T, tags...>(); 
    }

    /** Checks if our content can be converted to a given typestring.
     * Note that this function is quite slow for returning false.
     * When you want to use the 'allow_converting_none' policy, you should
     * perhaps use is() instead.
     * @returns true if a conversion would succeed and false if 
     * - the conversion is not possible (with this policy);
     * - expected values holding errors would need to be converted to a non-expected 
     *   type during conversion;
     * - the supplied typestring is bad; or
     * - 'this' is bad: the type and serialized data of 'this' mismatch.
     * This function throws only std::bad_alloc.*/
    [[nodiscard]] bool converts_to(std::string_view t, serpolicy policy = allow_converting_all) const;

    /** Checks if our content can be converted to a given type 
     * (at least as a view, using these tags).
     * Note that this function is quite slow for returning false.
     * When you want to use the 'allow_converting_none' policy, you should
     * perhaps use is() instead.
     * @returns true if a conversion would succeed and false if
     * - the conversion is not possible (with this policy);
     * - expected values holding errors would need to be converted to a non-expected
     *   type during conversion;
     * - 'this' is bad: the type and serialized data of 'this' mismatch.
     * This function throws only std::bad_alloc.*/
    template <typename T, typename ...tags>
    [[nodiscard]] bool converts_to(serpolicy policy = allow_converting_all,
                     uf::use_tags_t = uf::use_tags, tags...) const;

    /** Converts the content to the type given using the policy.
     * @param [in] t The typestring  of the target type
     * @param [in] policy The conversion policy
     * @param [in] check If true, we check the validity of from_type/from_data and of to_type
     *             Defaults to false.
     * @returns an uf::any containing the converted type/value.
     * @exception uf::type_mismatch_error the conversion is not possible (with this policy)
     * @exception uf::expected_with_error expected values holding errors would need to be
     *   converted to a non-expected type during conversion.
     * @exception uf::typestring_error bad typestring
     * @exception uf::value_mismatch_error the from type and serialized data mismatch*/
    [[nodiscard]] any convert_to(std::string_view t, serpolicy policy = allow_converting_all, bool check = false) const;

    /** Converts the content to the type given using the policy.
     * @param [in] policy The conversion policy
     * @param [in] check If true, we check the validity of from_type/from_data and of to_type
     *             Defaults to false.
     * @returns an uf::any containing the converted type/value.
     * @exception uf::type_mismatch_error the conversion is not possible (with this policy)
     * @exception uf::expected_with_error expected values holding errors would need to be
     *   converted to a non-expected type during conversion.
     * @exception uf::typestring_error bad typestring
     * @exception uf::value_mismatch_error the from type and serialized data mismatch*/
    template <typename T>
    [[nodiscard]] any convert_to(serpolicy policy = allow_converting_all, bool check = false) const;

    /** Converts the content to the type given using the policy.
     * @param [in] t The typestring  of the target type
     * @param [in] policy The conversion policy
     * @param [in] check If true, we check the validity of from_type/from_data and of to_type
     *             Defaults to false.
     * @returns the serialized value matching 't'.
     * @exception uf::type_mismatch_error the conversion is not possible (with this policy)
     * @exception uf::expected_with_error expected values holding errors would need to be
     *   converted to a non-expected type during conversion.
     * @exception uf::typestring_error bad typestring
     * @exception uf::value_mismatch_error if the from type and serialized data mismatch*/
    [[nodiscard]] std::string convert_to_ser(std::string_view t, serpolicy policy = allow_converting_all, bool check = false) const
    { auto s = convert(_type, t, policy, _value, check); return s ? std::move(*s) : std::string(_value); }

protected:
    explicit any_view(std::string_view t, std::string_view v) noexcept : _type(t), _value(v) {}
    std::string_view _type;
    std::string_view _value;
    friend std::pair<std::string, bool> impl::parse_value(std::string &to, std::string_view &value, bool liberal);
};

/** An object that can hold any value and its type.
  * It can be empty, meaning void.
  * It can be created from a typed C++ value.
  * It can also be created from raw form (serialized `tuple<string, T>` containing
  * the type string of `T` and its value.
  * It can also be created from a string description, like: <2si>("str";5)
  * It can also be pretty printed into such a form.
  *
  * See any_view for a type providing views to and into any variables.*/
struct any : any_view
{
    [[nodiscard]] any() noexcept(noexcept(std::string())) = default;
    [[nodiscard]] any(const any &o) : any_view(), _storage(o._storage) {
        _type  = std::string_view(_storage.data()+(o._type.data() -o._storage.data()), o._type.length()); 
        _value = std::string_view(_storage.data()+(o._value.data()-o._storage.data()), o._value.length());
    }
    [[nodiscard]] any(any &&o) noexcept : any_view() { operator=(std::move(o)); }
    template<typename ...tags>
    [[nodiscard]] any(const any &o, use_tags_t = {}, tags...) : any(o) {}
    template<typename ...tags>
    [[nodiscard]] any(any &&o, use_tags_t = {}, tags...) : any(std::move(o)) {}
    template<typename ...tags>
    [[nodiscard]] any(const any_view &o, use_tags_t = {}, tags...) { operator=(o); }
    /** Create an 'any' from an arbitrary serializable C++ variable.
     * You can also provide tags to select the appropriate helper functions.*/
    template<typename T, typename ...tags>
    [[nodiscard]] explicit any(const T &value, use_tags_t = {}, tags... tt) { 
        static_assert(uf::impl::is_serializable_f<T, true, tags...>(), "Type must be serializable.");
        if constexpr (uf::impl::is_serializable_f<T, false, tags...>())
            assign(value, use_tags, tt...);
    }
    /** Create an 'any' from a string-like type. Specialization.*/
    template<typename ...tags>
    [[nodiscard]] explicit any(const char* value, use_tags_t = {}, tags...) { assign(std::string_view(value)); }
    /** Create an 'any' from the serialized version of another any.
     * @param [in] v The string containing the serialized (type,value) 
     * @param [in] check If set, we check the content of contained
     *             any elements recursively. On OK, this is justa perf degradation,
     *             on failure it is an exception.*/
    [[nodiscard]] any(from_raw_t, std::string_view v, bool check=true) { assign(from_raw, v, check); }

    /** Create an 'any' from the typestring and serialized version of a value.
     * Here we do not check if the type and the value match. 
     * Use when this is already checked.
     * @param [in] t The typestring
     * @param [in] v The serialized value.*/
    [[nodiscard]] any(from_type_value_unchecked_t, std::string_view t, std::string_view v) :
        _storage(uf::concat(t, v)) { _set_type_value(t.length()); }

    /** Create an 'any' from the typestring and serialized version of a value.
     * We assume that the type and value follow each other in 'storage'
     * Here we do not check if the type and the value match.
     * Use when this is already checked.
     * @param [in] storage The concatenated type and value
     * @param [in] tlen The length of the type.*/
    [[nodiscard]] any(from_type_value_unchecked_t, std::string &&storage, size_t tlen) :
        _storage(std::move(storage)) { _set_type_value(tlen); }

    /** Create an 'any' from the typestring and serialized version of a value.
     * @param [in] t The typestring
     * @param [in] v The serialized value.
     * @param [in] allow_longer If set, both the type and value may be longer
     *             than their actual value, we simply trim them the appropriate length.
     * @exception uf::value_mismatch_error the type does not match the value.
     * @exception uf::typestring_error if 't' is invalid. */
    [[nodiscard]] any(from_type_value_t, std::string_view t, std::string_view v, bool allow_longer = false)
    { assign(from_type_value, t, v, allow_longer); }

    /** Create an 'any' from the typestring and serialized version of a value.
     * In this version you can specify the value by giving its length and a function,
     * which will copy the value to its char* parameter.
     * @param [in] t The typestring
     * @param [in] len The length of the value
     * @param [in] copier The function (void f(char*)) that copies the serialized value.
     * @exception uf::value_mismatch_error if the type does not match the value.
     * @exception uf::typestring_error if 't' is invalid. */
    [[nodiscard]] any(from_type_value_t, std::string_view t, size_t len, std::function<void(char*)> copier) {
        _storage.resize(t.length() + len);
        char * const after_type = std::copy(t.begin(), t.end(), _storage.data());
        copier(after_type);
        _set_type_value(t.length());
        auto [err, tlen, vlen] = impl::serialize_scan_by_type(_type, _value, false, true);
        (void)tlen, (void)vlen;
        if (err) err->throw_me();
    }

    /** Creates a any from a typestring with its default value. */
    [[nodiscard]] any(from_typestring_t, std::string_view typestring)
    { assign(from_typestring, typestring); }

    /** Create an 'any' from a textual description
     * @param [in] v The string containing the textual description.
     *               The description may or may not contain a type description
     *               before the value, so \<s\>"aaa" and just "aaa" are both valid.
     * @returns the created any.*/
    [[nodiscard]] any(from_text_t, std::string_view v);

    any &operator=(const any_view &o) { assign(o); return *this; }
    any &operator=(const any &o) { 
        _storage = o._storage; 
        _type = std::string_view(_storage.data()+(o._type.data() -o._storage.data()), o._type.length());
        _value = std::string_view(_storage.data()+(o._value.data()-o._storage.data()), o._value.length());
        return *this; 
    }
    any &operator=(any &&o) noexcept { 
        const size_t off_type = o._type.data() -o._storage.data();
        const size_t off_value = o._value.data()-o._storage.data();
        _storage = std::move(o._storage);
        _type = std::string_view(_storage.data()+ off_type, o._type.length());
        _value = std::string_view(_storage.data()+off_value, o._value.length());
        o.clear();
        return *this; 
    }

    /** Sets the value to void. Also frees memory. */
    void clear() noexcept { any_view::clear(); _storage = {}; }

    void swap(any &o) noexcept { //consider SSO and that _storage may have front and back padding 
        const ptrdiff_t ot1 =   _type.data()-  _storage.data(), ov1 =   _value.data()-  _storage.data();
        const ptrdiff_t ot2 = o._type.data()-o._storage.data(), ov2 = o._value.data()-o._storage.data();
        _storage.swap(o._storage); 
        any_view::swap(o);
        if (_type.length())  _type =  std::string_view(_storage.data()+ot2, _type.length());
        if (_value.length()) _value = std::string_view(_storage.data()+ov2, _value.length());
        if (o._type.length())  o._type =  std::string_view(o._storage.data()+ot1, o._type.length());
        if (o._value.length()) o._value = std::string_view(o._storage.data()+ov1, o._value.length());
    }

    auto tuple_for_serialization() const noexcept { return any_view::tuple_for_serialization(); } //Could not inherit due to =delete below.
    auto tuple_for_serialization() noexcept = delete; //Disable this (as it would allow view only). Provide special L1 handlers for any.

    /** Set the value of us to the value of an arbitrary serializable C++ variable.
     * You can also provide tags to select the appropriate helper functions.
     * @param [in] value The variable to serialize into us.
     * @param [in] tt You can specify additional data, which will be used to select which
     *                helper function (e.g., tuple_for_serialization) to apply.
     * @returns a reference to us.
     * If any exception happens (thrown by tuple_for_serialization() and other helpers), we 
     * leave 'this' unchanged.*/
    template<typename T, typename ...tags>
    any &assign(const T& value, uf::use_tags_t={}, tags... tt)
    {
        static_assert(uf::impl::is_serializable_f<T, true, tags...>(), "Type must be serializable.");
        if constexpr (uf::impl::is_serializable_f<T, false, tags...>()) {
            constexpr size_t tlen = impl::serialize_type_static<false, T, tags...>().size();
            std::string tmp(serialize_type<T, tags...>());
            if constexpr (impl::has_before_serialization_inside_v<T, tags...>)
                if (auto r = impl::call_before_serialization(&value, tt...); r.obj)
                    impl::call_after_serialization(&value, r, tt...); //This shall throw
            try {
                tmp.resize(tlen+impl::serialize_len(value, tt...));
                char *p = tmp.data()+tlen;
                impl::serialize_to(value, p, tt...);
                if constexpr (impl::has_after_serialization_inside_v<T, tags...>)
                    impl::call_after_serialization(&value, true, tt...);
                _storage = std::move(tmp);
                _set_type_value(tlen);
            } catch (...) {
                if constexpr (impl::has_after_serialization_inside_v<T, tags...>)
                    impl::call_after_serialization(&value, false, tt...);
                throw;
            }
        }
        return *this;
    }
    /** Set the value of us to the value of another any. A specialization
     * with no headroom.
     * @param [in] value The variable to serialize into us.
     * @returns a reference to us.*/
    template <typename ...tags>
    any &assign(const any &value, use_tags_t = {}, tags...) { return operator=(value); }
    /** Set the value of us to the value of another any. A specialization
     * with no headroom.
     * @param [in] value The variable to serialize into us.
     * @returns a reference to us.*/
    template <typename ...tags>
    any &assign(any&& value, use_tags_t = {}, tags...) { return operator=(std::move(value)); }
    /** Set the value of us to the value of an any_view. A specialization.
     * @param [in] value The variable to serialize into us.
     * @returns a reference to us.*/
    template <typename ...tags>
    any &assign(const any_view& value, use_tags_t = {}, tags...) 
    { return assign(from_type_value, value.type(), value.value(), false); }

    /** Sets us to the typestring and serialized version of a value
     * We accept that the value matches the type and perform no such checks.
     * @param [in] t The typestring
     * @param [in] v The serialized value.
     * @returns us.*/
    any &assign(from_type_value_unchecked_t, std::string_view t, std::string_view v) {
        _storage = uf::concat(t, v);
        _set_type_value(t.length());
        return *this;
    }
    /** Sets us to the typestring and serialized version of a value.
     * We assume that the type and value follow each other in 'storage'
     * Here we do not check if the type and the value match.
     * Use when this is already checked.
     * @param [in] storage The concatenated type and value
     * @param [in] tlen The length of the type.
     * @returns us.*/
    any &assign(from_type_value_unchecked_t, std::string &&storage, size_t tlen) {
        _storage = std::move(storage);
        _set_type_value(tlen);
        return *this;
    }
    /** Sets us to the typestring and serialized version of a value.
     * @param [in] t The typestring
     * @param [in] v The serialized value.
     * @param [in] allow_longer If set, both the type and value may be longer 
     *             than their actual value, we simply trim them the appropriate length.
     * @returns us. In case of an exception, we leavle 'this' unchanged.
     * @exception uf::value_mismatch_error if the type does not match the value. */
    any& assign(from_type_value_t, std::string_view t, std::string_view v, bool allow_longer = false) {
        auto [err, tlen, vlen] = impl::serialize_scan_by_type(t, v, allow_longer, true);
        if (err) err->throw_me();
        return assign(from_type_value_unchecked, t.substr(0, tlen), v.substr(0, vlen));
    }

    /** Set to the serialized version of another any.
     * @param [in] v The string containing the serialized (type,value) 
     * @param [in] check If set, we check the content of contained
     *             any elements recursively. On OK, this is justa perf degradation,
     *             on failure it is an exception.
     * In case of an exception, we leave 'this' unchanged.*/
    any &assign(from_raw_t, std::string_view v, bool check = true);

    /** Set to the value of a textual description
     * @param [in] v The string containing the textual description.
     *               The description may or may not contain a type description
     *               before the value, so \<s\>"aaa" and just "aaa" are both valid.*/
    any& assign(from_text_t, std::string_view v) 
    { return assign(any(from_text, v)); }

    /** Set to a type with its default value.
     * @exception uf::typestring error bad typestring (and leaves 'this' unchanged).
     * In case of an exception, we leavle 'this' unchanged.*/
    any& assign(from_typestring_t, std::string_view typestring)
    {
        std::string s(typestring);
        _storage = default_serialized_value(typestring, std::move(s));
        _set_type_value(typestring.length());
        return *this;
    }

    /** Create an 'any' value already serialized.
     * You can also provide tags to select the appropriate helper functions.
     * This is an allocation and memcopy efficient version of
     * uf::serialize(uf::any::create_from_type(t)); */
    template <typename T, typename ...tags>
    [[nodiscard]] static std::string create_serialized(const T &t, tags... tt)
    {
        using type = typename std::remove_cvref_t<T>;
        static_assert(uf::impl::is_serializable_f<T, true, tags...>(), "Type must be serializable.");
        if constexpr (uf::impl::is_serializable_f<T, false, tags...>()) {
            auto typestr = serialize_type<T, tags...>();
            std::string ret;
            if constexpr (impl::has_before_serialization_inside_v<type, tags...>)
                if (auto r = impl::call_before_serialization(&t, tt...); r.obj)
                    impl::call_after_serialization(&t, r, tt...); //This shall throw
            try {
                const uint32_t vlen = impl::serialize_len(t, tt...);
                ret.resize(4 + typestr.length() + 4 + vlen);
                char *p = ret.data();
                impl::serialize_to(typestr, p);
                impl::serialize_to(vlen, p);
                impl::serialize_to(t, p, tt...);
                assert(ret.data() + ret.size() == p);
                if constexpr (impl::has_after_serialization_inside_v<type, tags...>)
                    impl::call_after_serialization(&t, true, tt...);
                return ret;
            } catch (...) {
                if constexpr (impl::has_after_serialization_inside_v<type, tags...>)
                    impl::call_after_serialization(&t, false, tt...);
                throw;
            }
        } else
            return {};
    }

    /** Set the content to this part. Successful (rets true) only if both type and value
     * of the view is inside the current content.*/
    bool reduce_to(const any_view &part) noexcept {
        if (part.type().length())
            if (part.type().data() < _type.data() || _type.data()+_type.length() < part.type().data()+part.type().length())
                return false;
        if (part.value().length())
            if (part.value().data() < _value.data() || _value.data()+_value.length() < part.value().data()+part.value().length())
                return false;
        any_view::operator=(part);
        return true;
    }

protected:
    void _set_type_value(size_t tlen) noexcept {
        _type = std::string_view(_storage).substr(0, tlen);
        _value = std::string_view(_storage).substr(tlen);
    }
    std::string _storage;  ///<The place where we store the type and value (in no specific order and maybe with extra bytes before, after or in-between.
};

/** @}  serialization */

template <bool view, typename ...tags> inline bool impl::deserialize_from(const char *&p, const char *end, any &s, tags...) {
    std::pair<std::string_view, std::string_view> type_value;
    if (deserialize_from<true>(p, end, type_value)) return true;
    s.assign(from_type_value_unchecked, type_value.first, type_value.second);
    return false;
}
inline any_view::any_view(const any &o) noexcept : _type(o._type), _value(o._value) {}

inline any any_view::wrap() const {
    std::string storage = "a";
    //No need to call before or after serialization as we dont have those members
    storage.resize(1+impl::serialize_len(*this));
    char *p = storage.data()+1;
    impl::serialize_to(*this, p);
    return any(from_type_value_unchecked, std::move(storage), 1);
}

inline void any_view::get(any &t, serpolicy policy) const {
    any_view v;
    get_view(v, policy); //may throw
    t = v;
}

inline any any_view::convert_to(std::string_view t, serpolicy policy, bool check) const {
    auto s = convert(_type, t, policy, _value, check );
    return {from_type_value_unchecked, std::string(t), s ? std::move(*s) : std::string(_value)};
}

template <typename T>
inline any any_view::convert_to(serpolicy policy, bool check) const {
    static_assert(uf::impl::is_deserializable_f<T, true>(), "Type must be possible to deserialize into.");
    if constexpr (uf::impl::is_deserializable_f<T, true>())
        return convert_to(uf::serialize_type<T>(), policy, check);
    else
        return {};
}

static_assert(has_tuple_for_serialization_tag_v<false, any_view>, "HEJ!!!");
static_assert(has_tuple_for_serialization_tag_v<true, any_view>, "HEJ!!!");

static_assert(has_tuple_for_serialization_tag_v<false, any>, "HEJ!!!");
static_assert(!has_tuple_for_serialization_tag_v<true, any>, "HEJ!!!");

inline std::string to_string(const uf::any_view& a) { return a.print(); }


} //ns uf

namespace std {
  template <> struct hash<uf::any> {
    std::size_t operator()(const uf::any &a) const noexcept
    { return std::hash<std::string_view>()(a.type()) ^ (std::hash<std::string_view>()(a.value())<<1) ; }
  };
  template <> struct hash<uf::any_view> {
    std::size_t operator()(const uf::any_view &a) const noexcept
    { return std::hash<std::string_view>()(a.type()) ^ (std::hash<std::string_view>()(a.value())<<1) ; }
  };
}


namespace uf {

/** @addtogroup error
 * @{*/


/** Error class contained in expected */
struct error_value : public error
{
    error_value() : uf::error({}) {}
    error_value(const error_value&) = default;
    error_value(error_value&&) noexcept = default;
    error_value& operator=(const error_value&) = default;
    error_value& operator=(error_value&&) noexcept = default;
    error_value(std::string_view t, std::string_view m) : error({}), type(t), msg(m) {}
    error_value(std::string_view t, std::string_view m, uf::any v) : error({}), type(t), msg(m), value(std::move(v)) {}
    template <typename ...TT>
    error_value(std::string_view t, std::string_view m, TT&&... v) : error({}), type(t), msg(m), value(std::forward_as_tuple(v...)) { //forward_as_tuple allows non-lvalues
        static_assert(uf::impl::is_serializable_f<impl::single_type_t<TT...>, true>(), "Type must be serializable.");
    }
    std::string type;
    std::string msg;
    uf::any value;
    mutable std::string my_what;
    const char* what() const noexcept override { my_what = uf::concat(type, ':', msg); return my_what.c_str(); }
    auto tuple_for_serialization() const {return std::tie(type, msg, value); }
    auto tuple_for_serialization() { return std::tie(type, msg, value); }
    /** We convert to true if 'type' is non-empty.*/
    operator bool() const { return type.length(); }
};

static_assert(is_deserializable_v<error_value>, "Porce??");

/** @} */

/** @addtogroup serialization
 * @{*/

template <typename T, typename >
class expected
{
    std::variant<T, error_value> c;
public:
    //We allow implicit construction and assignment from T and expected error
    expected() noexcept(std::is_nothrow_default_constructible_v<std::variant<T, error_value>>) = default;
    expected(const expected &) = default;
    expected(expected &&) noexcept(std::is_nothrow_move_constructible_v<std::variant<T, error_value>>) = default;
    expected(const T&t) : c(t) {}
    expected(T&&t) noexcept(std::is_nothrow_constructible_v<T, T>) : c(std::move(t)) {}
    template <typename U, typename = std::enable_if_t<std::is_constructible_v<T, const U&>>>
    explicit expected(const expected<U> &e) noexcept(std::is_nothrow_constructible_v<T, const U&>) : 
        c(e.has_value() ? typename std::variant<T, error_value>(std::in_place_type<T>, *e) : e.error()) {}
    template <typename U, typename = std::enable_if_t<std::is_constructible_v<T, U&&>>>
    explicit expected(expected<U> &&e) noexcept(std::is_nothrow_constructible_v<T, U&&>) : 
        c(e.has_value() ? typename std::variant<T, error_value>(std::in_place_type<T>, std::move(*e)) : std::move(e.error())) {}
    expected(const error_value &e) : c(e) {}
    expected(error_value&&e) noexcept(std::is_nothrow_constructible_v<error_value, error_value>) : c(std::move(e)) {}
    expected &operator=(expected &&) = default;
    expected &operator=(const expected &) = default;
    expected &operator=(T&&t) { c = std::move(t); return *this; }
    expected &operator=(const T&t) { c = t; return *this; }
    expected &operator=(error_value&&e) { c = std::move(e); return *this; }
    expected &operator=(error_value &e) { c = e; return *this; }

    template <typename ...Args>
    T& emplace(Args&&... args) { c.template emplace<0>(std::forward<Args>(args)...); return std::get<0>(c); }
    error_value& emplace(const error_value& e) { set_error(e); return std::get<1>(c); }
    error_value& emplace(error_value&& e) noexcept { set_error(std::move(e)); return std::get<1>(c); }
    
    using value_type = T;
    bool has_value() const noexcept { return c.index()==0; }
    void set_default_value() { c = T(); }
    void set_error(error_value &&e) noexcept { c = std::move(e); }
    void set_error(const error_value &e) { c = e; }
    template <typename ...TT>
    void set_error(TT&&... args) { c = error_value(std::forward<TT>(args)...); }
    value_mismatch_error deref() const {
        return value_mismatch_error(concat("Dereferencing an uf::expected<%1> that has an error: ", error().what()),
                                    serialize_type(*this));
    }
    value_mismatch_error deref_err() const {
        if constexpr (is_serializable_v<T>)
            return value_mismatch_error(concat("Getting the error from an uf::expected<%1> that has a value: ",
                                               any(std::get<0>(c)).print(100)),
                                        serialize_type(*this));
        else
            return value_mismatch_error("Getting the error from an uf::expected<not-serializable> that has a value.");
    }
    error_value &error() { if (!has_value()) return std::get<1>(c); throw deref_err(); }
    const error_value &error() const { if (!has_value()) return std::get<1>(c); throw deref_err(); }
    T& operator *() { if (has_value()) return std::get<0>(c); throw deref(); }
    const T& operator *() const { if (has_value()) return std::get<0>(c); throw deref(); }
    T* operator ->() { if (has_value()) return &std::get<0>(c); throw deref(); }
    const T* operator ->() const { if (has_value()) return &std::get<0>(c); throw deref(); }

    /** Assing a raw value - effective deserialize.
     * Note that you cannot provide tags, so it works only for
     * types that require no tags for deserialization.*/
    void assign(const char*p, size_t len)
    {
        const char *const end = p + len;
        if (impl::deserialize_from<false>(p, end, *this)) {
            throw value_mismatch_error(uf::concat(impl::ser_error_str(impl::ser::val)," (deser x) <x%1>."),
                                       deserialize_type(*this));
        }
        if (p != end)
            throw value_mismatch_error("Content remained when deserializing uf::expected<%1>",
                                       deserialize_type(*this));
    }
    [[nodiscard]] std::string print(size_t max_len=0) const {
        if (has_value()) return (*this)->print(max_len);
        else return uf::concat("err:\"", error().what(), '\"');
    }
};

template <> class expected<void, void> : public expected<std::monostate> { using expected<std::monostate>::expected; };

/** @} serialization */

namespace impl {
template <typename T, typename ...tags> inline err_place call_before_serialization(const uf::expected<T> *e, tags... tt) noexcept
{ if constexpr(has_before_serialization_inside_v<T, tags...>) if (auto r = call_before_serialization(&*e, tt...); r.obj) return r; return {}; }
template <typename T, typename ...tags> inline void call_after_serialization(const uf::expected<T> *e, bool success, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::len))
{ if constexpr (has_after_serialization_inside_v<T, tags...>) call_after_serialization(&*e, success, tt...); }
template <typename T, typename ...tags> inline void call_after_serialization(const uf::expected<T> *e, err_place err, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::ser))
{ call_after_serialization(&*e, err, tt...); }
} //ns impl

template <typename T, typename ...tags> constexpr size_t impl::serialize_len(const expected<T>&o, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::len))
{ return impl::serialize_len(o.has_value()) + (o.has_value() ? 
                                               impl::serialize_len(*o, tt...) : 
                                               impl::serialize_len(o.error(), tt...)); }

template <typename T, typename ...tags> inline void impl::serialize_to(const expected<T>&o, char *&p, tags... tt) noexcept(is_noexcept_for<T, tags...>(nt::ser))
{
    serialize_to(o.has_value(), p);
    if (!o.has_value())
        impl::serialize_to(o.error(), p, tt...);
    else if constexpr (!is_void_like<false, std::remove_cvref_t<T>>::value)
        impl::serialize_to(*o, p, tt...);
}

template <bool view, typename T, typename ...tags> inline bool 
impl::deserialize_from(const char *&p, const char *end, uf::expected<T> &o, tags... tt) {
    bool has;
    if (impl::deserialize_from<false>(p, end, has)) return true;
    if (has) {
        o.set_default_value();
        if constexpr (!is_void_like<true, std::remove_cvref_t<T>>::value)
            return impl::deserialize_from<view>(p, end, *o, tt...);
    } else {
        error_value e;
        if (impl::deserialize_from<false>(p, end, e)) return true;
        o.set_error(std::move(e));
    }
    return false;
}

inline bool any_view::converts_to(std::string_view t, serpolicy policy) const {
    return !uf::cant_convert(_type, t, policy, _value); 
}

template <typename T, typename ...tags>
inline bool any_view::converts_to(serpolicy policy, use_tags_t, tags...) const {
    static_assert(uf::impl::is_deserializable_f<T, true, true, tags...>(), "Type must be possible to deserializable into.");
    return !uf::cant_convert(_type, deserialize_type<T, tags...>(), policy, _value);
}


namespace impl
{

/** Parse the type from 'type' till 'tend'. Do not accept void. 
 * In the error we return we assume this is the source type.*/
inline std::pair<std::unique_ptr<value_error>, size_t> parse_type_or_error(const char *type, const deserialize_convert_params &p) {
    assert(p.tstart<=type && type<=p.tend); //type is inside the source type
    if (type >= p.tend)
        return {create_des_typestring_source(p, ser_error_str(ser::end)), 0};
    else if (auto [tlen, problem] = parse_type(type, p.tend, false); !problem) 
        return {std::unique_ptr<value_error>{}, tlen};
    else {
        deserialize_convert_params local_p(p);
        local_p.type = type+tlen;
        return {create_des_typestring_source(local_p, ser_error_str(problem)), 0};
    }
}

/** Advance the source bytes by the amount needed for the source type.
 * If a source type pointer is provided, use that as the beginning of the
 * type and leave p.type unchanged (on error we overwrite p.type).
 * If it is not provided, use p.type and also avance it. 
 * @returns an already encapsulated error.*/
inline std::unique_ptr<value_error> advance_source(deserialize_convert_params &p, StringViewAccumulator* target,
                                                   const char* type = nullptr)
{
    std::string_view v(type ? type : p.type, p.tend - (type ? type : p.type));
    const char* old_p = p.p;
    if (auto err = serialize_scan_by_type_from(v, p.p, p.end, false)) {
        p.type = v.data();
        return create_error_for_des(std::move(err), &p);
    }
    if (target) *target << std::string_view{old_p, size_t(p.p - old_p)};
    if (!type)
        p.type = v.data();
    return {};
}

//In these routines we dont need to handle the case when the type is exactly as we receive, as that is handled elsewhere
//They all return an already encapsulated error.
template<bool, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, bool &o, tags...) {
    can_disappear = false;
    switch (*p.type) {
    case 'c': if (p.convpolicy & allow_converting_bool) { char oo;     if (deserialize_from<false>(p.p, p.end, oo)) goto err; o = bool(oo); } else return create_des_type_error(p, allow_converting_bool); break;
    case 'i': if (p.convpolicy & allow_converting_bool) { uint32_t oo; if (deserialize_from<false>(p.p, p.end, oo)) goto err; o = bool(oo); } else return create_des_type_error(p, allow_converting_bool); break;
    case 'I': if (p.convpolicy & allow_converting_bool) { uint64_t oo; if (deserialize_from<false>(p.p, p.end, oo)) goto err; o = bool(oo); } else return create_des_type_error(p, allow_converting_bool); break;
    default: 
        return create_des_type_error(p);
    err:
        return create_des_value_error(p);
    }
    p.type++;
    p.target_type++;
    return {};
}

template<bool, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, unsigned char &o, tags...) {
    can_disappear = false;
    switch (*p.type) {
    case 'b': if (p.convpolicy & allow_converting_bool) { bool oo; if (deserialize_from<false>(p.p, p.end, oo)) goto err; o = char(oo); } else return create_des_type_error(p, allow_converting_bool); break;
    case 'i': if ((p.convpolicy & allow_converting_ints_narrowing) == allow_converting_ints_narrowing) { uint32_t oo; if (deserialize_from<false>(p.p, p.end, oo)) goto err; o = char(oo); } else return create_des_type_error(p, allow_converting_ints_narrowing); break;
    case 'I': if ((p.convpolicy & allow_converting_ints_narrowing) == allow_converting_ints_narrowing) { uint64_t oo; if (deserialize_from<false>(p.p, p.end, oo)) goto err; o = char(oo); } else return create_des_type_error(p, allow_converting_ints_narrowing); break;
    default:
        return create_des_type_error(p);
    err:
        return create_des_value_error(p);
    }
    p.type++;
    p.target_type++;
    return {};
}

template<bool view, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, signed char &o, tags...)
{ return deserialize_convert_from_helper<view>(can_disappear, p, reinterpret_cast<unsigned char&>(o)); }

template<bool view, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, char &o, tags...)
{ return deserialize_convert_from_helper<view>(can_disappear, p, reinterpret_cast<unsigned char&>(o)); }

template<bool, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, int32_t &o, tags...) {
    can_disappear = false;
    switch (*p.type) {
    case 'b': if (p.convpolicy & allow_converting_bool) { bool oo; if (deserialize_from<false>(p.p, p.end, oo)) goto err; o = int32_t(oo); } else return create_des_type_error(p, allow_converting_bool); break;
    case 'c': if (p.convpolicy & allow_converting_ints) { unsigned char oo; if (deserialize_from<false>(p.p, p.end, oo)) goto err; o = int32_t(oo); } else return create_des_type_error(p, allow_converting_ints); break;
    case 'I': if ((p.convpolicy & allow_converting_ints_narrowing) == allow_converting_ints_narrowing) { int64_t oo; if (deserialize_from<false>(p.p, p.end, oo)) goto err; o = int32_t(oo); } else return create_des_type_error(p, allow_converting_ints_narrowing); break;
    case 'd': if (p.convpolicy & allow_converting_double) { double oo; if (deserialize_from<false>(p.p, p.end, oo)) goto err; o = int32_t(oo); } else return create_des_type_error(p, allow_converting_double); break;
    default:
        return create_des_type_error(p);
    err:
        return create_des_value_error(p);
    }
    p.type++;
    p.target_type++;
    return {};
}

template<bool view, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, uint32_t &o, tags...)
{ return deserialize_convert_from_helper<view>(can_disappear, p, reinterpret_cast<int32_t&>(o)); }

template<bool view, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, int16_t &o, tags...)
{ int32_t oo; auto ret = deserialize_convert_from_helper<view>(can_disappear, p, oo); o = oo; return ret; }

template<bool view, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, uint16_t &o, tags...)
{ int32_t oo; auto ret = deserialize_convert_from_helper<view>(can_disappear, p, oo); o = (int16_t)oo; return ret; }

template<bool, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, int64_t &o, tags...) {
    can_disappear = false;
    switch (*p.type) {
    case 'b': if (p.convpolicy & allow_converting_bool) { bool oo; if (deserialize_from<false>(p.p, p.end, oo)) goto err; o = int64_t(oo); } else return create_des_type_error(p, allow_converting_bool); break;
    case 'c': if (p.convpolicy & allow_converting_ints) { char oo; if (deserialize_from<false>(p.p, p.end, oo)) goto err; o = int64_t(oo); } else return create_des_type_error(p, allow_converting_ints); break;
    case 'i': if (p.convpolicy & allow_converting_ints) { int32_t oo; if (deserialize_from<false>(p.p, p.end, oo)) goto err; o = int64_t(oo); } else return create_des_type_error(p, allow_converting_ints); break;
    case 'd': if (p.convpolicy & allow_converting_double) { double oo; if (deserialize_from<false>(p.p, p.end, oo)) goto err; o = int64_t(oo); } else return create_des_type_error(p, allow_converting_double); break;
    default:
        return create_des_type_error(p);
    err:
        return create_des_value_error(p);
    }
    p.type++;
    p.target_type++;
    return {};
}

template<bool view, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, uint64_t &o, tags...)
{ return deserialize_convert_from_helper<view>(can_disappear, p, reinterpret_cast<int64_t&>(o)); }

template<bool view, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, double &o, tags...) {
    can_disappear = false;
    switch (*p.type) {
    case 'i': if (p.convpolicy & allow_converting_double) { int32_t oo; if (deserialize_from<false>(p.p, p.end, oo)) goto err; o = double(oo); } else return create_des_type_error(p, allow_converting_double); break;
    case 'I': if (p.convpolicy & allow_converting_double) { int64_t oo; if (deserialize_from<false>(p.p, p.end, oo)) goto err; o = double(oo); } else return create_des_type_error(p, allow_converting_double); break;
    default:
        return create_des_type_error(p);
    err:
        return create_des_value_error(p);
    }
    p.type++;
    p.target_type++;
    return {};
}

template<bool view, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, float &o, tags...)
{ double d; auto ret = deserialize_convert_from_helper<view>(can_disappear, p, d); o = d;  return ret;}

template<bool view, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, long double &o, tags...)
{ double d; auto ret = deserialize_convert_from_helper<view>(can_disappear, p, d); o = d; return ret;}

template<bool, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, std::string &s, tags...) {
    can_disappear = false;
    //allow conversion from 'lc'
    if (*p.type == 'l' && p.type + 1 < p.tend && p.type[1] == 'c') {
        if (!(p.convpolicy & allow_converting_aux))
            return create_des_type_error(p, allow_converting_aux);
        if (deserialize_from<false>(p.p, p.end, s))
            return create_des_value_error(p);
        p.type += 2;
        p.target_type++;
        return {};
    }
    return create_des_type_error(p);
}

template<bool, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, std::string_view &s, tags...) {
    can_disappear = false;
    //allow conversion from 'lc'
    if (*p.type == 'l' && p.type + 1 < p.tend && p.type[1] == 'c') {
        if (!(p.convpolicy & allow_converting_aux))
            return create_des_type_error(p, allow_converting_aux);
        if (deserialize_from<true>(p.p, p.end, s))
            return create_des_value_error(p);
        p.type += 2;
        p.target_type++;
        return {};
    }
    return create_des_type_error(p);
}

template <bool view, typename E, typename ...tags> inline typename std::enable_if<std::is_enum<E>::value, std::unique_ptr<value_error>>::type
deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, E &e, tags...)
{ uint32_t val; auto ret = deserialize_convert_from_helper<view>(can_disappear, p, val); e = E(val); return ret;}


//Backtracking code trying if some of the input types can decay to void
//bool is here to allow tail call optimization
//we return if we can be skipped AND-ed with what we got
//here 'can_disappear' is also an input parameter telling if we could be skipped
template <bool view, typename ...TT, typename ...tags> //tuples, not including empty
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, std::tuple<TT...> &t, tags... tt) {
    if constexpr (std::tuple_size_v<typename std::tuple<TT...>> == 0) {
        //nothing, convert incoming can_disappear to outgoing
        return {};
    } else {
        std::unique_ptr<value_error> saved_error;
        do {
            const char * const saved_target_type = p.target_type;
            //If the first element of the source type can disappear and does not match the first element
            //of the target tuple (e.g., source = an empty 'any', target = int), we return OK here, with the
            //target type left at 'int' (and not a type mismatch).
            bool loc_can_disappear;
            if (auto ret = deserialize_convert_from<view>(loc_can_disappear, p, std::get<0>(t), tt...))
                return ret; //I hope we get RVO here
            const bool new_could_be_skipped = loc_can_disappear && can_disappear;
            const char * const saved_p = p.p;
            const char * const saved_type = p.type;
            const size_t saved_error_size = p.errors ? p.errors->size() : 0;
            auto t2 = tuple_tail(t);
            can_disappear = new_could_be_skipped;
            if (auto ret = deserialize_convert_from_helper<view>(can_disappear, p, t2, tt...)) {
                if (!new_could_be_skipped) return ret; //I hope we get RVO here
                if (!saved_error)
                    saved_error = std::move(ret);
                //try re-doing things with the first type in in the incoming type skipped.
                //(but its errors kept)
                p.p = saved_p;
                p.type = saved_type;
                p.target_type = saved_target_type;
                if (p.errors)
                    p.errors->resize(saved_error_size);
                if (p.error_pos)
                    p.error_pos->resize(saved_error_size);
            } else
                return ret; //We are OK, I hope we get RVO here
        } while (p.type < p.tend);
        //no combination allows success. Throw the first error (probably the correct one)
        can_disappear = false;
        return saved_error;
    }
}

template <bool view, typename ...TT, typename ...tags> //tuples
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, std::tuple<TT...> &&t, tags... tt) {
    return deserialize_convert_from_helper<view>(can_disappear, p, t, tt...); //call lvalue-ref version
}

template <bool view, typename A, typename B, typename ...tags> //pair
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, std::pair<A, B> &t, tags... tt) {
    //do them as two-element tuples
    return deserialize_convert_from_helper<view>(can_disappear, p, std::tie(
            const_cast<typename std::add_lvalue_reference<typename std::remove_const<A>::type>::type>(t.first),
            t.second), //remove const needed for maps
        tt...);
}

template <bool view, typename T, size_t L, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, std::array<T, L> &o, tags... tt) {
    can_disappear = true;
    bool loc_can_disappear;
    for (T &t : o)
        if (auto ret = deserialize_convert_from<view>(loc_can_disappear, p, t, tt...)) return ret;
        else can_disappear &= loc_can_disappear;
    return {};
}

template <bool view, typename T, size_t L, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, T (&o)[L], tags... tt) {
    can_disappear = true;
    bool loc_can_disappear;
    for (T &t : o)
        if (auto ret = deserialize_convert_from<view>(loc_can_disappear, p, t, tt...)) return ret;
        else can_disappear &= loc_can_disappear;
    return {};
}

//guaranteed that the incoming type is lX
//source_type points to the list element data: l*X
template <bool view, typename ...TT, typename ...tags> //tuples, not including empty
inline std::unique_ptr<value_error> deserialize_convert_from_helper_from_list(bool& can_disappear, deserialize_convert_params& p, const char* source_type, std::tuple<TT...>& t, tags... tt) {
    if constexpr (std::tuple_size_v<typename std::tuple<TT...>> == 0) {
        //nothing, convert incoming can_disappear to outgoing
        return {};
    } else {
        //We dont really care if the incoming list elements can disappear. We try deserializing them into
        //the tuple members and that is it.
        bool loc_can_disappear;
        if (source_type) p.type = source_type;
        if (auto ret = deserialize_convert_from<view>(loc_can_disappear, p, std::get<0>(t), tt...))
            return ret; //I hope we get RVO here
        can_disappear &= loc_can_disappear;
        auto t2 = tuple_tail(t);
        return deserialize_convert_from_helper_from_list<view>(can_disappear, p, source_type, t2, tt...);
        //on success we will have p.type point to after the list: lX*
    }
}

//guaranteed that the incoming type is lX
//source_type points to the list element data: l*X
template <bool view, typename ...TT, typename ...tags> //tuples
inline std::unique_ptr<value_error> deserialize_convert_from_helper_from_list(bool& can_disappear, deserialize_convert_params& p, const char* source_type, std::tuple<TT...>&& t, tags... tt) {
    return deserialize_convert_from_helper_from_list<view>(can_disappear, p, source_type, t, tt...); //call lvalue-ref version
}

//guaranteed that the incoming type is lX
//source_type points to the list element data: l*X
template <bool view, typename A, typename B, typename ...tags> //pair
inline std::unique_ptr<value_error> deserialize_convert_from_helper_from_list(bool& can_disappear, deserialize_convert_params& p, const char* source_type, std::pair<A, B>& t, tags... tt) {
    //do them as two-element tuples
    return deserialize_convert_from_helper_from_list<view>(can_disappear, p, source_type, std::tie(
        const_cast<typename std::add_lvalue_reference<typename std::remove_const<A>::type>::type>(t.first),
        t.second), //remove const needed for maps   
        tt...);
}

//guaranteed that the incoming type is lX
//source_type points to the list element data: l*X
template <bool view, typename T, size_t L, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper_from_list(bool& can_disappear, deserialize_convert_params& p, const char* source_type, std::array<T, L>& o, tags... tt) {
    can_disappear = true;
    bool loc_can_disappear;
    for (T& t : o) {
        p.type = source_type;
        if (auto ret = deserialize_convert_from<view>(loc_can_disappear, p, t, tt...)) return ret;
        else can_disappear &= loc_can_disappear;
    }
    return {};
}

//guaranteed that the incoming type is lX
//source_type points to the list element data: l*X
template <bool view, typename T, size_t L, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper_from_list(bool& can_disappear, deserialize_convert_params& p, const char* source_type, T(&o)[L], tags... tt) {
    can_disappear = true;
    bool loc_can_disappear;
    for (T& t : o) {
        p.type = source_type;
        if (auto ret = deserialize_convert_from<view>(loc_can_disappear, p, t, tt...)) return ret;
        else can_disappear &= loc_can_disappear;
    }
    return {};
}

//guaranteed that the incoming type is not 'o'
template <bool view, typename T, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, std::unique_ptr<T> &pp, tags... tt) {
    pp = std::make_unique<T>();
    p.target_type++;
    return deserialize_convert_from<view>(can_disappear, p, *pp, tt...);
}
//guaranteed that the incoming type is not 'o'
template <bool view, typename T, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, std::shared_ptr<T> &pp, tags... tt) {
    pp = std::make_shared<T>();
    p.target_type++;
    return deserialize_convert_from<view>(can_disappear, p, *pp, tt...);
}
//guaranteed that the incoming type is not 'o'
template <bool view, typename T, typename ...tags>
inline std::unique_ptr<value_error> deserialize_convert_from_helper(bool &can_disappear, deserialize_convert_params &p, std::optional<T> &o, tags... tt) {
    o.emplace();
    p.target_type++;
    return deserialize_convert_from<view>(can_disappear, p, *o, tt...);
}


/** Sees what the top-level type becomes if we remove X elements.
 * @returns the number of elements
 * - for lists 0 means lX, 1 means anthing else.
 * - for maps we always return 1
 * - for expected we return 0 for xX or xxX or xxxxX, etc.
 * - for tuples the retval counts the number of non-X elements.
 * - on bad type it returns undefined*/
inline unsigned ATTR_PURE__ count_non_X(const char *type, const char *tend) noexcept
{
    if (type >= tend) return 0;
    if (*type == 'X') return 0;
    if (*type == 'l' || *type=='x') return count_non_X(type+1, tend);
    //note maps cannot be all X, since key must be non-all-X
    if (*type != 't') return 1;
    type++;
    unsigned size = 0, count = 0;
    while (type < tend && '0' <= *type && *type <= '9')
        size = size * 10 + *type++ - '0';
    while (size-- && type < tend)
        if (type + 1 < tend && type[0] == 'X')
            type += 1;
        else {
            if (count_non_X(type, tend)) count++;
            if (auto [tlen, problem] = parse_type(type, tend, false); !problem)
                type += tlen;
            else return count; //on bad type return how many have we found so far
        }
    return count;
}

/** Determines if the type only contains 'any' primitive types and no maps.
 * (This is because it is tested if an expected<> of this type
 * carrying an error may disappear or not. Maps cannot disappear.)
 * On void or bad type we return false*/
constexpr bool ATTR_PURE__ is_all_any(std::string_view type)
{
    if (type.length() == 0) return false;
    switch (type.front()) {
    default:
        return false;
    case 'a': return true;
    case 'l':
        return is_all_any(type.substr(1));
    case 't':
        uint32_t size = 0, len = 1;
        type.remove_prefix(1);
        while (type.length()) {
            size = size * 10 + type.front() - '0';
            type.remove_prefix(1);
            len++;
        }
        if (size < 2) return false;
        while (size--)
            if (!is_all_any(type)) return false;
        return true;
    }
}

template <typename T>
constexpr int tuple_size = -1;

template <typename ...TT>
constexpr int tuple_size<std::tuple<TT...>> = sizeof...(TT);

template <typename A, typename B>
constexpr int tuple_size<std::pair<A,B>> = 2;

template <typename T, size_t L>
constexpr int tuple_size<std::array<T,L>> = L;

template <typename T, size_t L>
constexpr int tuple_size<T[L]> = L;

/** Checks if a source type can be deserialized into a target type.
 * If the source value is also available (in serialized form), we can
 * fully check if a deserialize_convert_from to a variable of target type
 * would succeed or not. That is, we can check content of 'a' and 'x'.
 * If the source value is not available, set p.p and p.tend to null.
 * In this case we return true liberally: if any potential value of the
 * source type allows conversion to the target type, we return success.
 * E.g., 't2ai' will be convertible to 'i' because the 'a' may hold void.
 * Additionally, if a 'target' is specified, we actually carry out the
 * conversion, producing a serialized value.*/
template <bool has_source, bool has_target>
std::unique_ptr<value_error> cant_convert(deserialize_convert_params &p, StringViewAccumulator *target);

extern template std::unique_ptr<value_error>
cant_convert<false,false>(deserialize_convert_params &p, StringViewAccumulator *target);
extern template std::unique_ptr<value_error>
cant_convert<true, false>(deserialize_convert_params &p, StringViewAccumulator *target);
extern template std::unique_ptr<value_error>
cant_convert<true, true>(deserialize_convert_params &p, StringViewAccumulator *target);
//cant_convert<false,true> is invalid

/** Deserialize a value of a source type into a variable of a target type.
 * @param p Contains pointers to the serialized value, the source and destination
 *          types.
 * @param [out] o The C++ variable to deserialize into. If it is a void-like type
 *                (such as std::monostate), we still process the source,
 *                as it may only contain expected<void> values, which may need to be
 *                checked for errors.
 * @param [in] tt You can specify additional data, which will be used to select which
 *                helper function (e.g., tuple_for_serialization) to apply.
 * @returns Any error that happened. If none, we also return a bool flag, which is
 *          true for xT types, where T can disappear (be an 'a' or 'X' or combinations)
 *          and where the expected<T> was carrying an error. This is used in trying to match
 *          a source tuple type to a shorter destination tuple type, where some elements may 
 *          decay to void.
 * If the source got an expected<T> that is not deserialized into an expected<T>,
 * but to a T (or compatible type) (so conversion happens), we
 *   a) throw the error_value in the expected<T> if it contains no value, but an error; or
 *   b) store the error_value and its positions in 'p.error' and 'p.error_pos' if non-null.
 * Note that for the above a) reason this is never a nothrow function as any input data may 
 * contain an expected, that we cannot convert. (Also we may fail to allocate in case b) to
 * the 'error' and 'error_pos' vectors.)
 * Note that this is a family of templated functions, one for all possible destination
 * type of 'o'. Do not use this just to check type convertability, use cant_convert()
 * for that.*/
template<bool view, typename T, typename ...tags, typename>
inline std::unique_ptr<value_error> deserialize_convert_from(bool &can_disappear, deserialize_convert_params &p, T &o, tags... tt)
{
    can_disappear = false; //this is our default
    //We need to handle single element aggregates (tuples, arrays, C-arrays) separately.
    //Their typestring is the same as that of their element, but they do not have those 
    //member function as their element (such as clear() for containers or emplace() for optional, etc.)
    //This could be done using an overload of deserialize_convert_from() with a tuple, but 
    //gcc found that ambiguous.
    if constexpr (uf::impl::is_single_element_tuple<T>::value)
        return deserialize_convert_from<view>(can_disappear, p, std::get<0>(o), tt...);
    else if constexpr (uf::impl::is_single_element_array<T>::value)
        return deserialize_convert_from<view>(can_disappear, p, o[0], tt...);
    else {
        constexpr auto otypestr = serialize_type_static<true, T, tags...>(); //may be empty for void targets
        //Check that the target type begins with the type of 'o'
        assert(p.target_tend >= p.target_type);
        assert(memcmp(otypestr.c_str(), p.target_type, std::min(otypestr.size(), unsigned(p.target_tend - p.target_type))) == 0);
        if constexpr (is_void_like<true, T, tags...>::value) {
            if (p.tend == p.type)
                //void->void
                return {};
            //else fallthrough to handle T->(void) (will succeed only for all-X types)
        } else {
            //A non-void type
            //fast path: p.type begins with otypestring: exact same types
            if (ptrdiff_t(otypestr.size()) <= p.tend - p.type &&
                memcmp(otypestr.c_str(), p.type, std::min(ptrdiff_t(otypestr.size()), p.tend - p.type)) == 0) {
                if (deserialize_from<view>(p.p, p.end, o, tt...))
                    return create_des_value_error(p);
                p.type += otypestr.size();
                p.target_type += otypestr.size();
                return {};
            }
        }
        if constexpr (otypestr[0] == 'a') {
            //Handle 'a' before testing for has_tuple_for_serialization<T>::value
            //Here we know that the from_type (p.type) is not 'a', as it was handled above
            //if (count_non_X(p.type, p.tend) == 0) <= allow X->a. Uncomment if you dont want that.
            //    //all-X -> a
            //    if (auto err = create_des_type_error(p)) err->throw_me(); //X only types cannot serialize into 'a'
            if (!(p.convpolicy & allow_converting_any))
                return create_des_type_error(p, allow_converting_any);
            //T->a, may be (void)->a
            //copy the upcoming type to an any. We keep xT as xT.
            std::string_view worktype{p.type, size_t(p.tend-p.type)};
            const char *work_p = p.p;
            if (auto err = serialize_scan_by_type_from(worktype, work_p, p.end, false)) //use this version to allow remaining types
                return err;
            o.assign(from_type_value_unchecked, {p.type, size_t(worktype.data()-p.type)}, {p.p, size_t(work_p - p.p)});
            p.type = worktype.data();
            p.p = work_p;
            p.target_type += otypestr.size();
            return {};
        } else if constexpr (has_tuple_for_serialization_v<true, T, tags...> && otypestr[0] != 'e') {
            //Handle struct targets with tuple_for_serialization()
            static_assert(is_deserializable_v<T, tags...> || view, 
                          "Cannot deserialize to view struct in deserialize_convert_from<false>().");
            static_assert(!has_after_deserialization_v<T, tags...> || !has_after_deserialization_simple_v<T, tags...>,
                          "Only one of after_deserialization_simple() or after_deserialization(U&&) should be defined.");
            bool need_to_call_after = true;
            try {
                auto &&tmp = invoke_tuple_for_serialization(o, tt...);
                auto ret = deserialize_convert_from<view>(can_disappear, p, tmp, tt...);
                if (ret) {
                    if constexpr (has_after_deserialization_error_v<T, tags...>)
                        invoke_after_deserialization_error(o, tt...);
                    return ret;
                }
                need_to_call_after = false; //dont call after_deserialization_error if after_deserialization() throws
                if constexpr (has_after_deserialization_v<T, tags...>) invoke_after_deserialization(o, std::move(tmp), tt...);
                else if constexpr (has_after_deserialization_simple_v<T, tags...>) invoke_after_deserialization_simple(o, tt...);
                return ret;
            } catch (...) { //some error other than uf::value_error, e.g., thrown by a tuple_for_serialization()
                if (need_to_call_after)
                    if constexpr (has_after_deserialization_error_v<T, tags...>)
                        invoke_after_deserialization_error(o, tt...);
                throw;
            }
#ifdef HAVE_BOOST_PFR
        } else if constexpr (is_really_auto_serializable_v<T> && otypestr[0] != 'e') {
            //Handle struct targets with auto serialization
            static_assert(is_deserializable_v<T, tags...> || view, 
                          "Cannot deserialize to view struct in deserialize_convert_from<false>().");
            auto &&tmp = boost::pfr::structure_tie(o);
            //let errors (some error other than uf::value_error, e.g., thrown by a tuple_for_serialization()) pass up
            return deserialize_convert_from<view>(can_disappear, p, tmp, tt...);
#endif
        } else if (p.type < p.tend && (*p.type == 'x' || *p.type == 'X')) {
            const bool is_void = *p.type == 'X';
            //We got an expected. xT->U
            bool has_value;
            if (deserialize_from<false>(p.p, p.end, has_value))
                return create_des_value_error(p);
            p.type++;
            if (p.type >= p.tend && !is_void)
                return create_des_typestring_source(p, ser_error_str(ser::end));
            if (has_value) {
                if constexpr (is_expected<T>::value) {
                    o.set_default_value();
                    if (is_void) {
                        //got an expected<void>
                        //X->xT (where t!=void). 
                        //First we try to convert void->T. 
                        deserialize_convert_params local_p(p, p.type, p.target_tend); //source is empty now
                        local_p.target_type++; //now local_p is (void)->x*T
                        if (auto ret = deserialize_convert_from<view>(can_disappear, local_p, *o, tt...)) { //void->T does not work, now try decaying X to void.
                            if (!(p.convpolicy & allow_converting_expected)) {
                                --p.type;
                                return create_des_type_error(p, allow_converting_expected);
                            } else //we dont move the value pointers as we have deserialized the X to a void
                                can_disappear = true;
                        } else {
                            p.target_type = local_p.target_type;
                            p.p = local_p.p;
                        }
                        return {};
                    } else {
                        //we have a non-void expected to deserialize into some expected
                        //x*T->*xO or x*T->*X
                        if constexpr (std::is_same_v<uf::expected<void>, T>) {
                            p.target_type++; //move to x*T->X*
                            //try deserializing T->void
                            deserialize_convert_params local_p(p, p.tend, p.target_type); //target is empty now
                            auto ret = deserialize_convert_from<view>(can_disappear, local_p, *o, tt...);
                            p.type = local_p.type;
                            p.p = local_p.p;
                            return ret;
                        } else {
                            p.target_type++; //move to x*T->x*O
                            //try deserializing to the content of 'o'
                            return deserialize_convert_from<view>(can_disappear, p, *o, tt...);
                        }
                    }
                } else {
                    //deserializing an expected to a non-expected type
                    //xT->U or X->U. U!='a' and U!=xV as that was handled above
                    if (!(p.convpolicy & allow_converting_expected)) {
                        --p.type;
                        return create_des_type_error(p, allow_converting_expected);
                    }
                    /* if (is_void) it is X*->U: 
                        *    Decay the X and try again with what comes after the X.
                        * else it is x*T->U. 
                        *    Try deserializing the value into 'o'. by doing x*T->U
                        *    (if o is error_value, we simply fail.)
                        * So in both cases we go on.*/
                    return deserialize_convert_from<view>(can_disappear, p, o, tt...);
                }
            } else { //the expected contains an error
                const char * const saved_type = p.type + 1;
                if constexpr (std::is_same_v<uf::error_value, T>) {
                    //xT(error)->e: OK
                    if (deserialize_from<false>(p.p, p.end, o))
                        return create_des_value_error(p);
                    if (auto err = advance_source(p, nullptr)) //jump over source type
                        return err;
                    //else fallthrough to return if we could have disappeared or not.
                } else {
                    //xT(error)->U
                    error_value e;
                    if (deserialize_from<false>(p.p, p.end, e))
                        return create_des_value_error(p);
                    //First compare types
                    if (auto err = uf::impl::cant_convert<false, false>(p, nullptr)) //also moves p.type, p.target_type
                        return err;
                    if constexpr (is_expected<T>::value) {
                        //xT->xU, types compatible
                        o.set_error(std::move(e));
                    } else {
                        //xT->U (U is not expected, nor error, nor any)
                        //Oops, we got an error and cannot place it.
                        if (p.errors) {
                            p.errors->push_back(std::move(e)); //leave 'o' unchanged
                            if (p.error_pos)
                                p.error_pos->emplace_back(p.type - 1 - p.tstart, p.target_type - p.target_tstart); //wont work inside an 'a'
                        } else  //Here we simply throw it and ignore the rest of the errors.
                            throw e;
                    }
                }
                can_disappear = is_all_any(std::string_view(saved_type, p.tend - saved_type));
                return {};
            }
        } else if (p.type < p.tend && *p.type == 'o') {
            //We got an optional. oT->U
            bool has_value;
            if (deserialize_from<false>(p.p, p.end, has_value))
                return create_des_value_error(p);
            p.type++;
            if (p.type >= p.tend)
                return create_des_typestring_source(p, ser_error_str(ser::end));
            if (has_value) {
                if constexpr (otypestr[0] == 'o') //oT->oU, T!=U
                    return deserialize_convert_from_helper<view>(can_disappear, p, o, tt...); //allocate and try receiving a value
                else //oT->U. U!='a' as that was handled above
                    return deserialize_convert_from<view>(can_disappear, p, o, tt...);
            } else {//the optional is empty
                if constexpr (otypestr[0] == 'o') {
                    //This is oT->oU with empty optional. Check that T->U is possible 
                    if (auto err = uf::impl::cant_convert<false, false>(p, nullptr)) //also moves p.type, p.target_type
                        return err;
                    o.reset(); //shared_ptr, unique_ptr and optional all have reset()
                    return {};
                } else {
                    p.type--;
                    return create_error_for_des(std::make_unique<uf::type_mismatch_error>(
                        "Empty optional <%1> can only convert to an optional and not <%2>", std::string_view{}, std::string_view{}), &p);
                }
            }
        } else if constexpr (is_expected<T>::value) {
            //We are expecting an expected, but got something else. Here we may still get a void
            if (p.type<p.tend && *p.type == 'e') {
                //e->xT
                if (!(p.convpolicy & allow_converting_expected))
                    return create_des_type_error(p, allow_converting_expected);
                error_value e;
                if (deserialize_from<false>(p.p, p.end, e))
                    return create_des_value_error(p);
                o.set_error(std::move(e));
                p.type++;
                p.target_type += otypestr.size();
                return {};
            } else if constexpr (otypestr[0] == 'X') { 
                //T->X, We got a non-expected value (*p.type=='x' is handled above) into an X.
                //This is only valid if we have void incoming
                if (p.type!=p.tend)
                    return create_des_type_error(p);
                else if (p.convpolicy & allow_converting_expected) {
                    o.set_default_value(); //set void, successfully and fall through to return false
                    return {};
                } else
                    return create_des_type_error(p, allow_converting_expected);
            } else {
                //T->xU
                //just try to deserialize into the expected
                if (!(p.convpolicy & allow_converting_expected))
                    return create_des_type_error(p, allow_converting_expected);
                o.set_default_value();
                p.target_type++; //step over 'x'
                return deserialize_convert_from<view>(can_disappear, p, *o, tt...); //this will fail if we dont have type bytes left (e.g., (void)->x*U)
            }
        } else if constexpr (otypestr[0] == 'o') {
            if (p.type == p.end) {
                //void->oT
                if (!(p.convpolicy & uf::allow_converting_aux))
                    return create_des_type_error(p, allow_converting_aux);
                o.reset();
                p.target_type += otypestr.size();
                return {};
            }
            //OK it is T->oU
            return deserialize_convert_from_helper<view>(can_disappear, p, o, tt...); //allocate and try receiving a value
        } else {
            //Here we handle the cases, when 
            //- neither source, nor target is optional or expected; 
            //- target is neither 'any' nor a structure (but can be a tuple); and
            //- source is not the same as target.
            //Ensure we have remaining incoming type left
            if (p.type >= p.tend) //type mismatch void->T (T!=any nor expected, nor optional)
                return create_des_type_error(p);
            //Now we know that what we got is different from the type of T
            //If we are getting X-only types, we need to eliminate them before
            //being able to deserialize into 'o'. This will be paid attention throught below

            //First handle special case of s->lc
            if constexpr (otypestr == make_string("lc")) {
                if (*p.type == 's') {
                    //s->lc
                    if (!(p.convpolicy & allow_converting_aux))
                        return create_des_type_error(p, allow_converting_aux);
                    if (deserialize_from<view>(p.p, p.end, o, tt...))
                        return create_des_value_error(p);
                    p.type++;
                    p.target_type += otypestr.size();
                    return {};
                }
                //else we fall through to normal T->lc processing
            }
            switch (*p.type) {
            case 'a':
                //we got an any, but we are not any, nor expected
                //Try deserializing the content of the any, if policy allows
                if (p.convpolicy & allow_converting_any) {
                    //this will work when we got an empty incoming any and
                    //deserialize into a void type or into an X.
                    any_view a;
                    if (deserialize_from<true>(p.p, p.end, a))
                        return create_des_value_error(p);
                    deserialize_convert_params local_p(a.value(), a.type(), &o, p.convpolicy, &p,
                                                       p.errors, p.error_pos, tt...);
                    auto ret = deserialize_convert_from<view>(can_disappear, local_p, o, tt...);
                    if (!ret) {
                        if (local_p.type < local_p.tend) ret = create_des_typestring_source(local_p, impl::ser_error_str(impl::ser::tlong));
                        else if (local_p.target_type < local_p.target_tend) ret = create_des_type_error(local_p);
                        else {
                            p.type++;
                            p.target_type += otypestr.size();
                        }
                    }
                    can_disappear = false;
                    return ret;
                } else
                    return create_des_type_error(p, allow_converting_any);
            case 'l':
            {
                //check special case of lc->s
                if constexpr (otypestr[0] == 's') 
                    if (p.type + 1 < p.tend && p.type[1] == 'c') {
                        if (p.convpolicy & allow_converting_aux) {
                            if (deserialize_from<view>(p.p, p.end, o))
                                return create_des_value_error(p);
                            p.type += 2;
                            p.target_type++;
                            return {};
                        } else
                            return create_des_type_error(p, allow_converting_aux);
                    } //Else we continue checking list normally
                uint32_t size;
                if (deserialize_from<false>(p.p, p.end, size))
                    return create_des_value_error(p);
                p.type++;
                if constexpr (otypestr[0] == 't') {
                    if (!(p.convpolicy & allow_converting_tuple_list))
                        return create_des_type_error(p, allow_converting_tuple_list);
                    //lX->t<num>XYZ
                    constexpr int target_tuple_size = tuple_size<T>;
                    static_assert(target_tuple_size>=0);
                    if (target_tuple_size!=size)
                        return create_error_for_des(std::make_unique<uf::value_mismatch_error>(uf::concat("Size mismatch when converting <%1> to <%2> (", size, "!=", target_tuple_size, ").")), &p);
                    auto [err, ttlen] = parse_type_or_error(p.type, p); //parse
                    if (err) return std::move(err);
                    const char* end_of_incoming = p.type + ttlen;
                    //Create a copy, where tend is replaced by end_of_incoming.
                    //We dont want to read beyond the incoming tuple's type
                    deserialize_convert_params local_p(p, end_of_incoming, p.target_tend);
                    //step over the tNUM in p.target_type
                    local_p.target_type++;
                    while (local_p.target_type < local_p.target_tend &&
                            '0' <= *local_p.target_type && *local_p.target_type <= '9') //note p.target_type is null terminated
                        local_p.target_type++;
                    if (auto r = deserialize_convert_from_helper_from_list<view>(can_disappear, local_p, p.type, o, tt...))
                        return r;
                    p.type = local_p.type;
                    p.target_type = local_p.target_type;
                    p.p = local_p.p;
                    return {};
                } else if constexpr (otypestr[0] != 'l') {
                    //lT->void must happen
                    if (size) {
                        std::monostate t;
                        const char *original_type = p.type;
                        while (size--) {
                            p.type = original_type;
                            if (auto ret = deserialize_convert_from<view>(can_disappear, p, t, tt...))
                                return ret;
                        }
                    } else {
                        //check type compatibility
                        deserialize_convert_params local_p(p, p.tend, p.target_type); //empty remaining target type
                        if (auto err = cant_convert<false, false>(local_p, nullptr))
                            return err;
                        p.type = local_p.type;
                    }
                    //Try again to fill 'o' (tail recursion)
                    return deserialize_convert_from<view>(can_disappear, p, o, tt...);
                } else {
                    //lT->lU
                    o.clear();
                    if (!size) {
                        if (auto [err, tlen] = parse_type_or_error(p.type, p); err)
                            return std::move(err);
                        else p.type += tlen;
                        //For empty lists we dont check if list element type are compatible with T::value_type
                        p.target_type += otypestr.size();
                        return {};
                    } else {
                        typename deserializable_value_type<T>::type e;
                        if constexpr (has_reserve_member<T>::value) o.reserve(size);
                        const char* original_type = p.type, *original_target_type = p.target_type+1; //+1 step over 'l' in target type
                        can_disappear = true;
                        bool loc_can_disappear;
                        while (size--) {
                            p.type = original_type;
                            p.target_type = original_target_type;
                            if (auto ret = deserialize_convert_from<view>(loc_can_disappear, p, e, tt...))
                                return ret;
                            else can_disappear &= loc_can_disappear;
                            add_element_to_container(o, std::move(e));
                        }
                        return {};
                    }
                }
            }
            case 'm':
            {
                uint32_t size;
                if (deserialize_from<false>(p.p, p.end, size))
                    return create_des_value_error(p);
                if constexpr (otypestr[0] == 'm') {
                    //mTU->mVW
                    o.clear();
                    p.type++;
                    if (!size) {
                        //jump across the key and value type
                        for (int i = 0; i<2; i++)
                            if (auto [err, tlen] = parse_type_or_error(p.type, p); err) 
                                return std::move(err);
                            else p.type += tlen;
                        //We dont check if they are compatible with T::value_type
                        p.target_type += otypestr.size();
                    } else {
                        std::pair<typename T::key_type, typename T::mapped_type> e;
                        const char* original_type = p.type, *original_target_type = p.target_type+1; //+1 step over 'm' in target type
                        while (size--) {
                            p.type = original_type;
                            p.target_type = original_target_type;
                            if (auto err = deserialize_convert_from<view>(can_disappear, p, e.first, tt...))
                                return err;
                            if (auto err = deserialize_convert_from<view>(can_disappear, p, e.second, tt...))
                                return err;
                            add_element_to_container(o, std::move(e));
                        }
                    }
                    can_disappear = false;
                    return {};
                } else if constexpr (otypestr[0] == 'l') {
                    //maps serialize to lists only if the mapped type is an X
                    //we know for sure that the key is not an all-X type
                    auto [err, ktlen] = parse_type_or_error(p.type + 1, p);
                    if (err) return std::move(err);
                    const char *mapped_type = p.type + 1 + ktlen;
                    if (count_non_X(mapped_type, p.tend))
                        //mTU->lV
                        return create_des_type_error(p);
                    //mT(all-X)->lU
                    o.clear();
                    auto [err2, mtlen] = parse_type_or_error(mapped_type, p);
                    if (err2)
                        return std::move(err2);
                    if (!size) {
                        //jump across the key and mapped type
                        p.type = mapped_type + mtlen;
                        //We dont check if they are compatible with T::value_type
                        p.target_type += otypestr.size();
                    } else {
                        const char *end_of_incoming = mapped_type + mtlen;
                        //Create a copy, where tend is replaced by end_of_incoming and target type is
                        //replaced to the list's type
                        deserialize_convert_params local_p(p, end_of_incoming, p.target_type+otypestr.size());
                        typename deserializable_value_type<T>::type key;
                        if constexpr (has_reserve_member<T>::value) o.reserve(size);
                        std::monostate t;
                        while (size--) {
                            local_p.type = p.type+1;
                            local_p.target_type = p.target_type + 1;
                            if (auto ret = deserialize_convert_from<view>(can_disappear, local_p, key, tt...))
                                return ret;
                            //here we are sure that an all-X value comes
                            //(may be lX, or other compound thing). Eat it
                            //by attempting to deserialize it into a void type
                            if (auto ret = deserialize_convert_from<view>(can_disappear, local_p, t))
                                return ret;
                            add_element_to_container(o, std::move(key));
                        }
                        p.p = local_p.p;
                        p.type = local_p.type;
                        p.target_type = local_p.target_type;
                    }
                    can_disappear = false;
                    return {};
                } else
                    return create_des_type_error(p);
            }
            case 't':
            {
                auto [err, ttlen] = parse_type_or_error(p.type, p);
                if (err) return std::move(err);
                const char *end_of_incoming = p.type + ttlen;
                p.type++;
                uint32_t size = 0;
                while (p.type < p.tend && '0' <= *p.type && *p.type <= '9') {
                    size = size*10 + *p.type - '0';
                    p.type++;
                }
                std::unique_ptr<value_error> ret;
                if constexpr (otypestr[0] == 'l') { 
                    //This is t<num>XYZ->lX
                    if (p.convpolicy & allow_converting_tuple_list) { 
                        //Create a copy, where tend is replaced by end_of_incoming.
                        //We dont want to ready beyond the incoming tuple's type
                        deserialize_convert_params local_p(p, end_of_incoming, p.target_tend);
                        typename deserializable_value_type<T>::type e;
                        o.clear();
                        if constexpr (has_reserve_member<T>::value) o.reserve(size);
                        const char* const original_target_type = local_p.target_type+1; //+1 step over 'l' in target type
                        can_disappear = true;
                        bool loc_can_disappear;
                        while (size--) {
                            local_p.target_type = original_target_type;
                            ret = deserialize_convert_from<view>(loc_can_disappear, local_p, e, tt...);
                            if (ret) break;
                            else can_disappear &= loc_can_disappear;
                            add_element_to_container(o, std::move(e));
                        }
                        if (!ret) {
                            p.p = local_p.p;
                            p.type = local_p.type;
                            p.target_type = local_p.target_type;
                            return {};
                        }
                        //else we could not convert member-by-member, try converting void-like members away - but keep the error we have here.
                    } // else if policy does not allow, we attempt to 'convert void-like members away', see below                    
                } 
                //Here we may have 'ret' set if policy allows tuple->list and we attempted member-by-member conversion
                //to the list element type and failed. In this case we attempt to convert void-like members away, e.g.,
                //t2Xli->li fill fail the member-by-member, as 'X' will never convert to 'i'. In that case we try to convert
                //(below) X to i *or nothing*, the latter of which may succeed if 'X' contains a value (which is void) and then
                //we can simply copy the second member of the tuple to the outgoing 'li'. However, if we cannot, because
                //the tuple is, e.g., t2ss (nothing to convert to 'li') we shall emit the error we have generated during the 
                //member-by-member conversion as the user is likely expecting that.
                //Create a copy, where tend is replaced by end_of_incoming.
                //We dont want to ready beyond the incoming tuple's type
                deserialize_convert_params local_p(p, end_of_incoming, p.target_tend);
                if constexpr (otypestr[0] == 't') {
                    //step over the tNUM in p.target_type
                    local_p.target_type++;
                    while (local_p.target_type < local_p.target_tend &&
                           '0' <= *local_p.target_type && *local_p.target_type <= '9') //note p.target_type is null terminated
                        local_p.target_type++;
                    if (auto r = deserialize_convert_from_helper<view>(can_disappear, local_p, o, tt...))
                        return ret ? std::move(ret) : std::move(r);
                } else { // the otypestr[0] == 'l' case arrives here, when the policy does not allow tuple<->list conversions
                    if (auto r = deserialize_convert_from_helper<view>(can_disappear, local_p, std::tie(o), tt...)) //do the backtracking thingy with 1 elemen tuple
                        return ret ? std::move(ret) : std::move(r);
                }
                //Save the result back
                p.p = local_p.p;
                p.type = local_p.type;
                p.target_type = local_p.target_type;
                //If we have not exhausted all of the incoming type, try deserializing
                //the remainder to void
                std::monostate t;
                bool loc_can_disappear;
                while (p.type < end_of_incoming)
                    //if so deserialize then into a void
                    if (auto r = deserialize_convert_from<view>(loc_can_disappear, p, t)) //monostate supports no tags
                        return r;
                if (p.type != end_of_incoming)
                    return create_des_type_error(p);
                can_disappear = false;
                return {};
            }
            case 'b':
            case 'c':
            case 'i':
            case 'I':
            case 'd':
                //T->T is already handled. Now handle numeric conversions
                if constexpr (otypestr[0] == 'b' || otypestr[0] == 'c' ||
                              otypestr[0] == 'i' || otypestr[0] == 'I' ||
                              otypestr[0] == 'd')
                    return deserialize_convert_from_helper<view>(can_disappear, p, o); //handles the case of o==tuple<int>, primitive types ignore tags
                //Here we have {bciId}->non-{bciId}
                //{bciId}->xT/oT already handled
                //{bciId}->a already handled
                //anything else cannot convert
                [[fallthrough]];
            case 'e':
                //e->e is already handled
                //xT->e is already handled
                //e->xT/oT is already handled
                //anything else cannot convert
            case 's':
                //s->lc is already handled
                //s->s is already handled
                //anything else cannot convert
            case 0:
                //X->(void) handled
                //a->(void) handled
                //anything else cannot convert to a void type
                return create_des_type_error(p);
            default:
                return create_des_typestring_source(p, ser_error_str(ser::chr));
            }
        }
    }
}
static_assert(serialize_type_static<false, std::tuple<std::string, std::string, uf::any>>() == make_string("t3ssa"), "KKKKKKKKKKKK");
static_assert(serialize_type_static<true, std::tuple<std::string, std::string, uf::any>>() == make_string("t3ssa"), "KKKKKKKKKKKK");

/** Determines if the type at the beginning of 'type' can be converted
 * to a void with the right value. E.g., 'a' and 'X' can be converted to void
 * if it holds a void. Likewise all combinations, like 'la' t2Xa, can, too.
 * Returns a value (length of type, may be zero if type is empty) if yes, else
 * no value. On a bad typestring it returns false.*/
constexpr std::optional<size_t> ATTR_PURE__ can_disappear(std::string_view type) noexcept
{
    if (type.length() == 0) return 0;
    switch (type.front()) {
    default:
        return {};
    case 'a': 
    case 'X': return 1;
    case 'x':
        if (type.length() == 1) return {};
        [[fallthrough]];
    case 'l':
        if (auto len = can_disappear(type.substr(1)))
            return *len + 1;
        return {};
    case 'o': return {};
    case 't':
        uint32_t size = 0, len = 1;
        type.remove_prefix(1);
        while (type.length() && '0'<= type.front() && type.front()<='9') {
            size = size * 10 + type.front() - '0';
            type.remove_prefix(1);
            len++;
        }
        if (size < 2) return {};
        while (size--)
            if (auto l = can_disappear(type))
                len += *l;
            else
                return {};
        return len;
    }
}

/** @name Type printer and parser functions
 * These functions are able to pretty print serialized values and also parse
 * type descriptions and values.
 *
 * Pretty printing happens by converting all non-ascii chars to %xx hex form.
 * It may happen in two variants. The 'chars' parameter contains additional
 * characters to encode. Use URL_CHARS if you want to we prepare the value to be
 * transmitted as part of a URL, thus we also encode
 * space, tab, quotation mark and ampersand. If empty, we prepare the string for
 * terminal printout, so we do not encode these chars (but we do encode CR and LF).
 * Any values will be preceeeded by a <type>. Errors are error(type,id,msg,value).
 * Optional values are printed as their content or the empty string if none.
 *
 * Parsing of values happens as follows.
 * The string may or may not start with a type enclosed in '<'/'>', so \<s\>"str"
 * is equally good as "str", with the exception that the former translates to an
 * 'any', whereas the latter to a 'string'. This makes no difference at the top level,
 * only as part of a tuple or list. So ("a";\<i\>5) will result in a 2sa type (a tuple of
 * 2 members, which are a string and an any, whereas ("a";5) will result in a 2si.
 * Tuples shall be enclosed between parenthesis and members separated by semicolons.
 * Lists (of same values) shall be enclosed in square brackets, elements separated by semicolons.
 * Maps shall be enclosed in square brackets, elements separated by semicolon. You can use
 * either ':' or '=' to separate key and value, so {"aaa":5;"bbb"=67} is OK.
 * Strings shall be enclosed in-between quotation marks. Quoatation marks shall be written as
 * %22. In general %xx characters are understood. If % is followed not by two hex digits,
 * it is parsed verbatim, so "%5" will be two characters: '%' and '5', so will "%5g" be three,
 * whereas "%55" will be one the capital 'U'.
 * Characters must be enclosed in single quotes and must contain one ascii char.
 * The %xx notation is valid, though, so '%55' is a valid char equal to 'U'.
 * Integers shall simply be numbers (with an optional sign). If they dont fit to 32 bits,
 * they will generate a 64-bit integer. There is no current way to specify a 64-bit integer of
 * small value.
 * Floating point numbers shall begin with a digit and must contain a dot. They may end in a dot
 * singifying a floating point number of integer value. (So 5. is float, 5 is integer type.)
 * Exponents cannot be specified so 1e3 is invalid.
 * Bools can be 'true' or 'false' (case insensitive)
 * Optionals cannot be entered.
 * Examples:
 * (5;5.3) -> 2id  (tuple of int and double)
 * [5;4;29;33] -> li (list of integers)
 * [5;4;29.33] -> invalid (list of elements of different types)
 * ('%00';["john";"ada"]) -> 2cls
 * {"john":23;"ada":18} -> msi == a map<string,int>
 * <li>[5,4,3] -> 'a' containing an li. You can omit the type
 * <>[5,4,3] -> 'a' containing an li
 * [5,4,3] -> 'li'
 * error("type","id","message",<s>"additional uf::any value") -> e
 *
 * @{ */


/** Append an ascii version of a string (encoding the binary chars
 * as %xx).
 * @param [out] to The string to append to.
 * @param [in] max_len A maximum length. Characters beyond are trimmed. Zero is unlimited.
 * @param [in] s The string to escape
 * @param [in] chars Specify a list of characters to also encode as %xx.
 *                   Urls require, the space, quotation mark and the ampersand
 *                   These are in the macro URL_CHARS.
 * @param [in] escape_char What escape character to use.
 * @returns true if the max_limit is reached and the string is trimmed.*/
#define URL_CHARS " \"&%"
inline bool print_escaped_to(std::string &to, unsigned max_len, std::string_view s,
                             std::string_view chars, char escape_char)
{
    for (char c : s) {
        if ((unsigned char)c<' ' || (unsigned char)c>=127 || c==escape_char ||
            chars.find_first_of(c)!=std::string_view::npos) {
            to.push_back(escape_char);
            if (((unsigned char)c)/16>=10) to.push_back('a'+((unsigned char)c)/16-10);
            else to.push_back('0'+((unsigned char)c)/16);
            if (((unsigned char)c)%16>=10) to.push_back('a'+((unsigned char)c)%16-10);
            else to.push_back('0'+((unsigned char)c)%16);
        } else
            to.push_back(c);
        if (max_len && to.length()>max_len)
            return true;
    }
    return false;
}

inline std::string print_escaped_json_string(std::string_view s)
{
    std::string tmp;
    tmp.reserve(s.length() * 2);
    //JSON escapes https://www.freeformatter.com/json-escape.html
    for (char c : s)
        switch (c) {
        case 0x8: tmp.append("\\b"); break;
        case 0x9: tmp.append("\\t"); break;
        case 0xa: tmp.append("\\n"); break;
        case 0xc: tmp.append("\\f"); break; //form feed
        case 0xd: tmp.append("\\r"); break;
        case '\\': tmp.append("\\\\"); break;
        default: tmp.push_back(c);
        }
    return tmp;
}

template <typename E, typename ...tags>
typename std::enable_if_t<std::is_enum<E>::value, bool> 
serialize_print_append(std::string &to, bool json_like, unsigned max_len, const E &e, std::string_view chars, char escape_char, tags... tt);
template <typename C, typename ...tags>
typename std::enable_if_t<is_serializable_container<C>::value && !is_map_container<C>::value && !has_tuple_for_serialization<false, C, tags...>::value, bool>
serialize_print_append(std::string &to, bool json_like, unsigned max_len, const C &e, std::string_view chars, char escape_char, tags... tt);
template <typename M, typename ...tags>
typename std::enable_if_t<is_map_container<M>::value && !has_tuple_for_serialization<false, M, tags...>::value, bool> serialize_print_append(std::string &to, bool json_like, unsigned max_len, const M &e, std::string_view chars, char escape_char, tags... tt);
template <typename S, typename ...tags>
typename std::enable_if_t<has_tuple_for_serialization<false, S, tags...>::value, bool> 
serialize_print_append(std::string &to, bool json_like, unsigned max_len, const S &, std::string_view chars, char escape_char, tags... tt);
template <typename S, typename ...tags>
typename std::enable_if_t<is_really_auto_serializable_v<S> && !has_tuple_for_serialization<false, S, tags...>::value, bool>
serialize_print_append(std::string &to, bool json_like, unsigned max_len, const S &, std::string_view chars, char escape_char, tags... tt);
template <typename A, typename B, typename ...tags> bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const std::pair<A, B> &p, std::string_view chars, char escape_char, tags... tt);
template <typename T, typename ...TT, typename ...tags> bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const std::tuple<T, TT...> &t, std::string_view chars, char escape_char, tags... tt);
template <typename T, typename ...tags> bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const std::tuple<T> &t, std::string_view chars, char escape_char, tags... tt);
template <typename T, size_t L, typename ...tags> bool serialize_print_append(std::string& to, bool json_like, unsigned max_len, const std::array<T, L>& o, std::string_view chars, char escape_char, tags... tt);
template <typename T, size_t L, typename ...tags> bool serialize_print_append(std::string& to, bool json_like, unsigned max_len, const T(&o)[L], std::string_view chars, char escape_char, tags... tt);
template <typename T, typename ...tags> bool serialize_print_append(std::string& to, bool json_like, unsigned max_len, const std::unique_ptr<T>& pp, std::string_view chars, char escape_char, tags... tt);
template <typename T, typename ...tags> bool serialize_print_append(std::string& to, bool json_like, unsigned max_len, const std::shared_ptr<T>& pp, std::string_view chars, char escape_char, tags... tt);
template <typename T, typename ...tags> bool serialize_print_append(std::string& to, bool json_like, unsigned max_len, const T* const& pp, std::string_view chars, char escape_char, tags... tt);
template <typename T, typename ...tags> bool serialize_print_append(std::string& to, bool json_like, unsigned max_len, const std::optional<T>& o, std::string_view chars, char escape_char, tags... tt);

template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool json_like, unsigned /*max_len*/, const bool &b, std::string_view /*chars*/, char /*escape_char*/, tags...)
{ to.append(json_like ? (b ? "true" : "false") : (b ? "True" : "False")); return false;}
template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const unsigned char &c, std::string_view chars, char escape_char, tags... tt)
{ to.push_back(json_like ? '\"' : '\''); print_escaped_to(to, max_len, std::string_view((const char*)&c,1), chars, escape_char); to.push_back(json_like ? '\"' : '\''); return false;}
template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const signed char &c, std::string_view chars, char escape_char, tags... tt)
{ to.push_back(json_like ? '\"' : '\''); print_escaped_to(to, max_len, std::string_view((const char*)&c,1), chars, escape_char); to.push_back(json_like ? '\"' : '\''); return false;}
template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const char &c, std::string_view chars, char escape_char, tags... tt)
{ to.push_back(json_like ? '\"' : '\''); print_escaped_to(to, max_len, std::string_view(&c,1), chars, escape_char); to.push_back(json_like ? '\"' : '\''); return false;}
template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool /*json_like*/, unsigned /*max_len*/, const uint16_t &i, std::string_view /*chars*/, char /*escape_char*/, tags...) { to.append(std::to_string(unsigned(i))); return false; }
template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool /*json_like*/, unsigned /*max_len*/, const int16_t &i, std::string_view /*chars*/, char /*escape_char*/, tags...) { to.append(std::to_string(int(i))); return false; }
template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool /*json_like*/, unsigned /*max_len*/, const uint32_t &i, std::string_view /*chars*/, char /*escape_char*/, tags...) { to.append(std::to_string(i)); return false; }
template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool /*json_like*/, unsigned /*max_len*/, const int32_t &i, std::string_view /*chars*/, char /*escape_char*/, tags...) { to.append(std::to_string(i)); return false; }
template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool /*json_like*/, unsigned /*max_len*/, const uint64_t &i, std::string_view /*chars*/, char /*escape_char*/, tags...) { to.append(std::to_string(i)); return false; }
template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool /*json_like*/, unsigned /*max_len*/, const int64_t &i, std::string_view /*chars*/, char /*escape_char*/, tags...) { to.append(std::to_string(i)); return false; }
template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool /*json_like*/, unsigned /*max_len*/, const float &d, std::string_view /*chars*/, char /*escape_char*/, tags...) { to.append(print_double(d)); return false; }
template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool /*json_like*/, unsigned /*max_len*/, const double &d, std::string_view /*chars*/, char /*escape_char*/, tags...) { to.append(print_double(d)); return false; }
template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool /*json_like*/, unsigned /*max_len*/, const long double &d, std::string_view /*chars*/, char /*escape_char*/, tags...) { to.append(print_double(d)); return false; }
template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const std::string_view &s, std::string_view chars, char escape_char, tags...)
{ to.reserve(to.length()+s.length()+2); to.push_back('\"'); 
if (json_like) {if (print_escaped_to(to, max_len, s, chars, escape_char)) return true; } 
else if (print_escaped_to(to, max_len, print_escaped_json_string(s), chars, escape_char)) return true; 
to.push_back('\"'); return false;}
template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const std::string &s, std::string_view chars, char escape_char, tags...)
{ to.reserve(to.length()+s.length()+2); to.push_back('\"'); 
if (json_like) {if (print_escaped_to(to, max_len, s, chars, escape_char)) return true; } 
else if (print_escaped_to(to, max_len, print_escaped_json_string(s), chars, escape_char)) return true; 
to.push_back('\"'); return false; }
template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const any_view &a, std::string_view chars, char escape_char, tags...) {
    std::string_view ty; 
    auto ret = a.print_to(to, ty, max_len, chars, escape_char, json_like);
    if (!ret) return false; //OK
    if (*ret) (*ret)->throw_me(); //Some error
    return true; //Just too long
}
template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const any &a, std::string_view chars, char escape_char, tags...)
{ return serialize_print_append(to, json_like, max_len, static_cast<const any_view &>(a), chars, escape_char); }
template <typename ...tags>
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const error_value &err, std::string_view chars, char escape_char, tags...)
{
    if (json_like) {
        std::string tmp;
        if (serialize_print_append(tmp, false, 0, err, {}, escape_char))
            return true;
        return serialize_print_append(to, true, max_len, tmp, chars, escape_char); 
    } else {
        to.append("err");
        auto t = err.tuple_for_serialization();
        return serialize_print_append(to, json_like, max_len, t, chars, escape_char);
    }
}
template <typename T, typename ...tags>
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const uf::expected<T> &x, std::string_view chars, char escape_char, tags... tt)
{ if (!x.has_value()) return serialize_print_append(to, json_like, max_len, x.error(), chars, escape_char);
  if constexpr (!is_void_like<false, T>::value) return serialize_print_append(to, json_like, max_len, *x, chars, escape_char, tt...);
  else return false; } //for expected<void> with a value we print exactly nothing.

template <typename E, typename ...tags> //enum
inline typename std::enable_if_t<std::is_enum<E>::value, bool>
serialize_print_append(std::string &to, bool json_like, unsigned max_len, const E &e, std::string_view /*chars*/, char /*escape_char*/, tags...)
{ if (!json_like) to.append("Enum("); to.append(std::to_string(size_t(e))); if (!json_like) to.push_back(')'); return max_len && to.length()>max_len;}
template <typename C, typename ...tags> //containers/ranges with begin() end()
inline typename std::enable_if_t<is_serializable_container<C>::value && !is_map_container<C>::value && !has_tuple_for_serialization<false, C, tags...>::value, bool>
serialize_print_append(std::string &to, bool json_like, unsigned max_len, const C &c, std::string_view chars, char escape_char, tags... tt) {
    to.push_back('[');
    for (auto &e : c) {
        if (serialize_print_append(to, json_like, max_len, e, chars, escape_char, tt...))
            return true;
        to.push_back(',');
    }
    if (c.size())
        to.pop_back();
    to.push_back(']');
    return max_len && to.length()>max_len;
}
template <typename M, typename ...tags>
typename std::enable_if<is_map_container<M>::value && !has_tuple_for_serialization<false, M, tags...>::value, bool>::type
serialize_print_append(std::string &to, bool json_like, unsigned max_len, const M &m, std::string_view chars, char escape_char, tags... tt) {
    to.push_back('{');
    for (auto &e : m) {
        if (serialize_print_append(to, json_like, max_len, e.first, chars, escape_char, tt...))
            return true;
        to.push_back(':');
        if (serialize_print_append(to, json_like, max_len, e.second, chars, escape_char, tt...))
            return true;
        to.push_back(',');
    }
    if (m.size())
        to.pop_back();
    to.push_back('}');
    return max_len && to.length()>max_len;
}
template <typename S, typename ...tags>
inline typename std::enable_if<has_tuple_for_serialization<false, S, tags...>::value, bool>::type
serialize_print_append(std::string &to, bool json_like, unsigned max_len, const S &s, std::string_view chars, char escape_char, tags... tt)
{ return serialize_print_append(to, json_like, max_len, invoke_tuple_for_serialization(s, tt...), chars, escape_char, tt...); }
#ifdef HAVE_BOOST_PFR
template <typename S, typename ...tags>
inline typename std::enable_if<is_really_auto_serializable_v<S> && !has_tuple_for_serialization<false, S, tags...>::value, bool>::type
serialize_print_append(std::string &to, bool json_like, unsigned max_len, const S &s, std::string_view chars, char escape_char, tags... tt) 
{ return serialize_print_append(to, json_like, max_len, boost::pfr::structure_tie(s), chars, escape_char, tt...); }
#endif
template <typename A, typename B, typename ...tags> //pair
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const std::pair<A, B> &p, std::string_view chars, char escape_char, tags... tt)
{
    to.push_back(json_like ? '[' : '(');
    if (serialize_print_append(to, json_like, max_len, p.first, chars, escape_char, tt...))
        return true;
    to.push_back(',');
    if (serialize_print_append(to, json_like, max_len, p.second, chars, escape_char, tt...))
        return true;
    to.push_back(json_like ? ']' : ')');
    return max_len && to.length()>max_len;
}
template <typename T, typename ...tags> //tuples
inline bool serialize_print_append_sub(std::string &to, bool json_like, unsigned max_len, const std::tuple<T> &t, std::string_view chars, char escape_char, tags... tt)
{ return serialize_print_append(to, json_like, max_len, std::get<0>(t), chars, escape_char, tt...); }
template <typename T, typename ...TT, typename ...tags> //tuples
inline bool serialize_print_append_sub(std::string &to, bool json_like, unsigned max_len, const std::tuple<T, TT...> &t, std::string_view chars, char escape_char, tags... tt){
    if (serialize_print_append(to, json_like, max_len, std::get<0>(t), chars, escape_char, tt...)) return true; 
    to.push_back(','); return serialize_print_append_sub(to, json_like, max_len, tuple_tail(t), chars, escape_char, tt...); }
template <typename T, typename ...TT, typename ...tags> //tuples
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const std::tuple<T, TT...> &t, std::string_view chars, char escape_char, tags... tt)
{
    to.push_back(json_like ? '[' : '(');
    if (serialize_print_append_sub(to, json_like, max_len, t, chars, escape_char, tt...))
        return true;
    to.push_back(json_like ? ']' : ')');
    return max_len && to.length()>max_len;
}
template <typename T, typename ...tags> //tuples
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const std::tuple<T> &t, std::string_view chars, char escape_char, tags... tt)
{
    if (serialize_print_append(to, json_like, max_len, std::get<0>(t), chars, escape_char, tt...))
        return true;
    return max_len && to.length()>max_len;
}

template <typename T, size_t L, typename ...tags>
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const std::array<T, L> &o, std::string_view chars, char escape_char, tags... tt)
{
    if constexpr (L>1) to.push_back(json_like ? '[' : '(');
    for (auto &e:o)
        if (serialize_print_append(to, json_like, max_len, e, chars, escape_char, tt...))
            return true;
        else if constexpr (L>1)
            to.push_back(',');
    if constexpr (L>1) to.back() = json_like ? ']' : ')';
    return max_len && to.length() > max_len;
}
template <typename T, size_t L, typename ...tags>
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const T(&o)[L], std::string_view chars, char escape_char, tags... tt)
{
    if constexpr (L>1) to.push_back(json_like ? '[' : '(');
    for (auto &e : o)
        if (serialize_print_append(to, json_like, max_len, e, chars, escape_char, tt...))
            return true;
        else if constexpr (L>1) 
            to.push_back(',');
    if constexpr (L>1) to.back() = json_like ? ']' : ')';
    return max_len && to.length() > max_len;
}
template <typename T, typename ...tags>
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const std::unique_ptr<T> &pp, std::string_view chars, char escape_char, tags... tt)
{ if (pp) return serialize_print_append(to, json_like, max_len, *pp, chars, escape_char, tt...);
  if (json_like) to.append("null"); 
  return false;}
template <typename T, typename ...tags>
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const std::shared_ptr<T> &pp, std::string_view chars, char escape_char, tags... tt)
{ if (pp) return serialize_print_append(to, json_like, max_len, *pp, chars, escape_char, tt...);
  if (json_like) to.append("null"); 
  return false;}
template <typename T, typename ...tags>
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const T* const& pp, std::string_view chars, char escape_char, tags... tt)
{ if (pp) return serialize_print_append(to, json_like, max_len, *pp, chars, escape_char, tt...);
  if (json_like) to.append("null"); 
  return false;}
template <typename T, typename ...tags>
inline bool serialize_print_append(std::string &to, bool json_like, unsigned max_len, const std::optional<T> &o, std::string_view chars, char escape_char, tags... tt)
{ if (o) return serialize_print_append(to, json_like, max_len, *o, chars, escape_char, tt...);
  if (json_like) to.append("null"); 
  return false;}


/** Deserializes a type given its textual type description into an ascii printable text.
 * @param [out] to The string to which we append our output.
 * @param [in] json_like Tf true, we attempt to be as json compatible as possible.
 *             - we dont print the type for uf::any
 *             - characters are printed as a single character string
 *             - void values and empty optionals are printed as 'null'
 *             - strings will contain backslash escaped backspace, tab, cr, lf, ff, quotation mark and backslash (in addition to 'chars')
 *             - errors are printed as string
 *             - tuples are printed as arrays.
 * @param [in] max_len The maximum length, after this the output is trimmed. Zero is unlimited.
 * @param type The string view containing the type of the serialized value.
 *             As we progress, we consume chars from this string view via
 *             remove_prefix()
 * @param p A pointer reference to the raw value to deserialize from. As we
 *          progress, we move it forward.
 * @param [in] end The character beyond the last character of the raw serialized value.
 *                 The function will not read this character or beyond. If serialization
 *                 would need more characters, a uf::value_mismatch_error is thrown.
 * @param [in] chars Specify a list of characters to also encode as %xx.
 *                   Urls require, the space, quotation mark and the ampersand
 *                   These are in the macro URL_CHARS.
 * @param [in] escape_char What escape character to use.
 * @param [in] expected_handler This function is called before printing an expected value (and is recursively passed down).
 *                              When it returns true, we consider the expected printed, else we print it from the serialized data.
 *                              It can be used to print something for errors that are missing from serialized data
 * @returns an empty optional on success; an optional with no error if we have exceeeded the max length and need to be trimmed; or
 *          an optional with some error. The exceptions in the errors will contain the typestring suffix remaining after the error. 
 *          This has to be adjusted in callers*/
[[nodiscard]] std::optional<std::unique_ptr<value_error>> 
serialize_print_by_type_to(std::string &to, bool json_like, unsigned max_len, std::string_view &type,
                           const char *&p, const char *end, std::string_view chars, char escape_char,
                           std::function<bool(std::string &, unsigned, std::string_view &,
                                              const char *&, const char *, std::string_view, char)> expected_handler =
                           [](std::string &, unsigned, std::string_view &,
                              const char *&, const char *, std::string_view, char)->bool {return false; });
/** @} */

/** @name Serialization functions
* @{ */


/** Returns numerical value for a hex character 0..9, a..f, A..F
 * or -1 if otherwise.*/
inline int hex_digit(unsigned char c) noexcept
{
    if (c<'0') return -1;
    if (c<='9') return c-'0';
    if (c<'A') return -1;
    if (c<='F') return 10+c-'A';
    if (c<'a') return -1;
    if (c<='f') return 10+c-'a';
    return -1;
}

/** Parses an ascii string, replacing %xx (x is a hex digit) to their
 * character value. It does the opposite as print_escaped_to().
 * We silently copy appearance of % followed by not a hex num.
 * @param [out] to The string to append our result to.
 * @param [in] value the encoded ascii string.
 * @param [in] escape_char What was the escape char when printed.*/
inline void parse_escaped_string_to(std::string &to, std::string_view value, char escape_char = '%')
{
    while (value.length())
        if (value.front()==escape_char && value.length()>2 && hex_digit(value[1])>=0 && hex_digit(value[2])>=0) {
            to.push_back(hex_digit(value[1])*16 + hex_digit(value[2]));
            value.remove_prefix(3);
        } else {
            to.push_back(value.front());
            value.remove_prefix(1);
        }
}

inline void skip_whitespace(std::string_view &value)
{
    if (auto p = value.find_first_not_of(" \t\n\r"); p==std::string_view::npos)
        value = {value.data(), 0}; //all whitespace or already empty. Keep head pointer though.
    else
        value.remove_prefix(p);
}

/** Creates a serialized raw bytestring from a textual description, such as
 * (\"string\";5;[23.2;54.32];'c';\<2si\>(\"xx\";123)).
 * @param [out] to The string we append our raw output to.
 * @param value The textual description to parse. We consume characters
 *              from this view as we progress.
 * @param liberal If true, then heterogeneous lists (and maps) will be converted to any values.
 * @returns the type of the parsed string and false or an error string and true.
 *          We dont throw (uf::error derived exceptions) in this function, but return an error instead.*/
std::pair<std::string, bool> parse_value(std::string &to, std::string_view &value, bool liberal);
/** @} */


} //ns impl

inline any::any(from_text_t, std::string_view v)
{
    if (v.empty()) return;
    std::string_view original = v;
    bool invalid;
    std::string t;
    std::tie(t, invalid) = impl::parse_value(_storage, v);
    if (invalid) 
        throw value_mismatch_error(uf::concat("Error parsing text: '", original.substr(0, v.data() - original.data()),
                                              '*', v, "': ", t)); //t is an error string if invalid
    _storage.insert(0, t);
    _storage.shrink_to_fit();
    _set_type_value(t.length());
}

namespace impl
{

struct parse_any_content_result_element {
    std::string_view type;         ///<The type string of a content element
    std::string_view value;        ///<The serialized value of a content element
    std::string_view type_length;  ///<The location of the length of the typestring (empty if typestring is non modifiable) (set only for any)
    std::string_view value_length; ///<The location of the length of the value (set only for any)
    parse_any_content_result_element(std::string_view t, std::string_view v, std::string_view lt = {}, std::string_view lv = {}) :
        type(t), value(v), type_length(lt), value_length(lv) {}
};

struct parse_any_content_result
{
    char typechar;               //always the first char of the type. Zero for void
    std::string_view inner_type1;//for 'lxo' The inner type. Empty for 'X'. For 'm' the key type
    std::string_view inner_type2;//for 'm' the value type
    std::string_view size;       //serialized uint32_t in value for map and list, ascii number in type for tuple, 1 byte in value for x and o, empty for the rest
    std::vector<parse_any_content_result_element> elements; //the elements
};

/** Parses a type-value pair into their constitutent types.
 * - lists and tuples result in an array of as many elements as they have
 * - maps have twice as many: keys and values interleaved (0:key0, 1:value0, 2:key1, 3: value1, ...
 * - any will return its content as a single element.
 * - expected wil return one element, either a value of its type or an error.
 * - optional will return zero or one element of its type.
 * - errors will return 3 strings and one any.
 * - primitive types (i,I,d,c,b,s) and voids will return zero elements.*/
std::variant<parse_any_content_result, std::unique_ptr<value_error>>
parse_any_content(std::string_view _type, std::string_view _value,
                  uint32_t max_no = std::numeric_limits<uint32_t>::max());


} //ns impl

/** @addtogroup serialization
 * @{ */

/** Helper to test your struct that it is both serializable from and deserializable to
 * and with the same typestring. In case of problems you get some explanation. 
 * When checking for deserialization we allow only owning objects. Use it like
 * @code
 *  struct mystruct {
 *      ....
 *      auto tuple_for_serialization() const noexcept {return std::tie(...); }
 *      auto tuple_for_serialization()       noexcept {return std::tie(...); }
 *  };
 *  static_assert(is_ser_deser_ok_v<mystruct>);
 * @endcode
 * This helper will always return true, thus the static_assert above always passes.
 * However, other static asserts will trigger detailing where the problem is (if there is one).*/
template <typename T, typename ...tags>
inline constexpr bool is_ser_deser_ok_v = uf::impl::is_ser_deser_ok_f<T, false, true, tags...>();

/** Helper to test your struct that it is both serializable from and deserializable to
 * and with the same typestring. In case of problems you get some explanation.
 * When checking for deserialization we allow view-like objects. Use it like
 * @code
 *  struct mystruct {
 *      ....
 *      auto tuple_for_serialization() const noexcept {return std::tie(...); }
 *      auto tuple_for_serialization()       noexcept {return std::tie(...); }
 *  };
 *  static_assert(is_ser_deser_view_ok_v<mystruct>);
 * @endcode
 * This helper will always return true, thus the static_assert above always passes.
 * However, other static asserts will trigger detailing where the problem is (if there is one).*/
template <typename T, typename ...tags>
inline constexpr bool is_ser_deser_view_ok_v = uf::impl::is_ser_deser_ok_f<T, true, true, tags...>();

/** Helper to test your struct that it is serializable.
 * In case of problems you get some explanation. Use it like
 * @code
 *  struct mystruct {
 *      ....
 *      auto tuple_for_serialization() const noexcept {return std::tie(...); }
 *  };
 *  static_assert(is_ser_ok_v<mystruct>);
 * @endcode
 * This helper will always return true, thus the static_assert above always passes.
 * However, other static asserts will trigger detailing where the problem is (if there is one).*/
template <typename T, typename ...tags>
inline constexpr bool is_ser_ok_v = uf::impl::is_serializable_f<T, true, tags...>() || true;

/** Helper to test your struct that it is deserializable.
 * In case of problems you get some explanation. Use it like
 * @code
 *  struct mystruct {
 *      ....
 *      auto tuple_for_serialization() noexcept {return std::tie(...); }
 *  };
 *  static_assert(is_deser_ok_v<mystruct>);
 * @endcode
 * This helper will always return true, thus the static_assert above always passes.
 * However, other static asserts will trigger detailing where the problem is (if there is one).*/
template <typename T, typename ...tags>
inline constexpr bool is_deser_ok_v = uf::impl::is_deserializable_f<T, false, true, tags...>() || true;

/** Helper to test your struct that it is deserializable.
 * In case of problems you get some explanation. Use it like
 * @code
 *  struct mystruct {
 *      ....
 *      auto tuple_for_serialization() noexcept {return std::tie(...); }
 *  };
 *  static_assert(is_deser_ok_v<mystruct>);
 * @endcode
 * This helper will always return true, thus the static_assert above always passes.
 * However, other static asserts will trigger detailing where the problem is (if there is one).*/
template <typename T, typename ...tags>
inline constexpr bool is_deser_view_ok_v = uf::impl::is_deserializable_f<T, true, true, tags...>() || true;

/** Serialize a C++ variable of arbitrary type to a bytearray */
template <typename T, typename ...tags>
inline std::string serialize(const T &t, uf::use_tags_t = {}, tags... tt) {
    static_assert(uf::impl::is_serializable_f<T, true, tags...>(), "Type must be serializable.");
    if constexpr (uf::impl::is_serializable_f<T, false, tags...>()) {
        using type = typename std::remove_cvref_t<T>;
        if constexpr (impl::has_before_serialization_inside_v<type, tags...>)
            if (auto r = impl::call_before_serialization(&t, tt...); r.obj)
                impl::call_after_serialization(&t, r, tt...); //This shall throw
        try {
            std::string ret(impl::serialize_len(t, tt...), char(0));
            char *p = &ret.front();
            impl::serialize_to(t, p, tt...);
            if constexpr (impl::has_after_serialization_inside_v<type, tags...>)
                impl::call_after_serialization(&t, true, tt...);
            return ret;
        } catch (...) {
            if constexpr (impl::has_after_serialization_inside_v<type, tags...>)
                impl::call_after_serialization(&t, false, tt...);
            throw;
        }
    } else
        return {};
}


/** Serialize a C++ variable of arbitrary type to a user-allocated area.
 * This function handes calling before/after serialization.
 * On any exception, if the memory has been allocated, it will remain allocated.
 * @param [in] t The variable to serlialize
 * @param [in] alloc A char*(size_t) function taking the length and returning a char
 *                    pointer where the serialized data has to be placed.
 *                    If the serializable object is of zero length, this function is
 *                    still called, but what is returned is simply passed on as return value
 *                    of uf::serialize().
 *                    If a nullptr is returned, no serialization occurs, but after_serialization(false) 
 *                    is called and nullptr and the required length is returned.
 * @param [in] tt You can specify additional data, which will be used to select which
 *                helper function (e.g., tuple_for_serialization) to apply.
 * @returns the allocated memory and its length. */
template <typename T, typename Alloc, typename ...tags>
inline std::pair<char*, size_t> serialize(Alloc alloc, const T &t, uf::use_tags_t = {}, tags... tt)
{
    static_assert(uf::impl::is_serializable_f<T, true, tags...>(), "Type must be serializable.");
    static_assert(std::is_invocable_r<char *, Alloc, size_t>::value, "Alloc must be of signature 'char* Alloc(size_t)'.");
    if constexpr (uf::impl::is_serializable_f<T, false, tags...>() && std::is_invocable_r<char *, Alloc, size_t>::value) {
        using type = typename std::remove_cvref_t<T>;
        if constexpr (impl::has_before_serialization_inside_v<type, tags...>)
            if (auto r = impl::call_before_serialization(&t, tt...); r.obj)
                impl::call_after_serialization(&t, r, tt...); //This shall throw
        try {
            std::pair<char *, size_t> ret;
            char *p = ret.first = alloc(ret.second = impl::serialize_len(t, tt...));
            if (p)
                impl::serialize_to(t, p, tt...);
            if constexpr (impl::has_after_serialization_inside_v<type, tags...>)
                impl::call_after_serialization(&t, bool(p), tt...);
            return ret;
        } catch (...) {
            if constexpr (impl::has_after_serialization_inside_v<type, tags...>)
                impl::call_after_serialization(&t, false, tt...);
            throw;
        }
    } else
        return {};
}

/** Deserialize from a bytearray to a type. Type cannot contain const or *_view elements.
 * @param [in] s The raw data to deserialize from.
 * @param [out] t The placeholder to deserialize into.
 * @param [in] allow_longer_data If false, then we throw a value_mismatch_error if the
 *                                deserialized data (according to the type of 't') is
 *                                shorter than what is in 's'. Setting this to true
 *                                enables you to deserialize only the beginning.
 *                                Use like: uf::deserialize<false>(s, t);
 * @param [in] tt You can specify additional data, which will be used to select which
 *                helper function (e.g., tuple_for_serialization) to apply.
 * @returns the remaining data as string_view. If !allow_longer data, it is empty.*/
template <typename T=int, typename ...tags>
inline std::string_view deserialize(std::string_view s, T &&t, bool allow_longer_data = false, 
                                    uf::use_tags_t = {}, tags... tt) {
    static_assert(uf::impl::is_deserializable_f<T, false, true, tags...>(), "Type must be possible to deserialize into.");
    const char *p = s.data();
    if (impl::deserialize_from<false>(p, s.data()+s.length(), t, tt...)) 
        throw value_mismatch_error(uf::concat(impl::ser_error_str(impl::ser::val), " (deser) <%1>."), deserialize_type<T, tags...>(), 0);
    if (!allow_longer_data && p!=s.data()+s.length())
        throw value_mismatch_error(uf::concat(s.data() + s.length()-p, " bytes left after deserializing ", p-s.data(), " bytes to <%1>"),
                                   deserialize_type<T, tags...>(), 0);
    return { p, s.data() + s.length() >= p ? size_t(s.data() + s.length() - p) : 0 };
}
/** Deserialize from a bytearray to a type.
 * Type cannot contain const or *_view elements and must be default constructible.
 * This version should be invoked with the type specified explicityly, like
 * deserialize<int>(s);
 * @param [in] s The raw data to deserialize from.
 * @param [in] allow_longer_data If false, then we throw a value_mismatch_error if the
 *                                deserialized data (according to the type of 't') is
 *                                shorter than what is in 's'. Setting this to true
 *                                enables you to deserialize only the beginning.
 *                                Use like T a = uf::deserialize<T, false>(s);
 * @param [in] tt You can specify additional data, which will be used to select which
 *                helper function (e.g., tuple_for_serialization) to apply.
 * @returns The placeholder to deserialize into.*/
template <typename T, typename ...tags>
inline T deserialize_as(std::string_view s, bool allow_longer_data = false, 
                        uf::use_tags_t = {}, tags...tt) {
    static_assert(std::is_default_constructible_v<T>, "Result must be default constructible.");
    T t;  
    deserialize(s, t, allow_longer_data, uf::use_tags, tt...);
    return t; 
}

/** Deserialize from a bytearray with a known type to a C++ variable with potential conversions.
 * Type cannot contain const or *_view elements.
 * @param [in] s The raw data to deserialize from.
 * @param [in] from_type The typestring of the data in 's'.
 * @param [out] v The placeholder to deserialize into.
 * @param [in] convpolicy The conversion policy to apply. You can enable/disable certain conversions here.
 * @param [in] allow_longer_data If false, then we throw a value_mismatch_error if the
 *                                deserialized data (according to the type of 't') is
 *                                shorter than what is in 's'. Setting this to true
 *                                enables you to deserialize only the beginning.
 *                                Use like: uf::deserialize_convert<false>(s, type, t);
 * @param [in] tt You can specify additional data, which will be used to select which
 *                helper function (e.g., tuple_for_serialization) to apply.
 * @returns the remaining data as string_view.If !allow_longer data, it is empty.*/
template <typename T = int, typename ...tags>
inline std::string_view  deserialize_convert(std::string_view s, std::string_view from_type, T &v,
                                             serpolicy convpolicy = uf::allow_converting_all,
                                             bool allow_longer_data = false, 
                                             uf::use_tags_t = {}, tags... tt)
{
    static_assert(uf::impl::is_deserializable_f<T, false, true, tags...>(), "Type must be possible to deserialize into.");
    std::vector<error_value> errors;
    std::vector<std::pair<size_t, size_t>> error_pos;
    impl::deserialize_convert_params p(s, from_type, &v, convpolicy, nullptr,
                                       &errors, &error_pos, tt...);
    bool can_disappear;
    if (auto err = impl::deserialize_convert_from<false>(can_disappear, p, v, tt...))
        err->throw_me();
    if (!allow_longer_data && p.p != s.data() + s.length())
        throw value_mismatch_error("Bytes left after deserializing <%1>", from_type);
    if (errors.size())
        throw uf::expected_with_error("In uf::deserialize_convert <%1> -> <%2> cannot place errors in expected values. Errors: %e",
                                      from_type, deserialize_type<T, tags...>, std::move(errors), std::move(error_pos));
    return { p.p, p.end>=p.p ? size_t(p.end - p.p) : 0 };
}

/** Deserialize from a bytearray with a known type to a C++ variable  with potential conversions.
 * Type cannot contain const or *_view elements.
 * This overload is provided, so temporary reference tuples can work, like:
 * deserialize(s, std::tie(a,b));
 * @param [in] s The raw data to deserialize from.
 * @param [in] from_type The typestring of the data in 's'.
 * @param [out] v The placeholder to deserialize into.
 * @param [in] convpolicy The conversion policy to apply. You can enable/disable certain conversions here.
 * @param [in] allow_longer_data If false, then we throw a value_mismatch_error if the
 *                                deserialized data (according to the type of 't') is
 *                                shorter than what is in 's'. Setting this to true
 *                                enables you to deserialize only the beginning.
 *                                Use like: uf::deserialize_convert<false>(s, type, t);
 * @param [in] tt You can specify additional data, which will be used to select which
 *                helper function (e.g., tuple_for_serialization) to apply.
 * @returns the remaining data as string_view.If !allow_longer data, it is empty.*/
template <typename T = int, typename ...tags>
inline std::string_view deserialize_convert(std::string_view s, std::string_view from_type, T &&v,
                                            serpolicy convpolicy = uf::allow_converting_all,
                                            bool allow_longer_data = false, 
                                            uf::use_tags_t = {}, tags... tt) {
    return deserialize(s, from_type, v, convpolicy, allow_longer_data, uf::use_tags, tt...);
}

/** Deserialize from a bytearray with a known type to a C++ variable  with potential conversions.
 * Type cannot contain const or *_view elements.
 * This version should be invoked with the type specified explicityly, like
 * deserialize_convert_as<long long int>(s);
 * @param [in] s The raw data to deserialize from.
 * @param [in] from_type The typestring of the data in 's'.
 * @param [in] convpolicy The conversion policy to apply. You can enable/disable certain conversions here.
 * @param [in] allow_longer_data If false, then we throw a value_mismatch_error if the
 *                               deserialized data (according to the type of 't') is
 *                               shorter than what is in 's'. Setting this to true
 *                               enables you to deserialize only the beginning.
 * @param [in] tt You can specify additional data, which will be used to select which
 *                helper function (e.g., tuple_for_serialization) to apply. */
template <typename T, typename ...tags>
inline T deserialize_convert_as(std::string_view s, std::string_view from_type,
                                serpolicy convpolicy = uf::allow_converting_all,
                                bool allow_longer_data = false, 
                                uf::use_tags_t = {}, tags... tt) {
    static_assert(std::is_default_constructible_v<T>, "Result must be default constructible.");
    T v;
    deserialize_convert(s, from_type, v, convpolicy, allow_longer_data, uf::use_tags, tt...); 
    return v; 
}


/** Deserialize from a bytearray to a type.
 * Type can contain string_views and any_views that will point back to 's'/'a', resp.
 * @param [in] s The raw data to deserialize from.
 * @param [out] t The placeholder to deserialize into.
 * @param [in] allow_longer_data If false, then we throw a value_mismatch_error if the
 *                                deserialized data (according to the type of 't') is
 *                                shorter than what is in 's'. Setting this to true
 *                                enables you to deserialize only the beginning.
 *                                Use like: uf::deserialize<false>(s, t);
 * @param [in] tt You can specify additional data, which will be used to select which
 *                helper function (e.g., tuple_for_serialization) to apply.
 * @returns the remaining data as string_view. If !allow_longer data, it is empty.*/
template <typename T, typename ...tags>
inline std::string_view deserialize_view(std::string_view s, T &t, bool allow_longer_data = false, 
                                         uf::use_tags_t = {}, tags... tt) {
    static_assert(uf::impl::is_deserializable_f<T, true, true, tags...>(), "Type must be possible to deserialize into.");
    const char *p = s.data();
    if (impl::deserialize_from<true>(p, s.data()+s.length(), t, tt...))
        throw value_mismatch_error(uf::concat(impl::ser_error_str(impl::ser::val), " (deser_view) <%1>."), deserialize_type<T, tags...>(), 0);
    if (!allow_longer_data && p!=s.data()+s.length())
        throw value_mismatch_error("Bytes left after deserializing to <%1>", deserialize_type<T>());
    return { p, s.data() + s.length() >= p ? size_t(s.data() + s.length() - p) : 0 };
}

/** Deserialize from a bytearray to a type.
 * Type can contain string_views and any_views that will point back to 's'/'a', resp.
 * This overload is provided, so temporary reference tuples can work, like:
 * deserialize(s, std::tie(a,b));
 * @param [in] s The raw data to deserialize from.
 * @param [out] t The placeholder to deserialize into.
 * @param [in] allow_longer_data If false, then we throw a value_mismatch_error if the
 *                                 deserialized data (according to the type of 't') is
 *                                 shorter than what is in 's'. Setting this to true
 *                                 enables you to deserialize only the beginning.
 *                                 Use like: uf::deserialize<false>(s, t);
 * @param [in] tt You can specify additional data, which will be used to select which
 *                helper function (e.g., tuple_for_serialization) to apply.
 * @returns the remaining data as string_view. If !allow_longer data, it is empty.*/
template <typename T, typename ...tags>
    inline std::string_view deserialize_view(std::string_view s, T &&t, bool allow_longer_data = false, 
                                             uf::use_tags_t = {}, tags... tt) {
    return deserialize_view(s, t, allow_longer_data, uf::use_tags, tt...);
}

/** Deserialize from a bytearray to a type.
 * Type can contain string_views and any_views that will point back to 's'/'a', resp.
 * This version should be invoked with the type specified explicityly, like
 * deserialize_view<std::string_view>(s);
 * @param [in] s The raw data to deserialize from.
 * @param [in] allow_longer_data If false, then we throw a value_mismatch_error if the
 *                                deserialized data (according to the type of 't') is
 *                                shorter than what is in 's'. Setting this to true
 *                                enables you to deserialize only the beginning.
 *                                Use like T a = uf::deserialize<T, false>(s);
 * @param [in] tt You can specify additional data, which will be used to select which
 *                helper function (e.g., tuple_for_serialization) to apply.
 * @returns The placeholder to deserialize into.*/
template <typename T, typename ...tags>
inline T deserialize_view_as(std::string_view s, bool allow_longer_data = false, 
                             uf::use_tags_t = {}, tags...tt) {
    static_assert(std::is_default_constructible_v<T>, "Result must be default constructible.");
    T t;  
    deserialize_view(s, t, allow_longer_data, uf::use_tags, tt...);
    return t; 
}

/** Deserialize from a bytearray with a known type to a C++ variable with potential conversions.
 * Type can contain string_views and any_views that will point back to 's'/'a', resp.
 * @param [in] s The raw data to deserialize from.
 * @param [in] from_type The typestring of the data in 's'.
 * @param [out] v The placeholder to deserialize into.
 * @param [in] convpolicy The conversion policy to apply. You can enable/disable certain conversions here.
 * @param [in] allow_longer_data If false, then we throw a value_mismatch_error if the
 *                                deserialized data (according to the type of 't') is
 *                                shorter than what is in 's'. Setting this to true
 *                                enables you to deserialize only the beginning.
 *                                Use like: uf::deserialize_convert<false>(s, type, t);
 * @param [in] tt You can specify additional data, which will be used to select which
 *                helper function (e.g., tuple_for_serialization) to apply.
 * @returns the remaining data as string_view.If !allow_longer data, it is empty.*/
template <typename T, typename ...tags>
inline std::string_view deserialize_view_convert(std::string_view s, std::string_view from_type, T &v,
                                                 serpolicy convpolicy = uf::allow_converting_all,
                                                 bool allow_longer_data = false,
                                                 uf::use_tags_t = {}, tags... tt) {
    static_assert(uf::impl::is_deserializable_f<T, true, true, tags...>(), "Type must be possible to deserialize into.");
    std::vector<error_value> errors;
    std::vector<std::pair<size_t, size_t>> error_pos;
    impl::deserialize_convert_params p(s, from_type, &v, convpolicy, nullptr,
                                       &errors, &error_pos, tt...);
    bool can_disappear;
    if (auto err = impl::deserialize_convert_from<true>(can_disappear, p, v, tt...))
        err->throw_me();
    if (!allow_longer_data && p.p != s.data() + s.length())
        throw value_mismatch_error("Bytes left after deserializing <%1>", from_type);
    if (errors.size())
        throw uf::expected_with_error("In uf::deserialize_convert <%1> -> <%2> cannot place errors in expected values. Errors: %e",
                                      from_type, deserialize_type<T, tags...>, std::move(errors), std::move(error_pos));
    return {p.p, p.end>=p.p ? size_t(p.end - p.p) : 0};
}

/** Deserialize from a bytearray with a known type to a C++ variable  with potential conversions.
 * Type can contain string_views and any_views that will point back to 's'/'a', resp.
 * This overload is provided, so temporary reference tuples can work, like:
 * deserialize(s, std::tie(a,b));
 * @param [in] s The raw data to deserialize from.
 * @param [in] from_type The typestring of the data in 's'.
 * @param [out] v The placeholder to deserialize into.
 * @param [in] convpolicy The conversion policy to apply. You can enable/disable certain conversions here.
 * @param [in] allow_longer_data If false, then we throw a value_mismatch_error if the
 *                                deserialized data (according to the type of 't') is
 *                                shorter than what is in 's'. Setting this to true
 *                                enables you to deserialize only the beginning.
 *                                Use like: uf::deserialize_convert<false>(s, type, t);
 * @param [in] tt You can specify additional data, which will be used to select which
 *                helper function (e.g., tuple_for_serialization) to apply.
 * @returns the remaining data as string_view.If !allow_longer data, it is empty.*/
template <typename T = int, typename ...tags>
    inline std::string_view deserialize_view_convert(std::string_view s, std::string_view from_type, T &&v,
                                                     serpolicy convpolicy = uf::allow_converting_all,
                                                     bool allow_longer_data = false,
                                                     uf::use_tags_t = {}, tags... tt) {
    return deserialize_view_convert(s, from_type, v, convpolicy, allow_longer_data, 
                                    uf::use_tags, tt...);
}

/** Deserialize from a bytearray with a known type to a C++ variable  with potential conversions.
 * Type can contain string_views and any_views that will point back to 's'/'a', resp.
 * This version should be invoked with the type specified explicitly, like
 * deserialize_view_convert_as<std::string_view>(s);
 * @param [in] s The raw data to deserialize from.
 * @param [in] from_type The typestring of the data in 's'.
 * @param [in] convpolicy The conversion policy to apply. You can enable/disable certain conversions here.
 * @param [in] allow_longer_data If false, then we throw a value_mismatch_error if the
 *                               deserialized data (according to the type of 't') is
 *                               shorter than what is in 's'. Setting this to true
 *                               enables you to deserialize only the beginning.
 * @param [in] tt You can specify additional data, which will be used to select which
 *                helper function (e.g., tuple_for_serialization) to apply. */
template <typename T, typename ...tags>
inline T deserialize_view_convert_as(std::string_view s, std::string_view from_type,
                                     serpolicy convpolicy = uf::allow_converting_all,
                                     bool allow_longer_data = false,
                                     uf::use_tags_t = {}, tags... tt) {
    static_assert(std::is_default_constructible_v<T>, "Result must be default constructible.");
    T v;
    deserialize_view_convert(s, from_type, v, convpolicy, allow_longer_data,
                             uf::use_tags, tt...);
    return v; 
}

/** Returns empty if from_type can be converted to to_type.
 * If not, it returns the appropriate exception - without throwing it.
 * Note that it cannot check dynamic type info due to lack of actual value
 * and returns true if there is any possible value that could allow this type 
 * conversion. Thus, e.g., a->T is always accepted (if `allow_converting_any` is 
 * part of the policy). Also, `a` values can disappear, as they may contain void,
 * thus, e.g., `t2ai` is convertible to `i`*/
[[nodiscard]] inline std::unique_ptr<value_error> cant_convert(std::string_view from_type, std::string_view to_type, serpolicy convpolicy)
{
    if (from_type == to_type) return {}; //fast path: equality
    impl::deserialize_convert_params p(nullptr, nullptr,
                                       from_type.data(), from_type.data(), from_type.data() + from_type.length(),
                                       to_type.data(), to_type.data(), to_type.data() + to_type.length(),
                                       convpolicy, nullptr, nullptr, nullptr);
    if (auto err = impl::cant_convert<false, false>(p, nullptr))
        return err;
    if (p.type < p.tend) return create_des_typestring_source(p, impl::ser_error_str(impl::ser::tlong));
    if (p.target_type < p.target_tend) return create_des_type_error(p);
    return {};
}

/** Returns empty if from_type can be converted to to_type.
 * If not, it returns the appropriate exception - without throwing it.
 * Note that it can even check dynamic type info due to the actual value
 * provided in serialized_data, which must be of type from_type.
 * Thus, e.g., a->T is accepted only if the value contained in the any
 * can be converted to T (and if allow_converting_any is part of the
 * policy). We return expected_with_error in case there are expected ('x')
 * values containing errors that cannot be placed.
 * On a bad typestring we return a typestring_error and if the serialized
 * value does not does not match the typestring we return a
 * value_mismatch_error. */
[[nodiscard]] inline std::unique_ptr<value_error>
cant_convert(std::string_view from_type, std::string_view to_type, serpolicy convpolicy,
             std::string_view serialized_data)
{
    //no fast path: we need to check the data, too.
    std::vector<error_value> errors;
    std::vector<std::pair<size_t, size_t>> error_pos;
    impl::deserialize_convert_params p(serialized_data.data(), serialized_data.data() + serialized_data.length(),
                                       from_type.data(), from_type.data(), from_type.data() + from_type.length(),
                                       to_type.data(), to_type.data(), to_type.data() + to_type.length(),
                                       convpolicy, nullptr, &errors, &error_pos);
    if (auto err = impl::cant_convert<true, false>(p, nullptr))
        return err;
    if (p.type < p.tend) return create_des_typestring_source(p, impl::ser_error_str(impl::ser::tlong));
    if (p.target_type < p.target_tend) return create_des_type_error(p);
    if (errors.size())
        return std::make_unique<expected_with_error>("Could not place expected error(s) <%1> -> <%2> Errors are: %e.",
                                                    from_type, to_type,
                                                    std::move(errors), std::move(error_pos));
    return {};
}

/** Converts a serialized version of a type.
 * @param [in] from_type The typestring of the source
 * @param [in] to_type The typestring  of the target type
 * @param [in] policy The conversion policy
 * @param [in] from_data The serialized data to convert (should be of type 'from_type')
 * @param [in] check If true, we check the validity of from_type/from_data and of to_type
 *             Defaults to false.
 * @exception uf::type_mismatch_error the conversion is not possible (with this policy)
 * @exception uf::expected_with_error if expected values holding errors would need to be
 *   converted to a non-expected type during conversion.
 * @exception uf::typestring_error bad typestring 
 * @exception uf::value_mismatch_error the from type and serialized data mismatch
 * @returns The converted value if different from 'from_data'. If it is
 *          the same as from_data, an empty optional is returned.*/
inline std::optional<std::string>
convert(std::string_view from_type, std::string_view to_type, 
        serpolicy policy, std::string_view from_data, bool check)
{
    if (from_type == to_type && !check) return {}; //fast path: equality
    std::vector<error_value> errors;
    std::vector<std::pair<size_t, size_t>> error_pos;
    impl::deserialize_convert_params p(from_data.data(), from_data.data() + from_data.length(),
                                       from_type.data(), from_type.data(), from_type.data() + from_type.length(),
                                       to_type.data(), to_type.data(), to_type.data() + to_type.length(),
                                       policy, nullptr, &errors, &error_pos);
    uf::impl::StringViewAccumulator target;
    if (auto err = impl::cant_convert<true, true>(p, &target))
        err->throw_me();
    if (p.type < p.tend) create_des_typestring_source(p, impl::ser_error_str(impl::ser::tlong))->throw_me();
    if (p.target_type < p.target_tend) create_des_type_error(p)->throw_me();
    if (errors.size())
        throw expected_with_error("Could not place expected error(s) <%1> -> <%2> Errors are: %e.",
                                   from_type, to_type,
                                   std::move(errors), std::move(error_pos));
    std::variant<std::string, std::string_view> ret = target; //conversion
    if (ret.index() == 0) return std::move(std::get<0>(ret));
    if (std::string_view sv = std::get<1>(ret); sv.data() == from_data.data() && sv.length() == from_data.length()) //we know they may be equal only in-place, no need for full compare
        return {};
    else
        return std::string(sv);
}

/** Convert the a C++ variable of arbitrary type to a printable string.
 * @param [in] t The C++ variable to print.
 * @param [in] json_like Tf true, we attempt to be as json compatible as possible.
 *             - characters are printed as a single character string
 *             - enumerations are printed as an integer
 *             - void values and empty optionals are printed as 'null'
 *             - strings will contain backslash escaped backspace, tab, cr, lf, ff, quotation mark and backslash (in addition to 'chars')
 *             - errors are printed as string
 *             - tuples are printed as arrays
 *             - for 'any' values, we omit the typestring, just print the value.
 * @param [in] max_len A maximum length. Characters beyond are trimmed. Zero is unlimited.
 * @param [in] chars Specify a list of characters to also encode as %xx.
 *                   Urls require, the space, quotation mark and the ampersand
 *                   These are in the macro URL_CHARS.
 * @param [in] escape_char What escape character to use.
 * @param [in] tt You can specify additional data, which will be used to select which
 *                helper function (e.g., tuple_for_serialization) to apply.
 * @returns the ascii printable text. If the string is trimmed, it ends in ...*/
template <typename T, typename ...tags>
inline std::string serialize_print(const T& t, bool json_like = false, unsigned max_len = 0, 
                                   std::string_view chars = {}, char escape_char = '%',
                                   uf::use_tags_t = {}, tags... tt) {
    static_assert(uf::impl::is_serializable_f<T, true, tags...>(), "Type must be serializable.");
    if constexpr (uf::impl::is_serializable_f<T, false, tags...>()) {
        std::string ret; ret.reserve(32);
        impl::serialize_print_append(ret, json_like, max_len, t, chars, escape_char, tt...);
        return ret;
    } else
        return {};
}

/** Deserializes a type given its textual description as an ascii printable text.
 * @param type The string view containing the type of the serialized value.
 * @param serialized The raw value to deserialize from.
 * @param [in] max_len A maximum length. Characters beyond are trimmed. Zero is unlimited.
 * @param [in] json_like Tf true, we attempt to be as json compatible as possible.
 *             - characters are printed as a single character string
 *             - void values and empty optionals are printed as 'null'
 *             - strings will contain backslash escaped backspace, tab, cr, lf, ff, quotation mark and backslash (in addition to 'chars')
 *             - errors are printed as string
 *             - tuples are printed as arrays.
 *             - for 'any' values, we omit the typestring, just print the value.
 * @param [in] chars Specify a list of characters to also encode as %xx.
 *                   Urls require, the space, quotation mark and the ampersand
 *                   These are in the macro URL_CHARS.
 * @param [in] escape_char What escape character to use.
 * @returns the ascii printable text. If the string is trimmed, it ends in ...
 */
inline std::string serialize_print_by_type(std::string_view type, std::string_view serialized, bool json_like = false, 
                                           unsigned max_len = 0, std::string_view chars = {}, char escape_char='%')
{
    const char *start = serialized.data();
    std::string_view worktype = type;
    std::string ret;
    if (auto err = impl::serialize_print_by_type_to(ret, json_like, max_len, worktype, start, 
                                                    serialized.data() + serialized.length(), chars, escape_char)) {
        if (*err) //error
            (*err)->prepend_type0(type, worktype).throw_me();
        //else just max_len overrun
        ret.resize(max_len);
        ret.append("...");
        return ret;
    }
    if (worktype.length())
        throw typestring_error(uf::impl::ser_error_str(uf::impl::ser::tlong), type, worktype.data()-type.data());
    return ret;
}

/** Parses a string to see if it is a valid type.
 * If OK, we return the pos of the next char after.
 * On error we return 0. 
 * Note: This is equivalent to successfully parsing a zero-length
 * typestring at the beginning of 'type', which is a valid typestring
 * for void-like types. */
inline size_t parse_type(std::string_view type)
{ if (auto [len, problem] = impl::parse_type(type, false); !problem) return len; else return 0; }

/** Prints an asci version of a potentially binary string (no spaces either)
 * @param [in] v The string to print.
 * @param [in] max_len A maximum length. Characters beyond are trimmed. Zero is unlimited.
 * @param [in] chars Specify a list of characters to also encode as %xx.
 *                   Urls require, the space, quotation mark and the ampersand
 *                   These are in the macro URL_CHARS.
 * @param [in] escape_char You can also change the escape char from % to something else.*/
inline std::string print_escaped(std::string_view v, unsigned max_len = 0, std::string_view chars = {}, char escape_char = '%')
{ std::string ret; impl::print_escaped_to(ret, max_len, v, chars, escape_char); return ret; }

/** Converts an ascii escaped version of a string to its original binary form.
 * @param [in] v The string to parse.
 * @param [in] escape_char Specify what was used as escaped char when printing.*/
inline std::string parse_escaped(std::string_view v, char escape_char = '%')
{ std::string ret; impl::parse_escaped_string_to(ret, v, escape_char); return ret; }
 
/** @} serialization */

/** @addtogroup serialization
 * @{ */

/** A simple wrapper around a contigous array of initialized type Ts.
 * A helper for deserializing into pre-allocated memory.
 * When deserializing an 'lT' type into an array_inserted and the pre-allocated length
 * is not sufficient, an uf::value_mismatch_error is thrown.
 * It can be instantiated for any type, but of course deserialization will work
 * only if the type is serializable (with some tags).*/
template <typename T, typename Size>
class array_inserter {
    static_assert(std::is_integral_v<Size>, "The location of the size must be an integer type.");
    T *const start;
    T *pos;
    const Size len;
    Size *const store_size_here;
    const bool throw_if_larger;
public:
    using value_type = T;
    explicit array_inserter(T *p, Size max_len, Size *s = nullptr, bool throw_on_larger = true) :
        start(p), pos(p), len(max_len), store_size_here(s), throw_if_larger(throw_on_larger) {
        if constexpr (std::numeric_limits<Size>::is_signed) {
            if (max_len < 0)
                throw api_error("array_inserter called with a negative max_size: " + std::to_string(len));
            if (std::numeric_limits<int32_t>::max() < len)
                throw api_error("array_inserter called with a too large max_size: " + std::to_string(len));
        } else
            if (std::numeric_limits<uint32_t>::max() < len)
                throw api_error("array_inserter called with a too large max_size: " + std::to_string(len));
        if (store_size_here)
            *store_size_here = 0;
    }
    T *begin() const { return start; }
    T *end() const { return pos; }
    void clear() { pos = start; if (store_size_here) *store_size_here = 0; }
    void push_back(T &&t) {
        if (store_size_here)
            (*store_size_here)++;
        if (pos < start + len) *pos++ = std::move(t);
        else if (throw_if_larger)
            throw value_mismatch_error(uf::concat("array_inserter<", deserialize_type<T>(),
                                                  ">(", len, ") overfilled."));
    }
};

static_assert(impl::is_deserializable_container_with_push_back<array_inserter<int, int>>::value);

/** @}  serialization */

namespace impl
{
template <typename ...T, std::size_t ... Is>
/** Return a tuple of pointers - one for each type of a variant.
 * Used for serializing variants.
 * Each pointer will be null, except the one, which is the current value
 * //TODO: For 'char' types instead of a const char * we return an (empty or set)
 * std::optional<char> as const char * is serialized as a null terminated
 * string*/
auto tup_ptr(const std::variant<T...> &v, std::index_sequence<Is...>) noexcept {
    return uf::tie(Is == v.index() ? &std::get<Is>(v) : (T *)nullptr ...);
}
} //ns impl
} //ns uf

namespace std 
{
template <typename ...T>
auto tuple_for_serialization(const std::variant<T...> &v) noexcept {
    return uf::impl::tup_ptr(v, std::make_index_sequence<sizeof...(T)>());
}
template <typename ...T>
auto tuple_for_serialization(std::variant<T...> &) noexcept {
    return std::tuple<std::optional<T>...>(); 
}
template <typename ...T>
void after_deserialization(std::variant<T...> &v, std::tuple<std::optional<T>...> &&t) {
    using Tuple = std::tuple<std::optional<T>...>;
    std::optional<size_t> first_index;
    uf::for_each_with_index(std::forward<Tuple>(t),
                            [&first_index, &v](size_t index, auto &&elem) {
                                if (!elem.has_value()) return;
                                if (first_index)
                                    throw uf::value_mismatch_error(
                                        uf::concat("Multiple elements (", *first_index, " and ", index,
                                                   ") of the variant are set on deserialization.")); //Dont print our type, we dont know it due to not knowing the tags.
                                first_index = index;
                                v = std::move(*elem);
                            });
    if (first_index) return;
    throw uf::value_mismatch_error("No elements of the variant are set on deserialization.");
}
} //ns std

namespace uf {

static_assert(has_before_serialization_tag_v<any> == false, "uf::any cannot contain before_serialization, as code calls uf::impl::serialize_to() on it regularly.");
static_assert(has_after_serialization_tag_v<any> == false, "uf::any cannot contain after_serialization, as code calls uf::serialize_to() on it regularly.");
static_assert(has_before_serialization_tag_v<any_view> == false, "uf::any_view cannot contain before_serialization, as code calls uf::serialize_to() on it regularly.");
static_assert(has_after_serialization_tag_v<any_view> == false, "uf::any_view cannot contain after_serialization, as code calls uf::serialize_to() on it regularly.");
static_assert(has_before_serialization_tag_v<error_value> == false, "uf::error_value cannot contain before_serialization, as code calls uf::serialize_to() on it regularly.");
static_assert(has_after_serialization_tag_v<error_value> == false, "uf::error_value cannot contain after_serialization, as code calls uf::serialize_to() on it regularly.");

} //ns uf
