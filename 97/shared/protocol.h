#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

namespace sgxagg {

// 最多同时存在的会话数量（>1000 时按 LRU 淘汰）
constexpr size_t kMaxSessions = 1000;
// 默认 TTL（秒）= 24 小时；可通过 --session-ttl 覆盖
constexpr uint32_t kDefaultSessionTTL = 24 * 60 * 60;
// 最大数据方数量（控制内存 < 256MB）
constexpr size_t kMaxParties = 64;
constexpr size_t kMaxRecordsPerParty = 16384;

// 帧结构：[Type:4][Len:4][Payload]
constexpr size_t kFrameHeader = 8;

enum class RequestType : uint32_t {
    Ping           = 0,
    GetQuote       = 1,   // 获取 enclave quote，内含 enclave 临时 ECDH 公钥
    Handshake      = 2,   // 客户端提交自己的 ECDH 公钥，建立会话
    SubmitData     = 3,   // 通过会话 AES-GCM 加密提交数据
    GetAggregates  = 4,   // 获取聚合结果（返回也可被会话密钥加密）
    CloseSession   = 5,
    PIRQuery       = 6,   // 隐私信息检索查询
    GetAuditLog    = 7,   // 导出审计日志（仅时间戳+查询类型）
};

enum class Status : uint32_t {
    Ok = 0,
    BadRequest = 1,
    CryptoError = 2,
    EnclaveError = 3,
    SessionNotFound = 4,
    SessionExpired = 5,
    SessionLimitExceeded = 6,
    NotReady = 7,
    Internal = 8
};

// 一条原始敏感记录
struct Record {
    double income;
    int32_t age;
};

// 聚合结果
struct AggregateResult {
    double mean_income;
    double median_income;
    double variance_income;
    double quantile25_income;
    double quantile75_income;
    double mean_age;
    double median_age;
    double variance_age;
    uint64_t total_records;
};

// 会话握手响应：服务端返回的 SessionID（uint64_t）

// —— PIR 查询条件 AST（客户端加密发送，enclave 内解密执行）——
enum class QueryField : uint8_t {
    Income = 0,
    Age = 1
};

enum class QueryOp : uint8_t {
    Eq = 0,     // ==
    Ne = 1,     // !=
    Gt = 2,     // >
    Ge = 3,     // >=
    Lt = 4,     // <
    Le = 5,     // <=
    And = 6,    // 逻辑与（二元）
    Or = 7      // 逻辑或（二元）
};

// 查询条件序列化格式（前缀编码）：
// - 叶子节点: [1字节类型=0][1字节字段][1字节比较op][8字节值] → 11字节
// - 内部节点: [1字节类型=1][1字节逻辑op] [左子树] [右子树] → 2字节 + 子树
// 最大深度 8，最大节点数 255

// —— PIR 查询类型 ——
enum class QueryType : uint8_t {
    CountRatio = 0,    // 符合条件的记录占比 (count / total)
    MeanOfField = 1    // 符合条件的记录的某字段均值
};

// PIR 查询请求（加密前）
struct PIRQuery {
    QueryType type;
    QueryField target_field;  // MeanOfField 时有效
    uint32_t condition_len;   // 序列化条件的字节数
    // 紧接着是 condition_len 字节的条件 AST
};

// PIR 查询结果（enclave 内加噪后返回）
struct PIRResult {
    double value;              // 查询结果（已加差分隐私噪声）
    double epsilon;            // 使用的 ε 值
    double noise_std;          // 噪声标准差
    uint64_t matching_records; // 匹配记录数（已加噪）
    uint64_t total_records;    // 总记录数（已加噪）
};

// —— 审计日志条目（导出格式，不含查询条件）——
struct AuditEntry {
    uint64_t timestamp_ms;  // Unix 时间戳（毫秒）
    uint8_t query_type;     // QueryType 枚举值
    uint8_t padding[7];     // 对齐
};

// —— 序列化工具 ——
std::vector<uint8_t> serialize_records(const std::vector<Record>& records);
std::vector<Record>   deserialize_records(const uint8_t* data, size_t len);

std::vector<uint8_t> serialize_aggregate(const AggregateResult& r);
AggregateResult       deserialize_aggregate(const uint8_t* data, size_t len);

// —— PIR 查询条件构建与序列化 ——
// 构建叶子条件: field op value (e.g. Income > 10000)
std::vector<uint8_t> make_leaf_condition(QueryField field, QueryOp op, double value);
// 构建 AND/OR 复合条件
std::vector<uint8_t> make_composite_condition(QueryOp op,
    const std::vector<uint8_t>& left, const std::vector<uint8_t>& right);

// 序列化 PIR 查询
std::vector<uint8_t> serialize_pir_query(const PIRQuery& q, const uint8_t* condition);
// 反序列化 PIR 查询
bool deserialize_pir_query(const uint8_t* data, size_t len, PIRQuery& q,
    const uint8_t*& out_condition, size_t& out_condition_len);

// 序列化 PIR 结果
std::vector<uint8_t> serialize_pir_result(const PIRResult& r);
PIRResult deserialize_pir_result(const uint8_t* data, size_t len);

// 评估条件 AST 在一条记录上的结果（enclave 内使用）
bool evaluate_condition(const uint8_t* cond, size_t cond_len, const Record& r);

// —— 协议辅助 ——
uint32_t read_be32(const uint8_t* p);
void     write_be32(uint8_t* p, uint32_t v);

} // namespace sgxagg
