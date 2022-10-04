#pragma once
#include "ufser.h"
#include <memory>
#include <map>
#include <iostream>
#include <forward_list>
#include <atomic>

#define XSTR(x) STR(x)
#define STR(x) #x
#define LOC __FILE__ ":" XSTR(__LINE__)

using namespace std::string_literals;

namespace uf {

struct string_variant : public std::variant<std::string_view, std::string>
{
    string_variant(std::string_view v) : std::variant<std::string_view, std::string>(v) {}
    string_variant(std::string&& v) : std::variant<std::string_view, std::string>(std::move(v)) {}
    operator std::string_view() const noexcept { if (index()) return std::get<1>(*this); else return std::get<0>(*this); }
    operator std::string()&& { if (index()) return std::move(std::get<1>(*this)); else return std::string(std::get<0>(*this)); }
    std::string_view as_view() const noexcept { return *this; }
    std::string_view sub_view(uint32_t off, uint32_t len = -1) const noexcept { return as_view().substr(off, len); }
    uint32_t size() const noexcept { return as_view().size(); }
    bool empty() const noexcept { return as_view().empty(); }
    bool operator ==(const std::string_view& sv) const noexcept { return as_view() == sv; }
    bool operator ==(const string_variant& o) const noexcept { return as_view() == o.as_view(); }
    bool has_view() const noexcept { return index() == 0; }
};

struct any_variant : public std::variant<any_view, any>
{
    any_variant(any_view v) : variant<any_view, any>(v) {}
    any_variant(any&& v) : variant<any_view, any>(std::move(v)) {}
    operator any_view() const noexcept { if (has_view()) return std::get<0>(*this); else return std::get<1>(*this); }
    any_view as_view() const noexcept { return *this; }
    [[nodiscard]] std::string print(unsigned max_len = 0, std::string_view chars = {}, char escape_char = '%', bool json_like = false) const {
        if (index())
            return std::get<1>(*this).print(max_len, chars,escape_char, json_like);
        return std::get<0>(*this).print(max_len, chars,escape_char, json_like);
    }
    bool has_view() const noexcept { return index() == 0; }
};

namespace impl {

inline std::string x_escape(std::string_view v) {
    auto s = print_escaped(v);
    for (size_t p = 0; p < s.size(); ++p)
        if (s[p] == '%') {
            s[p++] = '\\';
            s.insert(p, 1, 'x');
        }
    return s;
}

/** A base class for (potentially) shared pointers for objects (potentially) with a refcount field.
 * in case of 'has_refc' is set, 'object' must have a refc_inc() and refc_dec() function, both returning the (epheremal) new value of the refcount.
 * 'object' must have get_memsize() to return how much to deallocate. May default to sizeof(object)*/
template <typename object, bool has_refc, template <typename> typename Allocator>
class shared_ref_base
{
protected:
    object* p = nullptr;
    void unref() const noexcept
    {
        if constexpr (has_refc) if (p && p->refc_dec() == 0) {
            const size_t size = p->get_memsize();
            p->~object();
            Allocator<char>().deallocate((char*)(void*)p, size);
        }
    }
public:
    constexpr static bool has_refcount = has_refc;
    shared_ref_base() noexcept = default;
    shared_ref_base(const shared_ref_base & o) noexcept : p(o.p) {
        if constexpr (has_refc)
            if (p) { [[maybe_unused]] auto c = p->refc_inc(); assert(c); }
    }
    shared_ref_base(shared_ref_base && o) noexcept : p(o.p) { o.p = nullptr; }
    shared_ref_base& operator =(const shared_ref_base & o) noexcept {
        if (this != &o) {
            if constexpr (has_refc) {
                unref();
                if (o.p) { [[maybe_unused]] auto c = o.p->refc_inc(); assert(c); }
            }
            p = o.p;
        }
        return *this;
    }
    shared_ref_base& operator =(shared_ref_base && o) noexcept { if (this != &o) { unref(); p = o.p; o.p = nullptr; } return *this; }
    ~shared_ref_base() noexcept { unref(); }
    bool operator ==(const shared_ref_base& o) const noexcept { return p == o.p; }
    bool operator !=(const shared_ref_base& o) const noexcept { return p != o.p; }
    void clear() noexcept { unref(); p = nullptr; }
    object* operator ->() const noexcept { return p; }
    object& operator *() const noexcept { return *p; }
    explicit operator bool() const noexcept { return bool(p); }
    auto get_refcount() noexcept { return has_refc && p ? p->get_refcount() : 0; } //returned value may be invalid right after if multi-threaded
};

/** A reference count. This makes the descendant objects
 * non-copyable and non-movable as pointers point to it and
 * any such operation would mess with the refcount.*/
class RefCount {
    std::atomic<uint16_t> refcount = 1;
public:
    RefCount() noexcept = default;
    RefCount(const RefCount&) = delete;
    RefCount(RefCount&&) noexcept = delete;
    RefCount& operator=(const RefCount&) = delete;
    RefCount& operator=(RefCount&&) = delete;
    auto refc_inc() noexcept { return refcount.fetch_add(1, std::memory_order_acq_rel)+1; }
    auto refc_dec() noexcept { return refcount.fetch_sub(1, std::memory_order_acq_rel)-1; }
    auto get_refcount() const noexcept { return refcount.load(std::memory_order_relaxed); }
};

struct NoRefCount {};

/// A shared writable string view. Not thread safe
template <bool has_refc, template <typename> typename Allocator = std::allocator>
class sview : private std::conditional_t<has_refc, RefCount, NoRefCount> {
public:
    class ptr : public shared_ref_base<sview, has_refc, Allocator> {
    public:
        using shared_ref_base<sview, has_refc, Allocator>::shared_ref_base;
        using shared_ref_base<sview, has_refc, Allocator>::p;
        constexpr static size_t memsize(size_t string_len) noexcept { return std::max(sizeof(sview), sizeof(sview) + string_len - 4); }

        //Note: all sview ctors are noexcept, so no need to protect against
        //exceptions during placement new that would cause leaks.

        /** Create a read-only, non-owning or writeable, owning (latter is default). */
        ptr(std::string_view sv, bool copy = true) {
            void* mem = Allocator<char>().allocate(memsize(sv.length()));
            p = copy ? new(mem) sview(sv.length(), sv.data()) : new(mem) sview(sv);
        }
        /** Create a read-only, non-owning or writeable, owning (latter is default). */
        ptr(const char *sv, bool copy = true)
        {
            const size_t len = strlen(sv);
            void* mem = Allocator<char>().allocate(memsize(len));
            p = copy ? new(mem) sview(len, sv) : new(mem) sview(std::string_view(sv, len));
        }
        /** Create a read-only, non-owning or writeable, owning (latter is default). */
        ptr(uint32_t len, const char* sv, bool copy = true) {
            void* mem = Allocator<char>().allocate(memsize(len));
            p = copy ? new(mem) sview(len, sv) : new(mem) sview(std::string_view(sv, len));
        }
        /** Create a writable, non-owning or owning (latter is default). */
        ptr(std::string& ss, bool copy = true) {
            void* mem = Allocator<char>().allocate(memsize(ss.length()));
            p = copy ? new(mem) sview(ss.length(), ss.data()) : new(mem) sview(ss);
        }
        /** Create a writable, owning. Always copy. */
        ptr(std::string&& ss) {
            void* mem = Allocator<char>().allocate(memsize(ss.length()));
            p = new(mem) sview(ss.length(), ss.data());
        }
        /** Create a writable, non-owning or owning (latter is default). */
        ptr(char *ss, bool copy = true)
        {
            const size_t len = strlen(ss);
            void* mem = Allocator<char>().allocate(memsize(len));
            p = copy ? new(mem) sview(len, ss) : new(mem) sview(ss);
        }
        /** Create a writable, non-owning or owning (latter is default). */
        ptr(uint32_t len, char* ss, bool copy = true) {
            void* mem = Allocator<char>().allocate(memsize(len));
            p = copy ? new(mem) sview(len, ss) : new(mem) sview(ss, len);
        }
        /** Create a non-owning, non-writeable from a C string literal. */
        template <uint32_t LEN>
        ptr(const char (&c)[LEN]) : ptr(std::string_view(c, LEN-1), false) {assert(c[LEN-1]==0);} //null terminated string literal

        /** Create a fresh, uninitialized, owning, writable string of size 'l'. */
        ptr(uint32_t l) {
            void* mem = Allocator<char>().allocate(memsize(l));
            p = new(mem) sview(l);
        }
        /** Creates a copy of this string, trimmed & writable. */
        ptr clone(uint32_t off_ = 0, uint32_t len_ = (uint32_t)-1) const {
            assert(p);
            assert(off_ <= p->length);
            len_ = std::min(len_, p->length - off_);
            ptr ret;
            void* mem = Allocator<char>().allocate(memsize(len_));
            ret.p = new(mem) sview(len_, p->data() + off_); //copy
            return ret;
        }
    };
    friend class shared_ref_base<sview, has_refc, Allocator>;
    uint32_t size() const noexcept { return length; }
    const char* data() const noexcept { return owning ? data_ : ptr_; }
    std::string_view as_view() const noexcept { return { data(), size() }; }
    char* data_writable() noexcept { assert(is_writable()); return owning ? data_ : ptr_; }
    bool is_writable() const noexcept { return writable.load(std::memory_order_acquire); }
    bool is_unique() const noexcept { if constexpr (has_refc) return this->get_refcount() == 1; else return false; } //We are never unique if we do not manage refcount
    void make_read_only() noexcept { writable.store(false, std::memory_order_release); }
private:
    const uint32_t length;
    std::atomic_bool writable;
public:
    bool const owning;
    constexpr size_t get_memsize() const noexcept { return owning ? ptr::memsize(length) : sizeof(sview); }
private:
    union {
        char* const ptr_; //if we are non-owning
        struct { char data_[]; }; //if we are owning
    };
    //delete the big five. The latter four would mess with the refount.
    sview() = delete;
    explicit sview(std::string_view s) noexcept : length(s.length()), writable(false), owning(false), ptr_(const_cast<char*>(s.data())) {}
    explicit sview(std::string &s) noexcept : length(s.length()), writable(true), owning(false), ptr_(s.data()) {}
    explicit sview(char *c, uint32_t len) noexcept : length(len), writable(true), owning(false), ptr_(c) {}
    explicit sview(uint32_t l, const char *initial_data=nullptr) noexcept
        : length(l), writable(true), owning(true) { if (initial_data && l) memcpy(data_, initial_data, l); }
    ~sview() noexcept = default;
};

/// A chunk of a serialized uf::any that holds either a (part of a) typestring or some part of the value string.
/// If a value string, it should contain one or more full basic types' worth of data.
/// The chunk is a view into either a read-only area, or a managed sview
template <bool has_refc, template <typename> typename Allocator= std::allocator>
class chunk : private std::conditional_t<has_refc, RefCount, NoRefCount> {
    using sview_ptr = typename sview<has_refc, Allocator>::ptr;
    sview_ptr root; ///< a managed underlying string object. Never null;
    uint32_t off;   ///< offset inside root
    uint32_t len;   ///< size of this view

    /** Allocate a new owning/writable chunk */
    explicit chunk(uint32_t l) : root(l), off(0), len(l)
    { assert(l <= std::numeric_limits<decltype(len)>::max()); }
    /** Sub-chunk of an existing shared string (writable or non-writable alike) */
    explicit chunk(char const* b, uint32_t l, sview_ptr&& r) noexcept :
        root(std::move(r)), off(b - root->data()), len(l) {
        assert(l <= std::numeric_limits<decltype(len)>::max());
        assert(!l || root);
        assert(!l || b >= root->data());
        assert(!l || b + l <= root->data() + root->size());
    }
    /** Create chunk from an sview in its entirety. */
    explicit chunk(sview_ptr&& r) noexcept :
        root(std::move(r)), off(0), len(root ? root->size() : 0) {}
public:
    class ptr : public shared_ref_base<chunk, has_refc, Allocator>
    {
        using shared_ref_base<chunk, has_refc, Allocator>::p;
    public:
        using shared_ref_base<chunk, has_refc, Allocator>::shared_ref_base;

        explicit ptr(uint32_t l) {
            char* mem = Allocator<char>().allocate(sizeof(chunk));
            //This chunk ctor may throw, so deallocate if it does.
            try { p = new(mem) chunk(l); }
            catch (...) { Allocator<char>().deallocate(mem, sizeof(chunk)); throw; }
        }
        /** Sub-chunk of an existing shared string (writable or non-writable alike) */
        explicit ptr(char const* b, uint32_t l, sview_ptr&& r)
        { p = new(Allocator<char>().allocate(sizeof(chunk))) chunk(b, l, std::move(r)); } //non-throwing chunk ctor
        /** Create chunk from an sview in its entirety. */
        explicit ptr(sview_ptr&& r)
        { p = new(Allocator<char>().allocate(sizeof(chunk))) chunk(std::move(r)); } //non-throwing chunk ctor

        ptr& operator++() noexcept {
            if (p) *this = p->next;
            return *this;
        }
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::input_iterator_tag;
        using value_type = chunk const;
        using pointer = chunk const*;
        using reference = chunk const&;
    };
    friend class shared_ref_base<chunk, has_refc, Allocator>;
    ptr next;///< The next item in a forward list of chunks connected to the same wview

    static constexpr size_t get_memsize() noexcept { return sizeof(chunk); }
    char const* data() const noexcept { return root ? root->data() + off : nullptr; }
    char* data_writable() noexcept {
        if (!root) return nullptr;
        //since for writable only we own 'root' (refocunt must be 1),
        //there is no chance of a change of writable status between this check and the final line
        if (!root->is_writable()) {
            root = root.clone(off, len);
            off = 0;
        }
        return root->data_writable()+off;
    }
    uint32_t size() const noexcept { return len; }
    std::string_view as_view() const noexcept {return std::string_view{data(), size()}; }

    /// @name Chunk pointer builders
    /// @{
    /// Creates a chunk from us using offset and length. The next field is set to null. Excess length is trimmed.
    ptr sub_chunk(uint32_t offset, uint32_t l = -1) const {
        assert(offset <= size());
        return ptr(data() + offset, std::min(size() - offset, l), sview_ptr(root));
    }
    /// Creates copy of us with next set to null.
    ptr clone() const { return sub_chunk(0); }
    /// @}

