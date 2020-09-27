#!/usr/bin/env python3
import sys

argv = sys.argv
argc = len(argv)
if (argc != 3):
  print("Usage: %s coeffname filename" % argv[0])
  quit()

coeff_name = str(argv[1])
coeff_list = []
file = open(argv[2], "r")
for line in file:
  s = line.strip(' \r\n')
  if (s != ""):
    coeff_list.append(float(s))

print("const IQSampleCoeff FilterParameters::", end='')
print(coeff_name, end='')
print(" = {")

num = 0
for coeff in coeff_list:
  print(coeff, end='')
  print(", ", end='')
  num += 1
  if (num == 3):
    num = 0
    print("")

print("};")
