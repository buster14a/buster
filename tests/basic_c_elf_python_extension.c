// A CPython extension module: the shared object `import` loads, whose every
// reference into the interpreter is left for the loader to bind.
#define PY_SSIZE_T_CLEAN
#include <Python.h>

static int buster_extension_calls;

static PyObject* buster_extension_add(PyObject* self, PyObject* arguments)
{
    long left;
    long right;
    (void)self;
    if (!PyArg_ParseTuple(arguments, "ll", &left, &right)) return NULL;
    buster_extension_calls += 1;
    return PyLong_FromLong(left + right);
}

static PyObject* buster_extension_greet(PyObject* self, PyObject* arguments)
{
    const char* name;
    (void)self;
    if (!PyArg_ParseTuple(arguments, "s", &name)) return NULL;
    return PyUnicode_FromFormat("hello, %s (%d)", name, buster_extension_calls);
}

static PyObject* buster_extension_fail(PyObject* self, PyObject* arguments)
{
    (void)self;
    (void)arguments;
    PyErr_SetString(PyExc_ValueError, "refused");
    return NULL;
}

static PyMethodDef buster_extension_methods[] = {
    {"add", buster_extension_add, METH_VARARGS, "Add two integers."},
    {"greet", buster_extension_greet, METH_VARARGS, "Greet by name."},
    {"fail", buster_extension_fail, METH_NOARGS, "Raise ValueError."},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef buster_extension_module = {
    PyModuleDef_HEAD_INIT, "busterpic", "A Buster-built extension.", -1, buster_extension_methods,
};

PyMODINIT_FUNC PyInit_busterpic(void)
{
    PyObject* module = PyModule_Create(&buster_extension_module);
    if (module && PyModule_AddIntConstant(module, "answer", 42) != 0)
    {
        Py_DECREF(module);
        module = NULL;
    }
    return module;
}
