// OMEMO 0.3 (eu.siacs.conversations.axolotl) — 1:1 slice for Conversations/Gajim.
// Header-only; enabled with -DJABBER_OMEMO=1. Signal protocol version 3 via libomemo-c.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#if defined(JABBER_OMEMO)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#include <direct.h>

#include <mbedtls/base64.h>
#include <mbedtls/cipher.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/sha512.h>

extern "C" {
#include "signal_protocol.h"
#include "key_helper.h"
#include "session_builder.h"
#include "session_cipher.h"
#include "protocol.h"
#include "curve.h"
}
#endif // JABBER_OMEMO

namespace jabber {
namespace omemo {

constexpr const char *NS = "eu.siacs.conversations.axolotl";
constexpr const char *NS_DEVICELIST = "eu.siacs.conversations.axolotl.devicelist";

#if !defined(JABBER_OMEMO)

struct Manager {
  bool open(const std::string &, const std::string &) { return false; }
  void close() {}
  bool ready() const { return false; }
  uint32_t device_id() const { return 0; }
  std::string iq_publish_device_list(const std::vector<uint32_t> & = {}) { return {}; }
  std::string iq_publish_bundle() { return {}; }
  std::string iq_request_device_list(const std::string &) { return {}; }
  std::string iq_request_bundle(const std::string &, uint32_t) { return {}; }
  static std::vector<uint32_t> parse_device_list(const std::string &) { return {}; }
  bool ingest_bundle(const std::string &, uint32_t, const std::string &) { return false; }
  void set_devices(const std::string &, const std::vector<uint32_t> &) {}
  std::vector<uint32_t> devices(const std::string &) const { return {}; }
  bool encrypt_message(const std::string &, const std::string &, std::string *, std::string *) {
    return false;
  }
  bool decrypt_message(const std::string &, const std::string &, std::string *, bool *was_omemo,
                       std::string *) {
    if (was_omemo) *was_omemo = false;
    return false;
  }
};

#else // JABBER_OMEMO

namespace detail {

inline std::string sanitize_jid(std::string s) {
  for (char &c : s) {
    if (c == '@' || c == '/') c = '_';
  }
  return s;
}

inline bool mkdir_p(const std::string &path) {
  if (path.empty()) return false;
  std::string cur;
  for (size_t i = 0; i < path.size(); ++i) {
    cur.push_back(path[i]);
    if (path[i] == '/' || path[i] == '\\' || i + 1 == path.size()) {
      if (cur.size() >= 2 && !(cur.size() == 2 && cur[1] == ':')) {
        CreateDirectoryA(cur.c_str(), nullptr);
        _mkdir(cur.c_str());
      }
    }
  }
  return true;
}

inline bool write_file(const std::string &path, const uint8_t *data, size_t len) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(data, 1, len, f) == len;
  fclose(f);
  return ok;
}

inline bool read_file(const std::string &path, std::vector<uint8_t> *out) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz < 0) {
    fclose(f);
    return false;
  }
  out->resize((size_t)sz);
  bool ok = sz == 0 || fread(out->data(), 1, (size_t)sz, f) == (size_t)sz;
  fclose(f);
  return ok;
}

inline bool write_u32(FILE *f, uint32_t v) { return fwrite(&v, 4, 1, f) == 1; }
inline bool read_u32(FILE *f, uint32_t *v) { return fread(v, 4, 1, f) == 1; }

inline bool write_blob(FILE *f, const uint8_t *p, uint32_t n) {
  if (!write_u32(f, n)) return false;
  return n == 0 || fwrite(p, 1, n, f) == n;
}

inline bool read_blob(FILE *f, std::vector<uint8_t> *out) {
  uint32_t n = 0;
  if (!read_u32(f, &n)) return false;
  out->resize(n);
  return n == 0 || fread(out->data(), 1, n, f) == n;
}

inline std::string b64_enc(const uint8_t *data, size_t len) {
  size_t olen = 0;
  mbedtls_base64_encode(nullptr, 0, &olen, data, len);
  std::string out(olen, '\0');
  if (mbedtls_base64_encode((unsigned char *)&out[0], olen, &olen, data, len) != 0) return {};
  out.resize(olen);
  return out;
}

inline bool b64_dec(const std::string &in, std::vector<uint8_t> *out) {
  size_t olen = 0;
  mbedtls_base64_decode(nullptr, 0, &olen, (const unsigned char *)in.data(), in.size());
  out->resize(olen);
  if (olen == 0) return true;
  if (mbedtls_base64_decode(out->data(), olen, &olen, (const unsigned char *)in.data(), in.size()) !=
      0)
    return false;
  out->resize(olen);
  return true;
}

inline std::string xml_escape_attr(const std::string &s) { return s; }

inline std::string extract_attr(const std::string &tag, const char *name) {
  std::string key1 = std::string(name) + "='";
  std::string key2 = std::string(name) + "=\"";
  size_t p = tag.find(key1);
  char end = '\'';
  if (p == std::string::npos) {
    p = tag.find(key2);
    end = '"';
  }
  if (p == std::string::npos) return {};
  p += key1.size();
  size_t e = tag.find(end, p);
  if (e == std::string::npos) return {};
  return tag.substr(p, e - p);
}

inline std::string extract_element_text(const std::string &xml, const char *tag) {
  std::string open1 = std::string("<") + tag;
  size_t p = xml.find(open1);
  if (p == std::string::npos) return {};
  size_t gt = xml.find('>', p);
  if (gt == std::string::npos) return {};
  if (gt > p && xml[gt - 1] == '/') return {};
  size_t start = gt + 1;
  std::string close = std::string("</") + tag + ">";
  size_t e = xml.find(close, start);
  if (e == std::string::npos) return {};
  return xml.substr(start, e - start);
}

inline std::string trim_ws(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\r' || s.back() == '\t'))
    s.pop_back();
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
  return s.substr(i);
}

inline uint32_t parse_u32(const std::string &s) {
  if (s.empty()) return 0;
  return (uint32_t)strtoul(s.c_str(), nullptr, 10);
}

inline std::string next_iq_id(const char *prefix = "omemo") {
  static volatile LONG n = 0;
  char buf[64];
  snprintf(buf, sizeof(buf), "%s%ld", prefix, (long)InterlockedIncrement(&n));
  return buf;
}

inline std::string publish_options_open() {
  return "<publish-options><x xmlns='jabber:x:data' type='submit'>"
         "<field var='FORM_TYPE' type='hidden'>"
         "<value>http://jabber.org/protocol/pubsub#publish-options</value></field>"
         "<field var='pubsub#access_model'><value>open</value></field>"
         "</x></publish-options>";
}

inline int decode_pub(ec_public_key **key, const uint8_t *data, size_t len, signal_context *ctx) {
  if (len == 32) {
    uint8_t tmp[33];
    tmp[0] = 0x05;
    memcpy(tmp + 1, data, 32);
    return curve_decode_point(key, tmp, 33, ctx);
  }
  return curve_decode_point(key, data, len, ctx);
}