    /** Ensures that the current chunk is writable, and at least this big and returns a writable char array.
     * If not writable or not large enough, we allocate (and loose existing content)
     * We pay attention to preserve the 'next' field.*/
    char* reserve(decltype(len) l) & {
        if (!is_writable() || l > len) {
            root = sview_ptr(l);
            off = 0;
            len = l;
        }
        return data_writable();
    }
    /** Resizes the chunk. If we need to enlarge, we re-allocate and loose existing content.
     * We pay attention to preserve the 'next' field.*/
    chunk& resize(uint32_t l) & { if (l > len) reserve(l); else len = l; return *this; }
    bool is_writable() const noexcept { return root && root->is_writable(); }
    /** Assigns content to us, by ensuring we are writable and then copying over.
     * We pay attention to preserve the 'next' field.*/
    chunk& assign(std::string_view s) & {
        memcpy(reserve(s.size()), s.data(), s.size());
        len = s.size();
        return *this;
    }
    /** Ensure that either it is a non-writable swview or I have it for myself.
     * If read-only is set then it is ensured that the resulting sview will be
     * read-only.*/
    chunk& unshare() & {
        if (!is_writable()) return *this;
        if (root->is_unique()) return *this;
        root = root.clone(off, len);
        off = 0;
        return *this;
    }
    /** Assigns content to us.
     * We pay attention to preserve the 'next' field.*/
    chunk& assign(sview_ptr s) & {
        root = std::move(s);
        off = 0;
        len = root->size();
        return *this;
    }
    /** Copy content to us from another chunk.
     * We pay attention to *copy* the 'next' field, as well.*/
    chunk& copy_from(const chunk &c) & {
        root = c.root;
        off = c.off;
        len = c.len;
        next = c.next;
        return *this;
    }
    /** Swap our content with that of another chunk.
     * We pay attention to swap the 'next' field, as well.*/
    void swap_content_with(chunk& c)
    {
        std::swap(root, c.root);
        std::swap(off, c.off);
        std::swap(len, c.len);
        std::swap(next, c.next);
    }

    /** We reset the chunk to be the empty chunk. We also clear the 'next' field.*/
    void reset() { root = {}; len = 0; next = {}; }
    /** Try appending a chunk if it follows us directly in memory.
     * Return false if failed. */
    bool try_append(const chunk& ch) {
        if (root == ch.root && off + len == ch.off) { len += ch.len; return true; }
        return false;
    }
    operator std::string() const { return uf::concat("chunk{len: ", len, ", buf: \"", x_escape(as_view()), "\", mode: \"",
        is_writable() ? "" : "non-", "writable\""
        /*", next: " + std::to_string(uint32_t(next.get())) + ", root: " + std::to_string(uint32_t(root.get())) +*/ "}"); }
        // better use std::ostringstream s; s << (void const *)ptr; s.str();
        //  or even usafge count from root, etc

    std::string ATTR_NOINLINE__ print() const { return uf::concat('\"', x_escape(as_view()), "\"[", len, is_writable() ? "]*" : "]"); }
};

template <bool has_refc, template <typename> typename Allocator>
inline std::string to_string(typename chunk<has_refc, Allocator>::ptr i) { return i ? std::string(*i) : ""; }

/** Clones a linked list of chunks ['begin'..'end'). It also clones the underlying sviews if not read-only.
 * If 'into' is empty, a new first element will be allocated. If not empty, the first element will be copied to
 * 'into'. The end will point to 'new_end'.
 * If into is the same as begin, then we create a new chunk in it - effectively creating a new chunk list head.
 * @returns the last chunk ('next' member of which was set to 'new_end')*/
template <bool has_refc, template <typename> typename Allocator>
inline typename chunk<has_refc, Allocator>::ptr
clone_into(typename chunk<has_refc, Allocator>::ptr& into,
           typename chunk<has_refc, Allocator>::ptr begin,
           typename chunk<has_refc, Allocator>::ptr end,
           typename chunk<has_refc, Allocator>::ptr new_end = {})
{
    assert(begin);
    assert(begin != end);
    //First create a copy of chunks with merging if possible
    //Note that 'into' may be in the middle of the 'begin'->'end'
    //chunk chain, so we will not modify it while creating this copy.
    auto out = begin->clone();
    auto start = out;
    for (auto in = begin->next; in != end; in = in->next)
        if (!out->try_append(*in)) //optimize consecutive chunks
            out->next = in->clone(), out = out->next;
    out->next = new_end;
    //Only after we have created the copy shall we modify 'begin' as 'begin may be part of the chain we have copied.
    if (into == begin || !into)
        into = std::move(start);
    else
        into->copy_from(*start);
    //Then make the (potentially merged) sviews read-only or single-owner
    for (auto c = into; c!=new_end; c = c->next)
        c->unshare();
    return out;
}

/** Helper to create a fresh copy of a linked list of chunks.
 * 'new_end' is set at the last element.*/
template <bool has_refc, template <typename> typename Allocator>
inline typename chunk<has_refc, Allocator>::ptr clone_anew(typename chunk<has_refc, Allocator>::ptr begin,
                                                           typename chunk<has_refc, Allocator>::ptr end,
                                                           typename chunk<has_refc, Allocator>::ptr new_end = {})
{
    typename chunk<has_refc, Allocator>::ptr c;
    clone_into<has_refc, Allocator>(c, begin, end, new_end);
    return c;
}

template <bool has_refc, template <typename> typename Allocator>
inline void copy_into(std::string_view what,
                      typename chunk<has_refc, Allocator>::ptr& into,
                      typename chunk<has_refc, Allocator>::ptr end = {})
{
    if (into)
        into->assign(what);
    else
        into = typename chunk<has_refc, Allocator>::ptr(typename sview<has_refc, Allocator>::ptr(what));
    into->next = end;
}

/// @return a (char*,len) pair to the whole range if it is consecutive in memory,
/// (may also be a a single empty chunk: {"",0}), or an empty optional if not consecutive
template <bool has_refc, template <typename> typename Allocator>
inline std::optional<std::string_view>
get_consecutive(typename chunk<has_refc, Allocator>::ptr from,
                typename chunk<has_refc, Allocator>::ptr const to) noexcept {
    if (!from || from==to)
        return std::string_view{};
    auto b = from->data();
    auto l = from->size();
    for (++from; from && from != to; l += from->size(), ++from)
        if (from->size() && b + l != from->data())
            return {};
    return std::string_view{b,l};
}

/// @return the size required for storing the chunks in the given range
template <bool has_refc, template <typename> typename Allocator>
inline uint32_t flatten_size(typename chunk<has_refc, Allocator>::ptr from,
                             typename chunk<has_refc, Allocator>::ptr to) noexcept
{
    return std::accumulate(std::move(from), std::move(to), uint32_t{ 0 }, [](uint32_t n, auto &i) { return n + i.size(); });
}

/// Stores chunk data starting at the given address.
/// @param buf is assumed to accomodate flatten_size(from,to) bytes
template <bool has_refc, template <typename> typename Allocator>
inline void flatten_to(typename chunk<has_refc, Allocator>::ptr from,
                       typename chunk<has_refc, Allocator>::ptr to,
                       char *buf) noexcept {
    std::for_each(std::move(from), std::move(to), [&](auto& i) { memcpy(buf, i.data(), i.size()); buf += i.size(); });
}

template <bool has_refc, template <typename> typename Allocator>
inline string_variant flatten(typename chunk<has_refc, Allocator>::ptr from,
                              typename chunk<has_refc, Allocator>::ptr to)
{
    if (auto v = get_consecutive<has_refc, Allocator>(from, to)) return *v;
    std::string ret;
    ret.resize(flatten_size<has_refc, Allocator>(from, to));
    flatten_to<has_refc, Allocator>(std::move(from), std::move(to), ret.data());
    return ret;
}

/// Checks if [from1,off1->to1) starts with the content of [from2,off2->last2,last2_off2)
template <bool has_refc, template <typename> typename Allocator>
inline bool startswidth(typename chunk<has_refc, Allocator>::ptr from1, size_t off1,
                        typename chunk<has_refc, Allocator>::ptr const to1,
                        typename chunk<has_refc, Allocator>::ptr from2, size_t off2,
                        typename chunk<has_refc, Allocator>::ptr const last2, size_t last2_off) noexcept {
    while (true) {
        assert(from2); if (!from2) return false;
        if (from2==last2) {
            if (off2>=last2_off) return true;
        } else if (from2->size()<=off2) {
            from2 = from2->next;
            off2 = 0;
            continue;
        }
        if (from1==to1) return false;
        assert(from1); if (!from1) return false;
        if (from1->size()<=off1) {
            from1 = from1->next;
            off1 = 0;
            continue;
        }
        const size_t size2 = from2==last2 ? last2_off : from2->size();
        const int len = std::min(from1->size()-off1, size2-off2);
        if (memcmp(from1->data()+off1, from2->data()+off2, len)) return false;
        off1 += len;
        off2 += len;
    }
}

/// Call the functor with each non-empty chunk
template <bool has_refc, template<typename> typename Allocator, typename F>
inline void for_nonempty(typename chunk<has_refc, Allocator>::ptr const from,
                         typename chunk<has_refc, Allocator>::ptr const to,
                         F&& f)
{ std::for_each(from, to, [f=std::move(f)](auto& i){ if (i.size()) f(i); }); }

/// Return the first non-empty chunk, or null
template <bool has_refc, template <typename> typename Allocator>
inline typename chunk<has_refc, Allocator>::ptr const
find_nonempty(typename chunk<has_refc, Allocator>::ptr const from,
              typename chunk<has_refc, Allocator>::ptr const to = {}) noexcept {
    auto i = std::find_if(from, to, [](auto& i){ return i.size(); });
    return i != to ? i : typename chunk<has_refc, Allocator>::ptr{};
}

/// Return the chunk before 'what' or null if 'what' is not on the list or equal to 'from'
/// 'what' may be equal to 'to'
template <bool has_refc, template <typename> typename Allocator>
inline typename chunk<has_refc, Allocator>::ptr
find_before(typename chunk<has_refc, Allocator>::ptr const &what,
            typename chunk<has_refc, Allocator>::ptr const& from,
            typename chunk<has_refc, Allocator>::ptr const& to = {}) noexcept
{
    auto i = std::find_if(from, to, [&what](auto& c) { return what == c.next; });
    return i != to ? i : typename chunk<has_refc, Allocator>::ptr{};
}

/** Find the chunk+offset that is 'off' bytes further.
 * Return true if we have run out of chunks - in that case the value in 'ch_off' is undetermined.
 * It is also possible to have a ch_off.second that is already pointing beyond the end of ch_off.first.*/
template <bool has_refc, template <typename> typename Allocator>
inline bool advance(typename std::pair<typename chunk<has_refc, Allocator>::ptr, uint32_t>& ch_off,
                    uint32_t off,
                    typename chunk<has_refc, Allocator>::ptr const& to = {})
{
    assert(ch_off.first);
    ch_off.second += off;
    while (ch_off.first != to && ch_off.first->size() <= off)
        ch_off.second -= ch_off.first->size(), ch_off.first = ch_off.first->next;
    return ch_off.first == to;
}

/// Append a comma separated text rep of the chunks.
template <bool has_refc, template <typename> typename Allocator>
inline void append_to(std::string& out, typename chunk<has_refc, Allocator>::ptr const from, typename chunk<has_refc, Allocator>::ptr const to = {}) {
    bool first = true;
    for_nonempty<has_refc, Allocator>(from, to, [&](auto& i) { if (!first) out.append(", "); out.append(std::string(i)); first = false; });
}
/// Append a -> separated print() of the chunks.
template <bool has_refc, template <typename> typename Allocator>
inline void append_to_print(std::string& out, typename chunk<has_refc, Allocator>::ptr const from, typename chunk<has_refc, Allocator>::ptr const to = {}) {
    bool first = true;
    for_nonempty<has_refc, Allocator>(from, to, [&](auto& i) { if (!first) out.append("->"); out.append(i.print()); first = false; });
}

/** Chop up a chunk such that the part identified by off/len is in a chunk of its own.
 * We keep the split sub-chunks properly linked.
 * @Returns the new chunk. We assert that off/len is fully inside 'c'.*/
template <bool has_refc, template <typename> typename Allocator>
typename chunk<has_refc, Allocator>::ptr split(typename chunk<has_refc, Allocator>::ptr c, uint32_t off, uint32_t len) {
    assert(c);
    assert(off + len <= c->size());

    // Four cases:
    if (off + len >= c->size()) { //The element is aligned to the end of 'c'
        if (off) { //The element is aligned to the end, but not at the beginning
            auto elem = c->sub_chunk(off, len);
            elem->next = c->next;
            c->resize(off).next = elem;
            return elem;
        } else //The element is the whole of 'c'
            return c;
    } else { //The element is not aligned to the end
        if (off == 0) { // the element is at the beginning of this chunk
            auto rest = c->sub_chunk(len);
            rest->next = c->next;
            c->resize(len).next = rest;
            return c;
        } else { //there is unparsed stuff both before and after the new elem
            auto elem = c->sub_chunk(off, len);
            auto rest = c->sub_chunk(off + len);
            elem->next = rest;
            rest->next = c->next;
            c->resize(off).next = elem;
            return elem;
        }
    }
}

/** Split a chunk into two.
 * If off==size(), nothing happens and we return c->next (even if off==0)
 * If off==0, but size()>0 nothing happens and we return c.
 * We keep the split sub-chunks properly linked.
 * @Returns the new chunk. We assert that off<size().*/
template <bool has_refc, template <typename> typename Allocator>
typename chunk<has_refc, Allocator>::ptr split(typename chunk<has_refc, Allocator>::ptr c, uint32_t off)
{
    assert(c);
    assert(off <= c->size());
    if (off == c->size()) return c->next;
    if (!off) return c;
    auto ret = c->sub_chunk(off);
    ret->next = std::move(c->next);
    c->resize(off);
    c->next = ret;
    return ret;
}

/** Inserts a zero length chunk between 'c' and 'c->next'.*/
template <bool has_refc, template <typename> typename Allocator>
typename chunk<has_refc, Allocator>::ptr insert_empty_chunk_after(typename chunk<has_refc, Allocator>::ptr c)
{
    typename chunk<has_refc, Allocator>::ptr ret(0);
    ret->next = std::move(c->next);
    c->next = ret;
    return ret;
}

/** Returns the first 'n' member of a tuple type.
 * if n==0 we return 't'.
 * if n==1 we allow 't' to be a non-tuple type (and return 't'),
 * but for tuple types, we return the first member.
 * For n>1 we return the concatenated types of the first n members.
 * @returns a human readable text on error.*/
inline std::pair<std::string_view, std::string> parse_tuple_type(std::string_view t, int n) {
    if (t.empty()) return{{}, "Empty type."};
    if (n<0) return {{}, uf::concat("Negative number of requested elements: ", n)};
    if (n==0) return {t, {}};
    if (t.front()!='t') {
        if (n==1) return {t, {}};
        return {{}, "Non-tuple type."};
    }
    t.remove_prefix(1);
    int size = 0;
    while (t.size() && '0'<=t.front() && t.front()<='9') {
        size = size*10 + t.front() - '0';
        t.remove_prefix(1);
    }
    if (size<2) return {{}, std::string{ser_error_str(ser::num)}};
    if (n>size) return {{}, uf::concat("Tuple of size ", size, " too small for requested ", n, " elements.")};
    std::string_view ret = t;
    size_t len = 0;
    if (n==0) n = 1;
    while (n--)
        if (auto [l, problem] = uf::impl::parse_type(t, false); !problem) {
            t.remove_prefix(l);
            len += l;
        } else
            return {{}, std::string{ser_error_str(problem)}};
    return {ret.substr(0, len), {}};
}

/// A writable view of a serialized uf:: something
template <bool has_refc, template <typename> typename Allocator= std::allocator>
class wview : private std::conditional_t<has_refc, RefCount, NoRefCount>{
    using sview_ptr = typename sview<has_refc, Allocator>::ptr;
    using chunk_ptr = typename chunk<has_refc, Allocator>::ptr;
public:
    class ptr : public shared_ref_base<wview, has_refc, Allocator>
    {
        using shared_ref_base<wview, has_refc, Allocator>::p;
    public:
        using shared_ref_base<wview, has_refc, Allocator>::shared_ref_base;
        using shared_ref_base<wview, has_refc, Allocator>::clear;

