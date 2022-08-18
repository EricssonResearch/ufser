#include "uf_py.h"

auto error_value_check(PyObject *arg)
{ return arg->ob_type->tp_name == std::string_view(UF_ERRNAME) ? (uf_error_value *)arg : nullptr; }

static void serialize_append_uint32(serialize_output_t& to, uint32_t size)
{
    switch (to.index()) {
    case 0:
    {
        std::get<0>(to).append(4, 0);
        char* p = &std::get<0>(to).back() - 3;
        uf::impl::serialize_to(size, p);
        break;
    }
    case 1: uf::impl::serialize_to(size, std::get<1>(to).first); break;
    case 2: std::get<2>(to) += 4; break;
    default: assert(0);
    }
}

std::string serialize_append_guess(serialize_output_t &to,
                                   std::string& type, PyObject* v, bool liberal)
{
    if (v==Py_None) return {};
    if (v==Py_False || v==Py_True) {
        switch (to.index()) {
            case 0: std::get<0>(to).push_back(v==Py_True); break;
            case 1: *(std::get<1>(to).first++) = v==Py_True; break;
            case 2: std::get<2>(to) += 1; break;
            default: assert(0); 
        }
        type.push_back('b');
        return {};
    }
    if (PyLong_Check(v)) {
        switch (to.index()) {
        case 0: std::get<0>(to).append(uf::serialize(int64_t(PyLong_AsSsize_t(v)))); break;
        case 1: uf::impl::serialize_to(int64_t(PyLong_AsSsize_t(v)), std::get<1>(to).first); break;
        case 2: std::get<2>(to) += 8;
        default: assert(0);
        }
        type.push_back('I');
        return {};
    }
    if (PyFloat_Check(v)) {
        switch (to.index()) {
        case 0: std::get<0>(to).append(uf::serialize(double(PyFloat_AsDouble(v)))); break;
        case 1: uf::impl::serialize_to(double(PyFloat_AsDouble(v)), std::get<1>(to).first); break;
        case 2: std::get<2>(to) += 8; break;
        default: assert(0);
        }
        type.push_back('d');
        return {};
    }
    if (PyUnicode_Check(v)) {
        const std::string_view sv = PyUnicode_AsUTF8String_view(v);
        switch (to.index()) {
        case 0: std::get<0>(to).append(uf::serialize(sv)); break;
        case 1: uf::impl::serialize_to(sv, std::get<1>(to).first); break;
        case 2: std::get<2>(to) += uf::impl::serialize_len(sv); break;
        default: assert(0);
        }
        type.push_back('s');
        return {};
    }
    if (PyBytes_Check(v)) {
        const std::string_view sv(PyBytes_AsString(v), PyBytes_Size(v));
        switch (to.index()) {
        case 0: std::get<0>(to).append(uf::serialize(sv)); break;
        case 1: uf::impl::serialize_to(sv, std::get<1>(to).first); break;
        case 2: std::get<2>(to) += uf::impl::serialize_len(sv); break;
        default: assert(0);
        }
        type.append("lc");
        return {};
    }
    if (PyTuple_Check(v)) {
        if (PyTuple_Size(v)==0) return {}; //void
        if (PyTuple_Size(v)==1)
            return serialize_append_guess(to, type, PyTuple_GetItem(v, 0), liberal);
        type.push_back('t');
        type.append(std::to_string(PyTuple_Size(v)));
        for (unsigned u = 0; u<PyTuple_Size(v); u++) 
            if (PyObject* item = PyTuple_GetItem(v, u); item == Py_None) {
                //None items are represented in a tuple as an empty any.
                //This is to preserve tuple size - which the user maybe wants.
                serialize_append_uint32(to, 0);
                serialize_append_uint32(to, 0);
                type.push_back('a');
            } else {
                const size_t orig_size = type.size();
                auto err = serialize_append_guess(to, type, PyTuple_GetItem(v, u), liberal);
                if (err.length())
                    return err;
                if (type.size() == orig_size)
                    return "Python tuple member generated no type: " + to_string(PyTuple_GetItem(v, u));
            }
        return {};
    }
    if (PyDict_Check(v)) {
        const uint32_t size = PyDict_Size(v);
        serialize_append_uint32(to, size);
        if (size==0) {
            type.append("maa");
            return {};
        }
        std::string key_type;
        std::string mapped_type;
        //a saved value to be restored to
        const std::variant<size_t, char*> original =
            to.index()==0 ? std::variant<size_t, char*>(std::in_place_index<0>, std::get<0>(to).length()) :
            to.index()==1 ? std::variant<size_t, char*>(std::in_place_index<1>, std::get<1>(to).first) :
            to.index()==2 ? std::variant<size_t, char*>(std::in_place_index<0>, std::get<2>(to)) : 
            std::variant<size_t, char *>(std::in_place_index<0>, 0);
        PyObject *key, *value;
        bool key_auto = false, mapped_auto = false;
        bool restart;
        do {
            restart = false;
            //restore 'original'
            switch (to.index()) {
            case 0: std::get<0>(to).resize(std::get<0>(original)); break;
            case 1: std::get<1>(to).first = std::get<1>(original); break;
            case 2: std::get<2>(to) = std::get<0>(original);
            default: assert(0);
            }
            Py_ssize_t pos = 0;
            while (PyDict_Next(v, &pos, &key, &value)) {
                if (key_auto) {
                    std::string_view p = "a";
                    auto err = serialize_append(to, p, key);
                    if (err.length())
                        return err;
                } else {
                    std::string tmp_key_type;
                    auto err = serialize_append_guess(to, tmp_key_type, key, liberal);
                    if (err.length())
                        return err;
                    if (key_type.length()==0)
                        key_type = std::move(tmp_key_type);
                    else if (key_type!=tmp_key_type) {
                        if (liberal) {
                            key_auto = true;
                            key_type = "a";
                            restart = true;
                            break;
                        } else {
                            return uf::concat("Cannot serialize: non-uniform key types ('", key_type,
                                              "' vs. '", tmp_key_type, "') in dictionary: '", v, "'.");
                        }
                    }
                }
                if (mapped_auto) {
                    std::string_view p = "a";
                    auto err = serialize_append(to, p, value);
                    if (err.length())
                        return err;
                } else {
                    std::string tmp_mapped_type;
                    auto err = serialize_append_guess(to, tmp_mapped_type, value, liberal);
                    if (err.length())
                        return err;
                    if (mapped_type.length()==0)
                        mapped_type = std::move(tmp_mapped_type);
                    else if (mapped_type!=tmp_mapped_type) {
                        if (liberal) {
                            mapped_auto = true;
                            mapped_type = "a";
                            restart = true;
                            break;
                        } else {
                            return uf::concat("Cannot serialize: non-uniform value types ('", mapped_type,
                                              "' vs. '", tmp_mapped_type, "') in dictionary: '", v, "'.");
                        }
                    }
                }
            }
        } while (restart);
        if (key_type.length()==0)
            return uf::concat("Cannot serialize: all keys (", PyDict_Size(v), ") are None in dict.");
        if (mapped_type.length()==0)
            return uf::concat("Cannot serialize: all values (", PyDict_Size(v), ") are None in dict.");
        type.push_back('m');
        type.append(key_type);
        type.append(mapped_type);
        return {};
    }
    if (PyList_Check(v)) {
        const uint32_t size = PyList_Size(v);
        serialize_append_uint32(to, size);
        if (size==0) {
            type.append("la");
            return {};
        }
        std::string my_type;
        //a saved value to be restored to
        const std::variant<size_t, char*> original =
            to.index()==0 ? std::variant<size_t, char*>(std::in_place_index<0>, std::get<0>(to).length()) :
            to.index()==1 ? std::variant<size_t, char*>(std::in_place_index<1>, std::get<1>(to).first) :
            to.index()==2 ? std::variant<size_t, char*>(std::in_place_index<0>, std::get<2>(to)) : 
            std::variant<size_t, char *>(std::in_place_index<0>, 0);
        for (unsigned u = 0; u<size; u++) {
            std::string tmp_type;
            auto err = serialize_append_guess(to, tmp_type, PyList_GetItem(v, u), liberal);
            if (err.length())
                return err;
            if (u==0)
                my_type = std::move(tmp_type);
            else if (my_type!=tmp_type) {
                if (liberal)
                    goto list_again_as_any;
                else
                    return uf::concat("Cannot serialize: non-uniform types ('", my_type,
                                      "' vs. '", tmp_type, "') in list: '", v, "'.");
            }
        }
        if (my_type.length() == 0) {
            if (liberal) goto list_again_as_any;
            return uf::concat("Cannot serialize: all elements (", PyList_Size(v), ") are None in list.");
        }
        type.push_back('l');
        type.append(my_type);
        return {};
    list_again_as_any:
        //turn into any - start over and do it again
        //restore 'original'
        switch (to.index()) {
        case 0: std::get<0>(to).resize(std::get<0>(original)); break;
        case 1: std::get<1>(to).first = std::get<1>(original); break;
        case 2: std::get<2>(to) = std::get<0>(original);
        default: assert(0);
        }
        for (unsigned u = 0; u < size; u++) {
            std::string_view p = "a";
            auto err = serialize_append(to, p, PyList_GetItem(v, u));
            if (err.length())
                return err;
        }
        type.append("la");
        return {};
    }
    if (PySet_Check(v)) {
        const uint32_t size = PySet_Size(v);
        serialize_append_uint32(to, size);
        if (size == 0) {
            type.append("la");
            return {};
        }
        std::optional<std::string> my_type;
        //a saved value to be restored to
        const std::variant<size_t, char*> original =
            to.index()==0 ? std::variant<size_t, char*>(std::in_place_index<0>, std::get<0>(to).length()) :
            to.index()==1 ? std::variant<size_t, char*>(std::in_place_index<1>, std::get<1>(to).first) :
            to.index()==2 ? std::variant<size_t, char*>(std::in_place_index<0>, std::get<2>(to)) : 
            std::variant<size_t, char *>(std::in_place_index<0>, 0);
        PyObject *iterator = PyObject_GetIter(v);
        PyObject *item;
        while ((item = PyIter_Next(iterator))) {
            std::string tmp_type;
            auto err = serialize_append_guess(to, tmp_type, item, liberal);
            Py_DECREF(item);
            if (err.length()) {
                Py_DECREF(iterator);
                return err;
            }
            if (!my_type)
                my_type = std::move(tmp_type);
            else if (*my_type != tmp_type) {
                Py_DECREF(iterator);
                if (liberal)
                    goto again_as_any;
                else
                    return uf::concat("Cannot serialize: non-uniform types ('",
                                      *my_type, "' vs. '", tmp_type, "') in set: '", v, "'.");
            }
        }
        Py_DECREF(iterator);
        if (PyErr_Occurred())
            return "Could not iterate set.";
        if (my_type->length() == 0) {
            if (liberal) goto again_as_any;
            return uf::concat("Cannot serialize: all elements (", PySet_Size(v), ") are None in list.");
        }
        type.push_back('l');
        type.append(*my_type);
        return {};
    again_as_any:
        //turn into any - start over and do it again
        //restore 'original'
        switch (to.index()) {
        case 0: std::get<0>(to).resize(std::get<0>(original)); break;
        case 1: std::get<1>(to).first = std::get<1>(original); break;
        case 2: std::get<2>(to) = std::get<0>(original);
        default: assert(0);
        }
        iterator = PyObject_GetIter(v);
        while ((item = PyIter_Next(iterator))) {
            std::string_view p = "a";
            auto err = serialize_append(to, p, item);
            Py_DECREF(item);
            if (err.length()) {
                Py_DECREF(iterator);
                return err;
            }
        }
        Py_DECREF(iterator);
        if (PyErr_Occurred())
            return "Cannot serialize: could not iterate set.";
        type.append("la");
        return {};
    }
    if (auto e = error_value_check(v)) {
        auto error = e->error;
        if (!error)
            return "Cannot serialize invalid " UF_ERRNAME;
        switch (to.index()) {
        default: assert(0); break;
        case 4: break;
        case 3: break;
        case 2: std::get<2>(to) += uf::impl::serialize_len(*error); break;
        case 1: uf::impl::serialize_to(*error, std::get<1>(to).first); break;
        case 0:
            std::get<0>(to).reserve(std::get<0>(to).length() + uf::impl::serialize_len(*error));
            char *p = std::get<0>(to).data() + std::get<0>(to).length(); //current pos at end
            uf::impl::serialize_to(*error, p);
            break;
        }
        type.push_back('e');
        return {};
    }
    return uf::concat("Cannot serialize this value: '", to_string(v), "' of type '", to_string((PyObject*)Py_TYPE(v)), "' (to.index=", to.index(), ").");
}