// --- mbedTLS signal_crypto_provider ---

inline int sg_random(uint8_t *data, size_t len, void *) {
  HCRYPTPROV prov = 0;
  if (!CryptAcquireContextA(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    return SG_ERR_UNKNOWN;
  BOOL ok = CryptGenRandom(prov, (DWORD)len, data);
  CryptReleaseContext(prov, 0);
  return ok ? 0 : SG_ERR_UNKNOWN;
}

inline int sg_hmac_init(void **hmac_context, const uint8_t *key, size_t key_len, void *) {
  auto *ctx = new mbedtls_md_context_t();
  mbedtls_md_init(ctx);
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info || mbedtls_md_setup(ctx, info, 1) != 0 ||
      mbedtls_md_hmac_starts(ctx, key, key_len) != 0) {
    mbedtls_md_free(ctx);
    delete ctx;
    return SG_ERR_UNKNOWN;
  }
  *hmac_context = ctx;
  return 0;
}

inline int sg_hmac_update(void *hmac_context, const uint8_t *data, size_t data_len, void *) {
  return mbedtls_md_hmac_update((mbedtls_md_context_t *)hmac_context, data, data_len) == 0
             ? 0
             : SG_ERR_UNKNOWN;
}

inline int sg_hmac_final(void *hmac_context, signal_buffer **output, void *) {
  unsigned char md[32];
  if (mbedtls_md_hmac_finish((mbedtls_md_context_t *)hmac_context, md) != 0) return SG_ERR_UNKNOWN;
  *output = signal_buffer_create(md, 32);
  return *output ? 0 : SG_ERR_NOMEM;
}

inline void sg_hmac_cleanup(void *hmac_context, void *) {
  if (!hmac_context) return;
  auto *ctx = (mbedtls_md_context_t *)hmac_context;
  mbedtls_md_free(ctx);
  delete ctx;
}

inline int sg_sha512_init(void **digest_context, void *) {
  auto *ctx = new mbedtls_sha512_context();
  mbedtls_sha512_init(ctx);
  if (mbedtls_sha512_starts(ctx, 0) != 0) {
    mbedtls_sha512_free(ctx);
    delete ctx;
    return SG_ERR_UNKNOWN;
  }
  *digest_context = ctx;
  return 0;
}

inline int sg_sha512_update(void *digest_context, const uint8_t *data, size_t data_len, void *) {
  return mbedtls_sha512_update((mbedtls_sha512_context *)digest_context, data, data_len) == 0
             ? 0
             : SG_ERR_UNKNOWN;
}

inline int sg_sha512_final(void *digest_context, signal_buffer **output, void *) {
  unsigned char md[64];
  auto *ctx = (mbedtls_sha512_context *)digest_context;
  if (mbedtls_sha512_finish(ctx, md) != 0) return SG_ERR_UNKNOWN;
  mbedtls_sha512_starts(ctx, 0); // reusable
  *output = signal_buffer_create(md, 64);
  return *output ? 0 : SG_ERR_NOMEM;
}

inline void sg_sha512_cleanup(void *digest_context, void *) {
  if (!digest_context) return;
  auto *ctx = (mbedtls_sha512_context *)digest_context;
  mbedtls_sha512_free(ctx);
  delete ctx;
}

inline mbedtls_cipher_type_t aes_type(int cipher, size_t key_len) {
  if (cipher == SG_CIPHER_AES_CBC_PKCS5) {
    if (key_len == 16) return MBEDTLS_CIPHER_AES_128_CBC;
    if (key_len == 24) return MBEDTLS_CIPHER_AES_192_CBC;
    if (key_len == 32) return MBEDTLS_CIPHER_AES_256_CBC;
  } else if (cipher == SG_CIPHER_AES_CTR_NOPADDING) {
    if (key_len == 16) return MBEDTLS_CIPHER_AES_128_CTR;
    if (key_len == 24) return MBEDTLS_CIPHER_AES_192_CTR;
    if (key_len == 32) return MBEDTLS_CIPHER_AES_256_CTR;
  }
  return MBEDTLS_CIPHER_NONE;
}

inline int sg_encrypt(signal_buffer **output, int cipher, const uint8_t *key, size_t key_len,
                      const uint8_t *iv, size_t iv_len, const uint8_t *plaintext,
                      size_t plaintext_len, void *) {
  mbedtls_cipher_type_t type = aes_type(cipher, key_len);
  if (type == MBEDTLS_CIPHER_NONE || iv_len != 16) return SG_ERR_UNKNOWN;
  mbedtls_cipher_context_t ctx;
  mbedtls_cipher_init(&ctx);
  const mbedtls_cipher_info_t *info = mbedtls_cipher_info_from_type(type);
  int rc = SG_ERR_UNKNOWN;
  uint8_t *out_buf = nullptr;
  do {
    if (!info || mbedtls_cipher_setup(&ctx, info) != 0) break;
    if (mbedtls_cipher_setkey(&ctx, key, (int)key_len * 8, MBEDTLS_ENCRYPT) != 0) break;
    if (cipher == SG_CIPHER_AES_CBC_PKCS5) {
      if (mbedtls_cipher_set_padding_mode(&ctx, MBEDTLS_PADDING_PKCS7) != 0) break;
    }
    size_t bl = mbedtls_cipher_get_block_size(&ctx);
    out_buf = (uint8_t *)malloc(plaintext_len + bl);
    if (!out_buf) {
      rc = SG_ERR_NOMEM;
      break;
    }
    size_t olen = 0;
    if (mbedtls_cipher_crypt(&ctx, iv, iv_len, plaintext, plaintext_len, out_buf, &olen) != 0)
      break;
    *output = signal_buffer_create(out_buf, olen);
    rc = *output ? 0 : SG_ERR_NOMEM;
  } while (0);
  mbedtls_cipher_free(&ctx);
  free(out_buf);
  return rc;
}

inline int sg_decrypt(signal_buffer **output, int cipher, const uint8_t *key, size_t key_len,
                      const uint8_t *iv, size_t iv_len, const uint8_t *ciphertext,
                      size_t ciphertext_len, void *) {
  mbedtls_cipher_type_t type = aes_type(cipher, key_len);
  if (type == MBEDTLS_CIPHER_NONE || iv_len != 16) return SG_ERR_INVAL;
  mbedtls_cipher_context_t ctx;
  mbedtls_cipher_init(&ctx);
  const mbedtls_cipher_info_t *info = mbedtls_cipher_info_from_type(type);
  int rc = SG_ERR_UNKNOWN;
  uint8_t *out_buf = nullptr;
  do {
    if (!info || mbedtls_cipher_setup(&ctx, info) != 0) break;
    if (mbedtls_cipher_setkey(&ctx, key, (int)key_len * 8, MBEDTLS_DECRYPT) != 0) break;
    if (cipher == SG_CIPHER_AES_CBC_PKCS5) {
      if (mbedtls_cipher_set_padding_mode(&ctx, MBEDTLS_PADDING_PKCS7) != 0) break;
    }
    size_t bl = mbedtls_cipher_get_block_size(&ctx);
    out_buf = (uint8_t *)malloc(ciphertext_len + bl);
    if (!out_buf) {
      rc = SG_ERR_NOMEM;
      break;
    }
    size_t olen = 0;
    if (mbedtls_cipher_crypt(&ctx, iv, iv_len, ciphertext, ciphertext_len, out_buf, &olen) != 0)
      break;
    *output = signal_buffer_create(out_buf, olen);
    rc = *output ? 0 : SG_ERR_NOMEM;
  } while (0);
  mbedtls_cipher_free(&ctx);
  free(out_buf);
  return rc;
}

inline bool aes_gcm_encrypt(const uint8_t key[16], const uint8_t iv[12], const uint8_t *pt,
                            size_t pt_len, std::vector<uint8_t> *ct, uint8_t tag[16]) {
  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  bool ok = false;
  if (mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128) == 0) {
    ct->resize(pt_len);
    ok = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, pt_len, iv, 12, nullptr, 0, pt,
                                   ct->data(), 16, tag) == 0;
  }
  mbedtls_gcm_free(&ctx);
  return ok;
}

