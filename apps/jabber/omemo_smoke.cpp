// Host-side? No — MinGW Wine console: two OMEMO managers encrypt/decrypt round-trip.
#include "xmpp/omemo.h"

#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

static void wipe_tree_hint(const char *p) {
    // Best-effort: leave dirs; smoke uses unique subdirs under build/.
    (void)p;
}

int main() {
#if !defined(JABBER_OMEMO)
    std::fprintf(stderr, "JABBER_OMEMO not enabled\n");
    return 2;
#else
    const char *root = "omemo_smoke_store";
#ifdef _WIN32
    _mkdir(root);
#else
    mkdir(root, 0755);
#endif

    jabber::omemo::Manager alice, bob;
    if (!alice.open(root, "alice@example.com") || !bob.open(root, "bob@example.com")) {
        std::fprintf(stderr, "open failed\n");
        return 1;
    }

    // Alice publishes bundle XML; Bob ingests with Alice's device id.
    std::string bundle_iq = alice.iq_publish_bundle();
    if (bundle_iq.empty()) {
        std::fprintf(stderr, "alice bundle empty\n");
        return 1;
    }
    uint32_t alice_dev = alice.device_id();
    if (!bob.ingest_bundle("alice@example.com", alice_dev, bundle_iq)) {
        std::fprintf(stderr, "bob ingest alice failed\n");
        return 1;
    }

    std::string bob_bundle = bob.iq_publish_bundle();
    uint32_t bob_dev = bob.device_id();
    if (!alice.ingest_bundle("bob@example.com", bob_dev, bob_bundle)) {
        std::fprintf(stderr, "alice ingest bob failed\n");
        return 1;
    }

    alice.set_devices("bob@example.com", {bob_dev});
    bob.set_devices("alice@example.com", {alice_dev});

    std::string enc, err;
    const char *msg = "hello omemo from alice";
    if (!alice.encrypt_message("bob@example.com", msg, &enc, &err)) {
        std::fprintf(stderr, "encrypt failed: %s\n", err.c_str());
        return 1;
    }
    std::string plain;
    bool was = false;
    if (!bob.decrypt_message("alice@example.com", enc, &plain, &was, &err) || !was) {
        std::fprintf(stderr, "decrypt failed: %s\n", err.c_str());
        return 1;
    }
    if (plain != msg) {
        std::fprintf(stderr, "mismatch: got [%s]\n", plain.c_str());
        return 1;
    }
    std::printf("omemo smoke ok (alice=%u bob=%u)\n", alice_dev, bob_dev);
    wipe_tree_hint(root);
    return 0;
#endif
}
