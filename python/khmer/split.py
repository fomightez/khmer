import _khmer
from _khmer import new_ktable
try:  # CPython API
    from _khmer import new_hashtable
except ImportError:  # Cython
    from _khmer import _new_counting_hash as new_hashtable


class SplitHashtable(object):
    def __init__(self, ksize, tablesize, partition, n_partitions):
        self._kh = new_hashtable(ksize, tablesize)
        self.partition = partition
        self.n_partitions = n_partitions

        if partition < 0 or partition >= n_partitions:
            raise Exception("invalid partition: %d" % partition)

        self.partition_size = 4 ** ksize // n_partitions
        self.lower_bound = self.partition_size * partition

        if partition == n_partitions - 1:
            self.upper_bound = self.lower_bound + self.partition_size
        else:
            self.upper_bound = 4 ** ksize

    def consume(self, s):
        s = s.upper()
        if 'N' in s:
            return 0

        return self._kh.consume(s, self.lower_bound, self.upper_bound)

    def count(self, s):
        s = s.upper()
        if 'N' in s:
            return 0, 0

        return self._kh.get_min_count(s, self.lower_bound, self.upper_bound), \
            self._kh.get_max_count(s, self.lower_bound, self.upper_bound)
