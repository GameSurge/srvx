#include "sha256.h"
#include "common.h"

static int test_sha256(const char *input, unsigned int count, const char *expect)
{
    struct sha256_context ctx;
    const uint8_t *hash;
    unsigned int ii;
    char digest[65];

    sha256_init(&ctx);
    for (ii = 0; ii < count; ++ii)
        sha256_update(&ctx, input, strlen(input));
    hash = sha256_finish(&ctx);

    for (ii = 0; ii < SHA256_OUTPUT_SIZE; ++ii)
        sprintf(digest + 2*ii, "%02x", hash[ii]);

    if (strcmp(digest, expect)) {
        printf("Mismatch for \"%s\"x%u:\n  Got %s,\n  Expected %s\n",
	    input, count, digest, expect);
	return 1;
    }

    return 0;
}

int main(UNUSED_ARG(int argc), UNUSED_ARG(char *argv[]))
{
    size_t failed = 0;

    if (test_sha256("abc", 1,
        "bf1678baeacf018fde4041412322ae5da36103b09c7a179661ff10b4ad1500f2"))
        failed++;

    if (test_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1,
        "616a8d24b83806d29326c0e539603e0c59e43ca36721ff64d4edecf6c106db19"))
        failed++;

    if (test_sha256("a", 1000000,
        "5c6ec7cd92fb1499e2c7a181673ed784489a80f10e2097a4cc396d04d02c11c7"))
        failed++;

    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}