        /** This constructor is technically public, since we need it when emplacing to 'children'
         * Do not use otherwise.*/
        explicit ptr(chunk_ptr&& tb, chunk_ptr&& te,
                     chunk_ptr&& vb, const chunk_ptr& ve, wview *parent)
        { p = new(Allocator<char>().allocate(sizeof(wview))) wview(std::move(tb), std::move(te), std::move(vb), ve, parent); } //noexcept wview ctor
        /** Construct a wview from a read-only or writable type/value pair.
         * Note that sview::ptr can be initialized from std::string_view or std::string{&,&&} with
         * an optional second bool parameter, which dictates if we copy the data or the lifetime of
         * the provided object outlives the wview created. (Defaults to true, meaning copy.)
         * For std::string&& no second parameter is possible, we make a copy in any case.
         * For (std::string&, false) the memory provided will be written in case the wview is modified.
         * (But it no longer may be used as a serialized value or a coherent typestring, it becomes
         *  essentially random.)*/
        explicit ptr(sview_ptr &&t, sview_ptr &&v) {
            char* mem = Allocator<char>().allocate(sizeof(wview));
            try { p = new(mem) wview(std::move(t), std::move(v)); }
            catch (...) { Allocator<char>().deallocate(mem, sizeof(wview)); throw; }
        }
        /** Construct a wview from a read-only or writable serialized any.
         * Note that sview::ptr can be initialized from std::string_view or std::string{&,&&} with
         * an optional second bool parameter, which dictates if we copy the data or the lifetime of
         * the provided object outlives the wview created. (Defaults to true, meaning copy.)
         * For std::string&& no second parameter is possible, we make a copy in any case.
         * For (std::string&, false) the memory provided will be written in case the wview is modified.
         * (But it no longer may be used as a serialized value or a coherent typestring, it becomes
         *  essentially random.)*/
        explicit ptr(sview_ptr&& raw) {
            char* mem = Allocator<char>().allocate(sizeof(wview));
            try { p = new(mem) wview(std::move(raw)); }
            catch (...) { Allocator<char>().deallocate(mem, sizeof(wview)); throw; }
        }
        /** Constructs a wview by serializing a C++ type.*/
        template<typename T>
        explicit ptr(T const& t) {
            static_assert(is_serializable_v<T>);
            char* mem = Allocator<char>().allocate(sizeof(wview));
            try { p = new(mem) wview(t); }
            catch (...) { Allocator<char>().deallocate(mem, sizeof(wview)); throw; }
        }
        /** Construct a wview from an any. If you write anything in the resulting wview,
         * the memory area of the 'any' may be overwritten rendering the 'any' invalid.
         * Thus do not use the supplied 'any' after this ctor - but keep it allocated
         * as the resulting wview refers to it. The lifetime of the supplied 'any'
         * (even if it enters an undefined, but destoryable state) shall be longer than
         * that of the resulting wview.
         * This is done to save on memory allocations and copies. If you want the
         * 'any' to remain utouched, use a const ref or an any_view.*/
        explicit ptr(uf::from_raw_t, uf::any& a)
            : ptr(sview_ptr(a.type().size(),  const_cast<char*>(a.type().data()),  false),
                  sview_ptr(a.value().size(), const_cast<char*>(a.value().data()), false)) {}

        /** Construct a wview from an any_view. This is a no-copy operation.
         * The lifetime of the resulting wview shall be shorter than that of 'a'.*/
        explicit ptr(uf::from_raw_t, const uf::any_view& a)
            : ptr(sview_ptr(a.type(),  false),
                  sview_ptr(a.value(), false)) {}

        ptr() noexcept = default;
        ptr(const ptr&) noexcept = default;
        ptr(ptr&&) noexcept = default;
        /** To avoid wv1[0] = wv2 type of operations.
         * These are a no-op (assigning one iterator to a temporary iterator)
         * and are almost always 'intend wv1[0].set(wv2)'. */
        ptr&& operator =(const ptr&) && = delete;
        ptr& operator =(const ptr&) & noexcept = default;
        ptr& operator =(ptr&&) & noexcept = default;

        bool is_same_as(const ptr& o) const noexcept { return p == o.p; }

        /** Returns our type character or zero if empty.*/
        char typechar() const noexcept { return p ? p->typechar() : 0; }
        /** Returns our flattened typestring. */
        string_variant type() const { return p ? p->type() : string_variant(std::string_view()); }
        /** Returns our flattened serialized value. */
        string_variant value() const { return p ? p->value() : string_variant(std::string_view()); }
        /// @return view into the value IFF consecutive, else empty optional
        std::optional<std::string_view> get_consecutive_value() const noexcept { return p ? get_consecutive<has_refc, Allocator>(p->vbegin, p->vend) : std::string_view{}; }
        /** Returns how many contained elements we have. For tuples it is the tuple size,
         * for lm it is the number of elements in the list/map. For optionals it is 0/1 depending on
         * if the optional has a value or not. For expected and any, it is always 1. For the
         * rest of the primitives (including string) it is always zero.*/
        uint32_t size() const { return p ? p->size() : 0; }
        /** Returns our index in our parent or none if we have no parent.*/
        std::optional<uint32_t> indexof() const noexcept {
            if (p && p->parent)
                return p->parent->indexof(*this);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
            return {}; //Safely ignore "'<anonymous>' may be used uninitialized in this function" warnings. See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80635
#pragma GCC diagnostic pop
        }
        /** Try to extract the content of us into a C++ value.
         * We throw uf::type_mismatch_error if we cannot. No conversion
         * For uf::any or uf::any_view consider using as_any() instead.*/
        template<typename T>
        auto get_as(uf::serpolicy convpolicy = uf::allow_converting_all) const {
            static_assert(!std::is_reference<T>::value);
            static_assert(!std::is_const<T>::value);
            static_assert(!std::is_volatile<T>::value);
            assert(p);
            return p->template get_as<T>(convpolicy);
        }
        /** Flatten both type and value into an uf::any/uf::any_view. If both type and value
         * are in consecutive chunks, we return an uf::any_view, else we copy to an uf::any.*/
        any_variant as_any() const { assert(p); return p->as_any(); }
        /** Assuming the wview contains a string, we return either a string_view or string
         * depending on if the value is in one chunk. If wview is not a string we throw
         * an uf::type_mismatch_error.*/
        string_variant as_string() const { assert(p); return p->as_string(); }
        /** Prints the chunks in a more verbose way. For debug only.*/
        operator std::string() const { return p ? std::string(*p) : std::string("<empty>"); }
        /** Prints the chunks in a less verbose way. For debug only.*/
        [[nodiscard]] std::string print() const { return p ? p->print() : std::string("<empty>");; }

        /** How long would our type be when flattened? */
        uint32_t flatten_type_size() const noexcept { return p ? p->flatten_type_size() : 0; }
        /** How long would our value be when flattened? */
        uint32_t flatten_size() const noexcept { return p ? p->flatten_size() : 0; }
        /** Flatten our bytes to a pre-allocated buffer of appropriate size. */
        void flatten_to(char* buf) const { if (p) p->flatten_to(buf); }

        /** Get a wview to one of our constitutent. 'idx' starts at zero and
         *  must be lower than size() (or we throw std::out_of_range).
         * For types having no constitutent, we throw uf::type_mismatch_error.*/
        ptr operator[](uint32_t idx) const { return p ? p->operator[](idx) : ptr{}; }

        /** Set the value pointed to by us to the content of another wview. We make a copy,
         * so there is no link remaining between the wview we change and 'o', so chaning 'o'
         * will have no effect on 'this' or its parents.
         * If we have wviews to any of our constitutent elements, we break any link with them:
         * any such wviews continue to hold their value, but changes to them will have no effect
         * on 'this' or any parent of it.
         * If a type change is not possible, but needed we throw an uf::type_mismatch_error.*/
        ptr& set(const ptr& o) & { assert(p); if (o) p->set(*o); else clear();  return *this; }
        /** Set the value pointed to by us to the content of another wview. We make a copy,
         * so there is no link remaining between the wview we change and 'o', so chaning 'o'
         * will have no effect on 'this' or its parents.
         * If we have wviews to any of our constitutent elements, we break any link with them:
         * any such wviews continue to hold their value, but changes to them will have no effect
         * on 'this' or any parent of it.
         * If a type change is not possible, but needed we throw an uf::type_mismatch_error.*/
        ptr&& set(const ptr& o) && { assert(p); if (o) p->set(*o); else clear();  return std::move(*this); }
        /** Set the value pointed to by us to a new type and value.
         * If we have wviews to any of our constitutent elements, we break any link with them:
         * any such wviews continue to hold their value, but changes to them will have no effect
         * on 'this' or any parent of it.
         * If a type change is not possible, but needed we throw an uf::type_mismatch_error.*/
        template<typename T>
        ptr& set(T const& t) & {
            static_assert(is_serializable_v<T>);
            assert(p);
            p->set(uf::serialize_type(t), uf::serialize(t));
            return *this;
        }
        /** Set the value pointed to by us to a new type and value.
         * If we have wviews to any of our constitutent elements, we break any link with them:
         * any such wviews continue to hold their value, but changes to them will have no effect
         * on 'this' or any parent of it.
         * If a type change is not possible, but needed we throw an uf::type_mismatch_error.*/
        template<typename T>
        ptr&& set(T const& t)&& {
            static_assert(is_serializable_v<T>);
            assert(p);
            p->set(uf::serialize_type(t), uf::serialize(t));
            return std::move(*this);
        }
        /** Set the value pointed to by us to a new type and value.
         * If we have wviews to any of our constitutent elements, we break any link with them:
         * any such wviews continue to hold their value, but changes to them will have no effect
         * on 'this' or any parent of it.
         * If a type change is not possible, we throw an uf::type_mismatch_error.*/
        ptr& set(std::string_view type, std::string_view value) & { assert(p); p->set(type, value); return *this; }
        /** Set the value pointed to by us to a new type and value.
         * If we have wviews to any of our constitutent elements, we break any link with them:
         * any such wviews continue to hold their value, but changes to them will have no effect
         * on 'this' or any parent of it.
         * If a type change is not possible, but needed we throw an uf::type_mismatch_error.*/
        ptr&& set(std::string_view type, std::string_view value) && { assert(p); p->set(type, value); return std::move(*this); }
        /** Set the value pointed to by us to void.
         * If we have wviews to any of our constitutent elements, we break any link with them:
         * any such wviews continue to hold their value, but changes to them will have no effect
         * on 'this' or any parent of it.
         * If a type change is not possible, but needed we throw an uf::type_mismatch_error.*/
        ptr& set_void() & { return set({}, {}); }
        /** Set the value pointed to by us to void.
         * If we have wviews to any of our constituent elements, we break any link with them:
         * any such wviews continue to hold their value, but changes to them will have no effect
         * on 'this' or any parent of it.
         * If a type change is not possible, but needed we throw an uf::type_mismatch_error.*/
        ptr&& set_void() && { return std::move(*this).set({}, {}); }

        /** Erase one of our constitutent.
         * We throw an std::out_of_range if 'idx' is >= size().
         * If this incurs a type change (for tuples), and that is not possible, we throw
         * an uf::type_mismatch_error.*/
        void erase(uint32_t idx) {
            if (!p) throw std::out_of_range("Cannot erase from empty wview.");
            int32_t cindex;
            try {
                auto ci = p->cindexof(operator[](idx));
                assert(ci);
                if (!ci) return;
                cindex = *ci;
            } catch (const uf::type_mismatch_error& e) {
                throw uf::type_mismatch_error("erase() is not valid for type <%1>.", type().as_view(), {});
            } catch (const std::out_of_range & e) {
                throw std::out_of_range(uf::concat("Index (", idx, ") out of range [0..", size() - 1, "] in erase() for type <", type().as_view(), ">."));
            }
            if (p->do_erase(cindex))
                throw uf::type_mismatch_error("Cannot erase a child of <%1>.", type(), {});
        }
        /** Erase one of our constitutent.
         * If 'what' is not a member of us, we throw std::invalid_argument.
         * If this incurs a type change (for tuples), and that is not possible, we throw
         * an uf::type_mismatch_error.*/
        void erase(const ptr &what) {
            if (p)
                if (auto cindex = p->cindexof(what)) {
                    if (p->do_erase(*cindex))
                        throw uf::type_mismatch_error("Cannot erase a child of <%1>.", type(), {});
                    return;
                }
            throw std::invalid_argument("Wview to erase is not my child.");
        }

