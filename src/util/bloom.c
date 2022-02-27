/*!
 * bloom.c - bloom filter for rdb
 * Copyright (c) 2022, Christopher Jeffrey (MIT License).
 * https://github.com/chjj/rdb
 */

#include <stddef.h>
#include <stdint.h>
#include "bloom.h"
#include "hash.h"
#include "internal.h"
#include "slice.h"

/*
 * Default
 */

static void
rdb_bloom_add_(const rdb_bloom_t *bloom,
               uint8_t *data,
               const rdb_slice_t *key,
               size_t bits);

static int
rdb_bloom_match_(const rdb_bloom_t *bloom,
                 const rdb_slice_t *filter,
                 const rdb_slice_t *key);

static const rdb_bloom_t bloom_default = {
  /* .name = */ "filter.leveldb.BuiltinBloomFilter2",
  /* .add = */ rdb_bloom_add_,
  /* .match = */ rdb_bloom_match_,
  /* .bits_per_key = */ 10,
  /* .k = */ 6, /* (size_t)(10 * 0.69) == 6 */
  /* .user_policy = */ NULL
};

/*
 * Globals
 */

const rdb_bloom_t *rdb_bloom_default = &bloom_default;

/*
 * Bloom
 */

rdb_bloom_t *
rdb_bloom_create(int bits_per_key) {
  rdb_bloom_t *bloom = rdb_malloc(sizeof(rdb_bloom_t));
  rdb_bloom_init(bloom, bits_per_key);
  return bloom;
}

void
rdb_bloom_destroy(rdb_bloom_t *bloom) {
  rdb_free(bloom);
}

static uint32_t
rdb_bloom_hash(const rdb_slice_t *key) {
  return rdb_hash(key->data, key->size, 0xbc9f1d34);
}

static void
rdb_bloom_add_(const rdb_bloom_t *bloom,
               uint8_t *data,
               const rdb_slice_t *key,
               size_t bits) {
  /* Use double-hashing to generate a sequence of hash values.
     See analysis in [Kirsch,Mitzenmacher 2006]. */
  uint32_t hash = rdb_bloom_hash(key);
  uint32_t delta = (hash >> 17) | (hash << 15); /* Rotate right 17 bits. */
  size_t i;

  for (i = 0; i < bloom->k; i++) {
    uint32_t pos = hash % bits;

    data[pos / 8] |= (1 << (pos % 8));

    hash += delta;
  }
}

static int
rdb_bloom_match_(const rdb_bloom_t *bloom,
                 const rdb_slice_t *filter,
                 const rdb_slice_t *key) {
  const uint8_t *data = filter->data;
  size_t len = filter->size;
  uint32_t hash, delta;
  size_t i, bits, k;

  (void)bloom;

  if (len < 2)
    return 0;

  bits = (len - 1) * 8;

  /* Use the encoded k so that we can read filters generated by
     bloom filters created using different parameters. */
  k = data[len - 1];

  if (k > 30) {
    /* Reserved for potentially new encodings for short bloom
       filters. Consider it a match. */
    return 1;
  }

  hash = rdb_bloom_hash(key);
  delta = (hash >> 17) | (hash << 15); /* Rotate right 17 bits. */

  for (i = 0; i < k; i++) {
    uint32_t pos = hash % bits;

    if ((data[pos / 8] & (1 << (pos % 8))) == 0)
      return 0;

    hash += delta;
  }

  return 1;
}

void
rdb_bloom_init(rdb_bloom_t *bloom, int bits_per_key) {
  /* We intentionally round down to reduce probing cost a little bit. */
  bloom->name = bloom_default.name;
  bloom->add = rdb_bloom_add_;
  bloom->match = rdb_bloom_match_;
  bloom->bits_per_key = bits_per_key;
  bloom->k = bits_per_key * 0.69; /* 0.69 =~ ln(2). */
  bloom->user_policy = NULL;

  if (bloom->k < 1)
    bloom->k = 1;

  if (bloom->k > 30)
    bloom->k = 30;
}

size_t
rdb_bloom_size(const rdb_bloom_t *bloom, size_t n) {
  size_t bits = n * bloom->bits_per_key;

  if (bits < 64)
    bits = 64;

  return (bits + 7) / 8;
}
