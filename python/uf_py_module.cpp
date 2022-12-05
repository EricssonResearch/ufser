#define PY_SSIZE_T_CLEAN //for Python 3.8: see https://docs.python.org/3/c-api/arg.html#strings-and-buffers
#include <Python.h>
#if !defined(PY_MAJOR_VERSION) || PY_MAJOR_VERSION != 3
#error Need Python3.
#endif
#include "uf_py.h"
#include <cxxabi.h>

namespace {

std::optional<std::string> demangle(const char *name) {
    std::optional<std::string> ret;
    int status;
    char *n = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    if (n && !status) ret.emplace(n);
    if (n) free(n);
    return ret;
}

std::string current_exception_type() {
    auto r = demangle(abi::__cxa_current_exception_type()->name());
    if (r) return std::move(*r);
    return abi::__cxa_current_exception_type()->name();
}

extern "C" {

PyObject *python_serialize(PyObject *, PyObject *args, PyObject *kwargs) {
    PyObject *obj = nullptr;
    int liberal = true, type_value = false;
    const char *type_ptr = nullptr;
    Py_ssize_t type_len = 0;
    static char const *kws[] = {"value", "liberal", "type", "type_value", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|pz#p", const_cast<char **>(kws),
                                     &obj, &liberal, &type_ptr, &type_len, &type_value))
        return nullptr;
    std::optional<std::string_view> type;
    if (type_ptr) type = std::string_view(type_ptr, type_len);
    try {
        const uf::any val = serialize_as(obj, type, liberal ? uf::impl::ParseMode::Liberal : uf::impl::ParseMode::Normal);
        if (type_value) {
            return Py_BuildValue("y#y#", val.type().data(), val.type().size(), val.value().data(), val.value().size());
        } else {
            std::string ser = uf::serialize(val);
            return PyBytes_FromStringAndSize(ser.data(), ser.size());
        }
    } catch (uf::value_error const &e) {
        return err(PyExc_ValueError, e.what());
    } catch (uf::api_error const &e) {
        return err(PyExc_AttributeError, e.what());
    } catch (std::bad_alloc const &e) {
        return err(PyExc_MemoryError, e.what());
    } catch (std::exception const &e) {
        return err(PyExc_RuntimeError, e.what());
    } catch (...) {
        return err(PyExc_RuntimeError, "unhandled C++ exception of type "+current_exception_type());
    }
}

PyObject *python_deserialize(PyObject *, PyObject *args) {
    Py_buffer buff;
    if (!PyArg_ParseTuple(args, "y*", &buff))
        return nullptr;
    try {
        PyObject *ret = deserialize_as_python(uf::any_view{uf::from_raw, std::string_view{(char*)buff.buf, (size_t)buff.len}});
        PyBuffer_Release(&buff);
        return ret;
    } catch (uf::value_error const &e) {
        PyBuffer_Release(&buff);
        return err(PyExc_ValueError, e.what());
    } catch (std::bad_alloc const &e) {
        PyBuffer_Release(&buff);
        return err(PyExc_MemoryError, e.what());
    } catch (std::exception const &e) {
        PyBuffer_Release(&buff);
        return err(PyExc_RuntimeError, e.what());
    } catch (...) {
        PyBuffer_Release(&buff);
        return err(PyExc_RuntimeError, "unhandled C++ exception of type "+current_exception_type());
    }
}

PyMethodDef methods[] = {
    {"serialize", (PyCFunction)python_serialize, METH_VARARGS | METH_KEYWORDS, "Serialize the Python value into a bytes object in memory: 'serialize(value, liberal=True, type=None, type_value=False)'. Setting liberal allows serializing heterogeneous lists and dicts with 'la' or 'maa' types; You can specify a wanted type (ValueError is raised if 'value' is not that type). Returns a bytes object that contains both type and value encoded and can be fed to 'deserialize', but if type_value is True, a two-element tuple is returned with 2 bytes objects separate for typestring and serialized value."},
    {"deserialize", python_deserialize, METH_VARARGS, "Deserialize a bytes object into a Python value: 'deserialize(bytes)'."},
    {0},
};

static struct PyModuleDef ufmodule = {
    PyModuleDef_HEAD_INIT,
    "ufser",
    "uF serialization Python module",
    0,
    methods
};

static PyObject *
error_value_new(PyTypeObject *type, PyObject*, PyObject*)
{
    auto self = (uf_error_value *)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->error = nullptr;
    }
    return (PyObject *)self;
}

static int
error_value_init(uf_error_value *self, PyObject*, PyObject*)
{
    //keep error member non-null
    if (self->error)
        *self->error = uf::error_value();
    else
        self->error = new uf::error_value;
    return 0;
}