        /** Insert one more constitutent after 'idx'. To insert to the beginning
         * use any negative value for 'idx'.
         * We throw an std::out_of_range if 'idx' is >= size().
         * If this incurs a type change (for tuples), and that is not possible, we throw
         * an uf::type_mismatch_error.
         * If the type of what is not appropriate (for 'lmo') we also throw an uf::type_mismatch_error.*/
        void insert_after(int32_t idx, const ptr &what) {
            if (!p) throw std::out_of_range("Cannot insert to empty wview.");
            int32_t cindex = -1;
            if (idx>=0) try {
                auto ci = p->cindexof(operator[](idx));
                assert(ci);
                if (!ci) return;
                cindex = *ci;
            } catch (const uf::type_mismatch_error & e) {
                throw uf::type_mismatch_error("insert_after() is not valid for type <%1>.", type().as_view(), {});
            } catch (const std::out_of_range & e) {
                throw std::out_of_range(uf::concat("Index (", idx, ") out of range [0..", size()-1, "] in insert_after() for type <", type().as_view(), ">."));
            }
            if (p->do_insert_after(cindex, *what))
                throw uf::type_mismatch_error("Cannot insert a child into <%1>.", type(), {});
        }
        /** Insert one more constitutent after 'where'.
         * We throw an std::invalid_argument if 'where' is not a member of us.
         * If this incurs a type change (for tuples), and that is not possible, we throw
         * an uf::type_mismatch_error.
         * If the type of what is not appropriate (for 'lmo') we also throw an uf::type_mismatch_error.*/
        void insert_after(const ptr& where, const ptr& what)
        {
            if (!p) throw std::out_of_range("Cannot insert to empty wview.");
            if (auto cindex = p->cindexof(where)) {
                if (p->do_insert_after(*cindex, *what))
                    throw uf::type_mismatch_error("Cannot insert into a <%1>.", type(), {});
                return;
            }
            throw std::invalid_argument("Wview to insert after is not my child.");
        }

        /** Creates a copy of the current wview, with creating new chunks.
         * @returns a wview with no parents that can be modified without
         * modifying 'this'.*/
        ptr clone() const {
            return p ? ptr{ impl::clone_anew<has_refc, Allocator>(p->tbegin, p->tend), {},
                            impl::clone_anew<has_refc, Allocator>(p->vbegin, p->vend), {}, nullptr } :
                ptr{};
        }

        /** Create a wview containing an optional with the value (and type) provided.
         * We copy 'o' so it will not be linked to the result in any way.
         * For void 'o' we return null.*/
        static ptr create_optional_from(const ptr& o)
        {
            if (!o.typechar()) return {};
            chunk_ptr tb = chunk_ptr(sview_ptr("o", true));
            chunk_ptr vb = chunk_ptr(sview_ptr(std::string_view("\x1", 1), true));
            clone_into<has_refc, Allocator>(tb->next, o->tbegin, o->tend);
            clone_into<has_refc, Allocator>(vb->next, o->vbegin, o->vend);
            return ptr(std::move(tb), chunk_ptr(), std::move(vb), chunk_ptr(), nullptr);
        }

        /** Create a wview containing an expected with the value (and type) provided.
         * We copy 'o' so it will not be linked to the result in any way.*/
        static ptr create_expected_from(const ptr& o)
        {
            chunk_ptr vb = chunk_ptr(sview_ptr(std::string_view("\x1", 1), true));
            chunk_ptr tb;
            if (o.typechar()) {
                tb = chunk_ptr(sview_ptr("x", true));
                clone_into<has_refc, Allocator>(tb->next, o->tbegin, o->tend);
                clone_into<has_refc, Allocator>(vb->next, o->vbegin, o->vend);
            } else
                tb = chunk_ptr(sview_ptr("X", true));
            return ptr(std::move(tb), chunk_ptr(), std::move(vb), chunk_ptr(), nullptr);
        }

        /** Create a wview containing an expected with the error provided.
         * We copy 'o' so it will not be linked to the result in any way.
         * @param [in] o An wview containing an error.
         * @param [in] type The type of the expected (without the leading 'x').
         * @returns null, if 'o' does not contain an error.*/
        static ptr create_expected_from_error(const ptr& o, std::string_view type)
        {
            if (o.typechar() != 'e') return {};
            chunk_ptr tb;
            if (type.length()) {
                tb = chunk_ptr(sview_ptr("x", true));
                copy_into<has_refc, Allocator>(type, tb->next);
            } else
                tb = chunk_ptr(sview_ptr("X", true));
            chunk_ptr vb = chunk_ptr(sview_ptr(std::string_view("\x0", 1), true));
            clone_into<has_refc, Allocator>(vb->next, o->vbegin, o->vend);
            return ptr(std::move(tb), chunk_ptr(), std::move(vb), chunk_ptr(), nullptr);
        }

        /** Creates an 'e' error wview. The last parameter can be omitted. If not,
         * it will be cloned into the result and unlinked from 'o'.
         * Also the string views will be taken a snapshot of and copied.*/
        static ptr create_error(std::string_view type, std::string_view message, const ptr &o = {})
        {
            sview_ptr t(uf::impl::serialize_len(   type));
            sview_ptr m(uf::impl::serialize_len(message));
            char* tp = t->data_writable();
            char* mp = m->data_writable();
            uf::impl::serialize_to(type, tp);
            uf::impl::serialize_to(message, mp);

            chunk_ptr vb = chunk_ptr(std::move(t)), vbegin = vb;
            vb = vb->next = chunk_ptr(std::move(m));
            if (o.typechar()) {
                vb = vb->next = chunk_ptr(4);
                char* p = vb->data_writable();
                uf::impl::serialize_to(o->flatten_type_size(), p);
                vb = clone_into<has_refc, Allocator>(vb->next, o->tbegin, o->tend);
                vb = vb->next = chunk_ptr(4);
                p = vb->data_writable();
                uf::impl::serialize_to(o->flatten_size(), p);
                clone_into<has_refc, Allocator>(vb->next, o->vbegin, o->vend);
            } else {
                vb = vb->next = chunk_ptr(8);
                memset(vb->data_writable(), 0, 8);
            }
            return ptr(chunk_ptr(sview_ptr("e", true)), {}, std::move(vbegin), {}, nullptr);
        }

        /** Create a wview containing a tuple with the values (and types) provided.
         * We copy the incoming wviews so it will not be linked to the result in any way.
         * void or empty incoming wviews will be ignored. If we receive zero non-void wviews
         * we return void. If we receive one non-void wview, we return an unlinked
         * copy of it.*/
        static ptr create_tuple_from(const std::vector<wview::ptr>& o)
        {
            const uint32_t size = std::count_if(o.begin(), o.end(), [](const wview::ptr& w) { return w.typechar(); });
            if (size == 0) return ptr{ sview_ptr{""}, sview_ptr{""} };
            if (size == 1) return std::find_if(o.begin(), o.end(), [](const wview::ptr& w) { return w.typechar(); })->
                clone();
            //flatten the type into a single chunk - probably all sub-types fit into SSO
            std::string type = uf::concat('t', size);
            for (const ptr& w : o) //void types will automatically not append anything
                type.append(w.type().as_view());
            chunk_ptr tbegin(sview_ptr(std::move(type)));
            //Clone the values, start from the last
            chunk_ptr vbegin;
            for (size_t i = o.size(); i; --i)
                if (o[i-1].typechar())
                    vbegin = clone_anew<has_refc, Allocator>(o[i-1]->vbegin, o[i-1]->vend, vbegin);
            return ptr(std::move(tbegin), {}, std::move(vbegin), {}, nullptr);
        }

        /** Swap the content with that of 'w'.
         * E.g., this allows to set a value in a tuple or list, but get the previous value out.
         * We assert if either of us is empty.
         * @exception uf::type_mismatch_error we cannot set one of the wviews because its parent limits the type.
         * @exception uf::api_error 'this' and 'w' are in ancestor/descendant relation. */
        void swap_content_with(const ptr& w) const
        {
            assert(p && w.p);
            if (w.p == p) return;
            //Dont swap with your parent/child
            for (auto i = p->parent; i; i = i->parent)
                if (i == w.p)
                    throw uf::api_error("Cannot swap with an ancestor.");
            for (auto i = w->parent; i; i = i->parent)
                if (i == p)
                    throw uf::api_error("Cannot swap with a descendant.");
            const string_variant t1 = type(), t2 = w.p->type();
            const bool type_changed =
                p->check_type_change(t2.as_view(), "Swap: cannot set first (element of <%1>) to second (of type <%2>).") &&
                w.p->check_type_change(t1.as_view(), "Swap: cannot set second (element of <%1>) to first (of type <%2>).");

            //Swap the 'next' of the last chunk in both type and value.
            if (type_changed) {
                chunk_ptr last1 = find_before<has_refc, Allocator>(  p->tend,   p->tbegin,   p->tend);
                chunk_ptr last2 = find_before<has_refc, Allocator>(w.p->tend, w.p->tbegin, w.p->tend);
                std::swap(last1->next, last2->next);
            }
            chunk_ptr last1 = find_before<has_refc, Allocator>(  p->vend,   p->vbegin,   p->vend);
            chunk_ptr last2 = find_before<has_refc, Allocator>(w.p->vend, w.p->vbegin, w.p->vend);
            std::swap(last1->next, last2->next);
            //Note that the first chunk of us and p must remain the same object (elements prior point to them)
            //both for type and value chunks. So there we swap the content of the chunks.
            if (type_changed)
                p->tbegin->swap_content_with(*w.p->tbegin);
            p->vbegin->swap_content_with(*w.p->vbegin);
            //Dont swap the tends. Those point to a chunk not affected by the swap.
            //Keep parsed children alive. Note: no children uses our tbegin or vbegin, so it safe
            std::swap(p->children, w.p->children);
            //And we keep our parents intact. Swapping content keeps
            assert(p->check(LOC));
            assert(w->check(LOC));
        }

        /** Fast searching in lists in maps. We do not create children, only for
         * the element found. For lists, we can only serarch by the beginning of the
         * serialized value of the elements of 'l', for maps we only search by the
         * beginning of the key value of 'm'.
         * Only exact type matches expected and only exact value matches are found.
         * For this to work:
         * - 'this' has to be a list of tuples T1 or a map with key type T1
         * - 't' has to be a tuple T2
         * - the first 'n' fields of T1 and T2 must be the same.
         * Alternatively if n==1, then either T1 or T2 is allowed to be a non tuple.
         * Alternatively, if n==0, all of 't' is matched against the first member
         * of 'this's content (for lists) or key (for maps) (or all its content/key if not a tuple).
         * For example
         * this=lt3iis, t=t2ii, n=2 will search by two integers.
         * this=lt3iis, t=t32iid, n=2 will search by two integers. The rest of t is igonored
         * this=lt2is, t=t2is, n=1 will search by the integer index.
         * this=lt2is, t=t2is, n=0 will search by the integer+string index.
         * this=lt2is, t=i, n=1 or0  will search by the integer index.
         * this=mt3iisX, t=t2ii, n=2 will search by two integers.
         * this=mt3iisX, t=t32iid, n=2 will search by two integers. The rest of t is igonored
         * this=mt2isX, t=t2is, n=1 will search by the integer index.
         * this=mt2isX, t=t2is, n=0 will search by the integer+string index.
         * this=mt2isX, t=i, n=1 or 0  will search by the integer index.
         *   or the simplest case of maps:
         * this=msX, t=s, n=1 or 0, will search by the string index
         * @returns Human readable string on type mismatch; empty ptr & string
         * if not found, empty string and set ptr for the first element found.*/
        std::pair<ptr, std::string> linear_search(const ptr &t, int n) const {
            if (!p || n<0) return {};
            return p->linear_search(t, n);
        }

        /** Throws a value_mismatch_error if there is a problem with the internal representation. */
        void check(std::string_view loc = "???") const { if (p) p->check(loc); }
    };
private:
    static constexpr size_t get_memsize() noexcept { return sizeof(wview); }
    friend class shared_ref_base<wview, has_refc, Allocator>;
    chunk_ptr tbegin;///< points to the (first) chunk holding the uf type string
    chunk_ptr tend;///< points to the first chunk following type chunk(s)
    chunk_ptr vbegin;///< the first chunk holding the serialized value
    chunk_ptr vend;///< guess
    wview* parent = nullptr;
    using child = std::pair<uint32_t, wview::ptr>;
    friend bool operator<(const child& a, const child& b) { return a.first < b.first; }
    friend bool operator<(const child& a, uint32_t b) { return a.first < b; }
    std::vector<child, Allocator<child>> children;///< already parsed child wviews
public:
    wview() noexcept = delete;
    wview(const wview&) = delete; ///non-movable, non copyable, so that its parent keeps knowing about it.
    wview(wview&&) noexcept = delete;
    //wview& operator =(const wview&) = default; //but assignable (?)
    //wview& operator =(wview&&) noexcept = default;
    ~wview() { disown_children(true); }

    explicit wview(chunk_ptr &&tb, chunk_ptr &&te,
                   chunk_ptr &&vb, const chunk_ptr &ve, wview *p) noexcept
        : tbegin(std::move(tb)), tend(std::move(te)), vbegin(std::move(vb)), vend(ve), parent(p)
    { assert(check(LOC)); }

    /// Initialize a view with a type string and a value string
    explicit wview(sview_ptr &&t, sview_ptr &&v)
        : tbegin(std::move(t)), vbegin(std::move(v))
    {
        auto [err, tlen, vlen] = serialize_scan_by_type(tbegin->as_view(), vbegin->as_view(), false, true); //Test if type matches the value
        (void)tlen, (void)vlen;
        if (err) err->throw_me();
    }

