#include <stdio.h>
#include <sodium.h>

int main(void) {
    // returns 0 on success, -1 on failure, and 1 if the library had already been initialized
    if (sodium_init() < 0) { fprintf(stderr, "sodium_init failed\n"); return 1; }

    unsigned char key[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];   // 32 bytes
    crypto_aead_xchacha20poly1305_ietf_keygen(key);

    FILE *f = fopen("vpn.key", "wb");
    if (!f) { perror("fopen"); return 1; }
    fwrite(key, 1, sizeof key, f);
    fclose(f);
    printf("wrote vpn.key (%zu bytes)\n", sizeof key);
    return 0;
}