inline bool aes_gcm_decrypt(const uint8_t key[16], const uint8_t iv[12], const uint8_t *ct,
                            size_t ct_len, const uint8_t tag[16], std::vector<uint8_t> *pt) {
  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  bool ok = false;
  if (mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128) == 0) {
    pt->resize(ct_len);
    ok = mbedtls_gcm_auth_decrypt(&ctx, ct_len, iv, 12, nullptr, 0, tag, 16, ct, pt->data()) == 0;
  }
  mbedtls_gcm_free(&ctx);
  return ok;
}

inline std::vector<uint32_t> list_bin_ids(const std::string &dir) {
  std::vector<uint32_t> ids;
  std::string pattern = dir + "\\*.bin";
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) {
    pattern = dir + "/*.bin";
    h = FindFirstFileA(pattern.c_str(), &fd);
  }
  if (h == INVALID_HANDLE_VALUE) return ids;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    uint32_t id = (uint32_t)strtoul(fd.cFileName, nullptr, 10);
    if (id) ids.push_back(id);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  return ids;
}

inline void dedupe_ids(std::vector<uint32_t> *ids) {
  std::vector<uint32_t> out;
  for (uint32_t id : *ids) {
    bool seen = false;
    for (uint32_t x : out)
      if (x == id) {
        seen = true;
        break;
      }
    if (!seen && id) out.push_back(id);
  }
  *ids = std::move(out);
}

} // namespace detail

struct Manager {
  bool open(const std::string &store_root, const std::string &bare_jid) {
    std::lock_guard<std::mutex> lock(mu_);
    close_unlocked();
    bare_jid_ = bare_jid;
    store_dir_ = store_root + "/omemo/" + detail::sanitize_jid(bare_jid);
    detail::mkdir_p(store_root);
    detail::mkdir_p(store_root + "/omemo");
    detail::mkdir_p(store_dir_);
    detail::mkdir_p(store_dir_ + "/prekeys");
    detail::mkdir_p(store_dir_ + "/signed_prekeys");
    detail::mkdir_p(store_dir_ + "/sessions");
    detail::mkdir_p(store_dir_ + "/identities");
    detail::mkdir_p(store_dir_ + "/devices");

    signal_crypto_provider prov = {};
    prov.random_func = detail::sg_random;
    prov.hmac_sha256_init_func = detail::sg_hmac_init;
    prov.hmac_sha256_update_func = detail::sg_hmac_update;
    prov.hmac_sha256_final_func = detail::sg_hmac_final;
    prov.hmac_sha256_cleanup_func = detail::sg_hmac_cleanup;
    prov.sha512_digest_init_func = detail::sg_sha512_init;
    prov.sha512_digest_update_func = detail::sg_sha512_update;
    prov.sha512_digest_final_func = detail::sg_sha512_final;
    prov.sha512_digest_cleanup_func = detail::sg_sha512_cleanup;
    prov.encrypt_func = detail::sg_encrypt;
    prov.decrypt_func = detail::sg_decrypt;
    prov.user_data = this;

    if (signal_context_create(&ctx_, this) != 0) return false;
    if (signal_context_set_crypto_provider(ctx_, &prov) != 0) {
      close_unlocked();
      return false;
    }
    if (!setup_store()) {
      close_unlocked();
      return false;
    }

    std::string meta_path = store_dir_ + "/meta.dat";
    FILE *mf = fopen(meta_path.c_str(), "rb");
    if (mf) {
      char magic[5] = {};
      bool ok = fread(magic, 1, 5, mf) == 5 && memcmp(magic, "OMEM1", 5) == 0;
      ok = ok && detail::read_u32(mf, &device_id_);
      ok = ok && detail::read_u32(mf, &registration_id_);
      ok = ok && detail::read_blob(mf, &identity_public_);
      ok = ok && detail::read_blob(mf, &identity_private_);
      ok = ok && detail::read_u32(mf, &signed_prekey_id_);
      fclose(mf);
      if (!ok) {
        close_unlocked();
        return false;
      }
    } else {
      if (!generate_install()) {
        close_unlocked();
        return false;
      }
    }

    load_device_file(bare_jid_, &devices_[bare_jid_]);
    load_device_file("self", &devices_[bare_jid_]); // merge self.txt peers
    ready_ = true;
    return true;
  }

  void close() {
    std::lock_guard<std::mutex> lock(mu_);
    close_unlocked();
  }

  bool ready() const {
    std::lock_guard<std::mutex> lock(mu_);
    return ready_;
  }

  uint32_t device_id() const {
    std::lock_guard<std::mutex> lock(mu_);
    return device_id_;
  }

