#!/usr/bin/python3

# make a pattern full of little circles. 32*32 each.
import math
import sys

SZ=32

def make_pattern():
	""" make the pattern, return a list of byte-arrays (lines) """
	global SZ
	# first use single-bytes. replace with palette later
	B = [0][:] * (SZ*SZ)
	for y in range(SZ):
		cy = 0.5+y-0.5*SZ
		for x in range(SZ):
			cx = 0.5+x-0.5*SZ
			r = math.sqrt(cx*cx+cy*cy)
			ang = int( math.atan2(cy,cx)*128.0/math.pi + 0.5 )
			if r>0.5*SZ:
				continue
			if r>0.5*SZ-1.1:
				B[x+SZ*y] = 1
				continue
			B[x+SZ*y] = ((ang>>4)&1)+2

	# convert to output
	res = list()
	palette = (b'\x00\x00\x00\xff',b'\xdd\xdd\xdd\xff',b'\x00\x00\x77\xff',b'\x77\x00\x00\xff')
	for y in range(SZ):
		lin = B[y*SZ:y*SZ+SZ]
		lin = b''.join(map(lambda gg:palette[gg],lin))
		res.append(lin)
	return res

pat = make_pattern()
#print(repr(pat))

for y in range(480):
	for x in range(0,640,SZ):
		sys.stdout.buffer.write(pat[y%SZ])