std::string serialize_append(serialize_output_t &to, std::string_view &type, PyObject* v)
{
    assert(to.index()<=2);
    if (v==nullptr) return "Internal python error: Cannot serialize null object.";
    if (type.empty()) {
        if (v==Py_None) return {};
        //a zero length tuple also passes as a void
        if (PyTuple_Check(v) && PyTuple_Size(v)==0) return {}; //void
        return "Empty type string or type string exhausted and still values remain.";
    }
    switch (type.front()) {
    case 'b':
        if (v == Py_False || v==Py_True) {
            type.remove_prefix(1);
            if (to.index()==0)
                std::get<0>(to).push_back(v==Py_True);
            else if (to.index()==1)
                *(std::get<1>(to).first++) = v==Py_True;
            else
                std::get<2>(to)++;
            return {};
        }
        return uf::concat("Cannot serialize '", v, "' as bool.");
    case 's': {
        if (!PyUnicode_Check(v))
            return uf::concat("Cannot serialize '", v, "' as string.");
        const std::string_view sv = PyUnicode_AsUTF8String_view(v);
        if (to.index()==0) {
            std::get<0>(to).append(uf::serialize(sv));
        } else if (to.index()==1) {
            uf::impl::serialize_to(sv, std::get<1>(to).first);
        } else
            std::get<2>(to) += uf::impl::serialize_len(sv);
        type.remove_prefix(1);
        return {};
    }
    case 'i':
    case 'I': {
        if (!PyLong_Check(v) && !PyBool_Check(v))
            return uf::concat("Cannot serialize '", v, "' as int.");
        Py_ssize_t val = PyLong_Check(v) ? PyLong_AsSsize_t(v) :
            v==Py_True;
        if (type.front()=='i') {
            if (val<-0x100000000 || val>=0x100000000)
                return uf::concat("Value '", val, "' does not fit into 32 bits for 'i'.");
            if (to.index()==0) {
                std::get<0>(to).append(4, 0);
                char *p = &std::get<0>(to).back()-3;
                uf::impl::serialize_to(uint32_t(val), p);
            } else if (to.index()==1)
                uf::impl::serialize_to(uint32_t(val), std::get<1>(to).first);
            else
                std::get<2>(to) += 4;
        } else {
            if (to.index()==0) {
                std::get<0>(to).append(8, 0);
                char *p = &std::get<0>(to).back()-7;
                uf::impl::serialize_to(int64_t(val), p);
            } else if (to.index()==1)
                uf::impl::serialize_to(int64_t(val), std::get<1>(to).first);
            else
                std::get<2>(to) += 8;
        }
        type.remove_prefix(1);
        return {};
    }
    case 'd': {
        if (!PyLong_Check(v) && !PyBool_Check(v) && !PyFloat_Check(v))
            return uf::concat("Cannot serialize '", v, "' as float.");
        double val = PyFloat_Check(v) ? PyFloat_AsDouble(v) :
            PyLong_Check(v) ? double(PyLong_AsSsize_t(v)) :
            v==Py_True ? 1.0 : 0.0;
        if (to.index()==0) {
            std::get<0>(to).append(8, 0);
            char *p = &std::get<0>(to).back()-7;
            uf::impl::serialize_to(val, p);
        } else if (to.index()==1)
            uf::impl::serialize_to(val, std::get<1>(to).first);
        else
            std::get<2>(to) += 8;
        type.remove_prefix(1);
        return {};
    }
    case 'a': {
        std::string my_type;
        if (to.index()==2) {
            type.remove_prefix(1);
            std::string err = serialize_append_guess(to, my_type, v, true); //guess type
            std::get<2>(to) += 4 + my_type.length() + 4; //add the length of 'value' then the length of serialized 'type';
            return err;
        }
        serialize_output_t my_value(std::in_place_index<0>);
        if (v!=Py_None) {
            auto err = serialize_append_guess(my_value, my_type, v, true); //guess type
            if (err.length())
                return err;
        }
        uf::any_view val(uf::from_type_value, my_type, std::get<0>(my_value));
        uint32_t len = uf::impl::serialize_len(val);
        if (to.index()==0) {
            std::get<0>(to).append(len, 0);
            char *p = &std::get<0>(to).back()-len+1;
            uf::impl::serialize_to(val, p);
        } else {
            //index()==1
            uf::impl::serialize_to(val, std::get<1>(to).first);
        }
        type.remove_prefix(1);
        return {};
    }
    case 'x':
    case 'X': {
        const bool is_void = type.front() == 'X';
        type.remove_prefix(1);
        if (error_value_check(v)) {
            //step over target type
            if (!is_void) {
                uint32_t type_len = uf::parse_type(type);
                if (type_len == 0)
                    return uf::concat("Invalid type string: '", type, "'.");
                type.remove_prefix(type_len);
            }
            if (!((uf_error_value*)v)->error)
                return "Cannot serialize invalid future.";
            bool has_value = false;
            auto t = std::tie(has_value, *((uf_error_value*)v)->error);
            switch (to.index()) {
            case 2:
                std::get<2>(to) += uf::impl::serialize_len(t);
                break;
            case 1:
                uf::impl::serialize_to(t, std::get<1>(to).first);
                break;
            case 0:
                std::get<0>(to).reserve(std::get<0>(to).length() + uf::impl::serialize_len(t));
                char *p = std::get<0>(to).data() + std::get<0>(to).length(); //current pos at end
                uf::impl::serialize_to(t, p);
                break;
            }
            return {};
        }
        //OK, not an error, not a future. Try to serialize an expected with the value
        //Add a true 'has_value'
        switch (to.index()) {
        case 2:
            std::get<2>(to) += 1;
            break;
        case 1:
            uf::impl::serialize_to(true, std::get<1>(to).first);
            break;
        case 0:
            std::get<0>(to).reserve(std::get<0>(to).length() + 1);
            char *p = std::get<0>(to).data() + std::get<0>(to).length(); //current pos at end
            uf::impl::serialize_to(true, p);
            break;
        }
        if (is_void) return {};
        return serialize_append(to, type, v);
    }
    case 'e': 
        if (error_value_check(v)) {
            type.remove_prefix(1);
            if (!((uf_error_value*)v)->error)
                return "Cannot serialize invalid future.";
            auto error = ((uf_error_value*)v)->error;
            switch (to.index()) {
            case 2:
                std::get<2>(to) += uf::impl::serialize_len(*error);
                break;
            case 1:
                uf::impl::serialize_to(*error, std::get<1>(to).first);
                break;
            case 0:
                std::get<0>(to).reserve(std::get<0>(to).length() + uf::impl::serialize_len(*error));
                char *p = std::get<0>(to).data() + std::get<0>(to).length(); //current pos at end
                uf::impl::serialize_to(*error, p);
                break;
            }
            return {};
        }
        return uf::concat("Cannot serialize '", v, "' as 'e'.");
    case 'o': {
        type.remove_prefix(1);
        //Add a 'has_value'
        const bool has_value = (v != Py_None);
        switch (to.index()) {
        case 2:
            std::get<2>(to) += 1;
            break;
        case 1:
            uf::impl::serialize_to(has_value, std::get<1>(to).first);
            break;
        case 0:
            std::get<0>(to).reserve(std::get<0>(to).length() + 1);
            char *p = std::get<0>(to).data() + std::get<0>(to).length(); //current pos at end
            uf::impl::serialize_to(has_value, p);
            break;
        }
        if (has_value)
            return serialize_append(to, type, v);
        //step over type
        if (uint32_t type_len = uf::parse_type(type))
            type.remove_prefix(type_len);
        else
            return uf::concat("Invalid type string: '", type, "'.");
        return {};
    }
    case 'l':
        if (type.size()>=2 && type[1]=='c' && PyBytes_Check(v)) {
            //hah, this is a bytestream and we want 'lc', good. Do that
            std::string dummy_type;
            return serialize_append_guess(to, dummy_type, v, false);
        } else {
            const bool is_tuple = PyTuple_Check(v);
            const bool is_dict = PyDict_Check(v);
            const bool is_list = PyList_Check(v);
            const Py_ssize_t len = is_tuple ? PyTuple_Size(v) : is_dict ? PyDict_Size(v) : is_list ? PyList_Size(v) : 0;
            if (is_tuple || is_list || is_dict || v==Py_None) {
                if (to.index()==0) {
                    std::get<0>(to).append(4, 0);
                    char *p = &std::get<0>(to).back()-3;
                    uf::impl::serialize_to(uint32_t(len), p);
                } else if (to.index()==1)
                    uf::impl::serialize_to(uint32_t(len), std::get<1>(to).first);
                else
                    std::get<2>(to) += 4;
                type.remove_prefix(1);
            } else
                return uf::concat("Cannot serialize '", v, "' as list.");
            if (len==0) {
                uint32_t type_len = uf::parse_type(type);
                if (type_len==0)
                    return uf::concat("Invalid type string: '", type, "'.");
                type.remove_prefix(type_len);
                return {};
            }
            const std::string_view t_save = type;
            if (is_dict) {
                PyObject *key, *value;
                Py_ssize_t pos = 0;
                while (PyDict_Next(v, &pos, &key, &value)) {
                    PyObject *pair = PyTuple_Pack(2, key, value);
                    auto err = serialize_append(to, type = t_save, pair);
                    Py_DECREF(pair);
                    if (err.length())
                        return err;
                }
            } else
                for (unsigned u = 0; u<len; u++) {
                    auto err = serialize_append(to, type = t_save, is_tuple ? PyTuple_GetItem(v, u) : PyList_GetItem(v, u));
                    if (err.length())
                        return err;
                }
            //t already in good position
            return {};
        }
    case 'm':
        if (PyDict_Check(v)) {
            const Py_ssize_t len = PyDict_Size(v);
            if (to.index()==0) {
                std::get<0>(to).append(4, 0);
                char *p = &std::get<0>(to).back()-3;
                uf::impl::serialize_to(uint32_t(len), p);
            } else if (to.index()==1)
                uf::impl::serialize_to(uint32_t(len), std::get<1>(to).first);
            else
                std::get<2>(to) += 4;
            type.remove_prefix(1);
            if (len==0) {
                uint32_t type_len = uf::parse_type(type);
                if (type_len==0)
                    return uf::concat("Invalid type string: '", type, "'.");
                type.remove_prefix(type_len);
                return {};
            } else {
                const std::string_view t_save = type;
                PyObject *key, *value;
                Py_ssize_t pos = 0;
                while (PyDict_Next(v, &pos, &key, &value)) {
                    type = t_save;
                    auto err = serialize_append(to, type, key);
                    if (err.length())
                        return err;
                    err = serialize_append(to, type, value);
                    if (err.length())
                        return err;
                }
                //type already in good position
                return {};
            }
        }
        return uf::concat("Cannot serialize '", v, "' as dict.");
    case 't':
        if (PyTuple_Check(v) || PyList_Check(v)) {
            type.remove_prefix(1);
            unsigned len = 0;
            while ('0'<=type.front() && type.front()<='9') {
                len = len*10 + type.front() -'0';
                type.remove_prefix(1);
            }
            const bool is_tuple = PyTuple_Check(v);
            const Py_ssize_t clen = is_tuple ? PyTuple_Size(v) : PyList_Size(v);
            if (clen!=len)
                return uf::concat("Attempt to serialize a ", PyList_Check(v) ? "list" : "tuple", " of size ",
                                    clen, " into a tuple of ", len, " size: '", v, "'.");
            for (unsigned u = 0; u<len; u++) {
                auto err = serialize_append(to, type, is_tuple ? PyTuple_GetItem(v, u) : PyList_GetItem(v, u));
                if (err.length())
                    return err;
            }
            return {};
        }
        return uf::concat("Cannot serialize '", v, "' as tuple.");
    default:
        return uf::concat("Invalid type string: '", type, "'.");
    }
}