  std::string iq_publish_device_list(const std::vector<uint32_t> &extra_remote_ids_of_ours = {}) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ready_) return {};
    std::vector<uint32_t> ids;
    ids.push_back(device_id_);
    for (uint32_t id : extra_remote_ids_of_ours) ids.push_back(id);
    auto it = devices_.find(bare_jid_);
    if (it != devices_.end())
      for (uint32_t id : it->second) ids.push_back(id);
    detail::dedupe_ids(&ids);
    devices_[bare_jid_] = ids;
    save_device_file("self", ids);
    save_device_file(bare_jid_, ids);

    std::string list = std::string("<list xmlns='") + NS + "'>";
    for (uint32_t id : ids) {
      char buf[64];
      snprintf(buf, sizeof(buf), "<device id='%u'/>", id);
      list += buf;
    }
    list += "</list>";
    std::string id = detail::next_iq_id("omdl");
    return "<iq type='set' id='" + id + "'><pubsub xmlns='http://jabber.org/protocol/pubsub'>"
           "<publish node='" +
           std::string(NS_DEVICELIST) + "'><item id='current'>" + list +
           "</item></publish>" + detail::publish_options_open() + "</pubsub></iq>";
  }

  std::string iq_publish_bundle() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ready_) return {};

    session_signed_pre_key *spk = nullptr;
    if (signal_protocol_signed_pre_key_load_key(store_, &spk, signed_prekey_id_) != 0 || !spk)
      return {};

    ec_key_pair *spk_pair = session_signed_pre_key_get_key_pair(spk);
    ec_public_key *spk_pub = ec_key_pair_get_public(spk_pair);
    signal_buffer *spk_buf = nullptr;
    ec_public_key_serialize(&spk_buf, spk_pub);
    std::string spk_b64 =
        detail::b64_enc(signal_buffer_data(spk_buf), signal_buffer_len(spk_buf));
    signal_buffer_free(spk_buf);

    std::string sig_b64 = detail::b64_enc(session_signed_pre_key_get_signature(spk),
                                          session_signed_pre_key_get_signature_len(spk));
    std::string ik_b64 = detail::b64_enc(identity_public_.data(), identity_public_.size());

    std::string prekeys_xml = "<prekeys>";
    auto ids = detail::list_bin_ids(store_dir_ + "/prekeys");
    for (uint32_t pid : ids) {
      session_pre_key *pk = nullptr;
      if (signal_protocol_pre_key_load_key(store_, &pk, pid) != 0 || !pk) continue;
      ec_public_key *pp = ec_key_pair_get_public(session_pre_key_get_key_pair(pk));
      signal_buffer *pb = nullptr;
      ec_public_key_serialize(&pb, pp);
      std::string b64 = detail::b64_enc(signal_buffer_data(pb), signal_buffer_len(pb));
      signal_buffer_free(pb);
      char buf[64];
      snprintf(buf, sizeof(buf), "<preKeyPublic preKeyId='%u'>", pid);
      prekeys_xml += buf;
      prekeys_xml += b64;
      prekeys_xml += "</preKeyPublic>";
      SIGNAL_UNREF(pk);
    }
    prekeys_xml += "</prekeys>";

    char spk_open[96];
    snprintf(spk_open, sizeof(spk_open), "<signedPreKeyPublic signedPreKeyId='%u'>",
             signed_prekey_id_);
    std::string bundle = std::string("<bundle xmlns='") + NS + "'>" + spk_open + spk_b64 +
                         "</signedPreKeyPublic><signedPreKeySignature>" + sig_b64 +
                         "</signedPreKeySignature><identityKey>" + ik_b64 + "</identityKey>" +
                         prekeys_xml + "</bundle>";
    SIGNAL_UNREF(spk);

    char node[160];
    snprintf(node, sizeof(node), "%s.bundles:%u", NS, device_id_);
    std::string iqid = detail::next_iq_id("ombndl");
    return "<iq type='set' id='" + iqid + "'><pubsub xmlns='http://jabber.org/protocol/pubsub'>"
           "<publish node='" +
           std::string(node) + "'><item id='current'>" + bundle + "</item></publish>" +
           detail::publish_options_open() + "</pubsub></iq>";
  }

  std::string iq_request_device_list(const std::string &bare_jid) {
    std::string iqid = detail::next_iq_id("omdlget");
    return "<iq type='get' id='" + iqid + "' to='" + detail::xml_escape_attr(bare_jid) +
           "'><pubsub xmlns='http://jabber.org/protocol/pubsub'><items node='" +
           std::string(NS_DEVICELIST) + "'/></pubsub></iq>";
  }

  std::string iq_request_bundle(const std::string &bare_jid, uint32_t device_id) {
    char node[160];
    snprintf(node, sizeof(node), "%s.bundles:%u", NS, device_id);
    std::string iqid = detail::next_iq_id("ombget");
    return "<iq type='get' id='" + iqid + "' to='" + detail::xml_escape_attr(bare_jid) +
           "'><pubsub xmlns='http://jabber.org/protocol/pubsub'><items node='" + std::string(node) +
           "'/></pubsub></iq>";
  }

  static std::vector<uint32_t> parse_device_list(const std::string &xml) {
    std::vector<uint32_t> ids;
    size_t pos = 0;
    while (true) {
      size_t p = xml.find("<device", pos);
      if (p == std::string::npos) break;
      size_t gt = xml.find('>', p);
      if (gt == std::string::npos) break;
      std::string tag = xml.substr(p, gt - p + 1);
      uint32_t id = detail::parse_u32(detail::extract_attr(tag, "id"));
      if (id) ids.push_back(id);
      pos = gt + 1;
    }
    detail::dedupe_ids(&ids);
    return ids;
  }

  bool ingest_bundle(const std::string &bare_jid, uint32_t device_id, const std::string &bundle_xml) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ready_) return false;

    std::string spk_b64 = detail::trim_ws(detail::extract_element_text(bundle_xml, "signedPreKeyPublic"));
    std::string sig_b64 =
        detail::trim_ws(detail::extract_element_text(bundle_xml, "signedPreKeySignature"));
    std::string ik_b64 = detail::trim_ws(detail::extract_element_text(bundle_xml, "identityKey"));
    if (spk_b64.empty() || sig_b64.empty() || ik_b64.empty()) return false;

    uint32_t spk_id = 0;
    size_t sp = bundle_xml.find("<signedPreKeyPublic");
    if (sp != std::string::npos) {
      size_t gt = bundle_xml.find('>', sp);
      std::string tag = bundle_xml.substr(sp, gt - sp + 1);
      std::string a = detail::extract_attr(tag, "signedPreKeyId");
      if (a.empty()) a = detail::extract_attr(tag, "id");
      spk_id = detail::parse_u32(a);
    }

    // Collect prekeys; pick one at random.
    struct Pk {
      uint32_t id;
      std::string b64;
    };
    std::vector<Pk> pks;
    size_t pos = 0;
    while (true) {
      size_t p = bundle_xml.find("<preKeyPublic", pos);
      if (p == std::string::npos) break;
      size_t gt = bundle_xml.find('>', p);
      if (gt == std::string::npos) break;
      std::string tag = bundle_xml.substr(p, gt - p + 1);
      std::string ida = detail::extract_attr(tag, "preKeyId");
      if (ida.empty()) ida = detail::extract_attr(tag, "id");
      size_t close = bundle_xml.find("</preKeyPublic>", gt);
      if (close == std::string::npos) break;
      Pk pk;
      pk.id = detail::parse_u32(ida);
      pk.b64 = detail::trim_ws(bundle_xml.substr(gt + 1, close - gt - 1));
      if (pk.id && !pk.b64.empty()) pks.push_back(pk);
      pos = close + 1;
    }
    if (pks.empty()) return false;

    uint8_t rnd;
    detail::sg_random(&rnd, 1, nullptr);
    const Pk &chosen = pks[rnd % pks.size()];

    std::vector<uint8_t> spk_raw, sig_raw, ik_raw, pk_raw;
    if (!detail::b64_dec(spk_b64, &spk_raw) || !detail::b64_dec(sig_b64, &sig_raw) ||
        !detail::b64_dec(ik_b64, &ik_raw) || !detail::b64_dec(chosen.b64, &pk_raw))
      return false;

    ec_public_key *spk_pub = nullptr, *ik_pub = nullptr, *pk_pub = nullptr;
    if (detail::decode_pub(&spk_pub, spk_raw.data(), spk_raw.size(), ctx_) != 0 ||
        detail::decode_pub(&ik_pub, ik_raw.data(), ik_raw.size(), ctx_) != 0 ||
        detail::decode_pub(&pk_pub, pk_raw.data(), pk_raw.size(), ctx_) != 0) {
      SIGNAL_UNREF(spk_pub);
      SIGNAL_UNREF(ik_pub);
      SIGNAL_UNREF(pk_pub);
      return false;
    }

    session_pre_key_bundle *bundle = nullptr;
    int r = session_pre_key_bundle_create(&bundle, 0, (int)device_id, chosen.id, pk_pub, spk_id,
                                          spk_pub, sig_raw.data(), sig_raw.size(), ik_pub);
    SIGNAL_UNREF(spk_pub);
    SIGNAL_UNREF(ik_pub);
    SIGNAL_UNREF(pk_pub);
    if (r != 0 || !bundle) return false;

    signal_protocol_address addr = {bare_jid.c_str(), bare_jid.size(), (int32_t)device_id};
    session_builder *builder = nullptr;
    r = session_builder_create(&builder, store_, &addr, ctx_);
    if (r != 0) {
      SIGNAL_UNREF(bundle);
      return false;
    }
    session_builder_set_version(builder, 3);
    r = session_builder_process_pre_key_bundle(builder, bundle);
    session_builder_free(builder);
    SIGNAL_UNREF(bundle);
    return r == 0;
  }

  void set_devices(const std::string &bare_jid, const std::vector<uint32_t> &ids) {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<uint32_t> copy = ids;
    detail::dedupe_ids(&copy);
    devices_[bare_jid] = copy;
    if (ready_) save_device_file(bare_jid, copy);
  }

  std::vector<uint32_t> devices(const std::string &bare_jid) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = devices_.find(bare_jid);
    if (it == devices_.end()) return {};
    return it->second;
  }

  bool encrypt_message(const std::string &to_bare, const std::string &plaintext,
                       std::string *encrypted_xml, std::string *err) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ready_) {
      if (err) *err = "omemo not ready";
      return false;
    }

    struct Target {
      std::string jid;
      uint32_t id;
    };
    std::vector<Target> targets;
    auto add_all = [&](const std::string &jid, bool skip_self) {
      auto it = devices_.find(jid);
      if (it == devices_.end()) return;
      for (uint32_t id : it->second) {
        if (skip_self && jid == bare_jid_ && id == device_id_) continue;
        targets.push_back({jid, id});
      }
    };
    add_all(to_bare, false);
    add_all(bare_jid_, true);
    // Also load self.txt peers if not cached under bare_jid_
    if (devices_.find(bare_jid_) == devices_.end()) {
      std::vector<uint32_t> self_ids;
      load_device_file("self", &self_ids);
      for (uint32_t id : self_ids)
        if (id != device_id_) targets.push_back({bare_jid_, id});
    }

    if (targets.empty()) {
      if (err) *err = "no devices";
      return false;
    }

    uint8_t key[16], iv[12], tag[16];
    if (detail::sg_random(key, 16, nullptr) != 0 || detail::sg_random(iv, 12, nullptr) != 0) {
      if (err) *err = "rng failed";
      return false;
    }
    std::vector<uint8_t> ct;
    if (!detail::aes_gcm_encrypt(key, iv, (const uint8_t *)plaintext.data(), plaintext.size(), &ct,
                                 tag)) {
      if (err) *err = "gcm encrypt failed";
      return false;
    }
    uint8_t key_tag[32];
    memcpy(key_tag, key, 16);
    memcpy(key_tag + 16, tag, 16);

    std::string keys_xml;
    int ok_count = 0;
    for (const Target &t : targets) {
      signal_protocol_address addr = {t.jid.c_str(), t.jid.size(), (int32_t)t.id};
      if (signal_protocol_session_contains_session(store_, &addr) != 1) continue;

      session_cipher *cipher = nullptr;
      if (session_cipher_create(&cipher, store_, &addr, ctx_) != 0) continue;
      session_cipher_set_version(cipher, 3);
      ciphertext_message *msg = nullptr;
      int r = session_cipher_encrypt(cipher, key_tag, 32, &msg);
      if (r != 0 || !msg) {
        session_cipher_free(cipher);
        continue;
      }
      signal_buffer *ser = ciphertext_message_get_serialized(msg);
      bool is_prekey = ciphertext_message_get_type(msg) == CIPHERTEXT_PREKEY_TYPE;
      std::string b64 = detail::b64_enc(signal_buffer_const_data(ser), signal_buffer_len(ser));
      SIGNAL_UNREF(msg);
      session_cipher_free(cipher);
      if (b64.empty()) continue;
      char buf[96];
      if (is_prekey)
        snprintf(buf, sizeof(buf), "<key prekey='true' rid='%u'>", t.id);
      else
        snprintf(buf, sizeof(buf), "<key rid='%u'>", t.id);
      keys_xml += buf;
      keys_xml += b64;
      keys_xml += "</key>";
      ++ok_count;
    }

    if (ok_count == 0) {
      if (err) *err = "no sessions";
      return false;
    }

    std::string iv_b64 = detail::b64_enc(iv, 12);
    std::string ct_b64 = detail::b64_enc(ct.data(), ct.size());
    char hdr[80];
    snprintf(hdr, sizeof(hdr), "<header sid='%u'>", device_id_);
    *encrypted_xml = std::string("<encrypted xmlns='") + NS + "'>" + hdr + keys_xml + "<iv>" +
                     iv_b64 + "</iv></header><payload>" + ct_b64 + "</payload></encrypted>";
    // Suggested plaintext fallback body for non-OMEMO clients:
    // "I sent you an OMEMO encrypted message but your client doesn't seem to support that."
    if (err) err->clear();
    return true;
  }

  bool decrypt_message(const std::string &from_bare, const std::string &stanza_or_encrypted,
                       std::string *plaintext, bool *was_omemo, std::string *err) {
    std::lock_guard<std::mutex> lock(mu_);
    if (was_omemo) *was_omemo = false;
    if (!ready_) {
      if (err) *err = "omemo not ready";
      return false;
    }

    size_t enc_pos = stanza_or_encrypted.find("<encrypted");
    if (enc_pos == std::string::npos) {
      if (err) *err = "no encrypted element";
      return false;
    }
    // Ensure it's our NS (best-effort)
    size_t enc_gt = stanza_or_encrypted.find('>', enc_pos);
    if (enc_gt == std::string::npos) return false;
    std::string enc_open = stanza_or_encrypted.substr(enc_pos, enc_gt - enc_pos + 1);
    if (enc_open.find(NS) == std::string::npos &&
        stanza_or_encrypted.find(NS) == std::string::npos) {
      // still try — some snippets omit xmlns on child
    }
    if (was_omemo) *was_omemo = true;

    // Find key for our device id
    std::string key_b64;
    bool is_prekey = false;
    uint32_t sid = 0;
    size_t hp = stanza_or_encrypted.find("<header", enc_pos);
    if (hp != std::string::npos) {
      size_t hgt = stanza_or_encrypted.find('>', hp);
      sid = detail::parse_u32(detail::extract_attr(stanza_or_encrypted.substr(hp, hgt - hp + 1), "sid"));
    }

    char rid_needle[64];
    snprintf(rid_needle, sizeof(rid_needle), "rid='%u'", device_id_);
    char rid_needle2[64];
    snprintf(rid_needle2, sizeof(rid_needle2), "rid=\"%u\"", device_id_);
    size_t pos = enc_pos;
    while (true) {
      size_t kp = stanza_or_encrypted.find("<key", pos);
      if (kp == std::string::npos || kp > stanza_or_encrypted.find("</encrypted", enc_pos)) break;
      size_t kgt = stanza_or_encrypted.find('>', kp);
      if (kgt == std::string::npos) break;
      std::string tag = stanza_or_encrypted.substr(kp, kgt - kp + 1);
      if (tag.find(rid_needle) != std::string::npos || tag.find(rid_needle2) != std::string::npos) {
        std::string pre = detail::extract_attr(tag, "prekey");
        is_prekey = (pre == "true" || pre == "1" || pre == "True");
        size_t close = stanza_or_encrypted.find("</key>", kgt);
        if (close == std::string::npos) break;
        key_b64 = detail::trim_ws(stanza_or_encrypted.substr(kgt + 1, close - kgt - 1));
        break;
      }
      pos = kgt + 1;
    }
    if (key_b64.empty()) {
      if (err) *err = "no key for our device";
      return false;
    }

    std::string iv_b64 = detail::trim_ws(detail::extract_element_text(stanza_or_encrypted, "iv"));
    std::string payload_b64 =
        detail::trim_ws(detail::extract_element_text(stanza_or_encrypted, "payload"));
    std::vector<uint8_t> key_msg, iv, payload;
    if (!detail::b64_dec(key_b64, &key_msg) || !detail::b64_dec(iv_b64, &iv) ||
        !detail::b64_dec(payload_b64, &payload)) {
      if (err) *err = "base64 decode failed";
      return false;
    }
    if (iv.size() != 12) {
      if (err) *err = "bad iv length";
      return false;
    }

    uint32_t sender_device = sid ? sid : 0;
    // Prefer sid; if missing, cannot address session — require sid
    if (!sender_device) {
      if (err) *err = "missing sid";
      return false;
    }

    signal_protocol_address addr = {from_bare.c_str(), from_bare.size(), (int32_t)sender_device};
    session_cipher *cipher = nullptr;
    if (session_cipher_create(&cipher, store_, &addr, ctx_) != 0) {
      if (err) *err = "cipher create failed";
      return false;
    }
    session_cipher_set_version(cipher, 3);

    signal_buffer *pt_buf = nullptr;
    int r = SG_ERR_UNKNOWN;
    if (is_prekey) {
      pre_key_signal_message *pkm = nullptr;
      r = pre_key_signal_message_deserialize(&pkm, key_msg.data(), key_msg.size(), ctx_);
      if (r == 0 && pkm) {
        r = session_cipher_decrypt_pre_key_signal_message(cipher, pkm, nullptr, &pt_buf);
        SIGNAL_UNREF(pkm);
      }
    } else {
      signal_message *sm = nullptr;
      r = signal_message_deserialize(&sm, key_msg.data(), key_msg.size(), ctx_);
      if (r == 0 && sm) {
        r = session_cipher_decrypt_signal_message(cipher, sm, nullptr, &pt_buf);
        SIGNAL_UNREF(sm);
      }
    }
    session_cipher_free(cipher);

    if (r != 0 || !pt_buf) {
      if (err) *err = "signal decrypt failed";
      if (pt_buf) signal_buffer_free(pt_buf);
      return false;
    }
    if (signal_buffer_len(pt_buf) < 32) {
      if (err) *err = "key material too short";
      signal_buffer_free(pt_buf);
      return false;
    }
    const uint8_t *kt = signal_buffer_const_data(pt_buf);
    uint8_t aes_key[16], tag[16];
    memcpy(aes_key, kt, 16);
    memcpy(tag, kt + 16, 16);
    signal_buffer_free(pt_buf);

    std::vector<uint8_t> pt;
    if (!detail::aes_gcm_decrypt(aes_key, iv.data(), payload.data(), payload.size(), tag, &pt)) {
      if (err) *err = "gcm decrypt failed";
      return false;
    }
    *plaintext = std::string(pt.begin(), pt.end());
    return true;
  }

