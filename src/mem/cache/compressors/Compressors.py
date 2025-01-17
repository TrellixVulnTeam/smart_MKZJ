# Copyright (c) 2018 Inria
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Daniel Carvalho

from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject

class BaseCacheCompressor(SimObject):
    type = 'BaseCacheCompressor'
    abstract = True
    cxx_header = "mem/cache/compressors/base.hh"

    block_size = Param.Int(Parent.cache_line_size, "Block size in bytes")
    size_threshold = Param.Unsigned(Parent.cache_line_size, "Minimum size, "
        "in bytes, in which a block must be compressed to. Otherwise it is "
        "stored in its uncompressed state")

class BaseDictionaryCompressor(BaseCacheCompressor):
    type = 'BaseDictionaryCompressor'
    abstract = True
    cxx_header = "mem/cache/compressors/dictionary_compressor.hh"

    dictionary_size = Param.Int(Parent.cache_line_size,
        "Number of dictionary entries")

class BDI(BaseCacheCompressor):
    type = 'BDI'
    cxx_class = 'BDI'
    cxx_header = "mem/cache/compressors/bdi.hh"

    use_more_compressors = Param.Bool(True, "True if should use all possible" \
        "combinations of base and delta for the compressors. False if using" \
        "only the lowest possible delta size for each base size.");

class CPack(BaseDictionaryCompressor):
    type = 'CPack'
    cxx_class = 'CPack'
    cxx_header = "mem/cache/compressors/cpack.hh"