    /// Initialize a view with a serialized any (tlen-type-vlen-value)
    explicit wview(sview_ptr &&raw) {
        const uf::any_view a(from_raw, raw->as_view()); //Tests if type matches the value
        tbegin = chunk_ptr(a.type().data(), a.type().length(), sview_ptr(raw));
        vbegin = chunk_ptr(a.value().data(), a.value().length(), sview_ptr(raw));
    }

    /// Create a view from any appropriate type
    template<typename T>
    explicit wview(T const& t) : tbegin{ chunk_ptr(sview_ptr(serialize_type<T>(), serialize_type<T>().length()<8))}
    {
        static_assert(is_serializable_v<T>);
        serialize([&](auto l) { vbegin = chunk_ptr(l); return vbegin->data_writable(); }, t);
        assert(check(LOC));
    }
private:

    //Go and check from the top wview
    bool check(std::string_view loc) const {
        if (parent) return parent->check(loc);
        else return check_me(loc);
    }

    //check this wview and its children
    bool check_me(std::string_view loc) const {
        //Check the type first
        //basic checks
        if (!tbegin) throw uf::value_mismatch_error(uf::concat("wany: empty tbegin at ", loc));
        if (tbegin == tend) throw uf::value_mismatch_error(uf::concat("wany: tbegin==tend at ", loc));
        if (tbegin->size() == 0 && tbegin->next != tend)
            throw uf::value_mismatch_error(uf::concat("wany: tbegin->next!=tend for void at ", loc)); //types cannot start empty, but for void
        //Check that tend is on the path from tbegin->null
        for (auto i = tbegin; i != tend; ++i)
            if (!i) throw uf::value_mismatch_error(uf::concat("wany: linked list error in type at ", loc));
        //Check the value
        if (!vbegin) throw uf::value_mismatch_error(uf::concat("wany: empty vbegin at ", loc), type().as_view());
        if (vbegin == vend) throw uf::value_mismatch_error(uf::concat("wany: vbegin==vend at ", loc), type().as_view());
        if (vbegin->size() == 0 && vbegin->next != vend && typechar() != 't' && typechar() != 'e')
            throw uf::value_mismatch_error(uf::concat("wany: vbegin->next!=vend for empty value at ", loc), type().as_view()); //values can start empty for void or tuple/error
        //Check that t/vend is on the path from t/vbegin->null
        for (auto i = vbegin; i != vend; ++i)
            if (!i) throw uf::value_mismatch_error(uf::concat("wany: linked list error in value at ", loc), type().as_view());
        //Check that all our children are valid (before we flatten)
        for (auto &[index, wv] : children)
            if (!wv) throw uf::value_mismatch_error(uf::concat("wany: empty child of index #", index, " at ", loc), type().as_view());
            else try { wv->check_me(LOC); } catch (uf::value_error &e) { e.append_msg(uf::concat("\nfrom ", loc, " index #", index, " of ", print())); throw; }
        if (!std::is_sorted(children.begin(), children.end())) {
            std::string s;
            for (const child &c : children)
                s += uf::concat(' ', c.first, ':', c.second.print());
            throw uf::value_mismatch_error(uf::concat("wany: unsordted children list", s, " at ", loc), type().as_view());
        }
        //Check that value is deserializable with type
        auto t = type(), v = value();
        const char *p = v.as_view().data(), *end = p + v.as_view().size();
        std::string_view ty = t.as_view();
        if (auto err = serialize_scan_by_type_from(ty, p, end, true))
            err->prepend_type0(t.as_view(), ty).append_msg(" (wany)").throw_me();
        return true;
    }

    /// Character code of the outmost type
    char typechar() const noexcept { if (auto &c = tbegin; c && c->size()) return c->data()[0]; return 0; }

    /// Return (a view into) the typestring
    /// May copy.
    string_variant type() const { return flatten<has_refc, Allocator>(tbegin, tend); }

    /// Return (a view into) the serialized value
    /// May copy.
    string_variant value() const { return flatten<has_refc, Allocator>(vbegin, vend); }

    /// Return number of elements; 0 may mean that the we are not a container type
    uint32_t size() const noexcept {
        switch (typechar()) {
        default: return 0;
        case 'a':
        case 'x':
        case 'X': return 1;
        case 'e': return 3;
        case 'o': if (auto v = vbegin; v && v->size()) return bool(*v->data());
                  else { assert(0); return 0; }
        case 'l':
        case 'm': if (auto v = vbegin; v && v->size()>=4) return deserialize_as<uint32_t>({ v->data(), 4 });
                  else { assert(0); return 0; }
        case 't':
            //We also need to ensure that after this call the t<num> header is in a single chunk at the beginning
            uint32_t size = 0;
            std::string_view t = tbegin->as_view();
            for (uint32_t i = 1; i < t.length(); i++)
                if ('0' <= t[i] && t[i] <= '9')
                    size = size * 10 + t[i] - '0';
                else {
                    //OK, full header is in one chunk, split it off
                    split<has_refc, Allocator>(tbegin, i);
                    return size;
                }
            //We have parsed all of the first chunk.
            assert(tbegin->next != tend); //or it is just a truncated type
            assert(tbegin->next->size()); //no empty chunks in type
            assert(!isdigit(tbegin->next->data()[0])); //number continues in another chunk!
            return size;
        }
    }

    /// Typized getter
    template<typename T>
    auto get_as(uf::serpolicy convpolicy=uf::allow_converting_all) const {
        static_assert(!uf::is_deserializable_view_v<T> || uf::is_deserializable_v<T>, "Cannot deserialize a wview to a view type. (Value may be in fragmented chunks.)");
        static_assert(uf::is_deserializable_v<T>, "Cannot deserialize into this type.");
        if constexpr (uf::is_deserializable_v<T>) {
            if constexpr (std::is_same<T, uf::any>::value) {
                auto av = as_any();
                return std::holds_alternative<uf::any>(av) ? std::move(std::get<uf::any>(av)) : uf::any{ av.as_view() };
            } else
                return uf::any_view(from_type_value, type().as_view(), value().as_view()).template get_as<std::remove_cvref_t<T>>(convpolicy);
        }
    }

    string_variant as_string() const
    {
        if (typechar() != 's')
            throw uf::type_mismatch_error("Cannot get from wview holding <%1> into a string.", type().as_view(), "s");
        string_variant ret = value();
        assert(ret.size() >= 4);
        assert(deserialize_as<uint32_t>(ret, true) == ret.size()-4);
        if (std::holds_alternative<std::string_view>(ret))
            std::get<std::string_view>(ret).remove_prefix(4);
        else
            std::get<std::string>(ret).erase(0, 4);
        return ret;
    }

    any_variant as_any() const
    {
        string_variant t = type(), v = value();
        if (std::holds_alternative<std::string_view>(t)) {
            if (std::holds_alternative<std::string_view>(v))
                return uf::any_view(uf::from_type_value, std::get<std::string_view>(t), std::get<std::string_view>(v));
            else
                return uf::any(uf::from_type_value, std::string(std::get<std::string_view>(t)), std::move(std::get<std::string>(v)));
        } else {
            if (std::holds_alternative<std::string_view>(v))
                return uf::any(uf::from_type_value, std::move(std::get<std::string>(t)), std::string(std::get<std::string_view>(v)));
            else
                return uf::any(uf::from_type_value, std::move(std::get<std::string>(t)), std::move(std::get<std::string>(v)));
        }
    }

     /* Invariants:
     * - void is represented as empty type and value chunks. Therefore
     *     - tbegin!=tend and vbegin!=vend all the time.
     *     - we may have empty chunks in both a type and a value chain
     * - The tbegin and vbegin of a child is never the same as the tbegin/vbegin of the parent.
     *     - For types this comes from how typestrings of containing types always start with the type of
     *       the container before the type of the contained element.) This is because we always split the
     *       type of the container from the type of the contained values on the first call to operator[] of
     *       a container. As a consequence, when overwriting the an xXolmt parent with something of
     *       different type, the child types chunks can remain at the child but made read-only in case of
     *       more than child having them to avoid overwriting all when one changes type).
     *     - For values this is because all container values start with a size (lm), a has_value byte (xXo)
     *       or a type (a). The only exceptions are tuple and error so there we insert an empty chunk at the beginning
     *       of a parent, when calling [0] the first time. This is needed so that when the parent is modified
     *       (its vbegin changes) child[0] will not change.
     * - Type chunks are shared not only between parent-child (maybe several generations), but also between
     *   siblings of the same 'lm' parent. The value chunks are only shared between child-parent (maybe
     *   several generations).*/

    /** Change tend to this. Also change tend of the last child,
     * if our last element is parsed.*/
    void change_tend(chunk_ptr c) noexcept
    {
        tend = std::move(c);
        if (children.empty()) return;
        if (size() - 1 == children.back().first)
            children.back().second->change_tend(tend); //tail recursion, IMO
    }

    /** Change vend to this. Also change vend of the last child,
     * if our last element is parsed.*/
    void change_vend(chunk_ptr c) noexcept
    {
        vend = std::move(c);
        if (children.empty()) return;
        if (size() - 1 == children.back().first)
            children.back().second->change_vend(vend); //tail recursion, IMO
    }

