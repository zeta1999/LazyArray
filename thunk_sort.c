
#include "thunk_sort.h"
#include "initializers.h"

#include "generated/thunk_lazy_functions.h"
#include "generated/thunkops_pipeline.h"

#include "thunk.h"

#define MERGE_ARRAYS(nptpe, npdefine)                                                                                       \
static int                                                                                                                  \
npdefine##_merge_arrays(nptpe *left, nptpe *right, size_t left_size, size_t right_size, nptpe *result) {                    \
    nptpe *left_end = left + left_size;                                                                                     \
    nptpe *right_end = right + right_size;                                                                                  \
    /* first merge the two arrays until either of the arrays runs out of values */                                          \
    while (left < left_end && right < right_end) {                                                                          \
        if (*left < *right) {                                                                                               \
            *result++ = *left++;                                                                                            \
        }                                                                                                                   \
        else {                                                                                                              \
            *result++ = *right++;                                                                                           \
        }                                                                                                                   \
    }                                                                                                                       \
    /* copy the remaining values from the array that has not run out of values */                                           \
    if (right < right_end) {                                                                                                \
        left = right;                                                                                                       \
        left_end = right_end;                                                                                               \
    }                                                                                                                       \
    memcpy(result, left, sizeof(nptpe) * (left_end - left));                                                                \
    return 1;                                                                                                               \
}

#define NPY_FUNC_TYPE_CASE(nptpe, npdefine, function)                                                                       \
    case npdefine:                                                                                                          \
        return npdefine##_##function((nptpe*) left, (nptpe*) right, left_size, right_size, (nptpe*) result);

MERGE_ARRAYS(npy_int8, NPY_INT8)
MERGE_ARRAYS(npy_int16, NPY_INT16)
MERGE_ARRAYS(npy_int32, NPY_INT32)
MERGE_ARRAYS(npy_int64, NPY_INT64)
MERGE_ARRAYS(npy_uint8, NPY_UINT8)
MERGE_ARRAYS(npy_uint16, NPY_UINT16)
MERGE_ARRAYS(npy_uint32, NPY_UINT32)
MERGE_ARRAYS(npy_uint64, NPY_UINT64)
MERGE_ARRAYS(npy_float, NPY_FLOAT)
MERGE_ARRAYS(npy_double, NPY_DOUBLE)



int
merge_arrays(void *left, void *right, size_t left_size, size_t right_size, void *result, int typenum) {
    switch(typenum) {
        NPY_FUNC_TYPE_CASE(npy_uint8, NPY_UINT8, merge_arrays)
        NPY_FUNC_TYPE_CASE(npy_uint16, NPY_UINT16, merge_arrays)
        NPY_FUNC_TYPE_CASE(npy_uint32, NPY_UINT32, merge_arrays)
        NPY_FUNC_TYPE_CASE(npy_uint64, NPY_UINT64, merge_arrays)
        NPY_FUNC_TYPE_CASE(npy_int8, NPY_INT8, merge_arrays)
        NPY_FUNC_TYPE_CASE(npy_int16, NPY_INT16, merge_arrays)
        NPY_FUNC_TYPE_CASE(npy_int32, NPY_INT32, merge_arrays)
        NPY_FUNC_TYPE_CASE(npy_int64, NPY_INT64, merge_arrays)
        NPY_FUNC_TYPE_CASE(npy_float, NPY_FLOAT, merge_arrays)
        NPY_FUNC_TYPE_CASE(npy_double, NPY_DOUBLE, merge_arrays)
        default:
            return -1;
    }
}

PyObject*
PyArrayObject_Merge(PyArrayObject *a, PyArrayObject *b, PyArray_Descr *out_type) {
    Py_INCREF(a);
    Py_INCREF(b);
    if (PyArray_DESCR(a)->type_num != out_type->type_num) {
        PyArrayObject *converted = (PyArrayObject*) PyArray_CastToType(a, out_type, 0);
        Py_DECREF(a);
        a = converted;
    }
    if (PyArray_DESCR(b)->type_num != out_type->type_num) {
        PyArrayObject *converted = (PyArrayObject*) PyArray_CastToType(b, out_type, 0);
        Py_DECREF(b);
        b = converted;
    }

    size_t left_size = PyArray_SIZE(a);
    size_t right_size = PyArray_SIZE(b);

    npy_intp elements[1] = { left_size + right_size };
    Py_INCREF(out_type);
    PyObject *result = PyArray_Empty(1, elements, out_type, 0);

    int ret = merge_arrays(PyArray_DATA(a), PyArray_DATA(b), left_size, right_size, PyArray_DATA((PyArrayObject*)result), out_type->type_num);
    if (ret < 0) {
        PyErr_Format(PyExc_TypeError, "Unsupported output typenum %d.", out_type->type_num);
        Py_DECREF(result);
        result = NULL;
    }

    Py_DECREF(a);
    Py_DECREF(b);
    return result;
}