static void
error_value_dealloc(uf_error_value* self)
{
    //We must leave pending Python exceptions alone
    delete self->error; //this is noexcept
    self->error = nullptr;
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject *
error_value_get(uf_error_value* self, void *c)
{
    if ((size_t)c == 0)
        return deserialize_as_python(self->error->value);
    const std::string *s;
    switch ((size_t)c) {
    case 1: s = &self->error->type; break;
    case 2: s = &self->error->msg; break;
    default:
        return err("Invalid closure in error_value_get");
    }
    return PyUnicode_FromStringAndSize(s->data(), s->length());
}

static int
error_value_set(uf_error_value* self, PyObject *v, void *c)
{
    if (!v) return err(PyExc_AttributeError, "Cannot delete " UF_ERRNAME " attributes."), -1;
    if ((size_t)c == 0) {
        try {
            self->error->value = serialize_as(v);
            return 0;
        } catch (uf::value_error const &e) {
            return err(PyExc_TypeError, e.what()), -1;
        } catch (uf::api_error const& e) {
            return err(PyExc_ValueError, e.what()), -1;
        } catch (std::bad_alloc const &e) {
            return err(PyExc_MemoryError, e.what()), -1;
        } catch (std::exception const &e) {
            return err(PyExc_RuntimeError, uf::concat("Python Error.set_error exception: ", e.what())), -1;
        } catch (...) {
            return err(PyExc_RuntimeError, "unhandled C++ exception of type "+current_exception_type()), -1;
        }
    }
    if (PyUnicode_Check(v)) {
        switch ((size_t)c) {
        case 1: self->error->type = PyUnicode_AsUTF8String_view(v); break;
        case 2: self->error->msg = PyUnicode_AsUTF8String_view(v); break;
        default:
            return err("Invalid closure in error_value_set"), -1;
        }
        return 0;
    }
    err(PyExc_TypeError, uf::concat("Expecting string and not ", v));
    return -1;
}

PyObject *error_value_str(uf_error_value* self)
{
    if (!self || !self->error)
        return PyUnicode_FromString("<Invalid " UF_ERRNAME " object>");
    return PyUnicode_FromString(self->error->what());
}

extern PyTypeObject uf_error_value_type;

PyObject *error_value__reduce__(uf_error_value *self, PyObject *) {
    //return tuple(uf_error_value_type, tuple(), param to setstate)
    PyObject *ret = PyTuple_New(3);
    Py_INCREF((PyObject *)&uf_error_value_type);
    PyTuple_SetItem(ret, 0, (PyObject *)&uf_error_value_type);
    PyTuple_SetItem(ret, 1, PyTuple_New(0)); //Arguments to error_value_new(): no arguments
    const std::string ser = uf::serialize(self->error);
    PyTuple_SetItem(ret, 2, PyBytes_FromStringAndSize(ser.data(), ser.length()));
    return ret;
}

PyObject *error_value__setstate__(uf_error_value *self, PyObject *state) {
    assert(self);
    assert(state);
    delete self->error;
    self->error = nullptr;
    bool ok = false;
    if (!PyTuple_Check(state) || PyTuple_Size(state) != 1) {
        err("Expecting a single element tuple in " UF_ERRNAME ".__setstate__: " + to_string(state));
    } else if (PyObject *bytes = PyTuple_GetItem(state, 0); !PyBytes_Check(bytes)) {
        err("Expecting bytes in a tuple in " UF_ERRNAME ".__setstate__: " + to_string(state));
    } else try {
        std::string_view ser(PyBytes_AsString(bytes), PyBytes_Size(bytes));
        self->error = uf::deserialize_view_as<std::unique_ptr<uf::error_value>>(ser).release();
        ok = true;
    } catch (const uf::value_error &e) {
        err(std::string("Deserialize error in " UF_ERRNAME ".__setstate__: ") + e.what());
    }
    if (!self->error)
        self->error = new uf::error_value;
    if (!ok)
        return nullptr;
    Py_RETURN_NONE;
}

static PyGetSetDef uf_error_value_getset[] = {
    {(char*)"type", (getter)error_value_get, (setter)error_value_set,
     (char*)"Type of error, like 'internal'. Empty on no error.", (void*)1},
    {(char*)"message", (getter)error_value_get, (setter)error_value_set,
     (char*)"Human readable error message.", (void*)2},
    {(char*)"value", (getter)error_value_get, (setter)error_value_set,
     (char*)"The value associated with the error.", (void*)0},
    {}
};

static PyMethodDef uf_error_value_methods[] = {
    { "__setstate__", (PyCFunction)error_value__setstate__, METH_VARARGS, "__setstate__ function for pickle." },
    { "__reduce__", (PyCFunction)error_value__reduce__, METH_NOARGS, "__reduce__ function for pickle." },
    {}
};

PyTypeObject uf_error_value_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    UF_ERRNAME,
    sizeof(uf_error_value), /* tp_basicsize */
    0,                         /* tp_itemsize */
    (destructor)error_value_dealloc,/* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_compare */
    (reprfunc)error_value_str,/* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash */
    0,                         /* tp_call */
    (reprfunc)error_value_str,/* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,        /* tp_flags */
    UF_ERRNAME " objects. Members:\n"
    "- type (str):      Type of error, like 'internal'. Empty on no error.\n"
    "- message (str):   Human readable error message.\n"
    "- value (obj):     The value associated with the error.\n",   /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    uf_error_value_methods,   /* tp_methods */
    0,                         /* tp_members */
    uf_error_value_getset,    /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)error_value_init,/* tp_init */
    0,                         /* tp_alloc */
    error_value_new,          /* tp_new */
};

}} // extern block & namespace

PyMODINIT_FUNC PyInit_ufser() {
    if (!PyImport_ImportModule("pickle"))
        return nullptr;
    auto mod = PyModule_Create(&ufmodule);
    if (mod) {
        auto x __attribute__((unused)) = PyType_Ready(&uf_error_value_type);
        assert(!x);
        Py_INCREF(&uf_error_value_type);
        PyModule_AddObject(mod, UF_ERRNAME_ONLY, (PyObject *)&uf_error_value_type);
        PyModule_AddStringConstant(mod, "version", PACKAGE_VERSION);
    }
    return mod;
}
