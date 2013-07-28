#! /usr/bin/env python
#Removes fastq entries that contain Ns

import sys
import screed

filein = sys.argv[1]
fileout = sys.argv[2]

fw = open(fileout, 'w')

count = 0
for n, record in enumerate(screed.open(filein)):
    name = record['name']
    sequence = record['sequence']
    accuracy = record['accuracy']

    if 'N' in sequence:
        continue
    else:
        fw.write('@%s\n%s\n+%s\n%s\n' % (name, sequence, name, accuracy))
        count += 1

    if n % 1000 == 0:
        print '...', n

print 'Original Number of Reads', n + 1
print 'Final Number of Reads', count
print 'Total Filtered because they contained Ns', n + 1 - int(count)