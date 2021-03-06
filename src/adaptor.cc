#include "adaptor.h"

#include "go_quic_dispatcher.h"
#include "go_quic_connection_helper.h"
#include "go_quic_server_packet_writer.h"
#include "go_quic_simple_server_stream.h"
#include "go_quic_alarm_go_wrapper.h"
#include "proof_source_goquic.h"
#include "go_ephemeral_key_source.h"

#include "net/quic/quic_connection.h"
#include "net/quic/quic_clock.h"
#include "net/quic/quic_time.h"
#include "net/quic/quic_protocol.h"
#include "net/quic/crypto/quic_random.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "base/strings/string_piece.h"

#include "base/command_line.h"
#include "base/at_exit.h"
#include "base/logging.h"

#include <iostream>
#include <vector>
#include <stddef.h>

#define EXPECT_TRUE(x) \
  {                    \
    if (!(x))          \
      printf("ERROR"); \
  }

using namespace net;
using namespace std;

static base::AtExitManager* exit_manager;
void initialize() {
  int argc = 1;
  const char* argv[] = {"test", nullptr};
  base::CommandLine::Init(argc, argv);
  exit_manager = new base::AtExitManager;  // Deleted at the end
}

void set_log_level(int level) {
  logging::SetMinLogLevel(level);
}

GoQuicDispatcher* create_quic_dispatcher(
    GoPtr go_writer,
    GoPtr go_quic_dispatcher,
    GoPtr go_task_runner,
    QuicCryptoServerConfig* crypto_config) {
  QuicConfig* config = new QuicConfig();

  // TODO(serialx, hodduc): What is "secret"?
  // TODO(hodduc) "crypto_config" should be shared as global constant, but there
  // is no clean way to do it now T.T
  // Deleted by ~GoQuicDispatcher()
  QuicClock* clock =
      new QuicClock();  // Deleted by scoped ptr of GoQuicConnectionHelper
  QuicRandom* random_generator = QuicRandom::GetInstance();

  GoQuicConnectionHelper* helper = new GoQuicConnectionHelper(
      go_task_runner, clock,
      random_generator);  // Deleted by delete_go_quic_dispatcher()
  QuicVersionVector versions(net::QuicSupportedVersions());

  /* Initialize Configs ------------------------------------------------*/

  // If an initial flow control window has not explicitly been set, then use a
  // sensible value for a server: 1 MB for session, 64 KB for each stream.
  const uint32_t kInitialSessionFlowControlWindow = 1 * 1024 * 1024;  // 1 MB
  const uint32_t kInitialStreamFlowControlWindow = 64 * 1024;         // 64 KB
  if (config->GetInitialStreamFlowControlWindowToSend() ==
      kMinimumFlowControlSendWindow) {
    config->SetInitialStreamFlowControlWindowToSend(
        kInitialStreamFlowControlWindow);
  }
  if (config->GetInitialSessionFlowControlWindowToSend() ==
      kMinimumFlowControlSendWindow) {
    config->SetInitialSessionFlowControlWindowToSend(
        kInitialSessionFlowControlWindow);
  }
  /* Initialize Configs Ends ----------------------------------------*/

  // Deleted by delete_go_quic_dispatcher()
  GoQuicDispatcher* dispatcher =
      new GoQuicDispatcher(*config, crypto_config, versions, helper, go_quic_dispatcher);

  GoQuicServerPacketWriter* writer = new GoQuicServerPacketWriter(
      go_writer, dispatcher);  // Deleted by scoped ptr of GoQuicDispatcher

  dispatcher->InitializeWithWriter(writer);

  return dispatcher;
}

void delete_go_quic_dispatcher(GoQuicDispatcher* dispatcher) {
  QuicConnectionHelperInterface* helper = dispatcher->helper();
  delete dispatcher;
  delete helper;
}

QuicCryptoServerConfig* init_crypto_config(ProofSourceGoquic* proof_source) {
  // Takes ownership of proof_source
  QuicCryptoServerConfig* crypto_config = new QuicCryptoServerConfig(
      "secret", QuicRandom::GetInstance(), proof_source);

  crypto_config->set_strike_register_no_startup_period();
  net::EphemeralKeySource* keySource = new GoEphemeralKeySource();
  crypto_config->SetEphemeralKeySource(keySource);

  QuicClock* clock = new QuicClock();  // XXX: Not deleted. This should be
                                       // initialized EXACTLY ONCE
  QuicRandom* random_generator =
      QuicRandom::GetInstance();  // XXX: Not deleted. This should be
                                  // initialized EXACTLY ONCE

  // TODO(jaeman, hodduc): What is scfg?
  scoped_ptr<CryptoHandshakeMessage> scfg(crypto_config->AddDefaultConfig(
      random_generator, clock, QuicCryptoServerConfig::ConfigOptions()));

  return crypto_config;
}