    /** Check whether children are allowed to change type.
     * Never called when the type does not change.
     * @param [in] char change_to The typechar of the new type.
     * @returns the ancestor that did not allow the change or nullptr if ok.*/
    const wview *allow_child(char change_to) const {
        switch (typechar()) {
        case 'a': return nullptr;
        case 'x':
        case 'X': return change_to == 'e' ? nullptr : this;
        case 'o': //we don't allow clearing an optional by assigning a void
        case 'e': //we don't allow any type change for errors
        case 'l':
        case 'm': return this;
        case 't': if (change_to == 0) return this; //dont allow changing a tuple member to void
                else if (!parent) return nullptr;
                else return parent->allow_child('t');
        default: assert(0); return this;
        }
    }
    /** This is operator [] for content types.
     * @param [in] idx The zero-based index of the requested element.
     * @param [in] key For maps, we return the key of the idx:th element if set. For others we ignore it.
     * @returns The content of an any, optional (only if not-empty) or expected value (either value or error),
     *          or the idx:th element in a list. For maps we get a pair back (e.g., t2is for a mis).
     * We throw
     * - an uf::type_mismatch_error if called on a primitive type.
     * - std::out_of_range if idx is too large.
     * - uf::value_error (of several kinds) if types and serialized values are in error.*/
    ptr operator[](uint32_t idx) {
        assert(check(LOC));
        const char t = typechar();
        if (std::string_view("lamoxeXt").find(t)==std::string_view::npos)
            throw uf::type_mismatch_error("Operator [] not valid for type <%1>.", type().as_view(), {});
        if (auto s = size();  s==0)
            throw std::out_of_range(uf::concat("Operator [", idx, "] called for empty container of type <", type().as_view(), ">."));
        else if (idx >= s)
            throw std::out_of_range(uf::concat("Index #", idx, " out of range [0..", size()-1, "] in operator [] for type <", type().as_view(), ">."));
        const auto loc = std::lower_bound(children.begin(), children.end(), idx);
        if (loc != children.end() && loc->first==idx)
            return loc->second;
        switch (t) {
        default: assert(0); return {};
        case 'a': {
            //in 4-t-4-v format.
            assert(vbegin);
            assert(vbegin != vend);
            assert(!idx);
            assert(vbegin->size() >= 4);
            auto tlen = deserialize_view_as<uint32_t>({vbegin->data(), 4});
            auto tc = split<has_refc, Allocator>(vbegin, 4);
            std::pair<chunk_ptr, uint32_t> p(tc, 0);
            advance<has_refc, Allocator>(p, tlen, vend);
            auto vlc = split<has_refc, Allocator>(p.first, p.second);
            if (tc == vlc)
                tc = insert_empty_chunk_after<has_refc, Allocator>(vbegin);
            assert(vlc->size() >= 4);
            //auto vlen = deserialize_view_as<uint32_t>({vlc->data(), 4}); //We dont actually need the vlen. Maybe we chould check that they fit the data...
            auto vc = split<has_refc, Allocator>(vlc, 4);
            if (vc == vend)
                vc = insert_empty_chunk_after<has_refc, Allocator>(vlc);
            assert(check(LOC));
            auto &w = children.emplace(loc, std::piecewise_construct, std::forward_as_tuple(idx),
                                       std::forward_as_tuple(std::move(tc), std::move(vlc),
                                                             std::move(vc), vend, this))->second;
            assert(check(LOC));
            assert(children.front().second->check(LOC));
            return w;
        }
        case 'X':
        case 'x':
            assert(vbegin);
            assert(vbegin != vend);
            assert(vbegin->size());
            assert(idx == 0);
            if (*vbegin->data()==0) {
                //x/X has an error value
                auto vc = split<has_refc, Allocator>(vbegin, 1);
                auto& w = children.emplace(loc, std::piecewise_construct, std::forward_as_tuple(idx),
                                           std::forward_as_tuple(chunk_ptr(sview_ptr("e", true)), chunk_ptr{},
                                                                 std::move(vc), vend, this))->second;
                assert(check(LOC));
                return w;
            } else if (t == 'X') {
                assert(tbegin->size() == 1);
                assert(vbegin->size() == 1);
                //'X' has a value : return a void wview
                //Insert an empty type and value chunk - these may be replaced to an error.
                ptr &w = children.emplace(loc, std::piecewise_construct, std::forward_as_tuple(idx),
                                          std::forward_as_tuple(insert_empty_chunk_after<has_refc, Allocator>(tbegin), chunk_ptr(tend),
                                                                insert_empty_chunk_after<has_refc, Allocator>(vbegin), vend, this))->second;
                assert(check(LOC));
                return w;
            }
            [[fallthrough]]; //'x' has a value - handle as an optional
        case 'o': {
            //If we are here, we know that the optional has an actual value (size()>0)
            assert(idx == 0);
            auto tc = split<has_refc, Allocator>(tbegin, 1);
            auto vc = split<has_refc, Allocator>(vbegin, 1);
            auto& w = children.emplace(loc, std::piecewise_construct, std::forward_as_tuple(idx),
                                       std::forward_as_tuple(std::move(tc), chunk_ptr(tend),
                                                             std::move(vc), vend, this))->second;
            assert(check(LOC));
            return w;
        }
        case 'e': {
            if (vbegin->size() && idx == 0) {
                //ensure value chain starts with an empty chunk
                //(so that we can disown our children and change vbegin's content at the same time)
                auto second = vbegin->clone();
                second->next = vbegin->next;
                vbegin->resize(0).next = std::move(second);
            }
            //parse as many strings as in idx, but only in value
            auto vc = vbegin;
            auto more_val = [&vc, this](const char*& p, const char*& end) {
                do {
                    if (vc->next == vend) return;
                    vc = vc->next;
                    p = vc->data();
                    end = p + vc->size();
                } while (p == end);
            };
            const char *p = vc->data(), *end = p + vc->size();
            for (uint32_t i = idx; i; i--) {
                std::string_view etype = "s";
                if (auto err = serialize_scan_by_type_from(etype, p, end, [](std::string_view&) {}, more_val, false))
                    err->prepend_type0("s", etype).throw_me();
            }
            auto vc_start = vc = split<has_refc, Allocator>(vc, p - vc->data());
            p = vc->data(); end = p + vc->size();
            const std::string_view original_etype = idx==2 ? "a" : "s";
            std::string_view etype = original_etype;
            if (auto err = serialize_scan_by_type_from(etype, p, end, [](std::string_view&) {}, more_val, false))
                err->prepend_type0(original_etype, etype).throw_me();
            vc = split<has_refc, Allocator>(vc, p - vc->data());
            ptr &w = children.emplace(loc, std::piecewise_construct, std::forward_as_tuple(idx),
                                      std::forward_as_tuple(chunk_ptr(sview_ptr(original_etype, true)), chunk_ptr{},
                                                            std::move(vc_start), vc, this))->second;
            assert(check(LOC));
            return w;
        }
        case 'l':
        case 'm': {
            assert(vbegin);
            assert(vbegin != vend);
            assert(vbegin->size());
            //Start by dissecting the type
            const auto type_ = type(); //own potentially flattened typestring
            std::array<std::string_view, 2> elem_type = { type_.sub_view(1) };
            if (t == 'm') {
                if (size_t len = uf::parse_type(elem_type[0])) {
                    elem_type[1] = elem_type[0].substr(len);
                    elem_type[0] = elem_type[0].substr(0, len);
                } else
                    throw uf::typestring_error("Invalid map typestring <%1>.", type_.as_view(), 1);
            }
            chunk_ptr tc_inner = split<has_refc, Allocator>(tbegin, 1);

            //Find the idx:th element
            static constexpr auto count_len = sizeof(uint32_t);
            // Value chunks hold either unparsed element(s), or a single whole element.
            // Therefore we should only look in the first chunk following the child's who's index is the largest that is less than i.
            // NB: Value chunks of children are chained one after the other.
            const bool insert_as_first = loc == children.begin();
            chunk_ptr vc = insert_as_first ? vbegin : std::prev(loc)->second->vend; //The chunk we need to search for idx. May be empty if a tuple
            uint32_t next_elem = insert_as_first ? 0 : std::prev(loc)->first + 1;
            char const* p = insert_as_first ? vc->data() + count_len : vc->data(); //jump over the count field when we are first
            const char* end = vc->data() + vc->size();
            auto more_val = [&vc, this](const char* &p, const char* &end) {
                do {
                    if (vc->next == vend) return;
                    vc = vc->next;
                    p = vc->data();
                    end = p + vc->size();
                } while (p == end);
            };
            for (; next_elem < idx; ++next_elem)
                for (size_t i = 0; i<(t=='m' ? 2 : 1); i++) { //two passes for 'm', one for 'l'
                    std::string_view etype = elem_type[i];
                    if (auto err = serialize_scan_by_type_from(etype, p, end, [](std::string_view &) {}, more_val, false))
                        err->prepend_type0(type_.as_view(), etype).append_msg(" (wany)").throw_me();
                }
            //now 'p' points to the vstart of the value of the idx:th element (pair for maps) in 'vc'
            auto vc_start = vc = split<has_refc, Allocator>(vc, p - vc->data());
            p = vc->data(); end = p + vc->size();
            for (size_t i = 0; i<(t=='m' ? 2 : 1); i++) { //two passes for 'm', one for 'l'
                std::string_view etype = elem_type[i];
                if (auto err = serialize_scan_by_type_from(etype, p, end, [](std::string_view &) {}, more_val, false))
                    err->prepend_type0(type_.as_view(), etype).append_msg(" (wany)").throw_me();
            }
            vc = split<has_refc, Allocator>(vc, p - vc->data());
            //now the selected data (pair for maps) is in the chunk range [vc_start, vc)
            if (t == 'm') {
                auto tpair = chunk_ptr(sview_ptr("t2", true));
                tpair->next = std::move(tc_inner);
                tc_inner = std::move(tpair);
            }
            ptr &w = children.emplace(loc, std::piecewise_construct, std::forward_as_tuple(idx),
                                      std::forward_as_tuple(std::move(tc_inner), chunk_ptr(tend),
                                                            std::move(vc_start), vc, this))->second;
            assert(check(LOC));
            return w;
        }
        case 't':
            //The call to size() above ensured that the whole t<num> header is in a separate chunk at the front alone.
            if (vbegin->size() && idx==0) {
                //ensure value chain starts with an empty chunk
                //(so that we can disown our children and change vbegin's content at the same time)
                auto second = vbegin->clone();
                second->next = vbegin->next;
                vbegin->resize(0).next = std::move(second);
            }
            const bool insert_as_first = loc == children.begin();
            //The chunk we need to search for idx
            chunk_ptr tc = insert_as_first ? tbegin->next : std::prev(loc)->second->tend;
            assert(tc->size());
            chunk_ptr vc = find_nonempty<has_refc, Allocator>(insert_as_first ? vbegin : std::prev(loc)->second->vend, vend);
            uint32_t next_elem = insert_as_first ? 0 : std::prev(loc)->first + 1;
            std::string_view etypes = tc->as_view();
            char const* p = vc->data();
            const char* end = vc->data() + vc->size();
            auto more_type = [&tc, this](std::string_view&t) {
                do {
                    if (tc->next == tend) return;
                    tc = tc->next;
                    t = tc->as_view();
                } while (t.empty());
            };
            auto more_val = [&vc, this](const char*&p, const char*&end) {
                do {
                    if (vc->next == vend) return;
                    vc = vc->next;
                    p = vc->data();
                    end = p + vc->size();
                } while (p == end);
            };
            for (; next_elem < idx; ++next_elem)
                if (auto err = serialize_scan_by_type_from(etypes, p, end, more_type, more_val, false))
                    err->append_msg(" (wany)").throw_me(); //TODO: prepend type (needs parsing the typestring up to 'loc' and following type consumption in more_type)
            //now 'p' and 'etypes' points to the vstart/tstart of the idx:th element
            auto tc_start = tc = split<has_refc, Allocator>(tc, etypes.data() - tc->data());
            auto vc_start = vc = split<has_refc, Allocator>(vc, p - vc->data());
            etypes = tc->as_view();
            p = vc->data(); end = p + vc->size();
            if (auto err = serialize_scan_by_type_from(etypes, p, end, more_type, more_val, false))
                err->append_msg(" (wany)").throw_me();
            tc = split<has_refc, Allocator>(tc, etypes.data() - tc->data());
            vc = split<has_refc, Allocator>(vc, p - vc->data());
            //now the selected type/value are in chunk ranges [{t,v}c_start,{t,v}c)
            //Split off the type and the value
            ptr &w = children.emplace(loc, std::piecewise_construct, std::forward_as_tuple(idx),
                                      std::forward_as_tuple(std::move(tc_start), std::move(tc),
                                                            std::move(vc_start), vc, this))->second;
            assert(check(LOC));
            return w;
        }
    }

    /** Unlinks one child from us.
     * @param [in] cindex The index of the child in 'children'.
     * @param [in] clone_type If set, we clone the type.
     * @param [in] clone_value If set, we clone the value.*/
    void disown_child(size_t cindex, bool clone_type, bool clone_value)
    {
        auto& c = children[cindex].second;
        if (clone_type)
            //duplicate the type so that changing one child leaves others and us and the parent intact (& vice versa)
            clone_into<has_refc, Allocator>(c->tbegin, c->tbegin, c->tend, c->tend);
        if (clone_value)
            //duplicate the value so that changing a child leaves us and the parent intact (& vice versa)
            clone_into<has_refc, Allocator>(c->vbegin, c->vbegin, c->vend, c->vend);
        c->parent = nullptr;
    }

    /** Break the bond between our children and us.
     * Any change later made to children will not impact any other children, us or our ancestors.
     * Any change made to us will not impact any children (if destructor is not set).
     * We also clear the child list. Should be called on destruction or when we are being overwritten.
     * @param [in] destructor When set, we assume we are being deleted. If we have no parents
     *             then a single child may have a potentially writeable sview for type/value for itself.*/
    void disown_children(bool destructor) //TODO: remove destructor. Overwirting a parent will never impact a child as tbegin and vbegin are not shared by parents & children
        //This will assume we change tbegin and vbegin after this (or drop them)
    {
        if (children.empty()) return;
        /** This is true if only the children remain after the operation calling disown_children().*/
        const bool children_only = destructor && !parent;
        /** This is true if potential children share the type. */
        const bool shared_type = children.size() > 1 && typechar() != 't';
        bool do_type_cloning = !children_only;
        for (size_t i = 0; i<children.size(); i++) {
            disown_child(i, do_type_cloning, !children_only);
            do_type_cloning |= shared_type;
        }
        children = {};
    }

    /** Check if our type can be changed to this in a set() or swap() operation.
     * @exception uf::type_mismatch_error not with 'msg' as message, which may
     *            contain <%1> to insert our type (in our parent) and <%2> to insert 'new_type'.
     * @returns true if our type has changed, false if new_type equals to our type.*/
    bool check_type_change(std::string_view new_type, std::string_view msg) const
    {
        assert(check(LOC));
        string_variant old_type = type();
        const bool type_changed = new_type != old_type.as_view();
        if (type_changed && parent) {
            //special handling is needed for the child of an expected. There it is allowed to
            //change from error to the exact type of the expected. Since in allow child
            //we take only the first char of the new type, we handle this case here.
            if (old_type == "e" && (parent->typechar() == 'x' || parent->typechar() == 'X')) {
                if (auto parent_type = parent->type(); parent_type.as_view().substr(1)!=new_type)
                    throw uf::type_mismatch_error(msg, parent_type.as_view(), new_type, 1);
                //else fallthrough to OK changing the type.
            } else if (auto p = parent->allow_child(new_type.empty() ? 0 : new_type.front())) {
                //find location of the star
                size_t pos = std::string::npos;
                //Step upwards and find us or an ancestor whose type is in the linked typelist of the
                //ancestor (p) which denied this change
                //if for some reason our type is not in the linked list of the ancestor's type list, do not emit a star
                for (const wview* pW = this; pW; pW = pW->parent) {
                    const chunk_ptr& until = pW->parent && pW->parent->typechar() == 'm' ? pW->tbegin->next : pW->tbegin; //for chlidren of 'm' peel off the 't2'
                    for (const chunk_ptr* i = &p->tbegin; *i!=p->tend; i=&(*i)->next)
                    if (*i==until) {
                        pos = ::uf::impl::flatten_size<has_refc, Allocator>(p->tbegin, until);
                        goto do_throw;
                    }
                }
            do_throw:
                throw uf::type_mismatch_error(msg, p->type(), new_type, pos);
            }
        }
        return type_changed;
    }

    void update_parent_any_sizes(int32_t diff) {
        if (diff==0) return;
        for (wview* p = this; p->parent; p=p->parent)
            if (p->parent->typechar() == 'a') {
                const uint32_t orig_len = uf::deserialize_as<uint32_t>(std::string_view(p->tend->data(), 4));
                assert(0 <= int64_t(orig_len) + int64_t(diff));
                assert(int64_t(orig_len) + int64_t(diff) < int64_t(std::numeric_limits<uint32_t>::max));
                char* ptr = p->tend->data_writable();
                uf::impl::serialize_to(uint32_t(orig_len+diff), ptr);
            }
    }

