#pragma once

#include "common/mutex.hpp"
#include "common/security/trusted_devices.hpp"
#include "psa/crypto.h"
#include "psa/crypto_struct.h"
#include "psa/crypto_types.h"
#include "psa/crypto_values.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <expected>
#include <mutex>
#include <span>
#include <zephyr/drivers/hwinfo.h>

#ifdef CONFIG_SECURE_SIGN
#include "tfm_crypto_defs.h"
#endif

#ifndef IDENTITY_KEY_ID
#define IDENTITY_KEY_ID (PSA_KEY_ID_USER_MIN + 1)
#endif

namespace common {

/**
 * @brief Trusted Device store, handles the signing of messages with the current
 * nodes private key. It also provides functionality for checking messages from
 * other nodes with their public keys.
 */
class TrustedDeviceStore {
public:
  static constexpr std::size_t SIG_LEN = 64;
  static constexpr std::size_t MAX_PEERS = 8;
  static constexpr std::size_t KEY_BITS = 256;
  static constexpr std::size_t BUFFER_LEN = 512;
  static constexpr std::size_t DIGEST_SIZE = 32;
  static constexpr std::size_t SEED_LEN = 32;
  static constexpr const char *DEVICE_LABEL = "flood-ews-sign-v1";

  using SignatureT = std::array<std::uint8_t, SIG_LEN>;

  /**
   * @brief Initialise the key, this is derived from a fixed seed per device so
   * it is deterministic but still cryptographically secure.
   * @param [in] eui The eui used to generate the key.
   * @return psa_status_t
   */
  [[nodiscard]] psa_status_t init(const std::uint64_t eui) noexcept {

    const auto seed_exp = derive_identity_seed(eui);
    if (!seed_exp.has_value()) {
      return seed_exp.error();
    }

    const auto &seed = seed_exp.value();

    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, KEY_BITS);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_HASH |
                                       PSA_KEY_USAGE_SIGN_MESSAGE);
    // Volatile so nothing sensitive is ever stored.
    psa_set_key_lifetime(&attr, PSA_KEY_LIFETIME_VOLATILE);

    const auto status =
        psa_import_key(&attr, seed.data(), seed.size(), &m_my_key_id);

    return status;
  }

  /**
   * @brief Get the public key for the stored key.
   * @return std::expected<PubKeyT, psa_status_t>
   */
  [[nodiscard]] std::expected<PubKeyT, psa_status_t>
  export_pubkey() const noexcept {
    std::size_t out_len = 0;
    PubKeyT out;

    if (const auto err = psa_export_public_key(m_my_key_id, out.data(),
                                               out.size(), &out_len);
        err != PSA_SUCCESS) {
      return std::unexpected(err);
    }

    return out;
  }

  /**
   * @brief Add trusted devices public keys to the trust store so we can verify
   * against them.
   * @param [in] devices
   * @return psa_status_t
   */
  [[nodiscard]] psa_status_t
  trust_peers(const std::span<const TrustedDevice> devices) noexcept {
    for (const auto &dev : devices) {
      psa_status_t status = trust_peer(dev.m_eui, dev.m_pub_key);
      if (status != PSA_SUCCESS) {
        return status;
      }
    }
    return PSA_SUCCESS;
  }

  /**
   * @brief Create a signature for the given payload bytes.
   * @param [in] rpc_bytes The payload to sign.
   * @return std::expected<SignatureT, psa_status_t> The signature or the error
   * on failure.
   */
  [[nodiscard]] std::expected<SignatureT, psa_status_t>
  sign(const std::span<const uint8_t> rpc_bytes) const noexcept {
    const std::scoped_lock guard(m_mutex);
    std::array<std::uint8_t, DIGEST_SIZE> digest;
    std::size_t digest_len = 0;
    auto status = compute_digest(rpc_bytes, digest, digest_len);
    if (status != PSA_SUCCESS) {
      return std::unexpected(status);
    }

    SignatureT sig_out;
    std::size_t sig_len = 0;
    status = psa_sign_hash(m_my_key_id, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                           digest.data(), digest_len, sig_out.data(),
                           sig_out.size(), &sig_len);

    if (status != PSA_SUCCESS) {
      return std::unexpected(status);
    }

    return sig_out;
  }

  /**
   * @brief Verify the payload is valid.
   * @param [in] sender_eui Who sent the message.
   * @param [in] rpc_bytes The message to validate.
   * @param [in] sig The messages signature.
   * @return psa_status_t
   */
  [[nodiscard]] psa_status_t
  verify(const std::uint64_t sender_eui,
         const std::span<const std::uint8_t> rpc_bytes,
         const SignatureT &sig) noexcept {
    const std::scoped_lock guard(m_mutex);
    auto const *const peer = find_peer(sender_eui);
    if (peer == nullptr) {
      return PSA_ERROR_INVALID_HANDLE;
    }

    std::array<std::uint8_t, DIGEST_SIZE> digest;
    std::size_t digest_len = 0;
    auto status = compute_digest(rpc_bytes, digest, digest_len);
    if (status != PSA_SUCCESS) {
      return status;
    }

    status = psa_verify_hash(peer->m_pubkey_id, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                             digest.data(), digest_len, sig.data(), sig.size());

    return status;
  }

