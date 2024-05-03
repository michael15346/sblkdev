#pragma once
#include <linux/rhashtable.h>
#include <crypto/hash.h>

struct hashed_block {
	struct rhash_head node;
	//KEY - sector_t (sector location?) sector_t is int-like. can use list. will need to store all of the data. 
	//md5_hash?
};

struct hashed_md5_to_data {
	struct rhash_head node;
	//KEY - md5_hash
	// sector_t - real position of data.
};

struct sdesc {
    struct shash_desc shash;
    char ctx[];
};

struct sdesc *init_sdesc(struct crypto_shash *alg);
int calc_hash(struct crypto_shash *alg,
             const unsigned char *data, unsigned int datalen,
             unsigned char *digest);
