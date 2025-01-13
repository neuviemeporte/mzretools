#include <vector>
#include <cstdint>
#include <limits>
#include <algorithm>

// Adapted from: https://github.com/roy-ht/editdistance

// -------
// License
// -------
//
// It is released under the MIT license.
//
//     Copyright (c) 2013 Hiroyuki Tanaka
//
//     Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
//     The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
//     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
template<typename T> uint32_t edit_distance_dp(T const *str1, size_t const size1, T const *str2, size_t const size2, const uint32_t thr) {
    std::vector<std::vector<uint32_t>> d(2, std::vector<uint32_t>(size2 + 1));
    d[0][0] = 0;
    d[1][0] = 1;
    for (size_t i = 0; i < size2 + 1; i++) d[0][i] = i;
    for (size_t i = 1; i < size1 + 1; i++) {
        d[i&1][0] = d[(i-1)&1][0] + 1;
        bool below_thr = false;
        for (size_t j = 1; j < size2 + 1; j++) {
            d[i&1][j] = std::min(std::min(d[(i-1)&1][j], d[i&1][j-1]) + 1, d[(i-1)&1][j-1] + (str1[i-1] == str2[j-1] ? 0 : 1));
            if (d[i&1][j] <= thr) below_thr = true;
        }
        if (!below_thr) return std::numeric_limits<uint32_t>::max();
    }
    return d[size1&1][size2];
}