private:
  /** @brief Helper to track peers. */
  struct TrustedPeer {
    std::uint64_t m_eui = 0;
    psa_key_id_t m_pubkey_id = 0;
    bool m_in_use = false;
  };

  /**
   * @brief Hash the payload.
   * @param [in] rpc_bytes The bytes to hash.
   * @param [inout] digest The running digest.
   * @param [inout] digest_len The digest length.
   * @return psa_status_t
   */
  [[nodiscard]] static psa_status_t
  compute_digest(std::span<const uint8_t> rpc_bytes,
                 std::array<std::uint8_t, DIGEST_SIZE> &digest,
                 size_t &digest_len) noexcept {
    psa_hash_operation_t mop = PSA_HASH_OPERATION_INIT;
    psa_status_t status = psa_hash_setup(&mop, PSA_ALG_SHA_256);
    if (status != PSA_SUCCESS) {
      return status;
    }

    status = psa_hash_update(&mop, rpc_bytes.data(), rpc_bytes.size());
    if (status != PSA_SUCCESS) {
      psa_hash_abort(&mop);
      return status;
    }

    return psa_hash_finish(&mop, digest.data(), DIGEST_SIZE, &digest_len);
  }

  /**
   * @brief Add one peer to the trusted peers.
   * @param [in] eui The devices eui.
   * @param [in] pubkey Their public key.
   * @return psa_status_t
   */
  [[nodiscard]] psa_status_t trust_peer(const std::uint64_t eui,
                                        const PubKeyT &pub_key) noexcept {
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key_id;

    psa_set_key_type(&attr,
                     PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, KEY_BITS);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_VERIFY_HASH |
                                       PSA_KEY_USAGE_VERIFY_MESSAGE);

    const auto status =
        psa_import_key(&attr, pub_key.data(), pub_key.size(), &key_id);
    if (status != PSA_SUCCESS) {
      return status;
    }

    for (auto &peer : m_peers) {
      if (!peer.m_in_use) {
        peer.m_eui = eui;
        peer.m_pubkey_id = key_id;
        peer.m_in_use = true;
        return PSA_SUCCESS;
      }
    }
    return PSA_ERROR_INSUFFICIENT_MEMORY;
  }

  /**
   * @brief Find a peer in the peer table.
   * @param [in] eui Their EUI
   * @return TrustedPeer* nullptr if not found.
   */
  [[nodiscard]] TrustedPeer *find_peer(const std::uint64_t eui) noexcept {
    for (auto &peer : m_peers) {
      if (peer.m_in_use && peer.m_eui == eui) {
        return &peer;
      }
    }
    return nullptr;
  }

  /**
   * @brief Generate a seed to create the key-pair using: The devices EUI-64,
   * the DEVICE_ID salt, and the secret HUK stored inside the KMU. This creates
   * a cryptographically secure but deterministic keypair.
   * @param [in] eui The devices eui.
   * @return std::expected<std::array<std::uint8_t, SEED_LEN>, psa_status_t>
   */
  [[nodiscard]] static std::expected<std::array<std::uint8_t, SEED_LEN>,
                                     psa_status_t>
  derive_identity_seed(const std::uint64_t eui) noexcept {

    std::array<std::uint8_t, SEED_LEN> seed_out{};

    // Initialize PSA Key Derivation operation
    psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;

    // Setup derivation algorithm.
    psa_status_t status =
        psa_key_derivation_setup(&op, PSA_ALG_HKDF(PSA_ALG_SHA_256));
    if (status != PSA_SUCCESS) {
      return std::unexpected(status);
    }

    // Add Salt (DEVICE_LABEL)
    status = psa_key_derivation_input_bytes(
        &op, PSA_KEY_DERIVATION_INPUT_SALT,
        reinterpret_cast<const std::uint8_t *>(DEVICE_LABEL),
        std::strlen(DEVICE_LABEL));
    if (status != PSA_SUCCESS) {
      psa_key_derivation_abort(&op);
      return std::unexpected(status);
    }

#ifdef CONFIG_SECURE_SIGN
    // Pass the hardware built-in unique key (HUK) as secret input.
    status = psa_key_derivation_input_key(&op, PSA_KEY_DERIVATION_INPUT_SECRET,
                                          TFM_BUILTIN_KEY_ID_HUK);
#else
    // Use the hardware key with is persistent between mass erase.
    std::array<std::uint8_t, 16> hw_id{};
    const ssize_t hw_id_len = hwinfo_get_device_id(hw_id.data(), hw_id.size());
    if (hw_id_len < 0) {
      return std::unexpected(PSA_ERROR_HARDWARE_FAILURE);
    }

    status = psa_key_derivation_input_bytes(
        &op, PSA_KEY_DERIVATION_INPUT_SECRET, hw_id.data(),
        static_cast<std::size_t>(hw_id_len));
#endif
    if (status != PSA_SUCCESS) {
      psa_key_derivation_abort(&op);
      return std::unexpected(status);
    }

    // Add Info parameter (EUI)
    status = psa_key_derivation_input_bytes(
        &op, PSA_KEY_DERIVATION_INPUT_INFO,
        reinterpret_cast<const std::uint8_t *>(&eui), sizeof(eui));
    if (status != PSA_SUCCESS) {
      psa_key_derivation_abort(&op);
      return std::unexpected(status);
    }

    // Extract derived seed output
    status =
        psa_key_derivation_output_bytes(&op, seed_out.data(), seed_out.size());

    // Clean up operation state
    psa_key_derivation_abort(&op);

    if (status != PSA_SUCCESS) {
      return std::unexpected(status);
    }

    return seed_out;
  }

  /** @brief This nodes key id. */
  psa_key_id_t m_my_key_id = 0;

  /** @brief The peer table. */
  std::array<TrustedPeer, MAX_PEERS> m_peers{};

  /** @brief Mutex to protect internal state. */
  mutable common::mutex m_mutex;
};

} // namespace common