PyObject *deserialize_as_python(std::string_view original_type, std::string_view &type, const char *&p, const char *end)
{
    if (type.empty()) {
        if (p==end)
            Py_RETURN_NONE;
    value_mismatch:
        throw uf::value_mismatch_error(uf::concat(uf::impl::ser_error_str(uf::impl::ser::val), " (python) <%1>."), 
                                       original_type, type.data()-original_type.data());
    }
    switch (type.front()) {
    case 's': {
        uint32_t len = 0;
        if (uf::impl::deserialize_from<false>(p, end, len)) goto value_mismatch;
        PyObject *ret = PyUnicode_FromStringAndSize(p, len);
        if (!ret) {
            PyErr_Clear();
            ret = PyByteArray_FromStringAndSize(p, len);
        }
        p += len;
        type.remove_prefix(1);
        return ret;
    }
    case 'c': {
        char val = 0;
        if (uf::impl::deserialize_from<false>(p, end, val)) goto value_mismatch;
        type.remove_prefix(1);
        PyObject* ret = PyUnicode_FromStringAndSize(&val, 1);
        if (!ret) {
            PyErr_Clear();
            ret = PyByteArray_FromStringAndSize(&val, 1);
        }
        return ret;
    }
    case 'b': {
        bool val = false;
        if (uf::impl::deserialize_from<false>(p, end, val)) goto value_mismatch;
        type.remove_prefix(1);
        if (val) Py_RETURN_TRUE;
        Py_RETURN_FALSE;
    }
    case 'i': {
        int32_t val = 0;
        if (uf::impl::deserialize_from<false>(p, end, val)) goto value_mismatch;
        type.remove_prefix(1);
        return PyLong_FromLong(val);
    }
    case 'I': {
        int64_t val = 0;
        if (uf::impl::deserialize_from<false>(p, end, val)) goto value_mismatch;
        type.remove_prefix(1);
        return PyLong_FromLong(val);
    }
    case 'd': {
        double val = 0;
        if (uf::impl::deserialize_from<false>(p, end, val)) goto value_mismatch;
        type.remove_prefix(1);
        return PyFloat_FromDouble(val);
    }
    case 'a': {
        uf::any_view val;
        if (uf::impl::deserialize_from<true>(p, end, val)) goto value_mismatch;
        type.remove_prefix(1);
        const char *p2 = val.value().data();
        std::string_view ty(val.type());
        try {
            return deserialize_as_python(val.type(), ty, p2, p2+val.value().length());
        } catch (uf::value_error &e) {
            const size_t consumed = type.data()-original_type.data();
            e.types[0].prepend('(');
            e.types[0].prepend(original_type.substr(0, consumed)); //also updates 'pos'
            e.types[0].type.push_back(')');
            e.types[0].type.append(type);
            e.regenerate_what();
            throw;
        }
    }
    case 'l':
        if (type.length() && type[1]=='c') {
            //hah, this must be a bytestring
            uint32_t len = 0;
            if (uf::impl::deserialize_from<false>(p, end, len)) goto value_mismatch;
            PyObject *ret = PyBytes_FromStringAndSize(p, len);
            p += len;
            type.remove_prefix(2);
            return ret;
        } else {
            uint32_t size = 0;
            if (uf::impl::deserialize_from<false>(p, end, size)) goto value_mismatch;
            PyObject *val = PyList_New(size);
            type.remove_prefix(1);
            if (size) {
                const std::string_view my_type = type;
                for (unsigned u = 0; u<size; u++) {
                    try {
                        type = my_type;
                        PyList_SetItem(val, u, deserialize_as_python(original_type, type, p, end));
                    } catch (...) {
                        Py_DECREF(val); //also kills objects already added
                        throw;
                    }
                }
            } else {
                if (auto [l, err] = uf::impl::parse_type(type, false); !err)
                    type.remove_prefix(l);
                else {
                    Py_DECREF(val);
                    throw uf::typestring_error(uf::concat(uf::impl::ser_error_str(err), " (python) <%1>."), 
                                               original_type, type.data()-original_type.data()+l);
                }
            }
            return val;
        }
    case 'm': {
        uint32_t size = 0;
        if (uf::impl::deserialize_from<false>(p, end, size)) goto value_mismatch;
        PyObject *val = PyDict_New();
        type.remove_prefix(1);
        if (size) {
            std::string_view my_type = type;
            for (unsigned u = 0; u<size; u++) {
                PyObject *key = nullptr;
                PyObject *value = nullptr;
                try {
                    type = my_type;
                    key = deserialize_as_python(original_type, type, p, end);
                    value = deserialize_as_python(original_type, type, p, end);
                } catch (...) {
                    Py_XDECREF(key);
                    Py_XDECREF(value);
                    Py_DECREF(val); //also kills objects already added
                    throw;
                }
                if (-1==PyDict_SetItem(val, key, value)) {
                    auto s = to_string(key);
                    Py_XDECREF(key);
                    Py_XDECREF(value);
                    Py_DECREF(val); //also kills objects already added
                    throw uf::value_mismatch_error(uf::concat("Error in inserting to dictionary: '", s, "'."),
                                                   original_type, type.data()-original_type.data());
                }
            }
        } else {
            for (int i = 0; i<2; i++) {
                if (auto [l,err] = uf::impl::parse_type(type, false); !err)
                    type.remove_prefix(l);
                else {
                    Py_DECREF(val);
                    throw uf::typestring_error(uf::concat(uf::impl::ser_error_str(err), " (python) <%1>."),
                                               original_type, type.data()-original_type.data()+l);
                }
            }
        }
        return val;
    }
    case 't': {
        type.remove_prefix(1);
        unsigned size = 0;
        while ('0'<=type.front() && type.front()<='9') {
            size = size*10 + type.front()-'0';
            type.remove_prefix(1);
        }
        PyObject *val = PyTuple_New(size);
        for (unsigned u = 0; u<size; u++) {
            try {
                PyTuple_SetItem(val, u, deserialize_as_python(original_type, type, p, end));
            } catch (...) {
                Py_DECREF(val); //also kills objects already added
                throw;
            }
        }
        return val;
    }
    case 'x':
    case 'X': {
        const bool is_void = type.front() == 'X';
        bool has_value = false;
        if (uf::impl::deserialize_from<false>(p, end, has_value)) goto value_mismatch;
        type.remove_prefix(1);
        if (has_value) {
            if (is_void) Py_RETURN_NONE;
            return deserialize_as_python(original_type, type, p, end);
        }
        if (!is_void) {
            if (auto [l, err] = uf::impl::parse_type(type, false); !err)
                type.remove_prefix(l);
            else
                throw uf::typestring_error(uf::concat(uf::impl::ser_error_str(err), " (python) <%1>."), 
                                           original_type, type.data()-original_type.data()+l);
        }
    } [[fallthrough]]; //fallthrough to error
    case 'e': {
        auto ret = [] {
            auto mod = PyDict_GetItemString(PyImport_GetModuleDict(), UF_MODNAME);
            if (!mod)
                throw uf::error("Module '" UF_MODNAME "' needs to be loaded to deserialize an " UF_ERRNAME ".");
            auto err_type = PyDict_GetItemString(PyModule_GetDict(mod), UF_ERRNAME_ONLY);
            if (!err_type)
                throw uf::error("Module '" UF_MODNAME "' lacks " UF_ERRNAME ".");
            //all borrowed references so far
            if (auto ret = PyObject_Call(err_type, nullptr, nullptr))
                return ret;
            throw uf::error(UF_ERRNAME "() call failed.");
        }();
        if (!ret)
            return ret;
        if (uf::impl::deserialize_from<false>(p, end, *((uf_error_value*)ret)->error)) {
            Py_DECREF(ret);
            goto value_mismatch;
        }
        type.remove_prefix(1);
        return ret;
    }
    case 'o': {
        bool has_value = false;
        if (uf::impl::deserialize_from<false>(p, end, has_value)) goto value_mismatch;
        type.remove_prefix(1);
        if (has_value)
            return deserialize_as_python(original_type, type, p, end);
        if (auto [l, err] = uf::impl::parse_type(type, false); !err)
            type.remove_prefix(l);
        else
            throw uf::typestring_error(uf::concat(uf::impl::ser_error_str(err), " (python) <%1>."),
                                       original_type, type.data()-original_type.data()+l);
        Py_RETURN_NONE;
    }
    default:
        throw uf::typestring_error(uf::concat(uf::impl::ser_error_str(uf::impl::ser::chr), " (python) <%1>."), 
                                   original_type, type.data()-original_type.data());
    }
}