private:
  mutable std::mutex mu_;
  bool ready_ = false;
  std::string store_dir_;
  std::string bare_jid_;
  uint32_t device_id_ = 0;
  uint32_t registration_id_ = 0;
  uint32_t signed_prekey_id_ = 0;
  std::vector<uint8_t> identity_public_;
  std::vector<uint8_t> identity_private_;
  signal_context *ctx_ = nullptr;
  signal_protocol_store_context *store_ = nullptr;
  std::map<std::string, std::vector<uint32_t>> devices_;

  void close_unlocked() {
    ready_ = false;
    if (store_) {
      signal_protocol_store_context_destroy(store_);
      store_ = nullptr;
    }
    if (ctx_) {
      signal_context_destroy(ctx_);
      ctx_ = nullptr;
    }
    identity_public_.clear();
    identity_private_.clear();
    devices_.clear();
    device_id_ = registration_id_ = signed_prekey_id_ = 0;
    store_dir_.clear();
    bare_jid_.clear();
  }

  std::string session_path(const signal_protocol_address *address) const {
    std::string name(address->name, address->name_len);
    return store_dir_ + "/sessions/" + detail::sanitize_jid(name) + "_" +
           std::to_string(address->device_id) + ".bin";
  }

  std::string identity_path(const signal_protocol_address *address) const {
    std::string name(address->name, address->name_len);
    return store_dir_ + "/identities/" + detail::sanitize_jid(name) + ".bin";
  }

  std::string prekey_path(uint32_t id) const {
    return store_dir_ + "/prekeys/" + std::to_string(id) + ".bin";
  }

  std::string signed_prekey_path(uint32_t id) const {
    return store_dir_ + "/signed_prekeys/" + std::to_string(id) + ".bin";
  }

  void save_device_file(const std::string &key, const std::vector<uint32_t> &ids) const {
    std::string path = store_dir_ + "/devices/" + detail::sanitize_jid(key) + ".txt";
    if (key == "self") path = store_dir_ + "/devices/self.txt";
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return;
    for (uint32_t id : ids) fprintf(f, "%u\n", id);
    fclose(f);
  }

  void load_device_file(const std::string &key, std::vector<uint32_t> *ids) const {
    std::string path = store_dir_ + "/devices/" + detail::sanitize_jid(key) + ".txt";
    if (key == "self") path = store_dir_ + "/devices/self.txt";
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
      uint32_t id = (uint32_t)strtoul(line, nullptr, 10);
      if (id) ids->push_back(id);
    }
    fclose(f);
    detail::dedupe_ids(ids);
  }

  bool persist_meta() const {
    FILE *f = fopen((store_dir_ + "/meta.dat").c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite("OMEM1", 1, 5, f) == 5;
    ok = ok && detail::write_u32(f, device_id_);
    ok = ok && detail::write_u32(f, registration_id_);
    ok = ok && detail::write_blob(f, identity_public_.data(), (uint32_t)identity_public_.size());
    ok = ok && detail::write_blob(f, identity_private_.data(), (uint32_t)identity_private_.size());
    ok = ok && detail::write_u32(f, signed_prekey_id_);
    fclose(f);
    return ok;
  }

  bool generate_install() {
    ratchet_identity_key_pair *ikp = nullptr;
    if (signal_protocol_key_helper_generate_identity_key_pair(&ikp, ctx_) != 0) return false;
    if (signal_protocol_key_helper_generate_registration_id(&registration_id_, 0, ctx_) != 0) {
      SIGNAL_UNREF(ikp);
      return false;
    }
    uint8_t rnd[4];
    if (detail::sg_random(rnd, 4, nullptr) != 0) {
      SIGNAL_UNREF(ikp);
      return false;
    }
    device_id_ = ((uint32_t)rnd[0] << 24) | ((uint32_t)rnd[1] << 16) | ((uint32_t)rnd[2] << 8) |
                 (uint32_t)rnd[3];
    device_id_ &= 0x7FFFFFFFu;
    if (device_id_ == 0) device_id_ = 1;

    signal_buffer *pub = nullptr, *priv = nullptr;
    ec_public_key_serialize(&pub, ratchet_identity_key_pair_get_public(ikp));
    ec_private_key_serialize(&priv, ratchet_identity_key_pair_get_private(ikp));
    identity_public_.assign(signal_buffer_data(pub),
                            signal_buffer_data(pub) + signal_buffer_len(pub));
    identity_private_.assign(signal_buffer_data(priv),
                             signal_buffer_data(priv) + signal_buffer_len(priv));
    signal_buffer_free(pub);
    signal_buffer_free(priv);

    signal_protocol_key_helper_pre_key_list_node *head = nullptr;
    if (signal_protocol_key_helper_generate_pre_keys(&head, 1, 100, ctx_) != 0) {
      SIGNAL_UNREF(ikp);
      return false;
    }
    for (auto *n = head; n; n = signal_protocol_key_helper_key_list_next(n)) {
      session_pre_key *pk = signal_protocol_key_helper_key_list_element(n);
      if (signal_protocol_pre_key_store_key(store_, pk) != 0) {
        signal_protocol_key_helper_key_list_free(head);
        SIGNAL_UNREF(ikp);
        return false;
      }
    }
    signal_protocol_key_helper_key_list_free(head);

    signed_prekey_id_ = 1;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    // FILETIME is 100ns since 1601; convert rough ms since Unix epoch
    uint64_t ts_ms = (uli.QuadPart / 10000ULL) - 11644473600000ULL;

    session_signed_pre_key *spk = nullptr;
    if (signal_protocol_key_helper_generate_signed_pre_key(&spk, ikp, signed_prekey_id_, ts_ms,
                                                          ctx_) != 0) {
      SIGNAL_UNREF(ikp);
      return false;
    }
    if (signal_protocol_signed_pre_key_store_key(store_, spk) != 0) {
      SIGNAL_UNREF(spk);
      SIGNAL_UNREF(ikp);
      return false;
    }
    SIGNAL_UNREF(spk);
    SIGNAL_UNREF(ikp);
    return persist_meta();
  }

  bool setup_store() {
    if (signal_protocol_store_context_create(&store_, ctx_) != 0) return false;

    signal_protocol_session_store ss = {};
    ss.user_data = this;
    ss.load_session_func = [](signal_buffer **record, signal_buffer **user_record,
                              const signal_protocol_address *address, void *user_data) -> int {
      auto *m = (Manager *)user_data;
      if (user_record) *user_record = nullptr;
      std::vector<uint8_t> data;
      if (!detail::read_file(m->session_path(address), &data)) return 0;
      *record = signal_buffer_create(data.data(), data.size());
      return *record ? 1 : SG_ERR_NOMEM;
    };
    ss.get_sub_device_sessions_func = [](signal_int_list **sessions, const char *name,
                                         size_t name_len, void *user_data) -> int {
      auto *m = (Manager *)user_data;
      *sessions = signal_int_list_alloc();
      if (!*sessions) return SG_ERR_NOMEM;
      std::string prefix = detail::sanitize_jid(std::string(name, name_len)) + "_";
      std::string dir = m->store_dir_ + "/sessions";
      std::string pattern = dir + "\\*.bin";
      WIN32_FIND_DATAA fd;
      HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
      if (h == INVALID_HANDLE_VALUE) {
        pattern = dir + "/*.bin";
        h = FindFirstFileA(pattern.c_str(), &fd);
      }
      if (h == INVALID_HANDLE_VALUE) return 0;
      do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string fn = fd.cFileName;
        if (fn.rfind(prefix, 0) != 0) continue;
        std::string rest = fn.substr(prefix.size());
        size_t dot = rest.find('.');
        if (dot != std::string::npos) rest = rest.substr(0, dot);
        int id = atoi(rest.c_str());
        if (id) signal_int_list_push_back(*sessions, id);
      } while (FindNextFileA(h, &fd));
      FindClose(h);
      return (int)signal_int_list_size(*sessions);
    };
    ss.store_session_func = [](const signal_protocol_address *address, uint8_t *record,
                               size_t record_len, uint8_t *, size_t, void *user_data) -> int {
      auto *m = (Manager *)user_data;
      return detail::write_file(m->session_path(address), record, record_len) ? 0 : SG_ERR_UNKNOWN;
    };
    ss.contains_session_func = [](const signal_protocol_address *address, void *user_data) -> int {
      auto *m = (Manager *)user_data;
      FILE *f = fopen(m->session_path(address).c_str(), "rb");
      if (!f) return 0;
      fclose(f);
      return 1;
    };
    ss.delete_session_func = [](const signal_protocol_address *address, void *user_data) -> int {
      auto *m = (Manager *)user_data;
      return DeleteFileA(m->session_path(address).c_str()) ? 1 : 0;
    };
    ss.delete_all_sessions_func = [](const char *name, size_t name_len, void *user_data) -> int {
      auto *m = (Manager *)user_data;
      int n = 0;
      std::string prefix = detail::sanitize_jid(std::string(name, name_len)) + "_";
      std::string dir = m->store_dir_ + "/sessions";
      std::string pattern = dir + "\\*.bin";
      WIN32_FIND_DATAA fd;
      HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
      if (h == INVALID_HANDLE_VALUE) {
        pattern = dir + "/*.bin";
        h = FindFirstFileA(pattern.c_str(), &fd);
      }
      if (h == INVALID_HANDLE_VALUE) return 0;
      do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string fn = fd.cFileName;
        if (fn.rfind(prefix, 0) != 0) continue;
        if (DeleteFileA((dir + "/" + fn).c_str())) ++n;
      } while (FindNextFileA(h, &fd));
      FindClose(h);
      return n;
    };
    ss.destroy_func = nullptr;

    signal_protocol_pre_key_store pks = {};
    pks.user_data = this;
    pks.load_pre_key = [](signal_buffer **record, uint32_t pre_key_id, void *user_data) -> int {
      auto *m = (Manager *)user_data;
      std::vector<uint8_t> data;
      if (!detail::read_file(m->prekey_path(pre_key_id), &data)) return SG_ERR_INVALID_KEY_ID;
      *record = signal_buffer_create(data.data(), data.size());
      return *record ? 0 : SG_ERR_NOMEM;
    };
    pks.store_pre_key = [](uint32_t pre_key_id, uint8_t *record, size_t record_len,
                           void *user_data) -> int {
      auto *m = (Manager *)user_data;
      return detail::write_file(m->prekey_path(pre_key_id), record, record_len) ? 0 : SG_ERR_UNKNOWN;
    };
    pks.contains_pre_key = [](uint32_t pre_key_id, void *user_data) -> int {
      auto *m = (Manager *)user_data;
      FILE *f = fopen(m->prekey_path(pre_key_id).c_str(), "rb");
      if (!f) return 0;
      fclose(f);
      return 1;
    };
    pks.remove_pre_key = [](uint32_t pre_key_id, void *user_data) -> int {
      auto *m = (Manager *)user_data;
      DeleteFileA(m->prekey_path(pre_key_id).c_str());
      return 0;
    };
    pks.destroy_func = nullptr;

    signal_protocol_signed_pre_key_store spks = {};
    spks.user_data = this;
    spks.load_signed_pre_key = [](signal_buffer **record, uint32_t signed_pre_key_id,
                                  void *user_data) -> int {
      auto *m = (Manager *)user_data;
      std::vector<uint8_t> data;
      if (!detail::read_file(m->signed_prekey_path(signed_pre_key_id), &data))
        return SG_ERR_INVALID_KEY_ID;
      *record = signal_buffer_create(data.data(), data.size());
      return *record ? 0 : SG_ERR_NOMEM;
    };
    spks.store_signed_pre_key = [](uint32_t signed_pre_key_id, uint8_t *record, size_t record_len,
                                   void *user_data) -> int {
      auto *m = (Manager *)user_data;
      return detail::write_file(m->signed_prekey_path(signed_pre_key_id), record, record_len)
                 ? 0
                 : SG_ERR_UNKNOWN;
    };
    spks.contains_signed_pre_key = [](uint32_t signed_pre_key_id, void *user_data) -> int {
      auto *m = (Manager *)user_data;
      FILE *f = fopen(m->signed_prekey_path(signed_pre_key_id).c_str(), "rb");
      if (!f) return 0;
      fclose(f);
      return 1;
    };
    spks.remove_signed_pre_key = [](uint32_t signed_pre_key_id, void *user_data) -> int {
      auto *m = (Manager *)user_data;
      DeleteFileA(m->signed_prekey_path(signed_pre_key_id).c_str());
      return 0;
    };
    spks.destroy_func = nullptr;

    signal_protocol_identity_key_store iks = {};
    iks.user_data = this;
    iks.get_identity_key_pair = [](signal_buffer **public_data, signal_buffer **private_data,
                                   void *user_data) -> int {
      auto *m = (Manager *)user_data;
      *public_data = signal_buffer_create(m->identity_public_.data(), m->identity_public_.size());
      *private_data =
          signal_buffer_create(m->identity_private_.data(), m->identity_private_.size());
      return (*public_data && *private_data) ? 0 : SG_ERR_NOMEM;
    };
    iks.get_local_registration_id = [](void *user_data, uint32_t *registration_id) -> int {
      *registration_id = ((Manager *)user_data)->registration_id_;
      return 0;
    };
    iks.save_identity = [](const signal_protocol_address *address, uint8_t *key_data, size_t key_len,
                           void *user_data) -> int {
      auto *m = (Manager *)user_data;
      std::string path = m->identity_path(address);
      // TOFU: keep first-seen key
      FILE *exist = fopen(path.c_str(), "rb");
      if (exist) {
        fclose(exist);
        return 0;
      }
      if (!key_data || key_len == 0) return 0;
      return detail::write_file(path, key_data, key_len) ? 0 : SG_ERR_UNKNOWN;
    };
    iks.is_trusted_identity = [](const signal_protocol_address *, uint8_t *, size_t,
                                 void *) -> int {
      // Always accept for this slice (TOFU does not block decrypt).
      return 1;
    };
    iks.destroy_func = nullptr;

    signal_protocol_sender_key_store sks = {};
    sks.user_data = this;
    sks.store_sender_key = [](const signal_protocol_sender_key_name *, uint8_t *, size_t, uint8_t *,
                              size_t, void *) -> int { return 0; };
    sks.load_sender_key = [](signal_buffer **record, signal_buffer **user_record,
                             const signal_protocol_sender_key_name *, void *) -> int {
      if (user_record) *user_record = nullptr;
      *record = nullptr;
      return 0;
    };
    sks.destroy_func = nullptr;

    if (signal_protocol_store_context_set_session_store(store_, &ss) != 0) return false;
    if (signal_protocol_store_context_set_pre_key_store(store_, &pks) != 0) return false;
    if (signal_protocol_store_context_set_signed_pre_key_store(store_, &spks) != 0) return false;
    if (signal_protocol_store_context_set_identity_key_store(store_, &iks) != 0) return false;
    if (signal_protocol_store_context_set_sender_key_store(store_, &sks) != 0) return false;
    return true;
  }
};

#endif // JABBER_OMEMO

} // namespace omemo
} // namespace jabber
