#include "persist_handler.h"

#include "logging.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

namespace sparkpush {

namespace {

bool ParseSingleSessionUsers(const std::string& session_id,
                             int64_t* user1,
                             int64_t* user2) {
  if (!user1 || !user2) return false;
  const std::string prefix = "single:";
  if (session_id.rfind(prefix, 0) != 0) return false;

  const std::string ids = session_id.substr(prefix.size());
  const auto sep = ids.find(':');
  if (sep == std::string::npos) return false;

  try {
    *user1 = std::stoll(ids.substr(0, sep));
    *user2 = std::stoll(ids.substr(sep + 1));
    return *user1 > 0 && *user2 > 0;
  } catch (...) {
    return false;
  }
}

bool ParseRoomSessionId(const std::string& session_id, int64_t* room_id) {
  if (!room_id) return false;
  const std::string prefix = "room:";
  if (session_id.rfind(prefix, 0) != 0) return false;

  try {
    *room_id = std::stoll(session_id.substr(prefix.size()));
    return *room_id > 0;
  } catch (...) {
    return false;
  }
}

bool UpsertSessionSnapshot(MYSQL* conn,
                           const std::string& session_id,
                           int64_t msg_seq,
                           std::string* err_msg) {
  if (!conn || session_id.empty()) {
    if (err_msg) *err_msg = "invalid session upsert arguments";
    return false;
  }

  int64_t user1 = 0;
  int64_t user2 = 0;
  int64_t room_id = 0;
  char sql[1024];

  if (ParseSingleSessionUsers(session_id, &user1, &user2)) {
    std::snprintf(
        sql,
        sizeof(sql),
        "INSERT INTO session(session_id, type, user1_id, user2_id, group_id, last_msg_seq) "
        "VALUES('%s', 0, %lld, %lld, 0, %lld) "
        "ON DUPLICATE KEY UPDATE last_msg_seq = GREATEST(last_msg_seq, VALUES(last_msg_seq))",
        session_id.c_str(),
        static_cast<long long>(user1),
        static_cast<long long>(user2),
        static_cast<long long>(msg_seq));
  } else if (ParseRoomSessionId(session_id, &room_id)) {
    std::snprintf(
        sql,
        sizeof(sql),
        "INSERT INTO session(session_id, type, user1_id, user2_id, group_id, last_msg_seq) "
        "VALUES('%s', 2, 0, 0, %lld, %lld) "
        "ON DUPLICATE KEY UPDATE last_msg_seq = GREATEST(last_msg_seq, VALUES(last_msg_seq))",
        session_id.c_str(),
        static_cast<long long>(room_id),
        static_cast<long long>(msg_seq));
  } else {
    return true;
  }

  if (mysql_query(conn, sql) != 0) {
    if (err_msg) *err_msg = mysql_error(conn);
    return false;
  }
  return true;
}

}  // namespace

PersistHandler::PersistHandler(const Config& cfg)
    : cfg_(cfg),
      persist_pool_(cfg.mysql_pool_size > 0 ? cfg.mysql_pool_size : 4,
                    "persist_pool") {}

bool PersistHandler::Init() {
  if (cfg_.mysql_host.empty()) {
    LogError("[PersistHandler] MySQL host not configured");
    return false;
  }

  MySqlConfig mysql_cfg;
  mysql_cfg.host = cfg_.mysql_host;
  mysql_cfg.port = cfg_.mysql_port;
  mysql_cfg.user = cfg_.mysql_user;
  mysql_cfg.password = cfg_.mysql_password;
  mysql_cfg.db = cfg_.mysql_db;
  mysql_cfg.pool_size = cfg_.mysql_pool_size > 0 ? cfg_.mysql_pool_size : 8;

  mysql_pool_ = std::make_unique<MySqlConnectionPool>();
  if (!mysql_pool_->Init(mysql_cfg)) {
    LogError("[PersistHandler] MySQL pool init failed");
    return false;
  }
  LogInfo("[PersistHandler] MySQL pool initialized");

  if (cfg_.kafka_brokers.empty()) {
    LogError("[PersistHandler] Kafka brokers not configured");
    return false;
  }

  KafkaConsumer::Options opts;
  opts.enable_auto_commit = false;
  std::string persist_group = cfg_.kafka_consumer_group + "_persist";

  if (!persist_consumer_.Init(cfg_.kafka_brokers,
                              persist_group,
                              cfg_.kafka_push_topic,
                              std::bind(&PersistHandler::HandleMessage, this,
                                        std::placeholders::_1,
                                        std::placeholders::_2),
                              opts)) {
    LogError("[PersistHandler] Kafka persist consumer init failed");
    return false;
  }
  LogInfo("[PersistHandler] Persist consumer initialized, group=" +
          persist_group);

  persist_pool_.Start();
  return true;
}

void PersistHandler::Start() {
  persist_consumer_.Start();
  LogInfo("[PersistHandler] Started");
}

void PersistHandler::Stop() {
  persist_consumer_.Stop();
  persist_pool_.Stop();
  if (mysql_pool_) {
    mysql_pool_->Stop();
  }
  LogInfo("[PersistHandler] Stopped");
}

void PersistHandler::HandleMessage(const std::string& key,
                                   const std::string& value) {
  (void)key;
  persist_pool_.Submit([this, payload = value]() {
    PushToCometRequest req;
    if (!req.ParseFromString(payload)) {
      LogError("[PersistHandler] Failed to parse PushToCometRequest");
      return;
    }
    if (req.need_persist()) {
      PersistMessage(req);
    }
  });
}

void PersistHandler::PersistMessage(const PushToCometRequest& req) {
  static constexpr int kMaxRetries = 3;
  static constexpr int kBaseBackoffMs = 100;

  if (!mysql_pool_) return;

  const auto& msg = req.message();
  std::string session_id = msg.session_id();

  for (int attempt = 0; attempt <= kMaxRetries; ++attempt) {
    auto conn_guard = mysql_pool_->Acquire(1000);
    MYSQL* conn = conn_guard.get();
    if (!conn) {
      if (attempt < kMaxRetries) {
        int backoff_ms = kBaseBackoffMs * (1 << (attempt * 2));
        LogError("[PersistHandler] No MySQL connection, retry " +
                 std::to_string(attempt + 1) + "/" +
                 std::to_string(kMaxRetries) + " after " +
                 std::to_string(backoff_ms) + "ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        continue;
      }
      LogError("[DLQ][Persist] All retries exhausted (no connection), msg_id=" +
               msg.msg_id() + " session_id=" + session_id);
      return;
    }

    char escaped_content[4096];
    mysql_real_escape_string(conn,
                             escaped_content,
                             msg.content_json().c_str(),
                             std::min(msg.content_json().length(), (size_t)2000));

    char sql[8192];
    std::snprintf(sql,
                  sizeof(sql),
                  "INSERT INTO message (session_id, msg_seq, sender_id, msg_type, "
                  "content_json, timestamp_ms, client_msg_id) "
                  "VALUES ('%s', %ld, %ld, '%s', '%s', %ld, '%s') "
                  "ON DUPLICATE KEY UPDATE session_id=session_id",
                  session_id.c_str(),
                  (long)msg.msg_seq(),
                  (long)msg.sender_id(),
                  msg.msg_type().c_str(),
                  escaped_content,
                  (long)msg.timestamp_ms(),
                  msg.client_msg_id().c_str());

    if (mysql_query(conn, sql) == 0) {
      std::string session_err;
      if (!UpsertSessionSnapshot(conn, session_id, msg.msg_seq(), &session_err)) {
        if (attempt < kMaxRetries) {
          int backoff_ms = kBaseBackoffMs * (1 << (attempt * 2));
          LogError("[PersistHandler] Session snapshot retry " +
                   std::to_string(attempt + 1) + "/" +
                   std::to_string(kMaxRetries) + " after " +
                   std::to_string(backoff_ms) + "ms, error: " + session_err);
          std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
          continue;
        }
        LogError("[DLQ][Persist] Session snapshot failed, msg_id=" +
                 msg.msg_id() + " session_id=" + session_id +
                 " error: " + session_err);
      }
      return;
    }

    std::string err = mysql_error(conn);
    if (attempt < kMaxRetries) {
      int backoff_ms = kBaseBackoffMs * (1 << (attempt * 2));
      LogError("[PersistHandler] MySQL persist retry " +
               std::to_string(attempt + 1) + "/" +
               std::to_string(kMaxRetries) + " after " +
               std::to_string(backoff_ms) + "ms, error: " + err);
      std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
    } else {
      LogError("[DLQ][Persist] All retries exhausted, msg_id=" + msg.msg_id() +
               " session_id=" + session_id + " error: " + err);
    }
  }
}

}  // namespace sparkpush
