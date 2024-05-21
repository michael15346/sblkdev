#ifndef DEDUP_H
#define DEDUP_H
#include <linux/rhashtable.h>
#include <crypto/hash.h>
struct sdesc {
    struct shash_desc shash;
    char ctx[];
};

struct sdesc *init_sdesc(struct crypto_shash *alg);
int calc_hash(struct crypto_shash *alg,
             const unsigned char *data, unsigned int datalen,
             unsigned char *digest);
#endif