PyObject *PyArray_MergeArrays(PyObject *self, PyObject *args) {
    PyObject *a, *b;
    if (PyArg_ParseTuple(args, "OO", &a, &b) < 0) {
        PyErr_BadArgument();
        return NULL;
    }
    if (!PyArray_CheckExact(a)) {
        PyErr_BadArgument();
        return NULL;
    }
    if (!PyArray_CheckExact(b)) {
        PyErr_BadArgument();
        return NULL;
    }
    return PyArrayObject_Merge((PyArrayObject*) a, (PyArrayObject*) b, PyArray_DescrFromType(NPY_DOUBLE));
}


void pipeline_blocksort(PyArrayObject **args, size_t start, size_t end) {
    void *inptr = PyArray_DATA(args[0]);
    void *outptr = PyArray_DATA(args[1]);
    PyArray_Descr *descr = PyArray_DESCR(args[1]);
    // the sort function we are calling is an in-place sort, so if the input and output data is different we first have to copy the data to the new location
    if (inptr != outptr) {
        // we only sort the current block, so we only copy the current block of data as well
        PyArrayObject *in = PyArrayObject_Block(args[0], start, end);
        PyArrayObject *out = PyArrayObject_Block(args[1], start, end);
        PyArray_CopyInto(out, in);
        Py_DECREF(in);
        Py_DECREF(out);
    }
    // call the actual sort function
    descr->f->sort[0]((void*)(PyArray_BYTES(args[1]) + start * descr->elsize), end - start, NULL);
}

void recursive_merge(char *inptr, size_t block_size, size_t total_size, size_t elsize, int typenum) {
    if (block_size > total_size) return;
    // recursive merge sort, first merge all blocks with size block_size together, resulting in sorted blocks of size (block_size * 2)
    // then recursively continue to merge those blocks together again
    void *temp_result = malloc(block_size * 2 * elsize);
    for(size_t el = 0; el + block_size < total_size; el += block_size * 2) {
        size_t right_size = min(block_size, total_size - (el + block_size));
        merge_arrays(inptr + (el * elsize), inptr + ((el + block_size) * elsize), block_size, right_size, temp_result, typenum);
        memcpy(inptr + (el * elsize), temp_result, (block_size + right_size) * elsize);
    }
    free(temp_result);
    recursive_merge(inptr, block_size * 2, total_size, elsize, typenum);
}

static void recursive_merge_nomemcpy(char **input_arrays, size_t *input_sizes, size_t array_count, char *result_ptr, size_t elsize, int typenum, bool temporary_array) {
    if (array_count == 1) return;

    // recursively merge the blocks without doing memcpy: should be faster
    // every iteration we reduce the amount of arrays by half
    char **result_arrays = NULL;
    size_t *result_sizes = NULL;
    size_t new_array_count = array_count % 2 == 0 ? array_count / 2 : array_count / 2 + 1;
    if (new_array_count == 1) {
        // final iteration: in this case we store the result in the result_ptr directly, instead of in intermediate arrays so we don't need to do any copying
        merge_arrays(input_arrays[0], input_arrays[1], input_sizes[0], input_sizes[1], result_ptr, typenum);
    } else {
        // regular iteration, we create intermediate arrays to store the merged arrays
        result_arrays = malloc(new_array_count * sizeof(char*));
        result_sizes = malloc(new_array_count * sizeof(size_t));
        size_t i;
        for(i = 0; i < array_count - 1; i += 2) {
            // we merge two arrays at a time, input_arrays[i] and input_arrays[i + 1]
            size_t array_index = i / 2;
            // total size of the merged array is the combined size of the merged arrays
            size_t new_size = input_sizes[i] + input_sizes[i + 1]; 
            // allocate space for the merged array
            result_arrays[array_index] = malloc(new_size * elsize);
            result_sizes[array_index] = new_size;
            // now merge the actual arrays into the allocated space
            merge_arrays(input_arrays[i], input_arrays[i + 1], input_sizes[i], input_sizes[i + 1], result_arrays[array_index], typenum);
            // free the previously allocated space, if it was allocated by a previous call of recursive_merge()
            if (temporary_array) {
                free(input_arrays[i]);
                free(input_arrays[i + 1]);
            }
        }
        // if the number of arrays is odd we have one 'unmerged' array, simply at that array to the end and push it to the next iteration
        if (i == array_count - 1) {
            result_arrays[new_array_count - 1] = input_arrays[i];
            result_sizes[new_array_count - 1] = input_sizes[i];
        }
    }
    free(input_sizes);
    free(input_arrays);
    recursive_merge_nomemcpy(result_arrays, result_sizes, new_array_count, result_ptr, elsize, typenum, true);
}