void delete_crypto_config(QuicCryptoServerConfig* crypto_config) {
  delete crypto_config;
}

void quic_dispatcher_process_packet(GoQuicDispatcher* dispatcher,
                                    uint8_t* self_address_ip,
                                    size_t self_address_len,
                                    uint16_t self_address_port,
                                    uint8_t* peer_address_ip,
                                    size_t peer_address_len,
                                    uint16_t peer_address_port,
                                    char* buffer,
                                    size_t length) {
  IPAddress self_ip_addr(self_address_ip, self_address_len);
  IPEndPoint self_address(self_ip_addr, self_address_port);
  IPAddress peer_ip_addr(peer_address_ip, peer_address_len);
  IPEndPoint peer_address(peer_ip_addr, peer_address_port);

  QuicEncryptedPacket packet(
      buffer, length,
      false /* Do not own the buffer, so will not free buffer in the destructor */);

  dispatcher->ProcessPacket(self_address, peer_address, packet);
}

SpdyHeaderBlock* initialize_header_block() {
  return new SpdyHeaderBlock;  // Delete by delete_header_block
}

void delete_header_block(SpdyHeaderBlock* block) {
  delete block;
}

void insert_header_block(SpdyHeaderBlock* block,
                         char* key,
                         size_t key_len,
                         char* value,
                         size_t value_len) {
  (*block)[base::StringPiece(std::string(key, key_len))] =
      base::StringPiece(std::string(value, value_len));
}

void quic_simple_server_stream_write_headers(GoQuicSimpleServerStream* wrapper,
                                             SpdyHeaderBlock* block,
                                             int is_empty_body) {
  wrapper->WriteHeaders(*block, is_empty_body, nullptr);
}

void quic_simple_server_stream_write_or_buffer_data(
    GoQuicSimpleServerStream* wrapper,
    char* buf,
    size_t bufsize,
    int fin) {
  wrapper->WriteOrBufferData_(StringPiece(buf, bufsize), (fin != 0), nullptr);
}

void go_quic_alarm_fire(GoQuicAlarmGoWrapper* go_quic_alarm) {
  go_quic_alarm->Fire_();
}

int64_t clock_now(QuicClock* quic_clock) {
  return quic_clock->Now().Subtract(QuicTime::Zero()).ToMicroseconds();
}

void packet_writer_on_write_complete(GoQuicServerPacketWriter* cb, int rv) {
  cb->OnWriteComplete(rv);
}

struct ConnStat quic_server_session_connection_stat(GoQuicServerSessionBase* sess) {
  QuicConnection* conn = sess->connection();
  QuicConnectionStats stats = conn->GetStats();

  struct ConnStat stat = {(uint64_t)(conn->connection_id()),

                          stats.bytes_sent,
                          stats.packets_sent,
                          stats.stream_bytes_sent,
                          stats.packets_discarded,

                          stats.bytes_received,
                          stats.packets_received,
                          stats.packets_processed,
                          stats.stream_bytes_received,

                          stats.bytes_retransmitted,
                          stats.packets_retransmitted,

                          stats.bytes_spuriously_retransmitted,
                          stats.packets_spuriously_retransmitted,
                          stats.packets_lost,

                          stats.slowstart_packets_sent,
                          stats.slowstart_packets_lost,

                          stats.packets_revived,
                          stats.packets_dropped,
                          stats.crypto_retransmit_count,
                          stats.loss_timeout_count,
                          stats.tlp_count,
                          stats.rto_count,

                          stats.min_rtt_us,
                          stats.srtt_us,
                          stats.max_packet_size,
                          stats.max_received_packet_size,

                          stats.estimated_bandwidth.ToBitsPerSecond(),

                          stats.packets_reordered,
                          stats.max_sequence_reordering,
                          stats.max_time_reordering_us,
                          stats.tcp_loss_events};

  return stat;
}

ProofSourceGoquic* init_proof_source_goquic(GoPtr go_proof_source) {
  return new ProofSourceGoquic(go_proof_source);
}

void proof_source_goquic_add_cert(ProofSourceGoquic* proof_source, char* cert_c, size_t cert_sz) {
  proof_source->AddCert(cert_c, cert_sz);
}

void proof_source_goquic_build_cert_chain(ProofSourceGoquic* proof_source) {
  proof_source->BuildCertChain();
}
