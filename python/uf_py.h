#pragma once
#define PY_SSIZE_T_CLEAN //for Python 3.8: see https://docs.python.org/3/c-api/arg.html#strings-and-buffers
#include <Python.h>
#if !defined(PY_MAJOR_VERSION) || PY_MAJOR_VERSION != 3
#error Need Python3.
#endif
#include "ufser.h"

#define UF_MODNAME "ufser"
#define UF_ERRNAME_ONLY "Error"
#define UF_ERRNAME UF_MODNAME "." UF_ERRNAME_ONLY

inline std::string_view PyUnicode_AsUTF8String_view(PyObject *v)
{
    Py_ssize_t len;
    const char * cstr = PyUnicode_AsUTF8AndSize(v, &len);
    return cstr ? std::string_view{ cstr, (size_t)len } : std::string_view{};
}

inline std::string to_string(PyObject *v)
{
    std::string ret;
    if (v == nullptr)
        ret = "null";
    else {
        PyObject* objectsRepresentation = PyObject_Str(v);
        ret.assign(PyUnicode_AsUTF8String_view(objectsRepresentation));
        Py_DECREF(objectsRepresentation);
    }
    return ret;
}

namespace
{

inline PyObject * err(PyObject *type, std::string const &s = "")
{
    PyErr_SetString(type, s.c_str());
    return nullptr;
}

inline PyObject * err(std::string const &s = "")
{
    return err(PyExc_RuntimeError, s);
}

}

/** The object for Error:s. We define it here, so that it is accessable
 * for serialization. */
typedef struct
{
    PyObject_HEAD
    uf::error_value *error;
} uf_error_value;

/** Used during serialization to determine what to do. */
using serialize_output_t = std::variant<
    std::string,                       //append serialized data to this
    std::pair<char *, const char *>,   //store serialized data to 'first' and move it by len. Check that we dont go beyond 'second'
    size_t                             //just add len to this
>;

/** Attempts to serialize a python variable guessing its type or just determining
 * how long the serialized object will be.
 * Lists and dicts are encoded as 'l' and 'm'.
 * Tuples are encoded as tuples ('t').
 * Integers and bools are encoded as i, longs as I, floats as d. Strings as 's'.
 * @param [out] to The string or length we result or a pre-allocated memory to serialize to. We append to/add to it.
 *                 In index #1, we supply the running pointer and where we started from (for future offsets)
 * @param [out] type The type we determine.
 * @param [in] v The Python object to serialize
 * @param [in] liberal If list or dict elements are of different type (or empty list or dict)
 *                     and 'liberal' is set we create an 'la' object (anywhere in the hieararchy),
 *                     else we return an error.
 * @returns On success, the empty string, on failure an error msg.
 * Also, if a list contains only None elements,
 * we thrown an error. Same if a dict has only None keys or values.*/
std::string serialize_append_guess(serialize_output_t &to,
                                   std::string& type, PyObject* v, bool liberal = true);

/** Attempts to serialize a python variable to a specific type or determine
 * the number of bytes needed.
 * @param [out] to The string to append the type description to or the length
 *                 of the result or a pre-alloacted memory to append to.
 * @param [inout] type The typestring to attempt to match. It will be consumed
 *                     from front as type characters are matched.
 *                     You can use 'a' in the type description, so 'la' will be encoded
 *                     as a heterogeneous type list of variable size. These values
 *                     will be encoded as an uf::any in c++.
 *                     If you specify an 'l', dicts, tuples and lists are all accepted.
 *                     If you specify a tuple, lists are also accepted if types match.
 * @param [in] v The Python object to serialize.
 * @returns a non-empty error message on error. */
std::string serialize_append(serialize_output_t &to, std::string_view &type, PyObject* v);

/** Deserialize memory into a python object.
 * We throw a value_error on problems or an error on x<> containing errors.*/
PyObject *deserialize_as_python(std::string_view original_type, std::string_view &type, const char *&p, const char *end);

/** Deserialize memory into a python object.
 * We throw value_error on an error (and then release any python references taken so far. */
inline PyObject *deserialize_as_python(const uf::any_view &value)
{
    const char *p = value.value().data();
    std::string_view ty(value.type());
    return deserialize_as_python(value.type(), ty, p, p + value.value().length());
}

/** Parses through a Python object for serialization or length.
 * @param [in] v The Python object to serialize
 * @param [in] t The typestring to try to match. If empty, we guess the type.
 * @param [in] liberal If true we allow lists and dicts of heterogeneous types via (la and mXa and maX)
 * @param [out] value. The serialized value as allocated string, as into a pre-allocated array or
 *                     just its length. This is the serialized value only not the type.
 * @returns the typestring (extracted from 't' or guessed) We throw a not_serializable_error if the type is a non
 *          serializable type or if liberal is false, and v is a heterogeneous dict or list. We throw an
 *          api_error if the typestring is not a string or None.*/
inline std::string serialize_as_helper(PyObject* v, std::optional<std::string_view> t, bool liberal,
                                       serialize_output_t &value)
{
    assert(value.index()<=2);
    std::string type;
    if (t) {
        type = t.value();
        auto err = serialize_append(value, t.value(), v);
        if (err.length())
            throw uf::not_serializable_error(std::move(err));
    } else {
        auto err = serialize_append_guess(value, type, v, liberal);
        if (err.length())
            throw uf::not_serializable_error(std::move(err));
    }
    return type;
}


/** Determines how long (and what type) a python object serialized as a certain type would be.
 * @param [in] v The Python object to serialize
 * @param [in] t The typestring to try to match. If empty, we guess the type.
 * @param [in] liberal If true we allow lists and dicts of heterogeneous types via (la and mXa and maX)
 * @returns The length of the value only, not the type, and the typestring (extracted from 't'
 * or guessed) We throw a not_serializable_error or api_error on error. */
inline std::pair<size_t, std::string>
serialize_as_len(PyObject* v, std::optional<std::string_view> t, bool liberal)
{
    serialize_output_t value(std::in_place_index<2>, 0);
    std::string type = serialize_as_helper(v, t, liberal, value);
    return { std::get<2>(value), std::move(type) };
}

/** Serializes a python object as a certain type to a pre-allocated buffer that will hold just
 *  enough space.
 * @param [in] v The Python object to serialize
 * @param [in] t The typestring to try to match. If empty, we guess the type.
 * @param [in] liberal If true we allow lists and dicts of heterogeneous types via (la and mXa and maX)
 * @param [out] buff This is the 'pre-allocated' buffer we have talked about above.
 * We throw a not_serializable_error or api_error on error. */
inline void serialize_as_to(PyObject* v, std::optional<std::string_view> t, bool liberal, char* buff)
{
    serialize_output_t value(std::in_place_index<1>, buff, buff);
    serialize_as_helper(v, t, liberal, value);
}


/** Serializes a python object as a certain type.
 * @param [in] v The Python object to serialize
 * @param [in] t The typestring to try to match. If empty, we guess the type.
 * @param [in] liberal If true we allow lists and dicts of heterogeneous types via (la and mXa and maX)
 * We throw a not_serializable_error or api_error on error. */
inline uf::any serialize_as(PyObject *v, std::optional<std::string_view> t = {}, bool liberal = true)
{
    serialize_output_t value(std::in_place_index<0>);
    std::string type = serialize_as_helper(v, t, liberal, value);
    return { uf::from_type_value, std::move(type), std::move(std::get<0>(value)) };
}
