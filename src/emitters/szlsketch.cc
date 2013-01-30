// Copyright 2010 Google Inc.
// Modified by Yuji Kaneda
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//      http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ------------------------------------------------------------------------

#include <string>
#include <math.h>
#include <assert.h>
#include <algorithm>

#include "openssl/md5.h"

// #include "public/szltype.h"
// #include "public/szlvalue.h"
// #include "public/szlencoder.h"
// #include "public/szldecoder.h"
// #include "public/szltabentry.h"

namespace SZaru {

// Return the dimensions of a sketch such that nTabs * tabSize ~= totalSize.
// Our algorithm assumes tabSize is a pow of 2, nTabs is odd.

template <typename Value>
void SzlSketch<Value>::Dims(int totalSize, int* nTabs, int* tabSize) {
  int ts = totalSize / 31;
  int bits;
  for (bits = 2; bits < 32 && ts > (1 << bits); bits++)
    ;
  int tabs;
  for (tabs = kMaxTabs; tabs > kMinTabs; tabs -= 2)
    if (((tabs - 2) << bits) < totalSize)
      break;
  *nTabs = tabs;
  *tabSize = 1 << bits;
}

template <typename Value>
SzlSketch<Value>::SzlSketch(int nTabs, int tabSize)
  : weights_(new Value[nTabs * tabSize]),
    nTabs_(nTabs),
    tabSize_(tabSize) {
  // SzlValues's are clear by default, so we don't need to clear weights_
  
  // Check for valid nTabs, pow(2) tabSize;
  // CHECK(nTabs >= kMinTabs && nTabs <= kMaxTabs && (nTabs & 1) == 1);
  // CHECK(tabSize > 0 && (tabSize & (tabSize - 1)) == 0);

  int bits;
  for (bits = 0; bits < 32 && tabSize > (1 << bits); bits++)
    ;
  tabBits_ = bits;
}

template <typename Value>
SzlSketch<Value>::~SzlSketch() {
  // int n = nTabs_ * tabSize_;
  // for (int i = 0; i <  n; i++)
  // weight_ops_.Clear(&weights_[i]);
  delete[] weights_;

  // for (int i = 0; i < nTabs_; i++)
  //    weight_ops_.Clear(&tmp_[i]);
}

// int SzlSketch<Value>::Memory() {
//   int mem = sizeof(SzlSketch);

//   int n = nTabs_ * tabSize_;
//   for (int i = 0; i < n; i++)
//     mem += weight_ops_.Memory(weights_[i]);

//   for (int i = 0; i < nTabs_; i++)
//     mem += weight_ops_.Memory(tmp_[i]);

//   return mem;
// }

// Encode the weights in the SzlSketch.
// void SzlSketch<Value>::Encode(SzlEncoder* enc) {
//   int n = nTabs_ * tabSize_;
//   for (int i = 0; i < n; i++)
//     weight_ops_.Encode(weights_[i], enc);
// }

// // Decode the weights in the SzlSketch.
// bool SzlSketch<Value>::Decode(SzlDecoder* dec) {
//   int n = nTabs_ * tabSize_;
//   for (int i = 0; i < n; i++)
//     if (!weight_ops_.Decode(dec, &weights_[i]))
//       return false;
//   return true;
// }

template <typename Value>
void SzlSketch<Value>::AddSketch(const SzlSketch& sketch) {
  assert(sketch.tabSize() == tabSize());
  int n = nTabs_ * tabSize_;
  for (int i = 0; i < n; ++i)
    // weight_ops_.Add(sketch.weights_[i], &weights_[i]);
    weights_[i] += sketch.weights_[i];
}

// Compute the indices into the sketch weights,
//   We need nTabs different hashes of the string,
//   which we get by computing md5 on slightly different strings.
template <typename Value>
void SzlSketch<Value>::ComputeIndex(const string& s, Index* index) {
  // original set of hash bits comes from a good hash of the key.
  uint8 digest[MD5_DIGEST_LENGTH];
  MD5Digest(s.data(), s.size(), &digest);

  int digi = 0;
  uint32 bits = 0;              // shift register with our hash bits
  int nbits = 0;
  int origin = 0;
  for (int i = 0; i < nTabs_; ++i) {
    // get enough hash bits from our good hash function
    while (nbits < tabBits_ + 1) {
      if (digi == MD5_DIGEST_LENGTH) {
        // rehash the hash to get more hash bits
        MD5Digest(digest, MD5_DIGEST_LENGTH, &digest);
        digi = 0;
      }
      bits |= digest[digi++] << nbits;
      nbits += 8;
    }

    // compute the index into this row of the sketch
    // and the sign we should use while summing.
    int ind = bits & ((1 << tabBits_) - 1);
    index->index[i].elem = origin + ind;
    origin += tabSize_;
    bits >>= tabBits_;
    index->index[i].sign = bits & 1;
    bits >>= 1;
    nbits -= tabBits_ + 1;
  }
}

// Adjust the sketch for a given index.
//   Just add or subtract the weight for each entry.
template <typename Value>
void SzlSketch<Value>::AddSub(Index* index,
			      Value value,
			      int isAdd) {
  for (int i = 0; i < nTabs_; ++i) {
    assert(index->index[i].elem >= static_cast<size_t>(i) * tabSize_
           && index->index[i].elem < (static_cast<size_t>(i) + 1) * tabSize_
           && index->index[i].sign <= 1);
    Value *w = &weights_[index->index[i].elem];
    if (index->index[i].sign == static_cast<uint32_t>(isAdd))
      // weight_ops_.Sub(value, w);
      *w -= value;
    else
      // weight_ops_.Add(value, w);
      *w += value;
  }
}

/*
 * Comparison function for finding the median
 */
// struct SzlSketchEstLess {
//   SzlSketchEstLess(const SzlOps& ops, int pos): ops_(ops), pos_(pos) {}
//   bool operator()(const double* w1, const double* w2) const {
//     return ops_.LessAtPos(*w1, pos_, *w2);
//   }
//   const SzlOps& ops_;
//   int pos_;
// };

// Estimate the weight for an index.
//   We use the median as the estimate, since it's supposed to be superior
//   to using the mean.
template <typename Value>
void SzlSketch<Value>::Estimate(Index* index, Value* est) {
  // first make the array of all weights with signs corrected
  Value values[kMaxTabs];
  for (int i = 0; i < nTabs_; ++i) {
    Value w = weights_[index->index[i].elem];
    if (index->index[i].sign) {
      // values[i] = &tmp_[i];
      // weight_ops_.Negate(*w, &tmp_[i]);
      values[i] = -w;
    } else {
      values[i] = w;
    }
  }

  unsigned nTabs2 = nTabs_ >> 1;
  Value* mid = &values[nTabs2];
  Value* last = &values[nTabs_];
  // for (int i = 0; i < weight_ops_.nflats(); i++) {
//     SzlSketchEstLess estless(weight_ops_, i);
//     nth_element(values, mid, last, estless);
//     weight_ops_.AssignAtPos(**mid, i, est);
//   }
  
  nth_element(values, mid, last);
  est = mid;
}

// Compute the estimated standard deviation of values in the sketch.
template <typename Value>
void SzlSketch<Value>::StdDeviation(double* deviations) {
  // int nvals = weight_ops_.nflats();
  int nvals = 1;
  for (int j = 0; j < nvals; j++)
    deviations[j] = 0.;

  if (tabSize_ == 0)
    return;

  // compute the error in each column of the sketch.
  double* columns = new double[tabSize_ * nvals];
  Value col;
  double* ave = new double[nvals];
  for (int i = 0; i < nvals; i++)
    ave[i] = 0.;
  double* column = columns;
  double* colv = new double[nvals];
  for (int i = 0; i < tabSize_; i++) {
    Index index;
    int origin = 0;
    for (int row = 0; row < nTabs_; row++) {
      index.index[row].elem = i + origin;
      index.index[row].sign = 0;
      origin += tabSize_;
    }
    Estimate(&index, &col);
    //weight_ops_.ToFloat(col, colv);
    colv[0] = col;

    for (int j = 0; j < nvals; j++) {
      double d = colv[j];
      column[j] = d;
      ave[j] += d;
    }
    column += nvals;
  }

  // Compute the standard deviation of columns.
  for (int j = 0; j < nvals; j++)
    ave[j] /= tabSize_;
  column = columns;
  for (int i = 0; i < tabSize_; i++) {
    for (int j = 0; j < nvals; j++) {
      double d = column[j] - ave[j];
      deviations[j] += d * d;
    }
    column += nvals;
  }

  // weight_ops_.Clear(&col);
  delete[] columns;
  delete[] ave;
  delete[] colv;

  for (int j = 0; j < nvals; j++) {
    if (deviations[j] > .00000001)
      deviations[j] = sqrt(deviations[j] / tabSize_);
    else
      deviations[j] = 0.;
  }
}

}