void unary_mergesort(PyArrayObject **args) {
    size_t total_size = PyArray_SIZE(args[0]);
    size_t elsize = PyArray_DESCR(args[0])->elsize;
    int typenum = PyArray_DESCR(args[0])->type_num;
    size_t array_count = total_size % BLOCK_SIZE == 0 ? total_size / BLOCK_SIZE : total_size / BLOCK_SIZE + 1;
    size_t right_size = total_size % BLOCK_SIZE == 0 ? BLOCK_SIZE : total_size % BLOCK_SIZE;

    char *input_data = PyArray_BYTES(args[0]);
    char *result_data = PyArray_BYTES(args[1]);

    char **input_arrays = malloc(array_count * sizeof(char*));
    size_t *input_sizes = malloc(array_count * sizeof(size_t));
    for(size_t i = 0; i < array_count; i++) {
        input_arrays[i] = input_data + (i * BLOCK_SIZE * elsize);
        input_sizes[i] = i == array_count - 1 ? right_size : BLOCK_SIZE;
    }
    recursive_merge_nomemcpy(input_arrays, input_sizes, array_count, result_data, elsize, typenum, false);
    /*if (PyArray_DATA(args[0]) != PyArray_DATA(args[1])) {
        // we do an in-place merge, so if the input and output data is different we first copy the data
        PyArray_CopyInto(args[1], args[0]);
    }
    // each of the blocks (should be) sorted here, so now we can merge them together recursively
    recursive_merge(PyArray_BYTES(args[1]), BLOCK_SIZE, PyArray_SIZE(args[1]), PyArray_DESCR(args[1])->elsize, PyArray_DESCR(args[1])->type_num);*/
}

void unary_sort(PyArrayObject **args) {
    PyArrayObject *in = args[0];
    PyArrayObject *out = args[1];
    void *inptr = PyArray_DATA(args[0]);
    void *outptr = PyArray_DATA(args[1]);
    PyArray_Descr *descr = PyArray_DESCR(args[1]);
    // the sort function we are calling is an in-place sort, so if the input and output data is different we first have to copy the data to the new location
    if (inptr != outptr) {
        PyArray_CopyInto(out, in);
    }
    if (descr->f->sort[0] != NULL) {
        // call the actual sort function
        descr->f->sort[0]((void*)(PyArray_BYTES(args[1])), PyArray_SIZE(out), NULL);        
    } else {
        int retval = PyArray_Sort(out, 0, NPY_QUICKSORT);
        if (retval < 0) {
            printf("Failure");
        }
    }
}

PyObject *thunk_lazysort(PyObject *v, PyObject *unused) {
    PyArrayObject *args[NPY_MAXARGS];
    PyArray_Descr *types[NPY_MAXARGS];
    ssize_t cardinality, cardinality_type;
    (void) unused;
    for(size_t i = 0; i < NPY_MAXARGS; i++) {
        types[i] = NULL;
        args[i] = NULL;
    }
    // sort is an in-place operation, so we can't just return a new thunk as we do with other functions
    // instead, we do the following:
    // create a copy of the original thunk "v" (stored in 'copy')
    // create the thunk responsible for sorting v
    // --> sorting happens in two steps
    // --> first pipeline sort, this sorts the individual blocks in a pipeline fashion
    // --> then merge sort, this is a unary function that merges the sorted blocks to create a fully sorted array
    // after creating these two thunks, we assign the final thunk to 'v' again, so 'v' is updated to reflect the sort status
    args[0] = (PyArrayObject*) PyThunk_AsTypeArray(v);
    types[0] = PyArray_DESCR(args[0]);
    sqrt_resolve_cardinality(PyThunk_Cardinality(v), &cardinality, &cardinality_type);

    //we only sort the pipeline merge sort for scalar types (int, float, uint, etc), for complex types/strings just call the NumPy sort function
    if (!PyArray_ScalarType(types[0]->type_num)) {
        PyObject *copy = (PyObject*) PyThunk_Copy((PyThunkObject*) v);
        PyObject *merge = PyThunkUnaryFunction_FromFunction(unary_sort, copy);
        PyThunk_FromOperation_Inplace((PyThunkObject*) v, merge, cardinality, cardinality_type, types[0]->type_num);
    } else {
        PyObject *copy = (PyObject*) PyThunk_Copy((PyThunkObject*) v);
        PyObject *blocksort = PyThunkUnaryPipeline_FromFunction(pipeline_blocksort, copy);
        PyObject *initial_thunk = PyThunk_FromOperation(blocksort, cardinality, cardinality_type, types[0]->type_num);
        PyObject *merge = PyThunkUnaryFunction_FromFunction(unary_mergesort, initial_thunk);
        PyThunk_FromOperation_Inplace((PyThunkObject*) v, merge, cardinality, cardinality_type, types[0]->type_num);
    }
    Py_RETURN_NONE;
}

void initialize_sort(void) {
    import_array();
    import_umath();
}