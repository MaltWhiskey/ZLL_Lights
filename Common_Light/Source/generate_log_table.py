#!/usr/bin/env python
#
# generate_log_table.py
#
# This will generate log_table.h, which contains a definition of the
# log tables used for quicker gamma computation.

from __future__ import print_function
from __future__ import division
import math

f = open("log_table.h", "w")
f.write("/* log_table.h\n")
f.write(" *\n")
f.write(" * Log tables for quick gamma computation.\n")
f.write(" * This file was generated by generate_log_table.py.\n")
f.write(" */\n")
f.write("\n")
f.write("#include <stdint.h>\n")
f.write("\n")

# Scaling factor for log table values.
# Increasing this will increase the precision of calculations, but this can't
# be too large or values will overflow 16 bits.
LOG_SCALING_FACTOR = 4096

# Generate log tables.
def generate_log_table(n, name):
    # Each entry is -ln(intensity) * LOG_SCALING_FACTOR, where intensity is in (0, 1]
    # Rounding error is approx. exp(0.5 / LOG_SCALING_FACTOR).
    global LOG_SCALING_FACTOR
    global f
    table = [0]
    for i in range(1, n):
        entry = int(round(-math.log(i / float(n - 1)) * LOG_SCALING_FACTOR))
        if entry > 65535:
            print("Error, overflow in log table. Reduce LOG_SCALING_FACTOR.")
            exit(1)
        table.append(entry)
    f.write("static const uint16_t {0}[{1}] = ".format(name, n))
    f.write("{\n")
    for i in range(n):
        f.write(str(table[i]))
        if i != (n - 1):
            f.write(",")
        if (i % 16) == 15:
            f.write("\n")
        else:
            f.write(" ")
    f.write("};\n")
    f.write("\n")
    return table

log_table_long = generate_log_table(4096, "log_table_long")

log_table_short = generate_log_table(256, "log_table_short")

f.close()

# Everything from here on is for testing only

def antilog(x):
    if x > log_table_long[1]:
        return 0
    left_index = 1
    right_index = 4095
    while (left_index + 1) != right_index:
        i = (right_index + left_index) // 2
        if log_table_long[i] < x:
            right_index = i
        else:
            left_index = i
    diff_left = log_table_long[left_index] - x
    diff_right = x - log_table_long[right_index]
    if diff_left < diff_right:
        return left_index
    else:
        return right_index

# Determine the size of errors
biggest_error = 0.0
average_error = 0.0
num_measurements = 0
for j in range(20, 500): # Try gamma values between 0.2 and 5.0
    gamma = float(j) / 100.0
    for i in range(1, 256):
        # Do calculation using tables
        x = log_table_short[i]
        # Simulate fixed-point arithmetic
        gamma_fp = int(round(gamma * 1024.0))
        x = x * gamma_fp
        x = x // 1024
        y = antilog(x)
        # Do calculation using math.pow
        actual_y = math.pow(float(i) / 255.0, gamma) * 4095.0
        # Compare
        err = abs(y - actual_y)
        average_error += err
        num_measurements += 1
        if err > biggest_error:
            biggest_error = err
print("Biggest absolute error: " + str(biggest_error))
print("Average absolute error: " + str(average_error / float(num_measurements)))