    /** Change the value and possibly type to that of the argument's.
     * @throw error if changing the type is not allowed */
    void set(wview const& w) & {
        //No-op on self-assign
        if (&w == this) return;
        string_variant new_type = w.type();
        const bool type_changed = check_type_change(new_type.as_view(), "Cannot set element of <%1> to <%2>.");
        disown_children(false);
        if (!parent) { //No parent. Simply clone 'p'
            if (type_changed)
                clone_into<has_refc, Allocator>(tbegin, w.tbegin, w.tend), tend = {};
            clone_into<has_refc, Allocator>(vbegin, w.vbegin, w.vend), vend = {};
            assert(check(LOC));
            return;
        }
        const char ptype = parent->typechar();
        const uint32_t w_tlen = type_changed ? w.flatten_type_size() : 0; //computed only if type changed
        const uint32_t w_vlen = w.flatten_size();
        const uint32_t old_tlen = !type_changed
            ? 0
            : ptype == 'a'
                ? uf::deserialize_as<uint32_t>(std::string_view(parent->vbegin->data(), 4)) //faster than flatten_type_size
                : flatten_type_size();
        const uint32_t old_vlen = ptype == 'a'
            ? uf::deserialize_as<uint32_t>(std::string_view(tend->data(), 4)) //faster than flatten_size
            : flatten_size();
        chunk_ptr new_tbegin;
        bool adjust_type = false;
        switch (ptype) {
        case 'a':
            //value of 'a' is parsed to <tlen> <type chunks...> <vlen> <value chunks...>
            assert(parent->vbegin->size() == 4);
            assert(tend->size() == 4);
            //We cannot change the sizes here to that of the new one, since we may have
            //self-assignment
            [[fallthrough]];
        case 't':
            if (type_changed)
                //Here we cannot yet change tbegin as it may be a part of the
                //vbegin->vend chunk list. It may happen if we assign a parent of us
                //to us. In that case our type is part of the parent's value, which
                //we clone below.
                clone_into<has_refc, Allocator>(new_tbegin, w.tbegin, w.tend, tend);
            [[fallthrough]];
        case 'l':
        case 'm':
        case 'e':
            clone_into<has_refc, Allocator>(vbegin, w.vbegin, w.vend, vend);
            if (new_tbegin)
                tbegin->copy_from(std::move(*new_tbegin));
            if (ptype == 'a') {
                if (old_tlen != w_tlen) {
                    char* p = parent->vbegin->data_writable();
                    uf::impl::serialize_to(w_tlen, p);
                }
                if (old_vlen != w_vlen) {
                    char* p = tend->data_writable();
                    uf::impl::serialize_to(w_vlen, p);
                }
            }
            goto update_parent_any_sizes;
        case 'x':
        case 'X':
            //value of expected is parsed as <has_value byte> <value chunks.../error chunks...>
            if (new_type.as_view() == "e") {
                //set type to 'e'
                if (type_changed)
                    tbegin = chunk_ptr(sview_ptr("e", true)); //copy the typestring into the sview
                //tbegin->next will be empty.
                auto val = parent->vbegin;
                assert(val);
                assert(val->size() == 1);
                char c = 0;
                val->assign(std::string_view(&c, 1));
                clone_into<has_refc, Allocator>(val->next, w.vbegin, w.vend, vend);
                goto update_parent_any_sizes;
            } else if (type_changed)
                //Set our type to that of the parent's (after the x or X, latter of which is void)
                //since we can only change type with a non-'e' 'w' by changing from an 'e'
                //back to having a value.
                //However, we do not update our tbegin as of yet, since we may set us to
                //a parent of us. In that case the type chunk chain is part of w.vbegin->w.vend
                //and we want to clone that in pristine condition. So here we only set a flag
                //and do it after cloning 'w'.
                adjust_type = true;
            [[fallthrough]]; //when setting a value handle same as an optional.
        case 'o':
            //value of optional is parsed as <has_value byte> <value chunks.../error chunks...>
            auto val = parent->vbegin;
            assert(val);
            assert(val->size() == 1);
            char c = 1;
            val->assign(std::string_view(&c, 1));
            clone_into<has_refc, Allocator>(val->next, w.vbegin, w.vend, vend);
            if (adjust_type) { //can only happen for 'x' or 'X'
                tbegin = parent->tbegin->next;
                tend = parent->tend;
            }
            goto update_parent_any_sizes;
        }
        assert(0);
    update_parent_any_sizes:
        const int32_t diff = int32_t(w_tlen - old_tlen) + int32_t(w_vlen - old_vlen);
        if (ptype == 'a') parent->update_parent_any_sizes(diff); //skip adjusting our parent, just do the parent's parent and up
        else update_parent_any_sizes(diff);
        assert(check(LOC));
    }

    /** Change the value and possibly type to that of the argument's.
     * We copy 'type' and 'value' so they can be discareded after the call.
     * @throw error if changing the type is not allowed*/
    void set(std::string_view type, std::string_view value) & {
        const bool type_changed = check_type_change(type, "Cannot set element of <%1> to <%2>.");
        disown_children(false);
        if (!parent) {//No parent. Simply clone 'p'
            if (type_changed)
                copy_into<has_refc, Allocator>(type, tbegin), tend = {};
            copy_into<has_refc, Allocator>(value, vbegin), vend = {};
            assert(check(LOC));
            return;
        }
        char const ptype = parent->typechar();
        const uint32_t old_tlen = !type_changed
            ? type.length()
            : ptype == 'a'
                ? uf::deserialize_as<uint32_t>(std::string_view(parent->vbegin->data(), 4)) //faster than flatten_type_size
                : flatten_type_size();
        const uint32_t old_vlen = ptype == 'a'
            ? uf::deserialize_as<uint32_t>(std::string_view(tend->data(), 4))
            : flatten_size();
        switch (ptype) {
        case 'a':
            //value of 'a' is parsed to <tlen> <type chunks...> <vlen> <value chunks...>
            assert(parent->vbegin->size() == 4);
            assert(tend->size() == 4);
            if (old_tlen != type.length()) {
                char* p = parent->vbegin->data_writable();
                uf::impl::serialize_to(uint32_t(type.size()), p);
            }
            if (old_vlen != value.length()) {
                char* p = tend->data_writable();
                uf::impl::serialize_to(uint32_t(value.size()), p);
            }
            [[fallthrough]];
        case 't':
            if (type_changed) {
                copy_into<has_refc, Allocator>(type, tbegin, tend);
            }
            [[fallthrough]];
        case 'l':
        case 'm':
        case 'e':
            copy_into<has_refc, Allocator>(value, vbegin, vend);
            goto update_parent_any_sizes;
        case 'x':
        case 'X':
            //value of expected is parsed as <has_value byte> <value chunks.../error chunks...>
            if (type == "e") {
                //set type to 'e'
                if (type_changed)
                    tbegin = chunk_ptr(sview_ptr("e", true)); //copy the typestring into the sview
                auto val = parent->vbegin;
                assert(val);
                assert(val->size() == 1);
                char c = 0;
                val->assign(std::string_view(&c, 1));
                copy_into<has_refc, Allocator>(value, val->next, vend);
                goto update_parent_any_sizes;
            } else if (type_changed) {
                //Need to restore the parent's type
                tbegin = parent->tbegin->next;
                tend = parent->tend;
            }
            [[fallthrough]]; //when setting a value handle same as an optional.
        case 'o':
            //value of optional is parsed as <has_value byte> <value chunks.../error chunks...>
            auto val = parent->vbegin;
            assert(val);
            assert(val->size() == 1);
            char c = 1;
            val->assign(std::string_view(&c, 1));
            copy_into<has_refc, Allocator>(value, val->next, vend);
            goto update_parent_any_sizes;
        }
        assert(0);
    update_parent_any_sizes:
        const int32_t diff = int32_t(type.length() - old_tlen) + int32_t(value.length() - old_vlen);
        if (ptype == 'a') parent->update_parent_any_sizes(diff);
        else update_parent_any_sizes(diff);
        assert(check(LOC));
    }

    /** Returns the index in children of what or nothing if not our child.*/
    std::optional<uint32_t> cindexof(const ptr &what) const noexcept {
        auto i = std::find_if(children.begin(), children.end(),
                              [&what](const child& c) { return c.second==what; });
        if (i == children.end()) return {};
        assert(i->second->vend == what->vend);
        assert(i->second->tbegin == what->tbegin);
        assert(i->second->tend == what->tend);
        assert(i->second->parent == what->parent);
        return i - children.begin();
    }

    /** Returns the index of what or nothing if not our child.*/
    std::optional<uint32_t> indexof(const ptr& what) const noexcept
    { if (auto i = cindexof(what)) return children[*i].first; return {}; }

    /** Deletes an element at this index in 'children'.
     * When deleting from a tuple, we change type. If this is not allowed by
     * the parent, we throw a type mismatch. If the deletion makes a two-element
     * tuple to a single elemented non-tuple, we return true.
     * We also return true when the user wants to delete from an 'abcdeiIxX' (not possible)
     * and false on success.*/
    bool do_erase(size_t cindex) {
        assert(check(LOC));
        assert(children.size() > cindex);
        uint32_t new_size = size()-1;
        int32_t size_diff = 0;
        switch (typechar()) {
        default: return true;
        case 'o': //set the has_value byte to zero
            if (!vbegin->is_writable()) {
                split<has_refc, Allocator>(vbegin, 1); //ensure has_value byte is its own chunk
                vbegin->reserve(1); //make sure writable
            }
            vbegin->data_writable()[0] = 0;
            break;
        case 'l':
        case 'm': {
            //decrement the size field at the beginning
            if (!vbegin->is_writable()) {
                split<has_refc, Allocator>(vbegin, 4); //does nothing if vbegin is already only 4 bytes.
                vbegin->reserve(4); //make sure length is writable (destroys original content)
            }
            char* p = vbegin->data_writable();
            serialize_to(new_size, p);
            break;
        }
        case 't':
            //Do not allow size to go below 2
            assert(new_size);
            if (new_size==1)
                return true;
            size_diff -= children[cindex].second->flatten_type_size();         //We insert remove this from the typestring
            //unlink the type
            auto tprev = find_before<has_refc, Allocator>(
                children[cindex].second->tbegin,
                cindex ? children[cindex - 1].second->tbegin : tbegin, tend);
            const bool just_after = cindex && tprev->next == children[cindex - 1].second->tend;
            tprev->next = children[cindex].second->tend;
            if (just_after)
                children[cindex - 1].second->change_tend(tprev->next);
                //decrement the size field at the beginning
            std::string new_len = uf::concat('t', new_size);
            tbegin->assign(std::string_view(new_len));
            if (new_size == 9 || new_size == 99 || new_size == 999 || new_size == 9999) size_diff--; //The size in the typestring requires less digits t10->t9
        }
        //unlink the data
        auto vprev = find_before<has_refc, Allocator>(
            children[cindex].second->vbegin,
            cindex ? children[cindex - 1].second->vbegin : vbegin, vend);
        const bool just_after = cindex && vprev->next == children[cindex - 1].second->vend;
        vprev->next = children[cindex].second->vend;
        if (just_after)
            children[cindex - 1].second->change_vend(vprev->next);
        //Decrease the indices after
        for (size_t i = cindex + 1; i < children.size(); i++)
            children[i].first--;
        //disown and delete this guy from children
        size_diff -= children[cindex].second->flatten_size();         //We insert remove this from the value
        disown_child(cindex, typechar()!='t', false);
        children.erase(children.begin() + cindex);
        update_parent_any_sizes(size_diff);
        assert(check(LOC));
        return false;
    }

    bool do_insert_after(int cindex, const wview& what) {
        int32_t size_diff = 0;
        switch (typechar()) {
        default: return true;
        case 'o': {
            if (vbegin->data()[0])
                throw std::out_of_range(uf::concat("Cannot insert to an <", type().as_view(), "> already having a value."));
            if (cindex >= 0)
                throw std::out_of_range(uf::concat("Can only insert at the very beginning of <", type().as_view(), ">."));
            if (auto t1 = type(), t2 = what.type(); t1.as_view().substr(1) != t2.as_view())
                throw uf::type_mismatch_error("Cannot insert a <%2> into <%1>.", t1.as_view(), t2.as_view(), 1);
            assert(children.empty());
            assert(vbegin->size() == 1);
            assert(vbegin->next == vend);
            vbegin->reserve(1)[0] = 1; //make sure writable and set the has_value byte to 1
            break;
        }
        case 'l':
            if (auto t1 = type(), t2 = what.type(); t1.as_view().substr(1) != t2.as_view())
                throw uf::type_mismatch_error("Cannot insert a <%2> into <%1>.", t1.as_view(), t2.as_view(), 1);
            goto insert_map_list;
        case 'm':
            if (auto t1 = type(), t2 = what.type(); t2.as_view().substr(0,2)!="t2" || t1.as_view().substr(1) != t2.as_view().substr(2))
                throw uf::type_mismatch_error("Cannot insert a <%2> into <%1>.", t1.as_view(), t2.as_view(), 1);
        insert_map_list: {
            //increment the size field at the beginning
            const uint32_t new_size = size() + 1;
            if (!vbegin->is_writable() || cindex<0) { //if we insert to the front, ensure vbegin is the length only and we can insert after it.
                split<has_refc, Allocator>(vbegin, 4); //ensure length is its own chunk
                vbegin->reserve(4); //make sure writable. Destroys size()
            }
            char* p = vbegin->data_writable();
            serialize_to(new_size, p);
            break;
        }
        case 't':
            if (what.tbegin==what.tend || what.tbegin->size()==0)
                throw uf::type_mismatch_error("Cannot insert a <%2> into <%1>.", type().as_view(), {});
            if (parent)
                if (auto p = parent->allow_child('t'))
                    throw uf::type_mismatch_error("Cannot insert <%2> into tuple, as type change is not allowed by parent <%2>.",
                                                  p->type(),
                                                  what.type().as_view(),
                                                  ::uf::impl::flatten_size<has_refc, Allocator>(
                                                      p->tbegin, cindex<0 ? tbegin->next : children[cindex].second->tend));
            //increment the size field at the beginning
            uint32_t size = this->size();
            if (size == 9 || size == 99 || size == 999 || size == 9999) size_diff++; //The typestring will increase in length t9->t10.
            std::string new_len = uf::concat('t', size+1);
            size_diff += what.flatten_type_size();         //We insert this into the typestring
            tbegin->assign(std::string_view(new_len));
            //link in a clone of the type of 'what'
            const chunk_ptr& link_after = cindex < 0 ?
                tbegin :
                find_before<has_refc, Allocator>(children[cindex].second->tend,
                                                 children[cindex].second->tbegin,
                                                 children[cindex].second->tend);
            chunk_ptr tcopy;
            clone_into<has_refc, Allocator>(tcopy, what.tbegin, what.tend, link_after->next);
            link_after->next = std::move(tcopy);
            if (cindex >= 0) {
                children[cindex].second->change_tend(link_after->next);
                //Fallthrough to inserting the value
            } else if (vbegin->size()) {
                //Value must be inserted at the front: must change the content of 'vbegin'
                chunk_ptr old_vbegin = vbegin->clone();
                old_vbegin->next = vbegin->next;
                clone_into<has_refc, Allocator>(vbegin, what.vbegin, what.vend, old_vbegin);
                goto adjust_children_indices;
            }
        }
        {
            //link in a clone of the data of 'what'
            const chunk_ptr& link_after = cindex < 0 ?
                vbegin :
                find_before<has_refc, Allocator>(children[cindex].second->vend,
                                                 children[cindex].second->vbegin,
                                                 children[cindex].second->vend);
            chunk_ptr vcopy;
            clone_into<has_refc, Allocator>(vcopy, what.vbegin, what.vend, link_after->next);
            link_after->next = std::move(vcopy);
            if (cindex >= 0)
                children[cindex].second->change_vend(link_after->next);
        }
    adjust_children_indices:
        //increase the index of everyone behind us in 'children'
        for (size_t i = cindex + 1; i < children.size(); i++)
            children[i].first++;

        //Adjust parent's any sizes
        size_diff += what.flatten_size();         //We insert this into the value
        update_parent_any_sizes(size_diff);
        assert(check(LOC));
        return false;
    }

