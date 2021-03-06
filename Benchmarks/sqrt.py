
import numpy
import time

element_count = 100000000

import thunklib

numpy.random.seed(137)
a = numpy.random.randint(1000,10000,element_count)


start = time.time()
d =  numpy.sqrt(numpy.sqrt(numpy.sqrt(numpy.sqrt(numpy.sqrt(a)))))
end = time.time()

print("NumPy Evaluation", end - start)

import thunklib

numpy.random.seed(137)
a = thunklib.thunk(numpy.random.randint(0,100,element_count))


start = time.time()
d = numpy.sqrt(numpy.sqrt(numpy.sqrt(numpy.sqrt(numpy.sqrt(a)))))
d.evaluate()
end = time.time()

print("Pipelined Evaluation", end - start)