    std::pair<ptr, std::string> linear_search(const ptr &t, int n) {
        const char c = typechar();
        if (c != 'l' && c != 'm')
            return { {}, uf::concat("linear_search() is possible only in lists/maps and not in <",
                                    type().as_view(), ">.") };
        const bool is_map = c == 'm';
        auto t1_ = type(), t2_ = t->type();
        std::string_view t1 = t1_.as_view().substr(1), t2 = t2_.as_view();

        std::string_view key_type = t1; //for lists the key is the whole element
        if (is_map) {
            if (auto [len, err] = parse_type(key_type, false); err != ser::ok)
                return { {}, uf::concat("internal error in linear search #4: ", key_type) };
            else key_type = key_type.substr(0, len);
        }
        auto [t1x, err1] = parse_tuple_type(key_type, std::max(1, n)); //the part of the key we compare
        if (err1.length()) return { {}, uf::concat(err1, " (<", key_type, ">)") };
        if (key_type != t2) {
            if (n == 0) {
                if (t1x != t2) return { {}, uf::concat("Mismatching types: <", t1x, "> and <", t2, ">.") };
            } else {
                auto [t2x, err2] = parse_tuple_type(t2, n);
                if (err2.length()) return { {}, uf::concat(err2, " (<", t2, ">)") };
                if (t1x != t2x) return { {}, uf::concat("Mismatching types: <", t1x, "> and <", t2x, ">.") };
            }
        }
        const uint32_t no_elements = size();
        if (no_elements==0) return {}; //avoid calling vc->data() below as vc will be tend below which may be null
        //Determine the last byte of the value to search for
        chunk_ptr vc = t->vbegin;
        auto more_val = [&vc, this](const char *&p, const char *&end) {
            do {
                if (vc->next == this->vend) return;
                vc = vc->next;
                p = vc->data();
                end = p + vc->size();
            } while (p == end);
        };
        const char *pVC = vc->data(), *end = pVC + vc->size();
        for (std::string_view type = t1x; type.length(); /*nope*/)
            if (auto err = serialize_scan_by_type_from(type, pVC, end, [](std::string_view &) {}, more_val, false))
                return {{}, uf::concat("Internal value error #3 in linear_search(): ", err->prepend_type0(t1_.as_view(), type).what())};
        size_t last_offset_compare = pVC-vc->data();
        chunk_ptr last_chunk_compare = std::move(vc);
        vc = split<has_refc, Allocator>(vbegin, 4);
        pVC = vc->data(), end = pVC + vc->size();
        size_t cindex = 0;
        for (uint32_t i = 0; i<no_elements; i++) {
            if (startswidth<has_refc, Allocator>(vc, pVC-vc->data(), vend, t->vbegin, 0, last_chunk_compare, last_offset_compare)) {
                //Done create/reuse child
                if (children.size()>cindex && children[cindex].first==i)
                    return {children[cindex].second, {}};
                //now 'pVC' points to the vstart of the value of the idx:th element in 'vc'
                chunk_ptr vc_start = vc = split<has_refc, Allocator>(vc, pVC - vc->data());
                pVC = vc->data(); end = pVC + vc->size();
                std::string_view type(t1);
                if (auto err = serialize_scan_by_type_from(type, pVC, end, [](std::string_view &) {}, more_val, false))
                    return {{}, uf::concat("Internal value error #2 in linear_search(): ", err->prepend_type0(t1_.as_view(), type).what())};
                if (is_map) {
                    if (type.empty()) return { {}, uf::concat("Internal value error #2c in linear_search(): ", t1)};
                    if (auto err = serialize_scan_by_type_from(type, pVC, end, [](std::string_view&) {}, more_val, false))
                        return { {}, uf::concat("Internal value error #2b in linear_search(): ", err->prepend_type0(t1_.as_view(), type).what()) };
                }
                if (type.size()) return { {}, uf::concat("Internal value error #2d in linear_search(): ", t1, "->", type)};
                vc = split<has_refc, Allocator>(vc, pVC - vc->data());
                //now the selected data is in the chunk range [vc_start, vc)
                chunk_ptr tc_inner = split<has_refc, Allocator>(tbegin, 1);
                if (is_map) { //prepend 't2' for maps
                    auto tpair = chunk_ptr(sview_ptr("t2", true));
                    tpair->next = std::move(tc_inner);
                    tc_inner = std::move(tpair);
                }
                ptr w = children.emplace(children.begin()+cindex, std::piecewise_construct,
                                         std::forward_as_tuple(i),
                                         std::forward_as_tuple(std::move(tc_inner), chunk_ptr(tend),
                                                               std::move(vc_start), vc, this))->second;
                assert(check(LOC));
                return {std::move(w), {}};

            }
            //If we have already parsed the next elem in the list, use its ready position
            while (children.size()>cindex && children[cindex].first<i+1)
                cindex++;
            if (children.size()>cindex && children[cindex].first==i+1) {
                vc = children[cindex].second->vbegin;
                pVC = vc->data();
                end = pVC + vc->size();
            } else {
                //scan ahead to the next elem in the list
                std::string_view type(t1);
                if (auto err = serialize_scan_by_type_from(type, pVC, end, [](std::string_view &) {}, more_val, false))
                    return {{}, uf::concat("Internal value error in linear_search(): ", err->prepend_type0(t1_.as_view(), type).what())};
                if (is_map) {
                    if (type.empty()) return { {}, uf::concat("Internal value error #bc in linear_search(): ", t1) };
                    if (auto err = serialize_scan_by_type_from(type, pVC, end, [](std::string_view&) {}, more_val, false))
                        return { {}, uf::concat("Internal value error #b in linear_search(): ", err->prepend_type0(t1_.as_view(), type).what()) };
                }
                if (type.size()) return { {}, uf::concat("Internal value error #bd in linear_search(): ", t1, "->", type) };
            }
        }
        return {};
    }
    /// Calculate flattened type string byte size
    uint32_t flatten_type_size() const noexcept { return impl::flatten_size<has_refc, Allocator>(tbegin, tend); }
    //Any optimization with wview& set(wview&& p) ?
    /// Calculate flattened value string byte size
    uint32_t flatten_size() const noexcept { return impl::flatten_size<has_refc, Allocator>(vbegin, vend); }

    /// Copy flattened value bytes to buf
    /// @param buf should have size returned by flatten_size
    void flatten_to(char *buf) const { impl::flatten_to<has_refc, Allocator>(vbegin, vend, buf); }

    operator std::string() const {
        std::string ret = "wv{type: [";
        append_to<has_refc, Allocator>(ret, tbegin, tend);
        ret += "], val: [";
        append_to<has_refc, Allocator>(ret, vbegin, vend);
        return ret + "]}";
    }
    std::string ATTR_NOINLINE__ print() const {
        std::string ret = "wv{type: ";
        append_to_print<has_refc, Allocator>(ret, tbegin, tend);
        ret += ", val: ";
        append_to_print<has_refc, Allocator>(ret, vbegin, vend);
        return ret + "}";
    }

};

template <size_t page_size>
struct MonotonicAllocatorBaseGlobal
{
    static inline std::forward_list<std::array<char, page_size>> pool;
    static inline size_t offset = 0;
};

template <size_t page_size>
struct MonotonicAllocatorBaseThread
{
    static inline thread_local std::forward_list<std::array<char, page_size>> pool;
    static inline thread_local size_t offset = 0;
};

template <template<size_t> typename Base, size_t page_size>
class MonotonicAllocatorBase : Base<page_size>
{
    using Base<page_size>::pool;
    using Base<page_size>::offset;
protected:
    void* do_allocate(size_t l) {
        assert(l <= page_size);
        l = (l + 7) & (-8); //round up to nex multiple of 8 for alignment
        if (!pool.empty() && (offset + l <= page_size)) { size_t ret = offset; offset += l; return ret+pool.front().data(); }
        if (l > page_size) return nullptr;
        pool.emplace_front();
        offset = l;
        return pool.front().data();
    }
public:
    static void clear() noexcept { pool.clear(); offset = 0; }
    static void reset() noexcept { if (!pool.empty()) pool.resize(1); offset = 0; } ///<Keeps one page allocated (but empty)
    constexpr size_t max_size() const noexcept { return page_size; }
};

/** A monotonic allocator with either global or per-thread state.
 * Use MonotonicAllocatorBaseGlobal or MonotonicAllocatorBaseThread for Base.
 * Note that each page_size creates a different allocator with different size.
 * If you clear one, the others remain intact.*/
template <typename T, template<size_t> typename Base, size_t page_size>
class MonotonicAllocator : public MonotonicAllocatorBase<Base, page_size>
{
public:
    using value_type = T;
    template <typename U> struct rebind { using other = MonotonicAllocator<U, Base, page_size>; };
    MonotonicAllocator() noexcept = default;
    template <typename U>
    constexpr MonotonicAllocator(const MonotonicAllocator<U, Base, page_size>&) noexcept {};
    T* allocate(size_t n) { return (T*)this->do_allocate(n * sizeof(T)); }
    static void deallocate(T*, size_t) noexcept {}
    template <typename U>
    constexpr bool operator ==(const MonotonicAllocator<U, Base, page_size>&) noexcept { return true; }
    template <typename U>
    constexpr bool operator !=(const MonotonicAllocator<U, Base, page_size>&) noexcept { return false; }
};

constexpr size_t page_size = 1024 * 1024; //1M
template <typename T>
using GMonoAllocator = MonotonicAllocator<T, MonotonicAllocatorBaseGlobal, page_size>;
template <typename T>
using TMonoAllocator = MonotonicAllocator<T, MonotonicAllocatorBaseThread, page_size>;
} // impl::


/** A shared string view using the default allocator. */
using sview = impl::sview<true, std::allocator>::ptr;
/** A shared writable any view using the default allocator. */
using wview = impl::wview<true, std::allocator>::ptr;

/** A shared string view using a global monotonic allocator. */
using gsview = impl::sview<false, impl::GMonoAllocator>::ptr;
/** A shared writable any view using a global monotonic allocator. */
using gwview = impl::wview<false, impl::GMonoAllocator>::ptr;

/** A shared string view using a thread-local monotonic allocator. */
using tsview = impl::sview<false, impl::TMonoAllocator>::ptr;
/** A shared writable any view using a thread-local monotonic allocator. */
using twview = impl::wview<false, impl::TMonoAllocator>::ptr;

/** Reset the global monotonic allocator, keeping one page allocated (but empty).
 * All uf::gsview and uf::gwview objects become invalid.*/
inline void gallocator_reset() noexcept { impl::MonotonicAllocatorBase<impl::MonotonicAllocatorBaseGlobal, impl::page_size>::reset(); }

/** Reset the thread_local monotonic allocator, keeping one page allocated (but empty).
 * All uf::tsview and uf::twview objects on this thread become invalid.*/
inline void tallocator_reset() noexcept { impl::MonotonicAllocatorBase<impl::MonotonicAllocatorBaseThread, impl::page_size>::reset(); }

} // uf::

/* On the thread safety of wview.
 * - wviews are not thread safe as a general rule.
 * - However, 2 different wviews NOT sharing data (one is not part of the other) never shares chunks, just sviews.
 * - For this reason sviews are thread safe in that
 *   - Their refcount is atomic.
 *   - They cannot be copied, moved, but only freshly initialized from memory.
 *   - As soon as their refocunt ever increases above 1, they become read-only,
 *     even if their refcount falls back to 1. This means that if you create 2 sview::ptrs
 *     to the same swiev, that sview becomes read-only and hence thread safe.
 *
 * To use vwiews in a thread-safe manner do the following.
 * - Use a wview only on one thread at a time.
 * - Sub-views shall not be used concurrently with the parent wview.
 * - You can, however, use wview::set() setting the content of a wview to
 *   the value of some other wview, like A.set(B) - and later use A and B
 *   concurrently.
 */

/* LUA language design

wview is a LUA table that represents a writable view into serialized, strongly typed data.
This data can be the value of a key (from a specific offset) or newly allocated (freshly
created) data. It shall also be possible to pass in wview parameters to the script.


Free functions
TODO '??' means check
- ?? create_list(wview)->wview - a wview, whose type will be used
- ?? create_map(wview, wview) - wviews, whose type will be used for key and value
- ?? create_list_from(view, wview, wview, ...) - must be of the same type
- create_map_from(t) - a table of strings to X mappings ?? why only strings?
- ?? create_any_from(wview) - wrap a wview in an any ?? needed, if yes, can this be the same as uf.create_any() but with an argument?
- ?? create_optional(wview) - just use the type
- create_expected_error(wview x, wview e) - use x just for the type (or typestring)

Functions of the wview
- ?? is_byte() ?? we have is_char
- is_valid() ?? what is this?
- is_empty() ?? and this?

//Children, Parent, siblings (e.g., elements of a list are its children)
- parent()->wview (the wview containing this element or nil if none)
- next()->wview (the next element in the parent container or nil if none)
- prev()->wview (the prev element in the parent container or nil if none)
- valueof()-wview (for a key in a map gives you the value, throws error if not in a map)

//Children manipulation
- ?? insert_before(int/wview/nil, x)  - nil inserts to end ?? we have insert meaning 'insert at'
- ?? insert_before(int/wview/nil, x, y) for maps
- ?? erase_element(int/wview) -> t2 will convert to the remaining member, m erases by key or value
- ?? swap_elements(int/wview, j/wview) - when keys are specified both key and value swapped, when values, then only the values

//These take a function - making them very flexible
- sort_elements(func) - func takes 2 wview (the keys for maps) and returns true for '<'
- upper_bound(wview, lessfunc), lower_bound(wview, lessfunc) - search in sorted lists or maps (sub-linear speed)


Examples:
- Append to a list of strings
uf.view().insert_before(nil, "appended");

- Insert k,v (both wviews) into sorted map, sort is via less()
upper_bound = ww.upper_bound(k, less);
ww.insert_before(upper_bound, k, v);

*/
